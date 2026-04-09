#include "ui.h"

#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"

#define LIST_TOP 8
#define LIST_ROWS 44

static unsigned text_addr;

static void write_cell(uint8_t x, uint8_t y, char c, uint8_t fg, uint8_t bg) {
    unsigned addr = text_addr + (((unsigned)y * SCREEN_COLS) + (unsigned)x) * BYTES_PER_CHAR;
    RIA.addr0 = addr;
    RIA.step0 = 1;
    RIA.rw0 = (uint8_t)c;
    RIA.rw0 = fg;
    RIA.rw0 = bg;
}

static void draw_text(uint8_t x, uint8_t y, const char *text, uint8_t fg, uint8_t bg) {
    while (*text != '\0' && x < SCREEN_COLS) {
        write_cell(x, y, *text, fg, bg);
        ++x;
        ++text;
    }
}

static void clear_row(uint8_t y, uint8_t bg) {
    uint8_t x;
    for (x = 0; x < SCREEN_COLS; ++x) {
        write_cell(x, y, ' ', UI_COL_WHITE, bg);
    }
}

void ui_init(void) {
    xregn(1, 0, 0, 1, 3);

    text_addr = TEXT_CONFIG_ADDR + sizeof(vga_mode1_config_t);

    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, x_wrap, 0);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, y_wrap, 0);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, x_pos_px, 0);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, y_pos_px, 0);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, width_chars, SCREEN_COLS);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, height_chars, SCREEN_ROWS);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, xram_data_ptr, text_addr);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(TEXT_CONFIG_ADDR, vga_mode1_config_t, xram_font_ptr, 0xFFFF);

    xregn(1, 0, 1, 4, 1, 3, TEXT_CONFIG_ADDR, 2);

    ui_clear();
    ui_draw_frame();
}

void ui_clear(void) {
    uint16_t i;

    RIA.addr0 = text_addr;
    RIA.step0 = 1;
    for (i = 0; i < SCREEN_COLS * SCREEN_ROWS; ++i) {
        RIA.rw0 = ' ';
        RIA.rw0 = UI_COL_WHITE;
        RIA.rw0 = UI_COL_BLACK;
    }
}

void ui_draw_frame(void) {
    uint8_t row;

    clear_row(0, UI_COL_BLUE);
    draw_text(1, 0, "MusicTime - OPL2 VGM Player", UI_COL_WHITE, UI_COL_BLUE);

    clear_row(1, UI_COL_DARKGREY);
    draw_text(1, 1, "Nav: Up/Down Select: Enter/A Back: Backspace/B", UI_COL_WHITE, UI_COL_DARKGREY);

    clear_row(2, UI_COL_DARKGREY);
    draw_text(1, 2, "Transport: Space/Start Pause  S/Select Stop  F/R Seek  Q/Esc Quit", UI_COL_WHITE, UI_COL_DARKGREY);

    clear_row(3, UI_COL_BLACK);
    draw_text(1, 3, "Path:", UI_COL_CYAN, UI_COL_BLACK);

    for (row = 4; row < 8; ++row) {
        clear_row(row, UI_COL_BLACK);
    }

    for (row = LIST_TOP; row < LIST_TOP + LIST_ROWS; ++row) {
        clear_row(row, UI_COL_BLACK);
    }

    clear_row(54, UI_COL_DARKGREY);
    clear_row(55, UI_COL_DARKGREY);
    clear_row(56, UI_COL_BLACK);
    clear_row(57, UI_COL_BLACK);
    clear_row(58, UI_COL_BLACK);
    clear_row(59, UI_COL_BLUE);
}

static const char *playback_label(ui_playback_state_t state) {
    if (state == UI_PLAYBACK_PLAYING) {
        return "Playing";
    }
    if (state == UI_PLAYBACK_PAUSED) {
        return "Paused";
    }
    return "Stopped";
}

void ui_render_browser(const browser_state_t *browser, bool browser_focus) {
    uint16_t i;
    char line[81];

    clear_row(3, UI_COL_BLACK);
    snprintf(line, sizeof(line), "Path: %s", browser->path);
    draw_text(1, 3, line, UI_COL_CYAN, UI_COL_BLACK);

    for (i = 0; i < LIST_ROWS; ++i) {
        uint16_t idx = (uint16_t)(browser->scroll + i);
        uint8_t row = (uint8_t)(LIST_TOP + i);

        clear_row(row, UI_COL_BLACK);
        if (idx >= browser->entry_count) {
            continue;
        }

        if (browser->entries[idx].is_dir) {
            snprintf(line, sizeof(line), "[DIR] %s", browser->entries[idx].name);
        } else {
            snprintf(line, sizeof(line), "      %s", browser->entries[idx].name);
        }

        if (idx == browser->selected && browser_focus) {
            draw_text(1, row, line, UI_COL_BLACK, UI_COL_YELLOW);
        } else if (idx == browser->selected) {
            draw_text(1, row, line, UI_COL_BLACK, UI_COL_CYAN);
        } else {
            draw_text(1, row, line, UI_COL_WHITE, UI_COL_BLACK);
        }
    }

}

void ui_render_playback(const char *active_file,
                        ui_playback_state_t playback_state,
                        uint32_t position_ms,
                        const char *status_line) {
    char line[81];

    clear_row(54, UI_COL_DARKGREY);
    snprintf(line, sizeof(line), "State: %s", playback_label(playback_state));
    draw_text(1, 54, line, UI_COL_WHITE, UI_COL_DARKGREY);

    clear_row(55, UI_COL_DARKGREY);
    snprintf(line, sizeof(line), "Pos: %lu.%03lus", (unsigned long)(position_ms / 1000u),
             (unsigned long)(position_ms % 1000u));
    draw_text(1, 55, line, UI_COL_WHITE, UI_COL_DARKGREY);

    clear_row(56, UI_COL_BLACK);
    snprintf(line, sizeof(line), "File: %s", active_file[0] != '\0' ? active_file : "(none)");
    draw_text(1, 56, line, UI_COL_GREEN, UI_COL_BLACK);

    clear_row(59, UI_COL_BLUE);
    snprintf(line, sizeof(line), "Status: %s", status_line);
    draw_text(1, 59, line, UI_COL_WHITE, UI_COL_BLUE);
}

void ui_render_position(uint32_t position_ms) {
    char line[81];

    clear_row(55, UI_COL_DARKGREY);
    snprintf(line, sizeof(line), "Pos: %lu.%03lus", (unsigned long)(position_ms / 1000u),
             (unsigned long)(position_ms % 1000u));
    draw_text(1, 55, line, UI_COL_WHITE, UI_COL_DARKGREY);
}
