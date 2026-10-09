#include "../src/core/reconcile.c"
#include <assert.h>

static unsigned char user[APPMGR_DIRTY_COUNT_OFF + sizeof(int)];
static int stage, notifications, received;
static int affected[65];

static void mark_package(void *self, uint32_t id, char flag) {
    assert(self == user && stage == 0 && id == 20200 && flag == 0);
    stage = 1;
}

static void recompute(void *self) {
    assert(self == user && stage == 1);
    stage = 2;
}

static void emit(void *self) {
    assert(self == user && stage == 2);
    stage = 3;
}

static void post(void *self, uint32_t callback, const void *data,
                 uint32_t size, uint64_t call) {
    const sx_app_licenses_changed_t *event = data;
    assert(self == user && stage == 3 && callback == 0xf90be);
    assert(size == 0x118 && call == 0 && !event->full_update && !event->flags);
    assert(event->app_count == (notifications ? 1 : 64));
    assert(event->remaining_batches == (notifications ? 0 : 1));
    for (uint32_t i = 0; i < event->app_count; i++)
        assert(event->app_ids[i] == (uint32_t)affected[received++]);
    notifications++;
}

int main(void) {
    sx_config_t *cfg = calloc(1, sizeof(*cfg));
    assert(cfg);
    cfg->package_ids = malloc(sizeof(*cfg->package_ids));
    cfg->app_ids = malloc(sizeof(*cfg->app_ids));
    assert(cfg->package_ids && cfg->app_ids);
    cfg->package_ids[0] = 20200;
    cfg->pkg_count = 1;
    cfg->app_ids[0] = 42;
    cfg->app_count = 1;
    sx_config_publish(cfg);
    for (int i = 0; i < 65; i++) affected[i] = 42 + i;
    // Most affected apps were removed; the native emitter no longer sees them.
    assert(sx_reconcile_set_library_refresh_apps(affected, 65) == 0);
    sx_reconcile_set_library_refresh_fns((uintptr_t)mark_package, (uintptr_t)recompute,
                                       (uintptr_t)emit, (uintptr_t)post);
    sx_reconcile_arm_library_refresh();
    assert(sx_reconcile_fire_library_refresh(user) == 0); // Wait for login.
    sx_reconcile_set_login_observed();
    assert(sx_reconcile_fire_library_refresh(user) == 1);
    assert(received == 65 && notifications == 2);
    assert(sx_reconcile_fire_library_refresh(user) == 0); // Consume once.

    // Also notify removed apps when the final Lua file leaves no configured apps.
    cfg = calloc(1, sizeof(*cfg));
    assert(cfg);
    cfg->package_ids = malloc(sizeof(*cfg->package_ids));
    assert(cfg->package_ids);
    cfg->package_ids[0] = 20200;
    cfg->pkg_count = 1;
    sx_config_publish(cfg);
    stage = notifications = received = 0;
    sx_reconcile_arm_library_refresh();
    assert(sx_reconcile_fire_library_refresh(user) == 1);
    assert(received == 65 && notifications == 2);
    assert(sx_reconcile_set_library_refresh_apps(NULL, 0) == 0);
    sx_config_publish(NULL);
    puts("Package refresh and removal notification checks passed (Steam calls mocked)");
    return 0;
}
