#include "opl.h"

#include <rp6502.h>

#include "constants.h"

void opl_config(uint8_t enable, uint16_t addr) {
#ifdef USE_NATIVE_OPL2
    (void)enable;
    xreg(0, 1, 0x01, addr);
#else
    xregn(2, 0, 0, 2, enable, addr);
#endif
}

void opl_write(uint8_t reg, uint8_t value) {
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

void opl_all_notes_off(void) {
    uint8_t ch;
    for (ch = 0; ch < 9; ++ch) {
        opl_write((uint8_t)(0xB0 + ch), 0x00);
    }
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
