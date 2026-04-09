#include "browser.h"

#include <ctype.h>
#include <fcntl.h>
#include <rp6502.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define VIEW_ROWS 44

static bool ends_with_vgm(const char *name) {
    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }
    return (tolower((unsigned char)name[len - 4]) == '.') &&
           (tolower((unsigned char)name[len - 3]) == 'v') &&
           (tolower((unsigned char)name[len - 2]) == 'g') &&
           (tolower((unsigned char)name[len - 1]) == 'm');
}

static bool is_macos_resource_fork(const char *name) {
    return name[0] == '.' && name[1] == '_';
}

static void safe_copy(char *dst, const char *src, uint16_t size) {
    if (size == 0) {
        return;
    }
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void copy_entry_name(char *dst, const char *src) {
    safe_copy(dst, src, MAX_ENTRY_NAME_LEN + 1);
}

static void join_path(char *out, uint16_t out_size, const char *base, const char *name) {
    if (strcmp(base, "/") == 0) {
        snprintf(out, out_size, "/%s", name);
        return;
    }
    snprintf(out, out_size, "%s/%s", base, name);
}

void browser_init(browser_state_t *state, const char *start_path) {
    memset(state, 0, sizeof(*state));
    safe_copy(state->path, start_path, sizeof(state->path));
}

bool browser_refresh(browser_state_t *state, char *status_line, uint16_t status_size) {
    int dirdes;
    f_stat_t item;
    uint16_t count = 0;

    dirdes = f_opendir(state->path);
    if (dirdes < 0) {
        snprintf(status_line, status_size, "Open dir failed: %s", state->path);
        return false;
    }

    if (strcmp(state->path, "/") != 0 && count < BROWSER_MAX_ENTRIES) {
        strcpy(state->entries[count].name, "..");
        state->entries[count].is_dir = true;
        state->entries[count].size = 0;
        ++count;
    }

    while (f_readdir(&item, dirdes) == 0) {
        if (item.fname[0] == '\0') {
            break;
        }

        if (strcmp(item.fname, ".") == 0 || strcmp(item.fname, "..") == 0) {
            continue;
        }

        if (is_macos_resource_fork(item.fname)) {
            continue;
        }

        if ((item.fattrib & 0x10) == 0 && !ends_with_vgm(item.fname)) {
            continue;
        }

        if (count >= BROWSER_MAX_ENTRIES) {
            break;
        }

        copy_entry_name(state->entries[count].name, item.fname);
        state->entries[count].is_dir = (item.fattrib & 0x10) != 0;
        state->entries[count].size = (uint32_t)item.fsize;
        ++count;
    }

    f_closedir(dirdes);

    state->entry_count = count;
    if (state->selected >= state->entry_count && state->entry_count > 0) {
        state->selected = state->entry_count - 1;
    }
    if (state->entry_count == 0) {
        state->selected = 0;
        state->scroll = 0;
    }

    if (state->selected < state->scroll) {
        state->scroll = state->selected;
    }
    if (state->selected >= state->scroll + VIEW_ROWS) {
        state->scroll = state->selected - (VIEW_ROWS - 1);
    }

    snprintf(status_line, status_size, "Loaded %u entries", state->entry_count);
    return true;
}

void browser_move_up(browser_state_t *state) {
    if (state->entry_count == 0) {
        return;
    }
    
    if (state->selected > state->scroll) {
        --state->selected;
    } else if (state->selected == state->scroll && state->selected > 0) {
        state->scroll = (state->scroll >= VIEW_ROWS) ? (state->scroll - VIEW_ROWS) : 0;
        state->selected = state->scroll + (VIEW_ROWS - 1);
        if (state->selected >= state->entry_count) {
            state->selected = state->entry_count - 1;
        }
    } else if (state->selected > 0) {
        --state->selected;
    }
}

void browser_move_down(browser_state_t *state) {
    if (state->entry_count == 0) {
        return;
    }
    
    uint16_t page_bottom = state->scroll + (VIEW_ROWS - 1);
    if (page_bottom >= state->entry_count) {
        page_bottom = state->entry_count - 1;
    }
    
    if (state->selected < page_bottom) {
        ++state->selected;
    } else if (state->selected == page_bottom && state->selected + 1 < state->entry_count) {
        state->scroll = (page_bottom + 1 < state->entry_count) ? (page_bottom + 1) : 
                        ((state->entry_count > VIEW_ROWS) ? (state->entry_count - VIEW_ROWS) : 0);
        state->selected = state->scroll;
        if (state->selected >= state->entry_count) {
            state->selected = state->entry_count - 1;
        }
    } else if (state->selected + 1 < state->entry_count) {
        ++state->selected;
    }
}

void browser_page_up(browser_state_t *state) {
    if (state->entry_count == 0) {
        return;
    }

    if (state->scroll == 0) {
        state->selected = 0;
        return;
    }

    uint16_t cursor_offset = (state->selected >= state->scroll) ? 
                             (state->selected - state->scroll) : 0;

    if (state->scroll >= VIEW_ROWS) {
        state->scroll -= VIEW_ROWS;
    } else if (state->scroll > 0) {
        state->scroll = 0;
    }

    state->selected = state->scroll + cursor_offset;
    if (state->selected >= state->entry_count) {
        state->selected = state->entry_count - 1;
    }
}

void browser_page_down(browser_state_t *state) {
    if (state->entry_count == 0) {
        return;
    }

    if (state->selected == state->entry_count - 1) {
        return;
    }

    uint16_t cursor_offset = (state->selected >= state->scroll) ? 
                             (state->selected - state->scroll) : 0;
    uint16_t old_scroll = state->scroll;
    uint16_t next_scroll = state->scroll + VIEW_ROWS;
    if (next_scroll < state->entry_count) {
        state->scroll = next_scroll;
    } else if (state->scroll + VIEW_ROWS <= state->entry_count) {
        state->scroll = (state->entry_count > VIEW_ROWS) ? (state->entry_count - VIEW_ROWS) : 0;
    }

    if (state->scroll == old_scroll) {
        state->selected = state->entry_count - 1;
        return;
    }

    state->selected = state->scroll + cursor_offset;
    if (state->selected >= state->entry_count) {
        state->selected = state->entry_count - 1;
    }
}

bool browser_go_parent(browser_state_t *state, char *status_line, uint16_t status_size) {
    char *slash;

    if (strcmp(state->path, "/") == 0) {
        snprintf(status_line, status_size, "Already at root");
        return true;
    }

    slash = strrchr(state->path, '/');
    if (slash == NULL || slash == state->path) {
        strcpy(state->path, "/");
    } else {
        *slash = '\0';
    }

    state->selected = 0;
    state->scroll = 0;
    return browser_refresh(state, status_line, status_size);
}

bool browser_activate_selected(browser_state_t *state,
                               char *out_file_path,
                               uint16_t out_file_path_size,
                               char *status_line,
                               uint16_t status_size) {
    browser_entry_t *entry;
    char full_path[MAX_PATH_LEN + 1];

    if (state->entry_count == 0 || state->selected >= state->entry_count) {
        snprintf(status_line, status_size, "No item selected");
        return false;
    }

    entry = &state->entries[state->selected];
    if (strcmp(entry->name, "..") == 0) {
        return browser_go_parent(state, status_line, status_size);
    }

    join_path(full_path, sizeof(full_path), state->path, entry->name);

    if (entry->is_dir) {
        safe_copy(state->path, full_path, sizeof(state->path));
        state->selected = 0;
        state->scroll = 0;
        return browser_refresh(state, status_line, status_size);
    }

    safe_copy(out_file_path, full_path, out_file_path_size);
    snprintf(status_line, status_size, "Selected %s", entry->name);
    return true;
}

int browser_next_playable_index(const browser_state_t *state, uint16_t start_index) {
    uint16_t i;

    if (state->entry_count == 0) {
        return -1;
    }

    for (i = (uint16_t)(start_index + 1); i < state->entry_count; ++i) {
        if (!state->entries[i].is_dir && strcmp(state->entries[i].name, "..") != 0) {
            return (int)i;
        }
    }

    return -1;
}

int browser_prev_playable_index(const browser_state_t *state, uint16_t start_index) {
    int i;

    if (state->entry_count == 0 || start_index == 0) {
        return -1;
    }

    for (i = (int)start_index - 1; i >= 0; --i) {
        if (!state->entries[i].is_dir && strcmp(state->entries[i].name, "..") != 0) {
            return i;
        }
    }

    return -1;
}
