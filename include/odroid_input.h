//
// Retro-go input API (the interface homebrew/emulators actually call).
//
// Mirrors the example's odroid_input: the firmware owns the buttons and an app
// asks for the current gamepad state, reading js.values[ODROID_INPUT_*] rather
// than touching GPIO. Backed by the firmware primitive gnw_input_read() (a raw
// 12-bit mask, see src/input.c). The overlay menu and retro-go-style apps use
// this struct API; an app may also read the raw mask directly.
//

#ifndef ODROID_INPUT_H
#define ODROID_INPUT_H

#include <stdint.h>

typedef enum {
    ODROID_INPUT_UP = 0,
    ODROID_INPUT_RIGHT,
    ODROID_INPUT_DOWN,
    ODROID_INPUT_LEFT,
    ODROID_INPUT_SELECT,
    ODROID_INPUT_START,
    ODROID_INPUT_A,
    ODROID_INPUT_B,
    ODROID_INPUT_MENU,     // overlay trigger (PAUSE/SET on the G&W)
    ODROID_INPUT_VOLUME,   // PAUSE (alias used by some ports, cf. zelda3)
    ODROID_INPUT_X,        // GAME
    ODROID_INPUT_Y,        // TIME
    ODROID_INPUT_MAX
} odroid_input_t;

typedef struct {
    uint8_t values[ODROID_INPUT_MAX];   // 1 = pressed
} odroid_gamepad_state_t;

// Raw firmware primitive: 12-bit active-high mask (physical GPIO | remote mailbox).
//   0 Up 1 Down 2 Left 3 Right 4 A 5 B 6 START 7 TIME 8 SELECT 9 GAME 10 PAUSE 11 POWER
uint32_t gnw_input_read(void);

// Retro-go-style poll: fills the gamepad struct from gnw_input_read().
void odroid_input_read_gamepad(odroid_gamepad_state_t *out);

#endif // ODROID_INPUT_H
