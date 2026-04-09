#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "constants.h"

typedef struct {
    uint8_t dpad;
    uint8_t sticks;
    uint8_t btn0;
    uint8_t btn1;
    int8_t lx;
    int8_t ly;
    int8_t rx;
    int8_t ry;
    uint8_t l2;
    uint8_t r2;
} gamepad_t;

typedef enum {
    ACTION_UP = 0,
    ACTION_DOWN,
    ACTION_SELECT,
    ACTION_BACK,
    ACTION_PLAY_PAUSE,
    ACTION_STOP,
    ACTION_FF,
    ACTION_RW,
    ACTION_QUIT,
    ACTION_COUNT
} input_action_t;

void input_init(void);
void input_poll(void);
bool input_action_pressed(input_action_t action);
bool input_action_held(input_action_t action);
bool input_take_pressed_action(input_action_t *action_out);

#endif
