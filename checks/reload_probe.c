#include "config/config.h"
#include "config/reload.h"
#include "core/reconcile.h"
#include "util/file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/macsteam-reload.XXXXXX";
static int refreshes, cache_refreshes, affected[16], affected_count;

const char *sx_resolve_home(void) { return root; }
void sx_macsteam_support_path(char *buf, size_t size, const char *home, const char *rel) {
    (void)home;
    snprintf(buf, size, "%s/%s", root, rel ? rel : "");
}
void sx_file_config_path(char *buf, size_t size) {
    snprintf(buf, size, "%s/config.yaml", root);
}
int sx_reconcile_reload_ready(void) { return 1; }
int sx_reconcile_login_observed(void) { return 1; }
int sx_reconcile_set_library_refresh_apps(const int *ids, int count) {
    assert(count <= 16);
    if (count) memcpy(affected, ids, (size_t)count * sizeof(int));
    affected_count = count;
    return 0;
}
void sx_reconcile_arm_after_inject(void) { cache_refreshes++; }
void sx_reconcile_arm_library_refresh(void) { refreshes++; }

static void write_file(const char *name, const char *text) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *f = fopen(path, "w");
    assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}

static void remove_file(const char *name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    assert(unlink(path) == 0);
}

static void wait_for_apps(int first, int second, int count) {
    for (int i = 0; i < 60; i++) {
        sx_reload_apply();
        sx_config_t *cfg = sx_config_acquire();
        int ready = cfg->app_count == count && (!first || sx_config_has_app(cfg, first)) &&
                    (!second || sx_config_has_app(cfg, second));
        sx_config_release(&cfg);
        if (ready) return;
        usleep(100000);
    }
    assert(!"hot reload timed out");
}

static void expect_rejected(sx_config_t *expected) {
    for (int i = 0; i < 20; i++) { usleep(100000); sx_reload_apply(); }
    sx_config_t *cfg = sx_config_acquire();
    assert(cfg == expected);
    sx_config_release(&cfg);
}

int main(void) {
    assert(mkdtemp(root));
    char path[1024], renamed[1024];
    snprintf(path, sizeof(path), "%s/lua", root);
    assert(mkdir(path, 0700) == 0);
    write_file("config.yaml", "Apps:\n  - 42\nPackageIds:\n  - 20200\n");
    sx_file_config_path(path, sizeof(path));
    sx_config_t *initial = calloc(1, sizeof(*initial));
    assert(initial && sx_config_load(path, initial) == 0);
    sx_config_publish(initial);
    sx_config_t *held = sx_config_acquire();
    sx_reload_start();

    write_file("lua/a.lua", "addappid(100)\n");
    wait_for_apps(42, 100, 2);
    assert(held->app_count == 1 && sx_config_has_app(held, 42));
    sx_config_release(&held); // Old snapshots remain alive until the last reader leaves.

    held = sx_config_acquire();
    write_file("lua/a.lua", "addappid(200)\ninvalid()\n");
    expect_rejected(held);
    write_file("config.yaml", "Apps:\n  - invalid\nPackageIds:\n  - 20200\n");
    write_file("lua/a.lua", "addappid(200)\n");
    expect_rejected(held);
    write_file("config.yaml", "Apps:\n  - 43\nPackageIds:\n  - 20200\nHideWhatsNew: true\n");
    wait_for_apps(43, 200, 2);
    sx_config_release(&held);
    assert(affected_count == 4); // Both removed and added apps must be marked dirty.
    assert(affected[0] == 42 && affected[1] == 100);

    snprintf(path, sizeof(path), "%s/lua/a.lua", root);
    snprintf(renamed, sizeof(renamed), "%s/lua/b.lua", root);
    assert(rename(path, renamed) == 0);
    write_file("lua/replacement.tmp", "addappid(300)\n");
    snprintf(path, sizeof(path), "%s/lua/replacement.tmp", root);
    assert(rename(path, renamed) == 0); // Editors commonly save using atomic replacement.
    wait_for_apps(43, 300, 2);
    remove_file("lua/b.lua");
    wait_for_apps(43, 0, 1);

    held = sx_config_acquire();
    assert(held->hide_whats_new);
    write_file("config.yaml", "Apps:\n  - 99\nPackageIds:\n  - 20201\n");
    expect_rejected(held);
    remove_file("config.yaml");
    expect_rejected(held);
    sx_config_release(&held);
    sx_reload_stop();
    sx_config_publish(NULL);
    assert(refreshes >= 4 && refreshes == cache_refreshes);
    snprintf(path, sizeof(path), "%s/lua", root);
    assert(rmdir(path) == 0 && rmdir(root) == 0);
    puts("Hot reload checks passed (Steam calls mocked)");
    return 0;
}
