#include "opl.h"

#include <rp6502.h>

#include "constants.h"

static bool g_opl_muted = false;
static uint8_t g_opl_shadow[256];

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

void opl_init(void) {
    uint16_t reg;

    opl_all_notes_off();

    for (reg = 1; reg <= 0xF5; ++reg) {
        opl_write((uint8_t)reg, 0x00);
    }

    opl_write(0x01, 0x20);
    opl_write(0xBD, 0x00);
}
