#include "input.h"

#include <rp6502.h>

#include "usb_hid_keys.h"

static uint8_t keyboard[KEYBOARD_BYTES];
static uint8_t prev_keyboard[KEYBOARD_BYTES];
static gamepad_t pads[GAMEPAD_COUNT];
static gamepad_t prev_pads[GAMEPAD_COUNT];
static bool action_gate_locked = false;

#define is_shift_down() (key_down(KEY_LSHIFT) || key_down(KEY_RSHIFT))

static bool key_down(uint8_t keycode) {
    return (keyboard[keycode >> 3] & (1u << (keycode & 7u))) != 0;
}

static bool key_pressed(uint8_t keycode) {
    return ((keyboard[keycode >> 3] & (1u << (keycode & 7u))) != 0) &&
           ((prev_keyboard[keycode >> 3] & (1u << (keycode & 7u))) == 0);
}

static uint8_t active_pad_index(void) {
    uint8_t i;
    for (i = 0; i < GAMEPAD_COUNT; ++i) {
        if ((pads[i].dpad & GP_CONNECTED) != 0) {
            return i;
        }
    }
    return 0xFF;
}

static bool pad_mask_down(uint8_t field, uint8_t mask) {
    uint8_t idx = active_pad_index();
    if (idx == 0xFF) {
        return false;
    }
    if (field == 0) {
        return (pads[idx].dpad & mask) != 0;
    }
    if (field == 1) {
        return (pads[idx].btn0 & mask) != 0;
    }
    return (pads[idx].btn1 & mask) != 0;
}

static bool pad_mask_pressed(uint8_t field, uint8_t mask) {
    uint8_t idx = active_pad_index();
    if (idx == 0xFF) {
        return false;
    }

    if (field == 0) {
        return ((pads[idx].dpad & mask) != 0) && ((prev_pads[idx].dpad & mask) == 0);
    }
    if (field == 1) {
        return ((pads[idx].btn0 & mask) != 0) && ((prev_pads[idx].btn0 & mask) == 0);
    }
    return ((pads[idx].btn1 & mask) != 0) && ((prev_pads[idx].btn1 & mask) == 0);
}

static bool action_down_raw(input_action_t action) {
    switch (action) {
    case ACTION_UP:
        return (key_down(KEY_UP) && !is_shift_down()) || pad_mask_down(0, GP_DPAD_UP);
    case ACTION_DOWN:
        return (key_down(KEY_DOWN) && !is_shift_down()) || pad_mask_down(0, GP_DPAD_DOWN);
    case ACTION_PAGE_UP:
        return key_down(KEY_PAGEUP) || (key_down(KEY_UP) && is_shift_down());
    case ACTION_PAGE_DOWN:
        return key_down(KEY_PAGEDOWN) || (key_down(KEY_DOWN) && is_shift_down());
    case ACTION_PREV_TRACK:
        return key_down(KEY_LEFT);
    case ACTION_NEXT_TRACK:
        return key_down(KEY_RIGHT);
    case ACTION_LOOP_TOGGLE:
        return key_down(KEY_L);
    case ACTION_SHUFFLE_TOGGLE:
        return key_down(KEY_H);
    case ACTION_SELECT:
        return key_down(KEY_ENTER) || pad_mask_down(1, GP_BTN_A);
    case ACTION_BACK:
        return key_down(KEY_BACKSPACE) || pad_mask_down(1, GP_BTN_B);
    case ACTION_PLAY_PAUSE:
        return key_down(KEY_SPACE) || pad_mask_down(2, GP_BTN_START);
    case ACTION_STOP:
        return key_down(KEY_S) || pad_mask_down(2, GP_BTN_SELECT);
    case ACTION_FF:
        return key_down(KEY_F) || pad_mask_down(1, GP_BTN_R1);
    case ACTION_RW:
        return key_down(KEY_R) || pad_mask_down(1, GP_BTN_L1);
    case ACTION_QUIT:
        return key_down(KEY_Q) || key_down(KEY_ESC) || pad_mask_down(2, GP_BTN_HOME);
    default:
        return false;
    }
}

static bool action_pressed_raw(input_action_t action) {
    switch (action) {
    case ACTION_UP:
        return (key_pressed(KEY_UP) && !is_shift_down()) || pad_mask_pressed(0, GP_DPAD_UP);
    case ACTION_DOWN:
        return (key_pressed(KEY_DOWN) && !is_shift_down()) || pad_mask_pressed(0, GP_DPAD_DOWN);
    case ACTION_PAGE_UP:
        return key_pressed(KEY_PAGEUP) || (key_pressed(KEY_UP) && is_shift_down());
    case ACTION_PAGE_DOWN:
        return key_pressed(KEY_PAGEDOWN) || (key_pressed(KEY_DOWN) && is_shift_down());
    case ACTION_PREV_TRACK:
        return key_pressed(KEY_LEFT);
    case ACTION_NEXT_TRACK:
        return key_pressed(KEY_RIGHT);
    case ACTION_LOOP_TOGGLE:
        return key_pressed(KEY_L);
    case ACTION_SHUFFLE_TOGGLE:
        return key_pressed(KEY_H);
    case ACTION_SELECT:
        return key_pressed(KEY_ENTER) || pad_mask_pressed(1, GP_BTN_A);
    case ACTION_BACK:
        return key_pressed(KEY_BACKSPACE) || pad_mask_pressed(1, GP_BTN_B);
    case ACTION_PLAY_PAUSE:
        return key_pressed(KEY_SPACE) || pad_mask_pressed(2, GP_BTN_START);
    case ACTION_STOP:
        return key_pressed(KEY_S) || pad_mask_pressed(2, GP_BTN_SELECT);
    case ACTION_FF:
        return key_pressed(KEY_F) || pad_mask_pressed(1, GP_BTN_R1);
    case ACTION_RW:
        return key_pressed(KEY_R) || pad_mask_pressed(1, GP_BTN_L1);
    case ACTION_QUIT:
        return key_pressed(KEY_Q) || key_pressed(KEY_ESC) || pad_mask_pressed(2, GP_BTN_HOME);
    default:
        return false;
    }
}

static bool any_mapped_action_down(void) {
    uint8_t a;
    for (a = 0; a < ACTION_COUNT; ++a) {
        if (action_down_raw((input_action_t)a)) {
            return true;
        }
    }
    return false;
}

void input_init(void) {
    xreg_ria_keyboard(KEYBOARD_XRAM_ADDR);
    xreg_ria_gamepad(GAMEPAD_XRAM_ADDR);
}

void input_poll(void) {
    uint8_t i;
    uint8_t p;

    for (i = 0; i < KEYBOARD_BYTES; ++i) {
        prev_keyboard[i] = keyboard[i];
    }

    for (p = 0; p < GAMEPAD_COUNT; ++p) {
        prev_pads[p] = pads[p];
    }

    RIA.addr0 = KEYBOARD_XRAM_ADDR;
    RIA.step0 = 1;
    for (i = 0; i < KEYBOARD_BYTES; ++i) {
        keyboard[i] = RIA.rw0;
    }

    RIA.addr0 = GAMEPAD_XRAM_ADDR;
    RIA.step0 = 1;
    for (p = 0; p < GAMEPAD_COUNT; ++p) {
        pads[p].dpad = RIA.rw0;
        pads[p].sticks = RIA.rw0;
        pads[p].btn0 = RIA.rw0;
        pads[p].btn1 = RIA.rw0;
        pads[p].lx = (int8_t)RIA.rw0;
        pads[p].ly = (int8_t)RIA.rw0;
        pads[p].rx = (int8_t)RIA.rw0;
        pads[p].ry = (int8_t)RIA.rw0;
        pads[p].l2 = RIA.rw0;
        pads[p].r2 = RIA.rw0;
    }
}

bool input_action_pressed(input_action_t action) {
    return action_pressed_raw(action);
}

bool input_action_held(input_action_t action) {
    switch (action) {
    case ACTION_UP:
        return action_down_raw(action);
    case ACTION_DOWN:
        return action_down_raw(action);
    default:
        return action_down_raw(action);
    }
}

bool input_take_pressed_action(input_action_t *action_out) {
    static const input_action_t order[] = {
        ACTION_QUIT,
        ACTION_PAGE_UP,
        ACTION_PAGE_DOWN,
        ACTION_UP,
        ACTION_DOWN,
        ACTION_PREV_TRACK,
        ACTION_NEXT_TRACK,
        ACTION_LOOP_TOGGLE,
        ACTION_SHUFFLE_TOGGLE,
        ACTION_BACK,
        ACTION_SELECT,
        ACTION_PLAY_PAUSE,
        ACTION_STOP,
        ACTION_FF,
        ACTION_RW,
    };
    uint8_t i;

    if (action_gate_locked) {
        if (!any_mapped_action_down()) {
            action_gate_locked = false;
        }
        return false;
    }

    for (i = 0; i < (uint8_t)(sizeof(order) / sizeof(order[0])); ++i) {
        input_action_t action = order[i];
        if (action_pressed_raw(action)) {
            action_gate_locked = true;
            *action_out = action;
            return true;
        }
    }

    return false;
}
