/*
 * Measures the Audio page's strings with the firmware's LVGL fonts (check_strings.sh builds it
 * once per target with conf/ui.h). Input, one per line: "<kind>\t<lang>\t<text>", kind one of
 * label, button, value, heading, hint. Output, one per line:
 *   "OK|WIDE|GLYPH <kind> <lang> <width>/<room> px <text>"
 * WIDE: wider than its column, GLYPH: a character the font does not have (shown as a box or
 * nothing: the CJK fonts only hold the characters of the translations they were made from).
 */

#include <stdio.h>
#include <string.h>

#include <lvgl/lvgl.h>

#include "../../conf/ui.h"

#define ROW_W (UI_AUDIO_COL_LABEL + 2 * UI_AUDIO_COL_BUTTON + UI_AUDIO_COL_SPACER + UI_AUDIO_COL_VALUE)

// lv_conf.h's tick source (the firmware's util/time.c); not used for measuring
uint32_t time_ms(void) {
    return 0;
}

// what create_label/create_slider/create_btn_group leave for the text, per kind
static int room(const char *kind, const lv_font_t **font) {
    *font = UI_PAGE_TEXT_FONT;
    if (strcmp(kind, "label") == 0)
        return UI_AUDIO_COL_LABEL - 8; // the label column, a few pixels to the slider / buttons
    if (strcmp(kind, "button") == 0)
        return UI_AUDIO_COL_BUTTON - UI_AUDIO_ARROW_W - UI_AUDIO_BTN_PAD;
    if (strcmp(kind, "value") == 0)
        return UI_AUDIO_COL_VALUE;
    if (strcmp(kind, "heading") == 0) {
        *font = UI_AUDIO_HEADING_FONT;
        return ROW_W;
    }
    if (strcmp(kind, "hint") == 0) {
        *font = UI_AUDIO_HINT_FONT;
        return ROW_W;
    }
    return -1;
}

int main(void) {
    char line[1024];
    int bad = 0;

    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        char *kind = line;
        char *lang = strchr(kind, '\t');
        if (!lang)
            continue;
        *lang++ = 0;
        char *text = strchr(lang, '\t');
        if (!text)
            continue;
        *text++ = 0;

        const lv_font_t *font;
        const int max = room(kind, &font);
        if (max < 0) {
            printf("BAD-KIND %s\n", kind);
            bad++;
            continue;
        }

        // glyphs: every code point must be in the font
        char missing[256] = "";
        uint32_t i = 0;
        while (text[i]) {
            const uint32_t letter = _lv_txt_encoded_next(text, &i);
            lv_font_glyph_dsc_t dsc;
            if (letter >= 0x20 && !lv_font_get_glyph_dsc(font, &dsc, letter, 0)) {
                const size_t n = strlen(missing);
                snprintf(missing + n, sizeof(missing) - n, " U+%04X", (unsigned)letter);
            }
        }

        lv_point_t size;
        lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        const char *verdict = missing[0] ? "GLYPH" : (size.x > max ? "WIDE" : "OK");
        if (strcmp(verdict, "OK") != 0)
            bad++;
        printf("%s %s %s %d/%d px %s%s%s\n", verdict, kind, lang, (int)size.x, max, text,
               missing[0] ? " missing:" : "", missing);
    }
    return bad ? 1 : 0;
}
