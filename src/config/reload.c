#include "reload.h"
#include "config.h"
#include "../core/reconcile.h"
#include "../util/file.h"
#include "../util/log.h"

#include <errno.h>
#include <glob.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_config_path[1024], g_lua_path[1024];
static pthread_t g_thread;
static atomic_int g_running;
static int g_started;
static pthread_mutex_t g_pending_lock = PTHREAD_MUTEX_INITIALIZER;
static sx_config_t *g_pending;

static void discard(sx_config_t *cfg) {
    if (cfg) { sx_config_free(cfg); free(cfg); }
}

static void hash_bytes(uint64_t *hash, const void *bytes, size_t size) {
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; i++) *hash = (*hash ^ p[i]) * UINT64_C(1099511628211);
}

static int hash_file(uint64_t *hash, const char *path) {
    struct stat st;
    hash_bytes(hash, path, strlen(path) + 1);
    if (stat(path, &st)) return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(st.st_mode)) return -1;
    hash_bytes(hash, &st.st_ino, sizeof(st.st_ino));
    hash_bytes(hash, &st.st_size, sizeof(st.st_size));
    hash_bytes(hash, &st.st_mtimespec, sizeof(st.st_mtimespec));
    hash_bytes(hash, &st.st_ctimespec, sizeof(st.st_ctimespec));
    return 0;
}

static int fingerprint(uint64_t *hash) {
    *hash = UINT64_C(14695981039346656037);
    if (hash_file(hash, g_config_path)) return -1;
    struct stat st;
    if (stat(g_lua_path, &st)) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return -1;
    char pattern[1100];
    if (snprintf(pattern, sizeof(pattern), "%s/*.lua", g_lua_path) >= (int)sizeof(pattern)) return -1;
    glob_t files = {0};
    int rc = glob(pattern, GLOB_ERR, NULL, &files);
    if (rc == GLOB_NOMATCH) rc = 0;
    for (size_t i = 0; !rc && i < files.gl_pathc; i++) rc = hash_file(hash, files.gl_pathv[i]);
    globfree(&files);
    return rc ? -1 : 0;
}

static void *watch_files(void *unused) {
    (void)unused;
    uint64_t observed = 0, attempted = 0;
    int have_observed = 0, have_attempted = 0;
    while (atomic_load(&g_running)) {
        uint64_t current;
        if (fingerprint(&current) != 0) {
            have_observed = 0;
        } else if (!have_observed || current != observed) {
            observed = current;
            have_observed = 1;
        } else if (!have_attempted || current != attempted) {
            sx_config_t *next = calloc(1, sizeof(*next));
            if (!next) { usleep(500000); continue; }
            int valid = sx_config_load(g_config_path, next) == 0 &&
                        sx_config_load_lua_dir(g_lua_path, next) == 0;
            uint64_t after;
            if (fingerprint(&after) == 0 && after == current) {
                attempted = current;
                have_attempted = 1;
                if (valid) {
                    pthread_mutex_lock(&g_pending_lock);
                    discard(g_pending);
                    g_pending = next;
                    pthread_mutex_unlock(&g_pending_lock);
                    next = NULL;
                    SX_LOG("[reload] valid config queued for the next Steam RunFrame");
                } else {
                    SX_WARN("[reload] rejected changes; keeping the last valid config");
                }
            }
            discard(next);
        }
        usleep(500000);
    }
    return NULL;
}

static int same_ids(const int *a, int na, const int *b, int nb) {
    return na == nb && (!na || memcmp(a, b, (size_t)na * sizeof(int)) == 0);
}

void sx_reload_apply(void) {
    if (!atomic_load(&g_running) || !sx_reconcile_login_observed()) return;
    if (pthread_mutex_trylock(&g_pending_lock) != 0) return;
    sx_config_t *next = g_pending;
    g_pending = NULL;
    pthread_mutex_unlock(&g_pending_lock);
    if (!next) return;
    sx_config_t *old __attribute__((cleanup(sx_config_release))) = sx_config_acquire();
    if (!old) { discard(next); return; }
    // License-list injection happens at login; a new package needs a fresh login.
    if (!same_ids(old->package_ids, old->pkg_count, next->package_ids, next->pkg_count)) {
        SX_WARN("[reload] PackageIds changed: restart Steam to apply; keeping the last valid config");
        discard(next);
        return;
    }
    if (same_ids(old->app_ids, old->app_count, next->app_ids, next->app_count) &&
        old->hide_whats_new == next->hide_whats_new && old->dk_count == next->dk_count &&
        memcmp(old->depot_keys, next->depot_keys, sizeof(old->depot_keys)) == 0) {
        discard(next);
        return;
    }
    int count = old->app_count + next->app_count;
    int *affected = count ? malloc((size_t)count * sizeof(int)) : NULL;
    if (count && !affected) { discard(next); SX_ERR("[reload] out of memory; config unchanged"); return; }
    int n = old->app_count;
    if (n) memcpy(affected, old->app_ids, (size_t)n * sizeof(int));
    for (int i = 0; i < next->app_count; i++)
        if (!sx_config_has_app(old, next->app_ids[i])) affected[n++] = next->app_ids[i];
    int rc = sx_reconcile_set_library_refresh_apps(affected, n);
    free(affected);
    if (rc) { discard(next); SX_ERR("[reload] cannot allocate refresh list; config unchanged"); return; }
    sx_config_publish(next);
    sx_reconcile_arm_after_inject();
    sx_reconcile_arm_library_refresh();
    SX_LOG("[reload] applied %d apps, %d depot groups; refreshing Library", next->app_count, next->dk_count);
}

void sx_reload_start(void) {
    if (g_started) return;
    if (!sx_reconcile_reload_ready()) {
        SX_WARN("[reload] disabled: package-cache/library refresh signatures unavailable");
        return;
    }
    sx_file_config_path(g_config_path, sizeof(g_config_path));
    const char *home = sx_resolve_home();
    if (!home) return;
    sx_macsteam_support_path(g_lua_path, sizeof(g_lua_path), home, "lua");
    atomic_store(&g_running, 1);
    if (pthread_create(&g_thread, NULL, watch_files, NULL)) {
        atomic_store(&g_running, 0);
        SX_ERR("[reload] cannot start watcher thread");
        return;
    }
    g_started = 1;
    SX_LOG("[reload] watching %s and %s (500 ms polling + stability delay)", g_config_path, g_lua_path);
}

void sx_reload_stop(void) {
    if (!g_started) return;
    atomic_store(&g_running, 0);
    pthread_join(g_thread, NULL);
    g_started = 0;
    pthread_mutex_lock(&g_pending_lock);
    discard(g_pending);
    g_pending = NULL;
    pthread_mutex_unlock(&g_pending_lock);
}
