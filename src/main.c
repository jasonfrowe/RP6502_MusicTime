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

static bool start_track_from_selected(browser_state_t *browser,
                                      vgm_player_t *player,
                                      playback_state_t *playback_state,
                                      bool loop_enabled,
                                      char *active_file,
                                      uint16_t active_file_size,
                                      char *status_line,
                                      uint16_t status_size) {
    char path[MAX_PATH_LEN + 1];

    if (browser->selected >= browser->entry_count || browser->entries[browser->selected].is_dir) {
        snprintf(status_line, status_size, "No track selected");
        return false;
    }

    full_path_from_entry(browser, browser->selected, path, sizeof(path));
    vgm_close(player);
    opl_init();

    if (!vgm_open(player, path, status_line, status_size)) {
        *playback_state = PLAYBACK_STOPPED;
        return false;
    }
    player->loop_enabled = loop_enabled;

    strncpy(active_file, path, active_file_size - 1);
    active_file[active_file_size - 1] = '\0';
    opl_set_muted(false);
    *playback_state = PLAYBACK_PLAYING;
    return true;
}

static void refresh_browser_after_selection_change(const browser_state_t *browser,
                                                   bool browser_focus,
                                                   uint16_t old_selected,
                                                   uint16_t old_scroll) {
    if (browser->scroll != old_scroll) {
        ui_render_browser(browser, browser_focus);
    } else {
        ui_render_browser_selection(browser, browser_focus, old_selected);
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
    uint16_t up_repeat_counter = 0;
    uint16_t down_repeat_counter = 0;
    uint16_t page_up_repeat_counter = 0;
    uint16_t page_down_repeat_counter = 0;
    bool loop_enabled = false;
    uint32_t frame_counter = 0;
    uint32_t last_left_press_frame = 0xFFFFFFFFu;

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
    ui_render_playback(g_active_file, UI_PLAYBACK_STOPPED, 0, loop_enabled, false, g_status_line, "", "");
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

        frame_counter += ticks;

        input_poll();

        if (input_take_pressed_action(&action)) {
            switch (action) {
            case ACTION_QUIT:
                quit = true;
                break;
            case ACTION_UP: {
                uint16_t old_selected = browser->selected;
                uint16_t old_scroll = browser->scroll;
                browser_move_up(browser);
                browser_focus = true;
                if (browser->scroll != old_scroll) {
                    ui_render_browser(browser, browser_focus);
                } else {
                    ui_render_browser_selection(browser, browser_focus, old_selected);
                }
                up_repeat_counter = 0;
                break;
            }
            case ACTION_DOWN: {
                uint16_t old_selected = browser->selected;
                uint16_t old_scroll = browser->scroll;
                browser_move_down(browser);
                browser_focus = true;
                if (browser->scroll != old_scroll) {
                    ui_render_browser(browser, browser_focus);
                } else {
                    ui_render_browser_selection(browser, browser_focus, old_selected);
                }
                down_repeat_counter = 0;
                break;
            }
            case ACTION_PAGE_UP:
            case ACTION_PAGE_DOWN:
                break;  // Handle these below with direct key checking
            case ACTION_PREV_TRACK:
                if (player->fd >= 0 && (frame_counter - last_left_press_frame) <= 20u) {
                    int prev = browser_prev_playable_index(browser, browser->selected);
                    if (prev >= 0) {
                        uint16_t old_selected = browser->selected;
                        uint16_t old_scroll = browser->scroll;
                        browser->selected = (uint16_t)prev;
                        if (browser->selected < browser->scroll) {
                            browser->scroll = browser->selected;
                        }
                        if (browser->selected >= browser->scroll + 44u) {
                            browser->scroll = browser->selected - 43u;
                        }
                        if (start_track_from_selected(browser,
                                                      player,
                                                      &playback_state,
                                                      loop_enabled,
                                                      g_active_file,
                                                      sizeof(g_active_file),
                                                      g_status_line,
                                                      sizeof(g_status_line))) {
                            strcpy(g_status_line, "Previous track");
                        }
                        refresh_browser_after_selection_change(browser, browser_focus, old_selected, old_scroll);
                        playback_dirty = true;
                        pos_dirty = true;
                    } else {
                        strcpy(g_status_line, "Start of list");
                        playback_dirty = true;
                    }
                } else if (player->fd >= 0) {
                    vgm_reset_decoder(player);
                    opl_init();
                    if (playback_state == PLAYBACK_PLAYING) {
                        opl_set_muted(false);
                    }
                    strcpy(g_status_line, "Restarted track");
                    playback_dirty = true;
                    pos_dirty = true;
                }
                last_left_press_frame = frame_counter;
                break;
            case ACTION_NEXT_TRACK: {
                int next = browser_next_playable_index(browser, browser->selected);
                if (next >= 0) {
                    uint16_t old_selected = browser->selected;
                    uint16_t old_scroll = browser->scroll;
                    browser->selected = (uint16_t)next;
                    if (browser->selected < browser->scroll) {
                        browser->scroll = browser->selected;
                    }
                    if (browser->selected >= browser->scroll + 44u) {
                        browser->scroll = browser->selected - 43u;
                    }
                    if (start_track_from_selected(browser,
                                                  player,
                                                  &playback_state,
                                                  loop_enabled,
                                                  g_active_file,
                                                  sizeof(g_active_file),
                                                  g_status_line,
                                                  sizeof(g_status_line))) {
                        strcpy(g_status_line, "Next track");
                    }
                    refresh_browser_after_selection_change(browser, browser_focus, old_selected, old_scroll);
                    playback_dirty = true;
                    pos_dirty = true;
                } else {
                    strcpy(g_status_line, "End of list");
                    playback_dirty = true;
                }
                break;
            }
            case ACTION_LOOP_TOGGLE:
                loop_enabled = !loop_enabled;
                if (player->fd >= 0) {
                    player->loop_enabled = loop_enabled;
                }
                snprintf(g_status_line,
                         sizeof(g_status_line),
                         "Loop %s",
                         loop_enabled ? "enabled" : "disabled");
                playback_dirty = true;
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
                        uint16_t old_selected = browser->selected;
                        player->loop_enabled = loop_enabled;
                        strncpy(g_active_file, selected_path, sizeof(g_active_file) - 1);
                        g_active_file[sizeof(g_active_file) - 1] = '\0';
                        opl_set_muted(false);
                        playback_state = PLAYBACK_PLAYING;
                        browser_focus = false;
                        ui_render_browser_selection(browser, browser_focus, old_selected);
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
                        opl_set_muted(true);
                        playback_state = PLAYBACK_PAUSED;
                        strcpy(g_status_line, "Paused");
                    } else {
                        opl_set_muted(false);
                        playback_state = PLAYBACK_PLAYING;
                        strcpy(g_status_line, "Playing");
                    }
                    playback_dirty = true;
                    pos_dirty = true;
                    if (browser_focus) {
                        browser_focus = false;
                        ui_render_browser_selection(browser, browser_focus, browser->selected);
                    }
                }
                break;
            case ACTION_STOP:
                if (player->fd >= 0) {
                    vgm_close(player);
                }
                opl_set_muted(false);
                opl_init();
                playback_state = PLAYBACK_STOPPED;
                strcpy(g_status_line, "Stopped");
                if (!browser_focus) {
                    browser_focus = true;
                    ui_render_browser_selection(browser, browser_focus, browser->selected);
                }
                playback_dirty = true;
                pos_dirty = true;
                break;
            case ACTION_FF:
                if (player->fd >= 0) {
                    bool resume_audio = (playback_state != PLAYBACK_PAUSED);
                    opl_set_muted(true);
                    vgm_seek_seconds(player, SEEK_SECONDS, g_status_line, sizeof(g_status_line));
                    if (resume_audio) {
                        opl_set_muted(false);
                        playback_state = PLAYBACK_PLAYING;
                    }
                    playback_dirty = true;
                    pos_dirty = true;
                }
                break;
            case ACTION_RW:
                if (player->fd >= 0) {
                    bool resume_audio = (playback_state != PLAYBACK_PAUSED);
                    opl_set_muted(true);
                    vgm_seek_seconds(player, -SEEK_SECONDS, g_status_line, sizeof(g_status_line));
                    if (resume_audio) {
                        opl_set_muted(false);
                        playback_state = PLAYBACK_PLAYING;
                    }
                    playback_dirty = true;
                    pos_dirty = true;
                }
                break;
            default:
                break;
            }
        }

        // Handle page up/down directly (not gated) for immediate response to Shift+Up/Down
        if (browser_focus) {
            if (input_action_pressed(ACTION_PAGE_UP)) {
                browser_page_up(browser);
                ui_render_browser(browser, browser_focus);
                page_up_repeat_counter = 0;
            }
            if (input_action_pressed(ACTION_PAGE_DOWN)) {
                browser_page_down(browser);
                ui_render_browser(browser, browser_focus);
                page_down_repeat_counter = 0;
            }
        }

        // Auto-repeat for navigation when keys are held
        if (browser_focus) {
            if (input_action_held(ACTION_UP)) {
                up_repeat_counter++;
                if (up_repeat_counter > 30 && up_repeat_counter % 6 == 0) {
                    uint16_t old_selected = browser->selected;
                    uint16_t old_scroll = browser->scroll;
                    browser_move_up(browser);
                    if (browser->scroll != old_scroll) {
                        ui_render_browser(browser, browser_focus);
                    } else {
                        ui_render_browser_selection(browser, browser_focus, old_selected);
                    }
                }
            } else {
                up_repeat_counter = 0;
            }

            if (input_action_held(ACTION_DOWN)) {
                down_repeat_counter++;
                if (down_repeat_counter > 30 && down_repeat_counter % 6 == 0) {
                    uint16_t old_selected = browser->selected;
                    uint16_t old_scroll = browser->scroll;
                    browser_move_down(browser);
                    if (browser->scroll != old_scroll) {
                        ui_render_browser(browser, browser_focus);
                    } else {
                        ui_render_browser_selection(browser, browser_focus, old_selected);
                    }
                }
            } else {
                down_repeat_counter = 0;
            }

            if (input_action_held(ACTION_PAGE_UP)) {
                page_up_repeat_counter++;
                if (page_up_repeat_counter > 10 && page_up_repeat_counter % 8 == 0) {
                    browser_page_up(browser);
                    ui_render_browser(browser, browser_focus);
                }
            } else {
                page_up_repeat_counter = 0;
            }

            if (input_action_held(ACTION_PAGE_DOWN)) {
                page_down_repeat_counter++;
                if (page_down_repeat_counter > 10 && page_down_repeat_counter % 8 == 0) {
                    browser_page_down(browser);
                    ui_render_browser(browser, browser_focus);
                }
            } else {
                page_down_repeat_counter = 0;
            }
        }

        if (playback_state == PLAYBACK_PLAYING) {
            vgm_update(player, 735u * (uint32_t)ticks, &track_ended, g_status_line, sizeof(g_status_line));
            if (track_ended) {
                int next = browser_next_playable_index(browser, browser->selected);
                if (next >= 0) {
                    char next_path[MAX_PATH_LEN + 1];
                    uint16_t old_selected = browser->selected;
                    uint16_t old_scroll = browser->scroll;
                    browser->selected = (uint16_t)next;
                    if (browser->selected < browser->scroll) {
                        browser->scroll = browser->selected;
                    }
                    if (browser->selected >= browser->scroll + 44u) {
                        browser->scroll = browser->selected - 43u;
                    }
                    full_path_from_entry(browser, browser->selected, next_path, sizeof(next_path));
                    vgm_close(player);
                    opl_init();
                    if (vgm_open(player, next_path, g_status_line, sizeof(g_status_line))) {
                        player->loop_enabled = loop_enabled;
                        strncpy(g_active_file, next_path, sizeof(g_active_file) - 1);
                        g_active_file[sizeof(g_active_file) - 1] = '\0';
                        opl_set_muted(false);
                        playback_state = PLAYBACK_PLAYING;
                        browser_focus = true;
                        strcpy(g_status_line, "Auto-advanced to next track");
                        refresh_browser_after_selection_change(browser, browser_focus, old_selected, old_scroll);
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
                               loop_enabled,
                               (player->fd >= 0) ? player->has_loop : false,
                               g_status_line,
                               player->gd3_title,
                               player->gd3_author);
            playback_dirty = false;
            pos_dirty = false;
        } else if (pos_dirty) {
            ui_render_position(vgm_position_ms(player));
            pos_dirty = false;
        }

        /* VU peak meters — decay and redraw every frame */
        opl_decay_peaks();
        ui_render_vu_meters(opl_peaks());
    }

    vgm_close(player);
    opl_all_notes_off();
    return 0;
}
