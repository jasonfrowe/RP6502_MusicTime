#ifndef BROWSER_H
#define BROWSER_H

#include <stdbool.h>
#include <stdint.h>

#include "constants.h"

typedef struct {
    char name[MAX_ENTRY_NAME_LEN + 1];
    bool is_dir;
    uint32_t size;
} browser_entry_t;

typedef struct {
    char path[MAX_PATH_LEN + 1];
    browser_entry_t entries[BROWSER_MAX_ENTRIES];
    uint16_t entry_count;
    uint16_t selected;
    uint16_t scroll;
} browser_state_t;

void browser_init(browser_state_t *state, const char *start_path);
bool browser_refresh(browser_state_t *state, char *status_line, uint16_t status_size);
void browser_move_up(browser_state_t *state);
void browser_move_down(browser_state_t *state);
bool browser_activate_selected(browser_state_t *state,
                               char *out_file_path,
                               uint16_t out_file_path_size,
                               char *status_line,
                               uint16_t status_size);
bool browser_go_parent(browser_state_t *state, char *status_line, uint16_t status_size);
int browser_next_playable_index(const browser_state_t *state, uint16_t start_index);

#endif
