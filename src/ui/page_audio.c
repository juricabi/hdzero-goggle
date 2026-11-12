#include "page_audio.h"

#include <stdio.h>

#include <log/log.h>

#include "../conf/ui.h"

#include "core/app_state.h"
#include "core/audio_mixer.h"
#include "core/common.hh"
#include "lang/language.h"
#include "ui/page_common.h"
#include "ui/ui_style.h"

/*
 * Audio mixer page. Every row is one [mixer] field of setting.ini:
 * sliders apply while they move (click to grab, dial, click to release),
 * toggles apply on click.
 */

typedef enum {
    ITEM_SLIDER,
    ITEM_TOGGLE,
    ITEM_BACK,
} item_type_t;

typedef struct {
    item_type_t type;
    audio_mixer_field_t field;
    const char *name;
    const char *btn0; // toggles: left button
    const char *btn1; // toggles: right button
    int btn0_value;   // toggles: field value of the left button
} item_t;

#define SLIDER(f, n)             {.type = ITEM_SLIDER, .field = (f), .name = (n)}
#define TOGGLE(f, n, b0, b1, v0) {.type = ITEM_TOGGLE, .field = (f), .name = (n), .btn0 = (b0), .btn1 = (b1), .btn0_value = (v0)}

static const item_t items[] = {
    SLIDER(AUDIO_MIXER_MIC, "Mic"),
    SLIDER(AUDIO_MIXER_LINE_IN, "Line In"),
    SLIDER(AUDIO_MIXER_AV, "A/V In"),
    SLIDER(AUDIO_MIXER_MIC_BOOST, "Mic Boost"),
    SLIDER(AUDIO_MIXER_LINE_IN_BOOST, "Line In Boost"),
    SLIDER(AUDIO_MIXER_HEADPHONES, "Headphones"),
    SLIDER(AUDIO_MIXER_PLAYBACK, "Playback Volume"),
    TOGGLE(AUDIO_MIXER_DVR_MODE, "DVR Audio", "As Heard", "Custom", AUDIO_MIXER_DVR_AS_HEARD),
    TOGGLE(AUDIO_MIXER_DVR_MIC, "Record Mic", "Yes", "No", 1),
    TOGGLE(AUDIO_MIXER_DVR_LINE_IN, "Record Line In", "Yes", "No", 1),
    TOGGLE(AUDIO_MIXER_DVR_AV, "Record A/V In", "Yes", "No", 1),
    SLIDER(AUDIO_MIXER_DVR_LEVEL, "Record Level"),
    {.type = ITEM_BACK, .field = AUDIO_MIXER_FIELD_COUNT, .name = "Back"},
};

#define ITEM_COUNT ((int)ARRAY_SIZE(items))

static lv_coord_t col_dsc[] = {UI_AUDIO_COLS};
static lv_coord_t row_dsc[] = {UI_AUDIO_ROWS};

static slider_group_t sliders[ARRAY_SIZE(items)];
static btn_group_t toggles[ARRAY_SIZE(items)];
static int focused = -1; // row of the grabbed slider

static bool is_dvr_source_row(int row) {
    const audio_mixer_field_t field = items[row].field;
    return field == AUDIO_MIXER_DVR_MIC || field == AUDIO_MIXER_DVR_LINE_IN || field == AUDIO_MIXER_DVR_AV;
}

static void update_slider(int row) {
    char buf[32];
    const int value = audio_mixer_get(items[row].field);

    lv_slider_set_value(sliders[row].slider, value, LV_ANIM_OFF);
    audio_mixer_value_str(items[row].field, value, buf, sizeof(buf));
    lv_label_set_text(sliders[row].label, buf);
}

static void update_items(void) {
    const bool custom = audio_mixer_get(AUDIO_MIXER_DVR_MODE) == AUDIO_MIXER_DVR_CUSTOM;

    for (int row = 0; row < ITEM_COUNT; row++) {
        const item_t *item = &items[row];
        if (item->type == ITEM_SLIDER) {
            update_slider(row);
        } else if (item->type == ITEM_TOGGLE) {
            btn_group_set_sel(&toggles[row], audio_mixer_get(item->field) == item->btn0_value ? 0 : 1);
        }

        // the DVR source selection only matters in custom mode
        if (is_dvr_source_row(row)) {
            btn_group_enable(&toggles[row], custom);
            if (custom)
                lv_obj_add_flag(pp_audio.p_arr.panel[row], FLAG_SELECTABLE);
            else
                lv_obj_clear_flag(pp_audio.p_arr.panel[row], FLAG_SELECTABLE);
        }
    }
}

static lv_obj_t *page_audio_create(lv_obj_t *parent, panel_arr_t *arr) {
    char buf[256];
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

    lv_obj_t *cont = lv_obj_create(section);
    lv_obj_set_size(cont, UI_PAGE_VIEW_SIZE);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_GRID);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont, &style_context, LV_PART_MAIN);

    lv_obj_set_style_grid_column_dsc_array(cont, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(cont, row_dsc, 0);
    // 13 rows: no gap between them, or the last ones fall off the screen
    lv_obj_set_style_pad_row(cont, 0, 0);

    create_select_item(arr, cont);

    for (int row = 0; row < ITEM_COUNT; row++) {
        const item_t *item = &items[row];
        switch (item->type) {
        case ITEM_SLIDER:
            create_slider_item(&sliders[row], cont, _lang(item->name), audio_mixer_max(item->field), 0, row);
            // rows are tighter than on the other pages
            lv_obj_set_style_pad_ver(sliders[row].slider, UI_AUDIO_KNOB_PAD, LV_PART_KNOB);
            break;
        case ITEM_TOGGLE:
            create_btn_group_item(&toggles[row], cont, 2, _lang(item->name), _lang(item->btn0), _lang(item->btn1), "", "", row);
            break;
        case ITEM_BACK:
            snprintf(buf, sizeof(buf), "< %s", _lang(item->name));
            create_label_item(cont, buf, 1, row, 1);
            break;
        }
    }

    lv_obj_t *note = lv_label_create(cont);
    snprintf(buf, sizeof(buf), "%s. %s.",
             _lang("Levels set the headphone mix, 0 is off"),
             _lang("DVR Audio As Heard records this mix, Custom records the selected sources directly"));
    lv_label_set_text(note, buf);
    lv_obj_set_style_text_font(note, UI_PAGE_LABEL_FONT, 0);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(TEXT_COLOR_DEFAULT), 0);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_grid_cell(note, LV_GRID_ALIGN_STRETCH, 1, 5, LV_GRID_ALIGN_START, ITEM_COUNT, 1);

    update_items();

    return page;
}

static void release_slider(bool back_to_submenu) {
    if (focused < 0)
        return;

    lv_obj_remove_style(sliders[focused].slider, &style_silder_select, LV_PART_MAIN);
    focused = -1;
    if (back_to_submenu)
        app_state_push(APP_STATE_SUBMENU);
}

static void page_audio_enter(void) {
    update_items();
}

static void page_audio_exit(void) {
    // submenu_exit() has already set the main menu state
    release_slider(false);
}

static void page_audio_on_roller(uint8_t key) {
    if (g_app_state != APP_STATE_SUBMENU_ITEM_FOCUSED || focused < 0) {
        return;
    }

    const audio_mixer_field_t field = items[focused].field;
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
    update_slider(focused);
    audio_mixer_apply();
}

static void page_audio_on_click(uint8_t key, int sel) {
    (void)key;

    if (sel < 0 || sel >= ITEM_COUNT) {
        return;
    }

    const item_t *item = &items[sel];

    if (item->type == ITEM_SLIDER) {
        if (focused == sel) {
            release_slider(true);
        } else {
            release_slider(false);
            focused = sel;
            app_state_push(APP_STATE_SUBMENU_ITEM_FOCUSED);
            lv_obj_add_style(sliders[sel].slider, &style_silder_select, LV_PART_MAIN);
        }
    } else if (item->type == ITEM_TOGGLE) {
        if (is_dvr_source_row(sel) && audio_mixer_get(AUDIO_MIXER_DVR_MODE) != AUDIO_MIXER_DVR_CUSTOM) {
            return;
        }
        btn_group_toggle_sel(&toggles[sel]);
        const int value = btn_group_get_sel(&toggles[sel]) == 0 ? item->btn0_value : !item->btn0_value;
        audio_mixer_set(item->field, value);
        update_items();
        audio_mixer_apply();
    }
}

page_pack_t pp_audio = {
    .p_arr = {
        .cur = 0,
        .max = ARRAY_SIZE(items),
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
