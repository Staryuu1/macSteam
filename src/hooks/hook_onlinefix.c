// Opt-in Spacewar launch and IPC identity routing.
#include "hooks.h"
#include "../util/log.h"
#include <dobby.h>
#include <ctype.h>
#include <stdatomic.h>
#include <string.h>

#define ONLINEFIX_APP_ID 480u
#define GET_APP_ID_HASH 0x09607ec4u
#define NETWORK_ACTIVE (UINT64_C(1) << 32)
#define KEEP_REAL_APP_ID (UINT64_C(1) << 33)

// shortcut: one active game, use per-pipe state before supporting concurrent games.
static _Atomic uint64_t g_launch;
static atomic_int g_ready;
static int onlinefix_enabled(void);

// CUtlBuffer layout verified on Steam ARM64 build 1788652215.
typedef struct {
    unsigned char *data;
    int32_t capacity, grow_size, get, put, max_put;
    uint8_t reserved[2], error, flags;
} onlinefix_buffer_t;

_Static_assert(offsetof(onlinefix_buffer_t, get) == 0x10, "CUtlBuffer get");
_Static_assert(offsetof(onlinefix_buffer_t, error) == 0x1e, "CUtlBuffer error");

static int has_launch_arg(const char *command, const char *wanted) {
    if (!command) return 0;
    while (*command) {
        while (isspace((unsigned char)*command)) command++;
        if (!*command) break;
        size_t matched = 0;
        int equal = 1;
        char quote = 0;
        while (*command && (quote || !isspace((unsigned char)*command))) {
            char c = *command++;
            if ((c == '\'' || c == '"') && (!quote || quote == c)) {
                quote = quote ? 0 : c;
                continue;
            }
            if (c == '\\' && quote != '\'' && *command) c = *command++;
            if (equal && wanted[matched] && c == wanted[matched]) matched++;
            else equal = 0;
        }
        if (!quote && equal && !wanted[matched]) return 1;
    }
    return 0;
}

static uint32_t real_app_id(uint32_t app_id, uint64_t launch) {
    return app_id == ONLINEFIX_APP_ID && (uint32_t)launch ? (uint32_t)launch : app_id;
}

static void instrument_SpawnProcess(void *address, DobbyRegisterContext *ctx) {
    (void)address;
    if (!onlinefix_enabled()) return;
    const char *command = (const char *)(uintptr_t)ctx->general.regs.x2;
    uint64_t *game_id = (uint64_t *)(uintptr_t)ctx->general.regs.x4;
    uint64_t launch = 0;
    if (game_id && *game_id && *game_id <= 0xffffff &&
        *game_id != ONLINEFIX_APP_ID && has_launch_arg(command, "-onlinefix")) {
        launch = *game_id;
        if (has_launch_arg(command, "-realappid")) launch |= KEEP_REAL_APP_ID;
        *game_id = ONLINEFIX_APP_ID;
        SX_LOG("[onlinefix] launch app %u via 480 (realappid=%d)",
               (uint32_t)launch, !!(launch & KEEP_REAL_APP_ID));
    }
    atomic_store(&g_launch, launch);
}

static void instrument_BuildSpawnEnvBlock(void *address, DobbyRegisterContext *ctx) {
    (void)address;
    if (!onlinefix_enabled()) return;
    uint64_t *game_id = (uint64_t *)(uintptr_t)ctx->general.regs.x1;
    uint64_t *overlay_id = (uint64_t *)(uintptr_t)ctx->general.regs.x2;
    uint64_t launch = atomic_load(&g_launch);
    // macOS overlay ID is x2; x1 still supplies SteamGameId and SteamAppId.
    if (game_id && overlay_id && overlay_id != game_id &&
        *game_id == ONLINEFIX_APP_ID && *overlay_id == ONLINEFIX_APP_ID && (uint32_t)launch)
        *overlay_id = (uint32_t)launch;
}

static void *orig_OptedInMask;
typedef uint64_t (*fn_OptedInMask)(void *, uint32_t);

static uint64_t hook_OptedInMask(void *self, uint32_t app_id) {
    if (onlinefix_enabled()) app_id = real_app_id(app_id, atomic_load(&g_launch));
    return ((fn_OptedInMask)orig_OptedInMask)(self, app_id);
}

static int buffer_valid(const onlinefix_buffer_t *buf) {
    return buf && buf->data && !buf->error && !(buf->flags & 1) &&
           buf->capacity >= 0 && buf->get >= 0 && buf->put >= 0 &&
           buf->max_put >= buf->get && buf->max_put <= buf->capacity &&
           buf->put <= buf->capacity;
}

typedef uint64_t (*fn_ServerDispatch)(void *, void *, void *, void *);
static void *orig_UtilsDispatch;
static void *orig_NetworkDispatch;

static uint64_t hook_UtilsDispatch(void *self, void *read, void *write, void *extra) {
    onlinefix_buffer_t *request = read, *response = write;
    uint64_t launch = atomic_load(&g_launch);
    uint32_t method = 0;
    int32_t start = -1;
    if (onlinefix_enabled() && (uint32_t)launch &&
        (!(launch & NETWORK_ACTIVE) || (launch & KEEP_REAL_APP_ID)) &&
        buffer_valid(request) && request->max_put - request->get >= 4 &&
        response && !response->error && !(response->flags & 1) &&
        response->put >= 0 && response->put <= response->capacity) {
        memcpy(&method, request->data + request->get, sizeof(method));
        start = response->put;
    }
    uint64_t result = ((fn_ServerDispatch)orig_UtilsDispatch)(self, read, write, extra);
    if (method == GET_APP_ID_HASH && start >= 0 && onlinefix_enabled() &&
        atomic_load(&g_launch) == launch && buffer_valid(response) &&
        response->put >= start && response->put - start >= 5 && response->data[start] == 0x0b) {
        uint32_t current;
        memcpy(&current, response->data + start + 1, sizeof(current));
        uint32_t real = real_app_id(current, launch);
        memcpy(response->data + start + 1, &real, sizeof(real));
    }
    return result;
}

static uint64_t hook_NetworkDispatch(void *self, void *read, void *write, void *extra) {
    onlinefix_buffer_t *request = read;
    if (onlinefix_enabled() && buffer_valid(request) && request->max_put - request->get >= 4) {
        uint64_t launch = atomic_load(&g_launch);
        if ((uint32_t)launch && !(launch & NETWORK_ACTIVE)) {
            if (atomic_compare_exchange_strong(&g_launch, &launch, launch | NETWORK_ACTIVE))
                SX_LOG("[onlinefix] networking active (GetAppID=%u)",
                       launch & KEEP_REAL_APP_ID ? (uint32_t)launch : ONLINEFIX_APP_ID);
        }
    }
    return ((fn_ServerDispatch)orig_NetworkDispatch)(self, read, write, extra);
}

static sx_hook_def_t g_hooks[] = {
    { "OnlineFixSpawn", "CUser::SpawnProcess", (void *)instrument_SpawnProcess,
      NULL, 1, SX_HOOK_INSTRUMENT },
    { "OnlineFixEnv", "CUser::BuildSpawnEnvBlock", (void *)instrument_BuildSpawnEnvBlock,
      NULL, 1, SX_HOOK_INSTRUMENT },
    { "OnlineFixInput", "CSteamController::OptedInMask", (void *)hook_OptedInMask,
      &orig_OptedInMask, 1, SX_HOOK_INLINE },
    { "OnlineFixUtils", "IClientUtils::__ipc_server_dispatch", (void *)hook_UtilsDispatch,
      &orig_UtilsDispatch, 1, SX_HOOK_INLINE },
    { "OnlineFixNetworking", "IClientNetworkingSocketsSerialized::__ipc_server_dispatch",
      (void *)hook_NetworkDispatch, &orig_NetworkDispatch, 1, SX_HOOK_INLINE },
};

int sx_hooks_onlinefix_count(void) { return sizeof(g_hooks) / sizeof(g_hooks[0]); }
sx_hook_def_t *sx_hooks_onlinefix_defs(void) { return g_hooks; }

void sx_hooks_onlinefix_set_ready(int ready) {
    atomic_store(&g_ready, ready);
    if (!ready) SX_WARN("[onlinefix] disabled: all five hooks are required");
}

static int onlinefix_enabled(void) {
    if (!atomic_load(&g_ready)) return 0;
    for (int i = 0; i < sx_hooks_onlinefix_count(); i++)
        if (sx_hook_passthrough(g_hooks[i].name)) return 0;
    return 1;
}
