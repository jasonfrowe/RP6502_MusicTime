#include <rp6502.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "browser.h"
#include "constants.h"
#include "input.h"
#include "opl.h"
#include "ui.h"
#include "vgm.h"

// Interrupt-driven vsync timing.
// IRQ fires every vsync (~60 Hz) and increments the counter.
// Main loop captures and clears it, then passes 735*ticks as the
// sample budget to vgm_update so audio stays in sync with real time
// even when the main loop occasionally takes more than one frame.
#define CLI() __asm__ volatile ("cli" ::: "memory")
#define SEI() __asm__ volatile ("sei" ::: "memory")
#define RIA_IRQ_VEC (*(volatile uint16_t *)0xFFFE)

static volatile uint8_t g_vsync_count = 0;
static volatile uint8_t g_irq_vsync_last = 0;

__attribute__((interrupt)) void vsync_irq_handler(void) {
    uint8_t vs = RIA.vsync;
    if (vs != g_irq_vsync_last) {
        g_irq_vsync_last = vs;
        if (g_vsync_count < 255u) {
            g_vsync_count++;
        }
    }
    (void)RIA.irq;
}

typedef enum {
    PLAYBACK_STOPPED = 0,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED
} playback_state_t;

static browser_state_t g_browser;
static vgm_player_t g_player;
static char g_active_file[MAX_PATH_LEN + 1];
static char g_status_line[80];

static void full_path_from_entry(const browser_state_t *browser,
                                 uint16_t index,
                                 char *out,
                                 uint16_t out_size) {
    if (index >= browser->entry_count) {
        out[0] = '\0';
        return;
    }

    if (strcmp(browser->path, "/") == 0) {
        snprintf(out, out_size, "/%s", browser->entries[index].name);
    } else {
        snprintf(out, out_size, "%s/%s", browser->path, browser->entries[index].name);
    }
}

int main(void) {
    browser_state_t *browser = &g_browser;
    vgm_player_t *player = &g_player;
    playback_state_t playback_state = PLAYBACK_STOPPED;
    uint8_t ticks;
    bool quit = false;
    bool browser_focus = true;
    bool browser_dirty = true;
    bool playback_dirty = true;
    bool pos_dirty = false;
    uint8_t pos_ui_div = 0;

    g_active_file[0] = '\0';
    g_status_line[0] = '\0';
    memset(player, 0, sizeof(*player));
    player->fd = -1;

    opl_config(1, OPL_XRAM_ADDR);
    opl_init();

    input_init();
    ui_init();

    browser_init(browser, "/");
    if (!browser_refresh(browser, g_status_line, sizeof(g_status_line))) {
        strcpy(g_status_line, "Failed to read root directory");
    }

    ui_draw_frame();
    ui_render_browser(browser, browser_focus);
    ui_render_playback(g_active_file, UI_PLAYBACK_STOPPED, 0, g_status_line);
    pos_dirty = false;

    g_irq_vsync_last = RIA.vsync;
    RIA_IRQ_VEC = (uint16_t)vsync_irq_handler;
    RIA.irq = 1;
    CLI();

    while (!quit) {
        bool track_ended = false;
        input_action_t action = ACTION_COUNT;

        SEI();
        ticks = g_vsync_count;
        g_vsync_count = 0;
        CLI();

        if (ticks == 0u) {
            uint8_t vsync_spin = RIA.vsync;
            while (RIA.vsync == vsync_spin) {
            }

            // Fallback path: consume one frame via direct VSYNC polling.
            // Clear any late IRQ count for that same frame so we don't run at double tempo.
            SEI();
            g_vsync_count = 0;
            CLI();
            g_irq_vsync_last = RIA.vsync;
            (void)RIA.irq;
            ticks = 1u;
        }

        input_poll();

        if (input_take_pressed_action(&action)) {
            switch (action) {
            case ACTION_QUIT:
                quit = true;
                break;
            case ACTION_UP:
                browser_move_up(browser);
                browser_focus = true;
                browser_dirty = true;
                break;
            case ACTION_DOWN:
                browser_move_down(browser);
                browser_focus = true;
                browser_dirty = true;
                break;
            case ACTION_BACK:
                if (!browser_go_parent(browser, g_status_line, sizeof(g_status_line))) {
                    strcpy(g_status_line, "Cannot go up");
                }
                browser_focus = true;
                browser_dirty = true;
                playback_dirty = true;
                pos_dirty = true;
                break;
            case ACTION_SELECT: {
            char selected_path[MAX_PATH_LEN + 1];
            selected_path[0] = '\0';
            if (browser_activate_selected(browser,
                                          selected_path,
                                          sizeof(selected_path),
                                          g_status_line,
                                          sizeof(g_status_line))) {
                int i;
                for (i = 0; i < browser->entry_count; ++i) {
                    if (!browser->entries[i].is_dir && strcmp(browser->entries[i].name, "..") != 0) {
                        char candidate[MAX_PATH_LEN + 1];
                        full_path_from_entry(browser, (uint16_t)i, candidate, sizeof(candidate));
                        if (strcmp(candidate, selected_path) == 0) {
                            browser->selected = (uint16_t)i;
                            break;
                        }
                    }
                }

                if (selected_path[0] != '\0' && selected_path[strlen(selected_path) - 1] != '/') {
                    vgm_close(player);
                    opl_init();
                    if (vgm_open(player, selected_path, g_status_line, sizeof(g_status_line))) {
                        strncpy(g_active_file, selected_path, sizeof(g_active_file) - 1);
                        g_active_file[sizeof(g_active_file) - 1] = '\0';
                        playback_state = PLAYBACK_PLAYING;
                        browser_focus = false;
                        browser_dirty = true;
                        playback_dirty = true;
                        pos_dirty = true;
                    } else {
                        playback_state = PLAYBACK_STOPPED;
                        playback_dirty = true;
                        pos_dirty = true;
                    }
                } else {
                    browser_dirty = true;
                    playback_dirty = true;
                }
            }
                break;
            }
            case ACTION_PLAY_PAUSE:
                if (player->fd >= 0) {
                    if (playback_state == PLAYBACK_PLAYING) {
                        playback_state = PLAYBACK_PAUSED;
                        strcpy(g_status_line, "Paused");
                    } else {
                        playback_state = PLAYBACK_PLAYING;
                        strcpy(g_status_line, "Playing");
                    }
                    playback_dirty = true;
                    pos_dirty = true;
                    browser_focus = false;
                    browser_dirty = true;
                }
                break;
            case ACTION_STOP:
                if (player->fd >= 0) {
                    vgm_close(player);
                }
                opl_init();
                playback_state = PLAYBACK_STOPPED;
                strcpy(g_status_line, "Stopped");
                browser_focus = true;
                browser_dirty = true;
                playback_dirty = true;
                pos_dirty = true;
                break;
            case ACTION_FF:
                if (player->fd >= 0) {
                    vgm_seek_seconds(player, SEEK_SECONDS, g_status_line, sizeof(g_status_line));
                    playback_state = PLAYBACK_PLAYING;
                    playback_dirty = true;
                    pos_dirty = true;
                }
                break;
            case ACTION_RW:
                if (player->fd >= 0) {
                    vgm_seek_seconds(player, -SEEK_SECONDS, g_status_line, sizeof(g_status_line));
                    playback_state = PLAYBACK_PLAYING;
                    playback_dirty = true;
                    pos_dirty = true;
                }
                break;
            default:
                break;
            }
        }

        if (playback_state == PLAYBACK_PLAYING) {
            vgm_update(player, 735u * (uint32_t)ticks, &track_ended, g_status_line, sizeof(g_status_line));
            if (track_ended) {
                int next = browser_next_playable_index(browser, browser->selected);
                if (next >= 0) {
                    char next_path[MAX_PATH_LEN + 1];
                    browser->selected = (uint16_t)next;
                    if (browser->selected < browser->scroll) {
                        browser->scroll = browser->selected;
                    }
                    full_path_from_entry(browser, browser->selected, next_path, sizeof(next_path));
                    vgm_close(player);
                    opl_init();
                    if (vgm_open(player, next_path, g_status_line, sizeof(g_status_line))) {
                        strncpy(g_active_file, next_path, sizeof(g_active_file) - 1);
                        g_active_file[sizeof(g_active_file) - 1] = '\0';
                        playback_state = PLAYBACK_PLAYING;
                        strcpy(g_status_line, "Auto-advanced to next track");
                        browser_dirty = true;
                        playback_dirty = true;
                        pos_dirty = true;
                    } else {
                        playback_state = PLAYBACK_STOPPED;
                        playback_dirty = true;
                        pos_dirty = true;
                    }
                } else {
                    playback_state = PLAYBACK_STOPPED;
                    strcpy(g_status_line, "End of list");
                    playback_dirty = true;
                    pos_dirty = true;
                }
            }

            pos_ui_div += ticks;
            if (pos_ui_div >= 6u) {
                pos_ui_div = 0;
                pos_dirty = true;
            }
        } else {
            pos_ui_div = 0;
        }

        if (browser_dirty) {
            ui_render_browser(browser, browser_focus);
            browser_dirty = false;
        }

        if (playback_dirty) {
            ui_render_playback(g_active_file,
                               playback_state == PLAYBACK_PLAYING
                                   ? UI_PLAYBACK_PLAYING
                                   : (playback_state == PLAYBACK_PAUSED ? UI_PLAYBACK_PAUSED : UI_PLAYBACK_STOPPED),
                               vgm_position_ms(player),
                               g_status_line);
            playback_dirty = false;
            pos_dirty = false;
        } else if (pos_dirty) {
            ui_render_position(vgm_position_ms(player));
            pos_dirty = false;
        }
    }

    vgm_close(player);
    opl_all_notes_off();
    return 0;
}
