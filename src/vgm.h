#ifndef VGM_H
#define VGM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int fd;
    uint32_t data_offset;
    uint32_t loop_offset;
    uint32_t total_samples;
    uint32_t sample_position;
    uint32_t wait_samples;
    bool reached_end;
    bool has_loop;

    /* GD3 metadata (ASCII-extracted from UTF-16 LE) */
    uint32_t gd3_offset;
    char gd3_title[48];
    char gd3_author[32];

    uint8_t buffer[512];
    uint16_t buffer_pos;
    uint16_t buffer_size;
} vgm_player_t;

bool vgm_open(vgm_player_t *player, const char *path, char *status_line, uint16_t status_size);
void vgm_close(vgm_player_t *player);
void vgm_reset_decoder(vgm_player_t *player);
void vgm_update(vgm_player_t *player, uint32_t sample_budget, bool *track_ended, char *status_line, uint16_t status_size);
void vgm_seek_seconds(vgm_player_t *player, int delta_seconds, char *status_line, uint16_t status_size);
uint32_t vgm_position_ms(const vgm_player_t *player);

#endif
