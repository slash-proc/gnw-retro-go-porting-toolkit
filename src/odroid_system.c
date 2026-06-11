//
// "Pretend retro-go" persistence API surface (see include/odroid_system.h).
//
// Stores the app's callbacks and turns logical path requests / slot indices into
// blob names, dispatching to the registered handlers. The actual byte storage is
// the flash-backed blob store in src/persist.c. This mirrors the orchestration
// half of examples/.../odroid_system.c so a future real-retro-go port matches.
//

#include <stdio.h>

#include "odroid_system.h"

#ifndef APP_SAVE_PREFIX
#define APP_SAVE_PREFIX "app"
#endif

static struct {
    state_handler_t             load;
    state_handler_t             save;
    screenshot_handler_t        screenshot;
    shutdown_handler_t          shutdown;
    sleep_post_wakeup_handler_t wakeup;
    sram_save_handler_t         sram_save;
} g_app;

void odroid_system_emu_init(state_handler_t load_cb, state_handler_t save_cb,
                            screenshot_handler_t screenshot_cb,
                            shutdown_handler_t shutdown_cb,
                            sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                            sram_save_handler_t sram_save_cb)
{
    g_app.load       = load_cb;
    g_app.save       = save_cb;
    g_app.screenshot = screenshot_cb;
    g_app.shutdown   = shutdown_cb;
    g_app.wakeup     = sleep_post_wakeup_cb;
    g_app.sram_save  = sram_save_cb;
}

const char *odroid_system_get_path(emu_path_type_t type, const char *rom_path)
{
    (void)rom_path;   // single-app flash-only build: no per-ROM namespacing yet
    static char buf[24];

    switch (type) {
    case ODROID_PATH_SAVE_SRAM:
        return APP_SAVE_PREFIX ".sram";
    case ODROID_PATH_SAVE_STATE_1:
    case ODROID_PATH_SAVE_STATE_2:
    case ODROID_PATH_SAVE_STATE_3:
        snprintf(buf, sizeof buf, APP_SAVE_PREFIX "-%d.sav", (int)type);
        return buf;
    default:
        return APP_SAVE_PREFIX ".dat";
    }
}

bool odroid_system_emu_save_state(int slot)
{
    const char *path = odroid_system_get_path(
        (emu_path_type_t)(ODROID_PATH_SAVE_STATE_1 + slot), NULL);
    return g_app.save ? g_app.save(path) : false;
}

bool odroid_system_emu_load_state(int slot)
{
    const char *path = odroid_system_get_path(
        (emu_path_type_t)(ODROID_PATH_SAVE_STATE_1 + slot), NULL);
    return g_app.load ? g_app.load(path) : false;
}

void odroid_system_sram_save(void)
{
    if (g_app.sram_save)
        g_app.sram_save();
}

// retro-go's "leave this app". On the real firmware this returns to the launcher;
// this toolkit has no launcher, so the G&W's equivalent of "exit" is Standby.
// (The core calls this from I_Quit; POWER+PAUSE reaches power_off directly.)
extern void power_off(void);
void odroid_system_switch_app(int app)
{
    (void)app;
    power_off();   // does not return
}
