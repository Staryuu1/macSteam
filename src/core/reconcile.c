// Package-cache and library refresh
#include "reconcile.h"
#include "../util/log.h"
#include "../util/file.h"
#include "../config/config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static int sx_reconcile_after_inject(void);
static int sx_reconcile_run_pending_if_ready(void);

static uintptr_t g_engine_ref_fn = 0;

typedef void (*fn_clearmap)(void *map_base);
static fn_clearmap g_clearmap_fn = NULL;

typedef int64_t (*fn_readdisk)(void *cache_base);
static fn_readdisk g_readdisk_fn = NULL;

typedef void (*fn_appmgr_markdirty)(void *app_mgr, uint32_t package_id, char flag);
typedef void (*fn_appmgr_op)(void *app_mgr);
static fn_appmgr_markdirty g_markdirty_fn = NULL;
static fn_appmgr_op g_recompute_fn = NULL;
static fn_appmgr_op g_emit_fn      = NULL;
typedef void (*fn_post_callback)(void *user, uint32_t callback, const void *data,
                                 uint32_t size, uint64_t call);
static fn_post_callback g_post_callback_fn = NULL;

// Layout and callback ID verified in the bundled ARM64 EmitAppLicensesChanged.
typedef struct {
    uint8_t full_update, reserved[3];
    uint32_t remaining_batches, app_count;
    uint32_t app_ids[64];
    uint32_t padding;
    uint64_t flags;
} sx_app_licenses_changed_t;
_Static_assert(sizeof(sx_app_licenses_changed_t) == 0x118, "license callback size");
_Static_assert(offsetof(sx_app_licenses_changed_t, flags) == 0x110, "license callback flags");

static int *g_refresh_app_ids = NULL;
static int        g_refresh_app_count = 0;

// Applied from CUser::RunFrame on Steam's user frame thread.
static atomic_int g_refresh_armed = 0;

static atomic_int g_login_observed = 0;

void sx_reconcile_set_login_observed(void) {
    if (!g_login_observed) {
        g_login_observed = 1;
        SX_LOG("[reconcile] login observed (BLoggedOn). Deferred reconcile + library refresh may now run");
    }
}

int sx_reconcile_login_observed(void) {
    return g_login_observed;
}

static atomic_int g_reconcile_pending = 0;

#define APPMGR_DIRTY_COUNT_OFF 9140
#define ENGINE_REF_ADRP_OFF   0x48 // Verified ADRP+ADD in the bundled ARM64 build.
#define CPKGINFOCACHE_OFF     4400
#define CPKGINFOCACHE_MAP_OFF 8

static uintptr_t decode_adrp_add(uintptr_t site) {
    uint32_t adrp = *(uint32_t *)site;
    uint32_t add  = *(uint32_t *)(site + 4);

    if ((adrp & 0x9F000000u) != 0x90000000u)
        return 0;
    if ((add & 0xFFC00000u) != 0x91000000u)
        return 0;

    uint32_t immlo = (adrp >> 29) & 0x3;
    uint32_t immhi = (adrp >> 5)  & 0x7FFFF;
    int64_t  imm   = (int64_t)((immhi << 2) | immlo);
    if (imm & (1LL << 20))
        imm -= (1LL << 21);
    uintptr_t page = site & ~(uintptr_t)0xFFF;
    uintptr_t adrp_target = page + (uintptr_t)(imm * 4096);

    uint32_t adrp_rd = adrp & 0x1F;
    uint32_t add_rn  = (add >> 5) & 0x1F;
    if (adrp_rd != add_rn)
        return 0;

    uint32_t imm12 = (add >> 10) & 0xFFF;
    uint32_t sh    = (add >> 22) & 0x1;
    if (sh) imm12 <<= 12;

    return adrp_target + imm12;
}

static void *resolve_csteamengine(void) {
    if (!g_engine_ref_fn) {
        SX_WARN("[reconcile] CSteamEngine anchor unresolved");
        return NULL;
    }
    uintptr_t global_addr = decode_adrp_add(g_engine_ref_fn + ENGINE_REF_ADRP_OFF);
    if (!global_addr) {
        SX_WARN("[reconcile] ADRP+ADD decode failed at anchor+0x%x",
                ENGINE_REF_ADRP_OFF);
        return NULL;
    }
    void *engine = *(void **)global_addr;
    SX_DBG("[reconcile] engine global @ %p -> CSteamEngine %p",
           (void *)global_addr, engine);
    return engine;
}


// Repair pkg-20200 in packageinfo.vdf so the re-read doesn't drop it.
static const uint8_t PKG_VDF_CORRUPT_MARKER[] = {
    0x20, 0x20, 0x01, 0x00, 0x02,
    'b', 'i', 'l', 'l', 'i', 'n', 'g', 't', 'y', 'p', 'e'
};
static const uint8_t PKG_VDF_CLEAN_ID[] = { 0xE8, 0x4E, 0x00, 0x00 };

static int sx_reconcile_repair_packageinfo_vdf(void) {
    const char *home = sx_resolve_home();
    if (!home) return 0;

    char path[1024];
    sx_steam_appcache_path(path, sizeof(path), home, "packageinfo.vdf");

    int rc = sx_file_patch_bytes(path, PKG_VDF_CORRUPT_MARKER,
                                 sizeof(PKG_VDF_CORRUPT_MARKER),
                                 PKG_VDF_CLEAN_ID, sizeof(PKG_VDF_CLEAN_ID));
    if (rc == 1)
        SX_LOG("[reconcile] packageinfo.vdf repaired pkg 20200 (73760 -> 20200)");
    else if (rc < 0)
        SX_WARN("[reconcile] packageinfo.vdf repair failed");
    return rc == 1;
}


void sx_reconcile_set_addrs(uintptr_t engine_ref_fn,
                            uintptr_t clearmap_fn,
                            uintptr_t readdisk_fn) {
    g_engine_ref_fn = engine_ref_fn;
    g_clearmap_fn   = (fn_clearmap)clearmap_fn;
    g_readdisk_fn   = (fn_readdisk)readdisk_fn;

    if (!engine_ref_fn || !clearmap_fn || !readdisk_fn) {
        SX_WARN("[reconcile] disabled, unresolved addrs "
                "(engine_ref=%p clearmap=%p readdisk=%p)",
                (void *)engine_ref_fn, (void *)clearmap_fn, (void *)readdisk_fn);
    } else {
        SX_DBG("[reconcile] addrs set: engine_ref=%p clearmap=%p readdisk=%p",
               (void *)engine_ref_fn, (void *)clearmap_fn, (void *)readdisk_fn);
    }
}

void sx_reconcile_set_library_refresh_fns(uintptr_t markdirty_fn,
                                          uintptr_t recompute_fn,
                                          uintptr_t emit_fn,
                                          uintptr_t post_callback_fn) {
    g_markdirty_fn = (fn_appmgr_markdirty)markdirty_fn;
    g_recompute_fn = (fn_appmgr_op)recompute_fn;
    g_emit_fn      = (fn_appmgr_op)emit_fn;
    g_post_callback_fn = (fn_post_callback)post_callback_fn;
    if (!markdirty_fn || !recompute_fn || !emit_fn || !post_callback_fn) {
        SX_WARN("[reconcile] auto-refresh disabled, native functions unresolved "
                "(markdirty=%p recompute=%p emit=%p post=%p). Reconcile still runs but "
                "UI needs a manual online/offline toggle",
                (void *)markdirty_fn, (void *)recompute_fn, (void *)emit_fn, (void *)post_callback_fn);
    } else {
        SX_DBG("[reconcile] library refresh functions set: MarkPackageDirty=%p "
               "RecomputeSubscribedApps=%p EmitAppLicensesChanged=%p",
               (void *)markdirty_fn, (void *)recompute_fn, (void *)emit_fn);
    }
}

int sx_reconcile_reload_ready(void) {
    return g_engine_ref_fn && g_clearmap_fn && g_readdisk_fn &&
           g_markdirty_fn && g_recompute_fn && g_emit_fn && g_post_callback_fn &&
           decode_adrp_add(g_engine_ref_fn + ENGINE_REF_ADRP_OFF);
}

int sx_reconcile_set_library_refresh_apps(const int *app_ids, int app_count) {
    int *copy = NULL;
    if (app_count > 0) {
        copy = malloc((size_t)app_count * sizeof(int));
        if (!copy) return -1;
        memcpy(copy, app_ids, (size_t)app_count * sizeof(int));
    }
    free(g_refresh_app_ids);
    g_refresh_app_ids = copy;
    g_refresh_app_count = app_count;
    return 0;
}

void sx_reconcile_arm_library_refresh(void) {
    if (!g_markdirty_fn || !g_recompute_fn || !g_emit_fn || !g_post_callback_fn) {
        SX_WARN("[reconcile] cannot arm library refresh, native functions unresolved");
        return;
    }
    g_refresh_armed = 1;
    SX_LOG("[reconcile] library refresh armed for %d app(s), waiting for CUser::RunFrame", g_refresh_app_count);
}

int sx_reconcile_fire_library_refresh(void *app_mgr) {
    if (!g_refresh_armed)
        return 0;
    if (!g_login_observed)
        return 0;
    if (!app_mgr) {
        SX_WARN("[reconcile] library refresh waiting for a live CUser");
        return 0;
    }

    if (!atomic_exchange(&g_refresh_armed, 0)) return 0; // Claim before native calls can re-enter.
    sx_reconcile_run_pending_if_ready();

    SX_LOG("[reconcile] firing native library refresh on CUserAppManager=%p (x%d apps)",
           app_mgr, g_refresh_app_count);

    sx_config_t *cfg __attribute__((cleanup(sx_config_release))) = sx_config_acquire();
    for (int i = 0; cfg && i < cfg->pkg_count; i++)
        g_markdirty_fn(app_mgr, (uint32_t)cfg->package_ids[i], 0);
    SX_LOG("[reconcile] marked %d package(s) dirty (user+9140=%d)",
           cfg ? cfg->pkg_count : 0, *(volatile int *)((uint8_t *)app_mgr + APPMGR_DIRTY_COUNT_OFF));

    g_recompute_fn(app_mgr);
    g_emit_fn(app_mgr);
    // Native emission only visits current package apps; include removed apps explicitly.
    for (int i = 0; i < g_refresh_app_count;) {
        sx_app_licenses_changed_t event = {0};
        int count = g_refresh_app_count - i;
        if (count > 64) count = 64;
        event.app_count = (uint32_t)count;
        event.remaining_batches = (uint32_t)((g_refresh_app_count - i - count + 63) / 64);
        for (int j = 0; j < count; j++)
            event.app_ids[j] = (uint32_t)g_refresh_app_ids[i++];
        g_post_callback_fn(app_mgr, 0xf90be, &event, sizeof(event), 0);
    }
    SX_LOG("[reconcile] notified Library of %d affected app(s), including removals", g_refresh_app_count);
    return 1;
}

void sx_reconcile_arm_after_inject(void) {
    if (!g_engine_ref_fn || !g_clearmap_fn || !g_readdisk_fn) {
        SX_WARN("[reconcile] cannot arm reconcile, addresses unavailable");
        return;
    }
    g_reconcile_pending = 1;
    SX_LOG("[reconcile] reconcile armed (ClearMap+ReadFromDisk), runs from first post-login hook");
}

static int sx_reconcile_run_pending_if_ready(void) {
    if (!g_reconcile_pending)
        return 0;
    if (!g_login_observed)
        return 0;
    if (!atomic_exchange(&g_reconcile_pending, 0)) return 0;
    SX_LOG("[reconcile] login complete. Running deferred package-cache "
           "reconcile now");
    return sx_reconcile_after_inject();
}

static int sx_reconcile_after_inject(void) {
    sx_reconcile_repair_packageinfo_vdf();

    void *engine = resolve_csteamengine();
    if (!engine) {
        SX_WARN("[reconcile] CSteamEngine unresolved, aborting reconcile");
        return -1;
    }

    void *cache    = (void *)((uint8_t *)engine + CPKGINFOCACHE_OFF);
    void *map_base = (void *)((uint8_t *)cache  + CPKGINFOCACHE_MAP_OFF);

    SX_LOG("[reconcile] engine=%p cache=%p map=%p, clearing map + re-reading disk",
           engine, cache, map_base);

    g_clearmap_fn(map_base);

    int64_t npkgs = g_readdisk_fn(cache);

    SX_LOG("[reconcile] ReadFromDisk re-read %lld package(s). PkgParse should have re-fired",
           (long long)npkgs);

    return (int)npkgs;
}
