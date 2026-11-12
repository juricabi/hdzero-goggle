#include <log/log.h>
#include <minIni.h>

#include "../conf/ui.h"

#include "core/common.hh"
#include "core/settings.h"
#include "lang/language.h"
#include "module/module.h"
#include "page_common.h"
#include "page_modulebay.h"
#include "ui/ui_style.h"

// Rows: 0 Type, 1 Mode (selectable only when the type has modes), 2 Pin Control,
// 3 Back.  A change applies (and is stored) when its list is closed with a click
// or Pin Control is toggled.
enum {
    ROW_TYPE = 0,
    ROW_MODE,
    ROW_PINS,
    ROW_BACK,
    ROW_COUNT,
    ROW_STATUS = ROW_COUNT + 1,
    ROW_NOTE,
};

static lv_obj_t *cont;
static lv_obj_t *btn_module_type;
static lv_obj_t *lbl_module_mode;
static lv_obj_t *btn_module_mode;
static lv_obj_t *lbl_status;
static btn_group_t btn_pins;

static lv_obj_t *page_item_focused = NULL;
static uint16_t focused_orig_sel = 0; // selection when the focused list was opened
static bool in_page = false;

static lv_coord_t col_dsc[] = {160, 200, 240, 240, 160, 40, LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[] = {60, 60, 60, 60, 60, 60, 60, 60, 60, 60, LV_GRID_TEMPLATE_LAST};

static const char *type_names = "Unknown\nSwitch\nrapidFIRE\nSteadyview\nSteadyview-X\nTrue-D";

// Mode list of a type, NULL when it has none here.
static const char *mode_names(int type) {
    // SteadyView changes mode through a bay power cycle: not possible without the
    // bay power switch (batch 1 hardware)
    if (type == MODULE_STEADYVIEW && getHwRevision() < HW_REV_2)
        return NULL;
    return module_mode_names(type);
}

static void show_module_settings(int mode) {
    const char *modes = mode_names(lv_dropdown_get_selected(btn_module_type));
    if (modes) {
        lv_dropdown_set_options(btn_module_mode, modes);
        if (mode < 0 || mode >= (int)lv_dropdown_get_option_cnt(btn_module_mode))
            mode = 0;
        lv_dropdown_set_selected(btn_module_mode, mode);
        lv_obj_clear_flag(btn_module_mode, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(lbl_module_mode, STATE_DISABLED);
        lv_obj_add_flag(pp_modulebay.p_arr.panel[ROW_MODE], FLAG_SELECTABLE);
    } else {
        lv_dropdown_set_options(btn_module_mode, "None");
        lv_dropdown_set_selected(btn_module_mode, 0);
        lv_obj_add_flag(btn_module_mode, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(lbl_module_mode, STATE_DISABLED);
        lv_obj_clear_flag(pp_modulebay.p_arr.panel[ROW_MODE], FLAG_SELECTABLE);
    }
}

static void update_status(void) {
    char buf[128];
    switch (module_status()) {
    case MODULE_STATUS_NO_BAY:
        snprintf(buf, sizeof(buf), "%s", _lang("No module bay"));
        break;
    case MODULE_STATUS_NONE:
        snprintf(buf, sizeof(buf), "%s", _lang("Type Unknown, bay pins not used"));
        break;
    case MODULE_STATUS_PINS_OFF:
        snprintf(buf, sizeof(buf), "%s", _lang("Pin Control Off, bay pins not used"));
        break;
    case MODULE_STATUS_WIFI:
        snprintf(buf, sizeof(buf), "#FFA000 %s#", _lang("Paused while WiFi is on"));
        break;
    case MODULE_STATUS_SDIO:
        snprintf(buf, sizeof(buf), "#FFA000 %s#", _lang("Paused, pins in use by WiFi"));
        break;
    case MODULE_STATUS_UNPOWERED:
        snprintf(buf, sizeof(buf), "%s", _lang("Waiting for bay power"));
        break;
    case MODULE_STATUS_STARTING:
        snprintf(buf, sizeof(buf), "%s", _lang("Waiting for the module to boot"));
        break;
    case MODULE_STATUS_ACTIVE:
        if (module_bay_is_source())
            snprintf(buf, sizeof(buf), "#00FF00 %s# %s", _lang("Active"), analog_channel_name(g_setting.source.analog_channel));
        else
            snprintf(buf, sizeof(buf), "#00FF00 %s#", _lang("Active"));
        break;
    default:
        buf[0] = 0;
        break;
    }
    char text[160];
    snprintf(text, sizeof(text), "%s: %s", _lang("Status"), buf);
    lv_label_set_text(lbl_status, text);
}

static lv_obj_t *page_create(lv_obj_t *parent, panel_arr_t *arr) {
    char buf[512];
    lv_obj_t *page = lv_menu_page_create(parent, NULL);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page, 1053, 900);
    lv_obj_add_style(page, &style_subpage, LV_PART_MAIN);
    lv_obj_set_style_pad_top(page, 94, 0);

    lv_obj_t *section = lv_menu_section_create(page);
    lv_obj_add_style(section, &style_submenu, LV_PART_MAIN);
    lv_obj_set_size(section, 1053, 894);

    create_text(NULL, section, false, "Module Bay:", LV_MENU_ITEM_BUILDER_VARIANT_2);

    cont = lv_obj_create(section);
    lv_obj_set_size(cont, 1040, 600);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_GRID);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont, &style_context, LV_PART_MAIN);

    lv_obj_set_style_grid_column_dsc_array(cont, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(cont, row_dsc, 0);

    create_select_item(arr, cont);

    create_label_item(cont, _lang("Type"), 1, ROW_TYPE, 1);
    btn_module_type = create_dropdown_item(cont, type_names, 2, ROW_TYPE,
                                           240, 40, 1, 4, LV_GRID_ALIGN_START, &lv_font_montserrat_26);

    lbl_module_mode = create_label_item(cont, _lang("Mode"), 1, ROW_MODE, 1);
    btn_module_mode = create_dropdown_item(cont, "None", 2, ROW_MODE,
                                           240, 40, 1, 4, LV_GRID_ALIGN_START, &lv_font_montserrat_26);

    create_btn_group_item(&btn_pins, cont, 2, _lang("Pin Control"), _lang("Off"), _lang("On"), "", "", ROW_PINS);
    btn_group_set_sel(&btn_pins, g_setting.module.pins ? 1 : 0);

    snprintf(buf, sizeof(buf), "< %s", _lang("Back"));
    create_label_item(cont, buf, 1, ROW_BACK, 1);

    lbl_status = create_label_item(cont, "", 1, ROW_STATUS, 4);

    lv_obj_t *note = lv_label_create(cont);
    snprintf(buf, sizeof(buf), "%s\n%s\n%s",
             _lang("Pin Control lets the goggle drive the bay pins (SoC PG0-PG2). They also carry"),
             _lang("the WiFi chip's SDIO bus, so module control pauses while WiFi is on."),
             _lang("True-D: set its BAY mode before choosing a type here (docs/module-bay.md)."));
    lv_label_set_text(note, buf);
    lv_obj_set_style_text_font(note, UI_PAGE_LABEL_FONT, 0);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(TEXT_COLOR_DEFAULT), 0);
    lv_obj_set_style_pad_top(note, UI_PAGE_TEXT_PAD, 0);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 880);
    lv_obj_set_grid_cell(note, LV_GRID_ALIGN_START, 1, 4, LV_GRID_ALIGN_START, ROW_NOTE, 3);

    int type = g_setting.module.type;
    if (type < 0 || type >= MODULE_TYPES)
        type = MODULE_UNKNOWN;
    lv_dropdown_set_selected(btn_module_type, type);
    show_module_settings(g_setting.module.setting);
    update_status();

    arr->max = ROW_COUNT;
    return page;
}

// Stores and applies what the page shows.
static void apply_settings(void) {
    const int type = lv_dropdown_get_selected(btn_module_type);
    const int mode = lv_obj_has_flag(btn_module_mode, LV_OBJ_FLAG_HIDDEN) ? 0 : (int)lv_dropdown_get_selected(btn_module_mode);
    const bool pins = btn_group_get_sel(&btn_pins) == 1;
    const bool type_changed = type != g_setting.module.type;
    const bool mode_changed = mode != g_setting.module.setting;
    const bool pins_changed = pins != g_setting.module.pins;

    if (!type_changed && !mode_changed && !pins_changed)
        return;

    LOGI("module bay: type %d, mode %d, pin control %d", type, mode, pins);
    g_setting.module.type = type;
    g_setting.module.setting = mode;
    g_setting.module.pins = pins;
    ini_putl("module", "type", g_setting.module.type, SETTING_INI);
    ini_putl("module", "setting", g_setting.module.setting, SETTING_INI);
    settings_put_bool("module", "pins", g_setting.module.pins);
    // a new type is set up from scratch (its mode included); same type: new mode only
    module_settings_changed(mode_changed && !type_changed);
    update_status();
}

static void open_list(lv_obj_t *dropdown) {
    lv_obj_t *list = lv_dropdown_get_list(dropdown);
    focused_orig_sel = lv_dropdown_get_selected(dropdown);
    lv_dropdown_open(dropdown);
    lv_obj_add_style(list, &style_dropdown, LV_PART_MAIN);
    lv_obj_set_style_text_color(list, lv_color_make(0, 0, 0), LV_PART_SELECTED | LV_STATE_CHECKED);
    page_item_focused = dropdown;
    pp_modulebay.p_arr.max = 0; // we are now in control of the roller
}

// keep: take the highlighted entry; otherwise back to the one before the list opened
static void close_list(bool keep) {
    if (!page_item_focused)
        return;
    if (keep) {
        lv_event_send(page_item_focused, LV_EVENT_RELEASED, NULL);
    } else {
        lv_dropdown_set_selected(page_item_focused, focused_orig_sel);
        lv_dropdown_close(page_item_focused);
    }
    lv_obj_remove_style(page_item_focused, &style_dropdown, LV_PART_MAIN);
    page_item_focused = NULL;
    pp_modulebay.p_arr.max = ROW_COUNT;
}

static void page_on_roller(uint8_t key) {
    if (page_item_focused) {
        lv_obj_remove_style(page_item_focused, &style_dropdown, LV_PART_MAIN);

        if (key == DIAL_KEY_UP) {
            uint32_t evt = LV_KEY_DOWN;
            lv_event_send(page_item_focused, LV_EVENT_KEY, &evt);
        } else if (key == DIAL_KEY_DOWN) {
            uint32_t evt = LV_KEY_UP;
            lv_event_send(page_item_focused, LV_EVENT_KEY, &evt);
        }
    }
}

static void page_on_click(uint8_t key, int sel) {
    (void)key;
    switch (sel) {
    case ROW_TYPE:
        if (!page_item_focused) {
            open_list(btn_module_type);
        } else {
            close_list(true);
            // another type starts with its first mode, the stored type keeps its mode
            show_module_settings(g_setting.module.type == (int)lv_dropdown_get_selected(btn_module_type) ? g_setting.module.setting : 0);
            apply_settings();
        }
        break;

    case ROW_MODE:
        if (lv_obj_has_flag(btn_module_mode, LV_OBJ_FLAG_HIDDEN))
            break; // (not selectable then)
        if (!page_item_focused) {
            open_list(btn_module_mode);
        } else {
            close_list(true);
            apply_settings();
        }
        break;

    case ROW_PINS:
        if (!page_item_focused) {
            btn_group_toggle_sel(&btn_pins);
            apply_settings();
        }
        break;

    default:
        break;
    }
}

static void page_enter() {
    in_page = true;
    update_status();
}

static void page_exit() {
    // left with a list open (long press): drop what was only highlighted there
    close_list(false);
    apply_settings();
    in_page = false;
}

static void page_on_update(uint32_t delta_ms) {
    static uint32_t calls = 0;
    (void)delta_ms;
    if (!in_page)
        return;
    // the status follows bay power, WiFi and the module's boot (main loop ~5 ms)
    if (++calls >= 100) {
        calls = 0;
        update_status();
    }
}

page_pack_t pp_modulebay = {
    .p_arr = {
        .cur = 0,
        .max = ROW_COUNT,
    },

    .name = "Module Bay",
    .create = page_create,
    .enter = page_enter,
    .exit = page_exit,
    .on_created = NULL,
    .on_update = page_on_update,
    .on_roller = page_on_roller,
    .on_click = page_on_click,
    .on_right_button = NULL,
};
