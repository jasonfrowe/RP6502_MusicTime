#include "vgm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "constants.h"
#include "opl.h"

#define VGM_WAIT_60HZ 735u

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static int fill_buffer(vgm_player_t *player) {
    int got;
    got = read(player->fd, player->buffer, sizeof(player->buffer));
    if (got <= 0) {
        return got;
    }
    player->buffer_pos = 0;
    player->buffer_size = (uint16_t)got;
    return got;
}

static bool read_byte(vgm_player_t *player, uint8_t *out) {
    if (player->buffer_pos >= player->buffer_size) {
        int got = fill_buffer(player);
        if (got <= 0) {
            return false;
        }
    }

    *out = player->buffer[player->buffer_pos++];
    return true;
}

static bool skip_bytes(vgm_player_t *player, uint32_t count) {
    uint8_t b;
    while (count > 0) {
        if (!read_byte(player, &b)) {
            return false;
        }
        --count;
    }
    return true;
}

static bool seek_data_start(vgm_player_t *player) {
    if (lseek(player->fd, (long)player->data_offset, SEEK_SET) < 0) {
        return false;
    }
    player->buffer_pos = 0;
    player->buffer_size = 0;
    player->wait_samples = 0;
    player->sample_position = 0;
    player->reached_end = false;
    return true;
}

/* GD3 is a series of UTF-16 LE null-terminated strings.
 * We read the whole tag block into player->buffer (temporarily), then
 * extract field 0 (track title EN) and field 6 (author EN) as ASCII.
 * Called before seek_data_start so seek_data_start clobbers no GD3 state. */
static void vgm_try_read_gd3(vgm_player_t *player) {
    uint8_t hdr[12];
    uint32_t data_len;
    int got;
    uint32_t i;
    uint8_t field;
    char *dest;
    uint16_t dest_max;
    uint16_t dest_pos;
    uint8_t lo, hi;

    player->gd3_title[0]  = '\0';
    player->gd3_author[0] = '\0';

    if (player->gd3_offset == 0) return;
    if (lseek(player->fd, (long)player->gd3_offset, SEEK_SET) < 0) return;
    if (read(player->fd, hdr, 12) != 12) return;
    if (memcmp(hdr, "Gd3 ", 4) != 0) return;

    data_len = read_u32_le(&hdr[8]);
    if (data_len == 0) return;
    if (data_len > (uint32_t)sizeof(player->buffer))
        data_len = (uint32_t)sizeof(player->buffer);

    got = read(player->fd, player->buffer, (int)data_len);
    if (got <= 0) return;

    /* Fields: 0=title EN, 1=title JP, 2=game EN, 3=game JP,
     *         4=sys EN, 5=sys JP, 6=author EN  — stop after 6 */
    field    = 0;
    dest     = player->gd3_title;
    dest_max = (uint16_t)sizeof(player->gd3_title);
    dest_pos = 0;

    for (i = 0; i + 1u < (uint32_t)got; i += 2u) {
        lo = player->buffer[i];
        hi = player->buffer[i + 1u];

        if (lo == 0u && hi == 0u) {
            if (dest != NULL)
                dest[dest_pos] = '\0';
            dest_pos = 0;
            field++;
            if (field > 6u) break;
            switch (field) {
            case 6:  dest = player->gd3_author;
                     dest_max = (uint16_t)sizeof(player->gd3_author); break;
            default: dest = NULL; dest_max = 0u; break;
            }
        } else if (dest != NULL && dest_pos < dest_max - 1u) {
            dest[dest_pos++] = (hi == 0u && lo >= 0x20u) ? (char)lo : '?';
        }
    }
    if (dest != NULL)
        dest[dest_pos] = '\0';
}

bool vgm_open(vgm_player_t *player, const char *path, char *status_line, uint16_t status_size) {
    uint8_t header[0x100];
    int got;
    uint32_t data_rel;
    uint32_t loop_rel;

    memset(player, 0, sizeof(*player));
    player->fd = -1;
    player->loop_enabled = false;

    player->fd = open(path, O_RDONLY);
    if (player->fd < 0) {
        snprintf(status_line, status_size, "Open failed (%d)", errno);
        return false;
    }

    got = read(player->fd, header, sizeof(header));
    if (got < 0x40) {
        snprintf(status_line, status_size, "VGM header too small");
        vgm_close(player);
        return false;
    }

    if (memcmp(header, "Vgm ", 4) != 0) {
        snprintf(status_line, status_size, "Not a VGM file");
        vgm_close(player);
        return false;
    }

    /* VGM 1.51+ has chip clock fields. Offset 0x50 = YM3812 (OPL2) clock.
     * If non-zero the file targets OPL2; if zero it was made for a different
     * chip (e.g., OPL3/YMF262, YM2612, etc.) and we cannot play it. */
    {
        uint32_t version = read_u32_le(&header[0x08]);
        if (version >= 0x151u && (uint16_t)got >= 0x54u) {
            uint32_t ym3812_clk = read_u32_le(&header[0x50]);
            if (ym3812_clk == 0u) {
                snprintf(status_line, status_size, "Incompatible chip (not OPL2/YM3812)");
                vgm_close(player);
                return false;
            }
        }
    }
    data_rel = read_u32_le(&header[0x34]);
    if (data_rel == 0) {
        player->data_offset = 0x40;
    } else {
        player->data_offset = 0x34 + data_rel;
    }

    loop_rel = read_u32_le(&header[0x1C]);
    if (loop_rel != 0) {
        player->has_loop = true;
        player->loop_offset = 0x1C + loop_rel;
    }

    player->total_samples = read_u32_le(&header[0x18]);

    {
        uint32_t gd3_rel = read_u32_le(&header[0x14]);
        player->gd3_offset = (gd3_rel != 0u) ? (0x14u + gd3_rel) : 0u;
    }
    vgm_try_read_gd3(player);

    if (!seek_data_start(player)) {
        snprintf(status_line, status_size, "Seek failed (%d)", errno);
        vgm_close(player);
        return false;
    }

    snprintf(status_line, status_size, "Playing");
    return true;
}

void vgm_close(vgm_player_t *player) {
    if (player->fd >= 0) {
        close(player->fd);
        player->fd = -1;
    }
    player->buffer_pos = 0;
    player->buffer_size = 0;
    player->wait_samples = 0;
    player->sample_position = 0;
    player->reached_end = false;
    player->gd3_title[0]  = '\0';
    player->gd3_author[0] = '\0';
}

void vgm_reset_decoder(vgm_player_t *player) {
    seek_data_start(player);
}

static bool parse_next_command(vgm_player_t *player, bool *track_ended, char *status_line, uint16_t status_size) {
    uint8_t cmd;

    if (!read_byte(player, &cmd)) {
        *track_ended = true;
        player->reached_end = true;
        return false;
    }

    if (cmd >= 0x70 && cmd <= 0x7F) {
        player->wait_samples = (uint32_t)((cmd & 0x0F) + 1u);
        return true;
    }

    switch (cmd) {
    case 0x5A: {
        uint8_t reg;
        uint8_t value;
        if (!read_byte(player, &reg) || !read_byte(player, &value)) {
            *track_ended = true;
            player->reached_end = true;
            return false;
        }
        opl_write(reg, value);
        return true;
    }
    case 0x61: {
        uint8_t lo;
        uint8_t hi;
        if (!read_byte(player, &lo) || !read_byte(player, &hi)) {
            *track_ended = true;
            player->reached_end = true;
            return false;
        }
        player->wait_samples = (uint32_t)lo | ((uint32_t)hi << 8u);
        return true;
    }
    case 0x62:
        player->wait_samples = 735u;
        return true;
    case 0x63:
        player->wait_samples = 882u;
        return true;
    case 0x66:
        if (player->has_loop && player->loop_enabled) {
            if (lseek(player->fd, (long)player->loop_offset, SEEK_SET) >= 0) {
                player->buffer_pos = 0;
                player->buffer_size = 0;
                player->sample_position = 0;
                return true;
            }
        }
        *track_ended = true;
        player->reached_end = true;
        return false;
    case 0x67: {
        uint8_t marker;
        uint8_t dtype;
        uint8_t s0;
        uint8_t s1;
        uint8_t s2;
        uint8_t s3;
        uint32_t size;

        if (!read_byte(player, &marker) || !read_byte(player, &dtype) || !read_byte(player, &s0) ||
            !read_byte(player, &s1) || !read_byte(player, &s2) || !read_byte(player, &s3)) {
            *track_ended = true;
            player->reached_end = true;
            return false;
        }

        if (marker != 0x66) {
            snprintf(status_line, status_size, "Warning: malformed data block");
            return true;
        }

        size = (uint32_t)s0 | ((uint32_t)s1 << 8u) | ((uint32_t)s2 << 16u) | ((uint32_t)s3 << 24u);
        if (!skip_bytes(player, size)) {
            *track_ended = true;
            player->reached_end = true;
            return false;
        }
        return true;
    }
    case 0x4F:
    case 0x50:
        return skip_bytes(player, 1);
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x58:
    case 0x59:
        return skip_bytes(player, 2);
    case 0xE0:
        return skip_bytes(player, 4);
    default:
        snprintf(status_line, status_size, "Warning: unsupported cmd 0x%02X", cmd);
        return true;
    }
}

void vgm_update(vgm_player_t *player,
                uint32_t sample_budget,
                bool *track_ended,
                char *status_line,
                uint16_t status_size) {
    *track_ended = false;

    if (player->fd < 0 || player->reached_end) {
        return;
    }

    while (sample_budget > 0 && !player->reached_end) {
        if (player->wait_samples > 0) {
            uint32_t step = player->wait_samples;
            if (step > sample_budget) {
                step = sample_budget;
            }
            player->wait_samples -= step;
            sample_budget -= step;
            player->sample_position += step;
            continue;
        }

        if (!parse_next_command(player, track_ended, status_line, status_size)) {
            break;
        }
    }
}

void vgm_seek_seconds(vgm_player_t *player, int delta_seconds, char *status_line, uint16_t status_size) {
    uint32_t target;

    if (player->fd < 0) {
        snprintf(status_line, status_size, "No track loaded");
        return;
    }

    if (delta_seconds < 0) {
        uint32_t back = (uint32_t)(-delta_seconds) * 44100u;
        target = player->sample_position > back ? player->sample_position - back : 0;
    } else {
        target = player->sample_position + ((uint32_t)delta_seconds * 44100u);
        if (player->total_samples > 0 && target > player->total_samples) {
            target = player->total_samples;
        }
    }

    vgm_reset_decoder(player);
    opl_init();

    while (player->sample_position < target && !player->reached_end) {
        bool ended = false;
        vgm_update(player, 4096u, &ended, status_line, status_size);
        if (ended) {
            break;
        }
    }

    snprintf(status_line, status_size, "Seeked to %lus", (unsigned long)(player->sample_position / 44100u));
}

uint32_t vgm_position_ms(const vgm_player_t *player) {
    return (player->sample_position * 1000u) / 44100u;
}
