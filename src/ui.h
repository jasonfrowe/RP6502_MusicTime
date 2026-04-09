#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#include "browser.h"

typedef enum {
    UI_PLAYBACK_STOPPED = 0,
    UI_PLAYBACK_PLAYING,
    UI_PLAYBACK_PAUSED
} ui_playback_state_t;

void ui_init(void);
void ui_clear(void);
void ui_draw_frame(void);
void ui_render_browser(const browser_state_t *browser, bool browser_focus);
void ui_render_browser_selection(const browser_state_t *browser, bool browser_focus, uint16_t old_selected);
void ui_render_playback(const char *active_file,
                        ui_playback_state_t playback_state,
                        uint32_t position_ms,
                        bool loop_enabled,
                        bool has_loop,
                        const char *status_line,
                        const char *meta_title,
                        const char *meta_author);
void ui_render_position(uint32_t position_ms);

#endif
