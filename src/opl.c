#include "opl.h"

#include <rp6502.h>

#include "constants.h"

static bool g_opl_muted = false;
static uint8_t g_opl_shadow[256];
static uint8_t g_ch_peaks[9];

/*
 * Carrier total-level register addresses for OPL2 channels 0-8.
 * OPL2 operator slots are arranged in rows of 3; the carrier is the
 * second operator in each channel pair (slot offset +3 within the row).
 * TL register = 0x40 + slot_offset.
 */
static const uint8_t k_carrier_tl[9] = {
    0x43, 0x44, 0x45,   /* channels 0-2 */
    0x4B, 0x4C, 0x4D,   /* channels 3-5 */
    0x53, 0x54, 0x55,   /* channels 6-8 */
};

static uint8_t carrier_level_for_channel(uint8_t ch) {
    uint8_t attn = g_opl_shadow[k_carrier_tl[ch]] & 0x3Fu;
    return (attn > 62u) ? 0u : (uint8_t)(63u - attn);
}

static void trigger_peak_for_channel(uint8_t ch) {
    uint8_t level = carrier_level_for_channel(ch);
    if (level < 8u) {
        level = 8u;
    }
    if (level > g_ch_peaks[ch]) {
        g_ch_peaks[ch] = level;
    }
}

static void opl_hw_write(uint8_t reg, uint8_t value) {
#ifdef USE_NATIVE_OPL2
    RIA.addr1 = OPL_XRAM_ADDR + reg;
    RIA.rw1 = value;
#else
    RIA.addr1 = OPL_XRAM_ADDR;
    RIA.step1 = 1;
    RIA.rw1 = reg;
    RIA.rw1 = value;
#endif
}

void opl_config(uint8_t enable, uint16_t addr) {
#ifdef USE_NATIVE_OPL2
    (void)enable;
    xreg(0, 1, 0x01, addr);
#else
    xregn(2, 0, 0, 2, enable, addr);
#endif
}

void opl_write(uint8_t reg, uint8_t value) {
    uint8_t old_value = g_opl_shadow[reg];

    /*
     * Meter pulse on note trigger: when key-on transitions 0 -> 1 on B0-B8.
     * This avoids a constantly-lit meter on sustained notes and gives
     * a more dynamic eye-candy effect.
     */
    if (reg >= 0xB0u && reg <= 0xB8u) {
        bool old_key_on = ((old_value >> 5u) & 1u) != 0u;
        bool new_key_on = ((value >> 5u) & 1u) != 0u;
        if (!old_key_on && new_key_on) {
            uint8_t ch = (uint8_t)(reg - 0xB0u);
            trigger_peak_for_channel(ch);
        }
    }

    /*
     * Rhythm mode drums are triggered via 0xBD bits, not B0-B8 key-on.
     * Map drum edges to channels 6-8 meters:
     *   BD -> ch6, (SD or HH) -> ch7, (TOM or TC) -> ch8.
     */
    if (reg == 0xBDu) {
        bool old_rhythm = ((old_value >> 5u) & 1u) != 0u;
        bool new_rhythm = ((value >> 5u) & 1u) != 0u;
        uint8_t rising = (uint8_t)(value & (uint8_t)~old_value);

        if (new_rhythm) {
            if ((rising & 0x10u) != 0u || (!old_rhythm && (value & 0x10u) != 0u)) {
                trigger_peak_for_channel(6u);
            }
            if ((rising & 0x08u) != 0u || (rising & 0x01u) != 0u ||
                (!old_rhythm && (((value & 0x08u) != 0u) || ((value & 0x01u) != 0u)))) {
                trigger_peak_for_channel(7u);
            }
            if ((rising & 0x04u) != 0u || (rising & 0x02u) != 0u ||
                (!old_rhythm && (((value & 0x04u) != 0u) || ((value & 0x02u) != 0u)))) {
                trigger_peak_for_channel(8u);
            }
        }
    }

    g_opl_shadow[reg] = value;
    if (!g_opl_muted) {
        opl_hw_write(reg, value);
    }
}

void opl_all_notes_off(void) {
    uint8_t ch;
    for (ch = 0; ch < 9; ++ch) {
        opl_write((uint8_t)(0xB0 + ch), 0x00);
    }
}

void opl_set_muted(bool muted) {
    uint8_t ch;
    uint16_t reg;

    if (g_opl_muted == muted) {
        return;
    }

    g_opl_muted = muted;

    if (g_opl_muted) {
        for (ch = 0; ch < 9; ++ch) {
            opl_hw_write((uint8_t)(0xB0 + ch), 0x00);
        }
        return;
    }

    for (reg = 1; reg <= 0xF5; ++reg) {
        opl_hw_write((uint8_t)reg, g_opl_shadow[(uint8_t)reg]);
    }
}

bool opl_is_muted(void) {
    return g_opl_muted;
}

void opl_decay_peaks(void) {
    uint8_t ch;
    for (ch = 0u; ch < 9u; ++ch) {
        /* Natural falloff; peaks are injected only on key-on edges in opl_write(). */
        if (g_ch_peaks[ch] > 1u) {
            g_ch_peaks[ch] = (uint8_t)(g_ch_peaks[ch] - 2u);
        } else {
            g_ch_peaks[ch] = 0u;
        }
    }
}

const uint8_t *opl_peaks(void) {
    return g_ch_peaks;
}

void opl_init(void) {
    uint16_t reg;
    uint8_t ch;

    opl_all_notes_off();

    for (reg = 1; reg <= 0xF5; ++reg) {
        opl_write((uint8_t)reg, 0x00);
    }

    for (ch = 0u; ch < 9u; ++ch) {
        g_ch_peaks[ch] = 0u;
    }

    opl_write(0x01, 0x20);
    opl_write(0xBD, 0x00);
}
