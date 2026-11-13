#include "page_audio.h"

#include <stdio.h>

#include <log/log.h>

#include "../conf/ui.h"

#include "core/app_state.h"
#include "core/audio_mixer.h"
#include "core/common.hh"
#include "core/hdmi_audio.h"
#include "lang/language.h"
#include "ui/page_common.h"
#include "ui/ui_style.h"

/*
 * Audio page: one list in sections (Headphones, Recording, Inputs, HDMI Audio), scrolled so that
 * the selected row is always on screen, with one hint line for the selected row below it.
 *
 * Sliders apply while they move (click to grab, dial, click to release), switches on click.
 * Applying runs in the mixer's worker; setting.ini is written when a switch is clicked, a slider
 * is released, the page is left, or SAVE_DELAY_MS after the last dial step of a slider that is
 * still grabbed. The two HDMI switches are [hdmi] audio_out / audio_in (core/hdmi_audio.c).
 */

#define SAVE_DELAY_MS 1000

// HDMI audio exists on the goggles only (core/hdmi_audio.h hdmi_audio_supported(): the BoxPro's
// device tree and board were not examined): the BoxPro shows neither the HDMI In level nor the
// HDMI Audio section, whose switches could never take effect there.
#if defined(HDZGOGGLE) || defined(HDZGOGGLE2)
#define PAGE_AUDIO_HDMI 1
#else
#define PAGE_AUDIO_HDMI 0
#endif

typedef enum {
    ROW_HEADING, // section heading, not selectable
    ROW_SLIDER,  // a mixer level
    ROW_SWITCH,  // a mixer on/off field
    ROW_HDMI_OUT,
    ROW_HDMI_IN,
    ROW_BACK,
} row_type_t;

typedef struct {
    row_type_t type;
    audio_mixer_field_t field; // sliders and mixer switches
    const char *name;
    const char *hint;
    const char *btn0; // switches: left button (field value 1), right button (0)
    const char *btn1;
} row_t;

#define HEADING(n)           {.type = ROW_HEADING, .field = AUDIO_MIXER_FIELD_COUNT, .name = (n)}
#define SLIDER(f, n, h)      {.type = ROW_SLIDER, .field = (f), .name = (n), .hint = (h)}
#define YES_NO(f, n, h)      {.type = ROW_SWITCH, .field = (f), .name = (n), .hint = (h), .btn0 = "Yes", .btn1 = "No"}
#define ON_OFF(t, f, n, h)   {.type = (t), .field = (f), .name = (n), .hint = (h), .btn0 = "On", .btn1 = "Off"}
#define RECORD_SOURCE(f, n)  YES_NO(f, n, NULL) // the hint depends on the state, see row_hint()

static const row_t rows[] = {
    HEADING("Headphones"),
    SLIDER(AUDIO_MIXER_HEADPHONES, "Volume", "Headphone volume. Does not affect recording"),
    SLIDER(AUDIO_MIXER_MIC, "Mic", "Goggle mic, heard in your headphones"),
    SLIDER(AUDIO_MIXER_LINE_IN, "Line In", "Line In jack (ext. mic), heard in headphones"),
#if defined(HDZGOGGLE2) || defined(HDZBOXPRO)
    SLIDER(AUDIO_MIXER_AV, "A/V In", "AV jack, module or receiver (needs Built-in RX Audio On)"),
#else
    SLIDER(AUDIO_MIXER_AV, "A/V In", "AV jack or analog module, heard in headphones"),
#endif
#if PAGE_AUDIO_HDMI
    // HDMI-in audio played into the DAC (audio_mixer_set_live_dac()); greyed while HDMI In Audio
    // is not in effect (selectable: its hint says why)
    SLIDER(AUDIO_MIXER_HDMI, "HDMI In", "HDMI in audio, heard in headphones"),
#endif
    SLIDER(AUDIO_MIXER_PLAYBACK, "Playback Volume", "Headphone volume while a DVR file plays"),

    // the ADC: the DVR, the WiFi stream audio and HDMI out audio all take it
    HEADING("Recording (DVR, WiFi Stream, HDMI Out)"),
    YES_NO(AUDIO_MIXER_REC_MIX, "Record Headphone Mix", "Yes: record exactly what you hear above"),
    RECORD_SOURCE(AUDIO_MIXER_REC_MIC, "Record Mic"),
    RECORD_SOURCE(AUDIO_MIXER_REC_LINE_IN, "Record Line In"),
    RECORD_SOURCE(AUDIO_MIXER_REC_AV, "Record A/V In"),
    SLIDER(AUDIO_MIXER_DVR_LEVEL, "Record Level", "Level of the recording, the stream and HDMI out"),

    HEADING("Inputs"),
    SLIDER(AUDIO_MIXER_MIC_BOOST, "Mic Boost", "Mic gain for headphones and recording"),
    SLIDER(AUDIO_MIXER_LINE_IN_BOOST, "Line In Boost", "Line In gain for headphones and recording"),
#if defined(HDZGOGGLE2) || defined(HDZBOXPRO)
    // audio output of the built-in analog receiver, one of the sources of A/V In
    ON_OFF(ROW_SWITCH, AUDIO_MIXER_RX_AUDIO, "Built-in RX Audio", "Audio of the built-in analog receiver on A/V In"),
#endif

#if PAGE_AUDIO_HDMI
    // both take effect at the next start only (core/hdmi_audio.h)
    HEADING("HDMI Audio (experimental)"),
    ON_OFF(ROW_HDMI_OUT, AUDIO_MIXER_FIELD_COUNT, "HDMI Out Audio", "Send the recorded audio to HDMI out. Restart to apply"),
    ON_OFF(ROW_HDMI_IN, AUDIO_MIXER_FIELD_COUNT, "HDMI In Audio", "Hear HDMI in audio. Restart to apply"),
#endif

    {.type = ROW_BACK, .field = AUDIO_MIXER_FIELD_COUNT, .name = "Back"},
};

#define ROW_COUNT ((int)ARRAY_SIZE(rows))

// hints of the Record source switches
#define HINT_IN_MIX     "Recorded via the mix, at its headphone level"
#define HINT_DIRECT_YES "Recorded even if its headphone level is Off"
#define HINT_DIRECT_NO  "Not recorded. Yes: record it directly"
// hints of the HDMI In level while HDMI In Audio is not in effect
#define HINT_HDMI_OFF     "Off: switch on HDMI In Audio at the bottom"
#define HINT_HDMI_RESTART "Restart the goggle to use HDMI In Audio"
// HDMI Out Audio switched on, but kept off at this start (core/hdmi_audio.h: the reason follows)
#define HINT_HDMI_FAULT "Off at this start"
// value column of the HDMI rows: the switch differs from what is in effect, or it failed
#define MARK_RESTART "Restart"
#define MARK_ERROR   "Error"

/*
 * Own column layout (conf/ui.h): indent | label | button | button | spacer | value. Sliders span
 * both button columns and the spacer; the value column of a Record source switch shows "Mix" while
 * that source is recorded through What I Hear. Widths checked for every language at the target's
 * fonts by utilities/audio_mixer/check_strings.py.
 */
static lv_coord_t col_dsc[] = {UI_AUDIO_COL_INDENT, UI_AUDIO_COL_LABEL, UI_AUDIO_COL_BUTTON, UI_AUDIO_COL_BUTTON,
                               UI_AUDIO_COL_SPACER, UI_AUDIO_COL_VALUE, LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[ARRAY_SIZE(rows) + 1];
static lv_coord_t btn_col_dsc[] = {UI_AUDIO_ARROW_W, UI_AUDIO_COL_BUTTON - UI_AUDIO_ARROW_W, LV_GRID_TEMPLATE_LAST};
_Static_assert(UI_AUDIO_COL_INDENT + UI_AUDIO_COL_LABEL + 2 * UI_AUDIO_COL_BUTTON + UI_AUDIO_COL_SPACER + UI_AUDIO_COL_VALUE <= UI_AUDIO_GRID_MAX_W,
               "the Audio page's columns must fit the page");

static slider_group_t sliders[ARRAY_SIZE(rows)];
static btn_group_t switches[ARRAY_SIZE(rows)];
static lv_obj_t *marks[ARRAY_SIZE(rows)];    // "Mix" of the Record source switches
static lv_obj_t *headings[ARRAY_SIZE(rows)]; // heading label of each heading row
static int sel_row[ARRAY_SIZE(rows)];        // selectable index (panel) -> row
static int sel_count;
static lv_obj_t *list; // the scrolling grid
static lv_obj_t *hint;
static int focused = -1; // selectable index of the grabbed slider
static lv_timer_t *save_timer;

_Static_assert(ARRAY_SIZE(rows) - (3 + PAGE_AUDIO_HDMI) <= MAX_PANELS, "one select panel per selectable row (3 or 4 headings)");

static void save_timer_cb(lv_timer_t *timer) {
    lv_timer_pause(timer);
    audio_mixer_save();
}

static bool is_record_source(const row_t *row) {
    return row->type == ROW_SWITCH && audio_mixer_level_of(row->field) < AUDIO_MIXER_FIELD_COUNT;
}

// value of a switch row: 1 = left button
static int switch_value(const row_t *row) {
    switch (row->type) {
    case ROW_HDMI_OUT:
        return hdmi_audio_out_enabled();
    case ROW_HDMI_IN:
        return hdmi_audio_in_enabled();
    default:
        return audio_mixer_get(row->field) != 0;
    }
}

static void update_slider(int r) {
    char buf[32];
    const int value = audio_mixer_get(rows[r].field);

    lv_slider_set_value(sliders[r].slider, value, LV_ANIM_OFF);
    audio_mixer_value_str(rows[r].field, value, buf, sizeof(buf));
    lv_label_set_text(sliders[r].label, buf);
}

// The HDMI In level works only while HDMI In Audio is in effect (switched on before this start).
static bool hdmi_level_usable(const row_t *row) {
    return !(row->type == ROW_SLIDER && row->field == AUDIO_MIXER_HDMI) || hdmi_audio_in_active();
}

// A Record source that is recorded through the mix: its own Yes/No does not matter then.
static bool in_mix(const row_t *row) {
    return is_record_source(row) && audio_mixer_recorded_via_mix(row->field);
}

// HDMI Out Audio was on at this start but stayed off (the reason, translated), NULL otherwise.
static const char *hdmi_out_fault(void) {
    return hdmi_audio_out_enabled() ? hdmi_audio_out_fault() : NULL;
}

// The switch differs from what is in effect: it applies at the next start.
static bool hdmi_restart_pending(const row_t *row) {
    if (row->type == ROW_HDMI_OUT)
        return hdmi_audio_out_enabled() != hdmi_audio_out_active() && !hdmi_out_fault();
    if (row->type == ROW_HDMI_IN)
        return hdmi_audio_in_enabled() != hdmi_audio_in_active();
    return false;
}

static void row_hint(const row_t *row, char *buf, size_t size) {
    const char *text = row->hint;
    if (is_record_source(row)) {
        if (in_mix(row))
            text = HINT_IN_MIX;
        else
            text = audio_mixer_get(row->field) ? HINT_DIRECT_YES : HINT_DIRECT_NO;
    } else if (!hdmi_level_usable(row)) {
        text = hdmi_audio_in_enabled() ? HINT_HDMI_RESTART : HINT_HDMI_OFF;
    } else if (row->type == ROW_HDMI_OUT && hdmi_out_fault()) {
        snprintf(buf, size, "%s: %s", _lang(HINT_HDMI_FAULT), _lang(hdmi_out_fault()));
        return;
    }
    snprintf(buf, size, "%s", text ? _lang(text) : "");
}

static void update_hint(void) {
    char buf[256] = "";
    const int sel = pp_audio.p_arr.cur;
    if (sel >= 0 && sel < sel_count)
        row_hint(&rows[sel_row[sel]], buf, sizeof(buf));
    lv_label_set_text(hint, buf);
}

static void update_rows(void) {
    char buf[64];
    for (int sel = 0; sel < sel_count; sel++) {
        const int r = sel_row[sel];
        const row_t *row = &rows[r];
        switch (row->type) {
        case ROW_SLIDER:
            update_slider(r);
            // (greyed, but selectable: its hint says how to switch it on)
            slider_enable(&sliders[r], hdmi_level_usable(row));
            break;
        case ROW_SWITCH:
            // a source recorded through the mix: recorded, so Yes, greyed (the row never shows
            // "No" next to "Mix"); its own setting comes back when it leaves the mix
            btn_group_set_sel(&switches[r], (in_mix(row) || switch_value(row)) ? 0 : 1);
            btn_group_enable(&switches[r], !in_mix(row));
            if (marks[r])
                lv_label_set_text(marks[r], in_mix(row) ? _lang("Mix") : "");
            break;
        case ROW_HDMI_OUT:
        case ROW_HDMI_IN:
            btn_group_set_sel(&switches[r], switch_value(row) ? 0 : 1);
            buf[0] = 0;
            if (row->type == ROW_HDMI_OUT && hdmi_out_fault())
                snprintf(buf, sizeof(buf), "#FFA000 %s#", _lang(MARK_ERROR));
            else if (hdmi_restart_pending(row))
                snprintf(buf, sizeof(buf), "#FFA000 %s#", _lang(MARK_RESTART));
            lv_label_set_text(marks[r], buf);
            break;
        default:
            break;
        }
    }
    update_hint();
}

// Keep the selected row on screen, with its section heading when it is the first row of a section.
// (ui_main_menu.c already scrolls the selected panel into view; this adds the heading.)
static void scroll_to_selection(void) {
    const int sel = pp_audio.p_arr.cur;
    if (sel < 0 || sel >= sel_count)
        return;
    const int r = sel_row[sel];
    if (r > 0 && rows[r - 1].type == ROW_HEADING)
        lv_obj_scroll_to_view(headings[r - 1], LV_ANIM_OFF);
    lv_obj_scroll_to_view(pp_audio.p_arr.panel[sel], LV_ANIM_OFF);
}

static lv_obj_t *create_heading(lv_obj_t *parent, const char *text, int r) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, _lang(text));
    lv_obj_set_style_text_font(label, UI_AUDIO_HEADING_FONT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_AUDIO_HEADING_COLOR), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    // the whole row (scrolling it into view then scrolls by whole rows), the text at its bottom
    // with a thin line under it, across the row
    lv_obj_set_size(label, UI_AUDIO_COL_LABEL + 2 * UI_AUDIO_COL_BUTTON + UI_AUDIO_COL_SPACER + UI_AUDIO_COL_VALUE, UI_AUDIO_ROW_H);
    lv_obj_set_style_pad_bottom(label, 2, 0);
    lv_obj_set_style_pad_top(label, UI_AUDIO_ROW_H - 2 - lv_font_get_line_height(UI_AUDIO_HEADING_FONT), 0);
    lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(label, 1, 0);
    lv_obj_set_style_border_color(label, lv_color_hex(UI_AUDIO_HEADING_COLOR), 0);
    lv_obj_set_style_border_opa(label, LV_OPA_50, 0);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 1, 5, LV_GRID_ALIGN_START, r, 1);
    return label;
}

static void create_switch(int r, int sel) {
    const row_t *row = &rows[r];
    btn_group_t *t = &switches[r];
    create_btn_group_item(t, list, 2, _lang(row->name), _lang(row->btn0), _lang(row->btn1), "", "", r);
    lv_obj_set_grid_cell(t->label, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, r, 1);
    lv_obj_set_width(t->label, UI_AUDIO_COL_LABEL);
    for (int i = 0; i < 2; i++) {
        btn_with_arr_t *b = &t->btn_a[i];
        lv_obj_set_width(b->container, UI_AUDIO_COL_BUTTON);
        lv_obj_set_style_grid_column_dsc_array(b->container, btn_col_dsc, 0);
        lv_obj_set_width(b->btn, UI_AUDIO_COL_BUTTON - UI_AUDIO_ARROW_W);
        lv_obj_set_style_pad_left(b->btn, UI_AUDIO_BTN_PAD, 0);
        lv_obj_set_style_pad_right(b->btn, 0, 0);
    }
    // value column: "Mix" of a Record source, "Restart" / "Error" (orange) of the HDMI switches
    if (is_record_source(row) || row->type == ROW_HDMI_OUT || row->type == ROW_HDMI_IN) {
        marks[r] = lv_label_create(list);
        lv_label_set_text(marks[r], "");
        lv_label_set_recolor(marks[r], true);
        lv_obj_set_style_text_font(marks[r], UI_PAGE_TEXT_FONT, 0);
        lv_obj_set_style_text_color(marks[r], lv_color_hex(UI_AUDIO_HEADING_COLOR), 0);
        lv_obj_set_style_pad_top(marks[r], UI_PAGE_TEXT_PAD, 0);
        lv_obj_set_size(marks[r], UI_AUDIO_COL_VALUE, UI_AUDIO_ROW_H);
        lv_label_set_long_mode(marks[r], LV_LABEL_LONG_DOT);
        lv_obj_set_grid_cell(marks[r], LV_GRID_ALIGN_START, 5, 1, LV_GRID_ALIGN_CENTER, r, 1);
    }
    (void)sel;
}

static lv_obj_t *page_audio_create(lv_obj_t *parent, panel_arr_t *arr) {
    char buf[128];
    lv_obj_t *page = lv_menu_page_create(parent, NULL);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page, UI_PAGE_VIEW_SIZE);
    lv_obj_add_style(page, &style_subpage, LV_PART_MAIN);

    lv_obj_t *section = lv_menu_section_create(page);
    lv_obj_add_style(section, &style_submenu, LV_PART_MAIN);
    lv_obj_set_size(section, UI_PAGE_VIEW_SIZE);
    lv_obj_set_style_pad_top(section, UI_AUDIO_PAD_TOP, 0);

    snprintf(buf, sizeof(buf), "%s:", _lang(pp_audio.name));
    create_text(NULL, section, false, buf, LV_MENU_ITEM_BUILDER_VARIANT_2);

    // The rows: taller than the window, scrolled only by the selection (lv_obj_scroll_to_view).
    list = lv_obj_create(section);
    lv_obj_set_size(list, lv_pct(100), UI_AUDIO_VIEW_H);
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_layout(list, LV_LAYOUT_GRID);
    lv_obj_add_style(list, &style_context, LV_PART_MAIN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    // a scroll bar shows that there is more
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_bg_color(list, lv_color_hex(TEXT_COLOR_DISABLE), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 4, LV_PART_SCROLLBAR);

    // every row the same height, headings too: scrolled by whole rows, the window never shows half a row
    for (int r = 0; r < ROW_COUNT; r++)
        row_dsc[r] = UI_AUDIO_ROW_H;
    _Static_assert(UI_AUDIO_VIEW_H % UI_AUDIO_ROW_H == 0, "the window shows whole rows");
    row_dsc[ROW_COUNT] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_style_grid_column_dsc_array(list, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(list, row_dsc, 0);
    // no column gaps (the columns are sized for the longest translation), no row gaps
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_style_pad_column(list, 0, 0);

    create_select_item(arr, list);

    sel_count = 0;
    for (int r = 0; r < ROW_COUNT; r++) {
        const row_t *row = &rows[r];
        if (row->type == ROW_HEADING) {
            headings[r] = create_heading(list, row->name, r);
            continue;
        }

        // this row's selection panel
        const int sel = sel_count++;
        sel_row[sel] = r;
        lv_obj_set_grid_cell(arr->panel[sel], LV_GRID_ALIGN_STRETCH, 0, 6, LV_GRID_ALIGN_STRETCH, r, 1);

        switch (row->type) {
        case ROW_SLIDER: {
            slider_group_t *s = &sliders[r];
            create_slider_item(s, list, _lang(row->name), audio_mixer_max(row->field), 0, r);
            lv_obj_set_grid_cell(s->name, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, r, 1);
            lv_obj_set_width(s->name, UI_AUDIO_COL_LABEL);
            lv_obj_set_grid_cell(s->slider, LV_GRID_ALIGN_START, 2, 3, LV_GRID_ALIGN_CENTER, r, 1);
            lv_obj_set_width(s->slider, 2 * UI_AUDIO_COL_BUTTON + UI_AUDIO_COL_SPACER - UI_AUDIO_SLIDER_GAP);
            lv_obj_set_width(s->label, UI_AUDIO_COL_VALUE);
            // the knob stays inside its row: nothing of a row scrolled out shows at the window's edge
            lv_obj_set_style_pad_ver(s->slider, UI_AUDIO_KNOB_PAD, LV_PART_KNOB);
            break;
        }
        case ROW_SWITCH:
        case ROW_HDMI_OUT:
        case ROW_HDMI_IN:
            create_switch(r, sel);
            break;
        case ROW_BACK:
            snprintf(buf, sizeof(buf), "< %s", _lang(row->name));
            create_label_item(list, buf, 1, r, 1);
            break;
        default:
            break;
        }
    }

    // one line of help for the selected row, fixed below the list
    hint = lv_label_create(section);
    lv_label_set_text(hint, "");
    lv_obj_set_style_text_font(hint, UI_AUDIO_HINT_FONT, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(TEXT_COLOR_DEFAULT), 0);
    lv_obj_set_style_pad_top(hint, UI_AUDIO_HINT_PAD, 0);
    lv_obj_set_style_pad_left(hint, UI_AUDIO_COL_INDENT, 0);
    lv_obj_set_width(hint, UI_AUDIO_COL_INDENT + UI_AUDIO_COL_LABEL + 2 * UI_AUDIO_COL_BUTTON + UI_AUDIO_COL_SPACER + UI_AUDIO_COL_VALUE);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);

    pp_audio.p_arr.max = sel_count;
    update_rows();
    // no hint while the page is only previewed from the main menu
    lv_label_set_text(hint, "");

    // runs in lv_timer_handler(), under lvgl_mutex like the input handlers
    save_timer = lv_timer_create(save_timer_cb, SAVE_DELAY_MS, NULL);
    lv_timer_pause(save_timer);

    return page;
}

static void release_slider(bool back_to_submenu) {
    if (focused < 0)
        return;

    lv_obj_remove_style(sliders[sel_row[focused]].slider, &style_silder_select, LV_PART_MAIN);
    focused = -1;
    lv_timer_pause(save_timer);
    audio_mixer_save();
    if (back_to_submenu)
        app_state_push(APP_STATE_SUBMENU);
}

static void page_audio_enter(void) {
    // from the top, with the first heading
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    update_rows();
    scroll_to_selection();
}

static void page_audio_exit(void) {
    // submenu_exit() has already set the main menu state
    release_slider(false);
    audio_mixer_save();
    // previewed from the main menu as when it was first shown: from the top, no hint
    lv_label_set_text(hint, "");
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
}

static void page_audio_on_roller(uint8_t key) {
    if (g_app_state != APP_STATE_SUBMENU_ITEM_FOCUSED || focused < 0) {
        // the selection moved
        scroll_to_selection();
        update_hint();
        return;
    }

    const audio_mixer_field_t field = rows[sel_row[focused]].field;
    int value = audio_mixer_get(field);

    // same direction as the other slider pages
    if (key == DIAL_KEY_UP && value > 0) {
        value--;
    } else if (key == DIAL_KEY_DOWN && value < audio_mixer_max(field)) {
        value++;
    } else {
        return;
    }

    audio_mixer_set(field, value);
    // the "Mix" marks and the hint follow the levels
    update_rows();
    audio_mixer_apply();
    // saved on release, or once the dial rests
    lv_timer_reset(save_timer);
    lv_timer_resume(save_timer);
}

static void page_audio_on_click(uint8_t key, int sel) {
    (void)key;

    if (sel < 0 || sel >= sel_count) {
        return;
    }

    const int r = sel_row[sel];
    const row_t *row = &rows[r];

    switch (row->type) {
    case ROW_SLIDER:
        if (!hdmi_level_usable(row))
            break; // (greyed: the hint says why)
        if (focused == sel) {
            release_slider(true);
        } else {
            release_slider(false);
            focused = sel;
            app_state_push(APP_STATE_SUBMENU_ITEM_FOCUSED);
            lv_obj_add_style(sliders[r].slider, &style_silder_select, LV_PART_MAIN);
        }
        break;
    case ROW_SWITCH:
        if (in_mix(row))
            break; // (greyed: recorded through the mix whatever it says)
        audio_mixer_set(row->field, !switch_value(row));
        audio_mixer_save();
        update_rows();
        audio_mixer_apply();
        break;
    case ROW_HDMI_OUT:
        // stored only: in effect from the next start (core/hdmi_audio.h)
        hdmi_audio_set(!hdmi_audio_out_enabled(), hdmi_audio_in_enabled());
        update_rows();
        break;
    case ROW_HDMI_IN:
        hdmi_audio_set(hdmi_audio_out_enabled(), !hdmi_audio_in_enabled());
        update_rows();
        break;
    default:
        break;
    }
}

page_pack_t pp_audio = {
    .p_arr = {
        .cur = 0,
        .max = 0, // the selectable rows, counted by page_audio_create()
    },
    .name = "Audio",
    .create = page_audio_create,
    .enter = page_audio_enter,
    .exit = page_audio_exit,
    .on_created = NULL,
    .on_update = NULL,
    .on_roller = page_audio_on_roller,
    .on_click = page_audio_on_click,
    .on_right_button = NULL,
};
