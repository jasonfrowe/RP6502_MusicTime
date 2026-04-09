#ifndef OPL_H
#define OPL_H

#include <stdint.h>

void opl_config(uint8_t enable, uint16_t addr);
void opl_init(void);
void opl_write(uint8_t reg, uint8_t value);
void opl_all_notes_off(void);

#endif
