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
void ui_render(const browser_state_t *browser,
               const char *active_file,
               ui_playback_state_t playback_state,
               uint32_t position_ms,
               const char *status_line,
               bool browser_focus);

#endif
