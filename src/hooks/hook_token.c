// PICS access tokens, applied before Steam computes the serialized size.
#include "hooks.h"
#include "../config/config.h"
#include "../steam_types.h"
#include "../util/log.h"
#include <limits.h>
#include <string.h>

typedef struct { const uint8_t *p, *end; } wire_t;

static int varint(wire_t *w, uint64_t *value) {
    *value = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
        if (w->p == w->end) return -1;
        uint8_t byte = *w->p++;
        if (shift == 63 && byte > 1) return -1;
        *value |= (uint64_t)(byte & 127) << shift;
        if (!(byte & 128)) return 0;
    }
    return -1;
}

// Read only the protobuf wire types used by PICS; malformed input produces no verdict.
static int field(wire_t *w, uint32_t *tag, uint64_t *value, wire_t *bytes) {
    uint64_t key, length;
    if (varint(w, &key) || key < 8 || key >> 3 > 0x1fffffff) return -1;
    *tag = (uint32_t)key;
    *value = 0;
    *bytes = (wire_t){w->p, w->p};
    switch (key & 7) {
        case 0: return varint(w, value);
        case 1: length = 8; break;
        case 2: if (varint(w, &length)) return -1; break;
        case 5: length = 4; break;
        default: return -1;
    }
    if (length > (uint64_t)(w->end - w->p)) return -1;
    *bytes = (wire_t){w->p, w->p + (size_t)length};
    w->p = bytes->end;
    return 0;
}

static int observe_response(wire_t body, sx_config_t *cfg, int log) {
    while (body.p < body.end) {
        uint32_t tag;
        uint64_t value;
        wire_t app;
        if (field(&body, &tag, &value, &app)) return -1;
        if (tag != 10) continue; // apps = field 1, length-delimited
        uint64_t id = 0, missing = 0, public_only = 0;
        size_t metadata_size = 0;
        while (app.p < app.end) {
            wire_t bytes;
            if (field(&app, &tag, &value, &bytes)) return -1;
            if (tag == 8) id = value;
            if (tag == 24) missing = value;
            if (tag == 48) public_only = value;
            if (tag == 42) metadata_size = (size_t)(bytes.end - bytes.p);
        }
        if (log && id && id <= INT_MAX && sx_config_has_app(cfg, (int)id) &&
            sx_config_get_token(cfg, (uint32_t)id)) {
            SX_LOG("PICS response: app=%u missing_token=%s only_public=%s metadata_bytes=%zu",
                   (uint32_t)id, missing ? "true" : "false",
                   public_only ? "true" : "false", metadata_size);
        }
    }
    return 0;
}

// shortcut: packet backing and virtual size slot verified on build 1788652215; reverify on updates.
void sx_hooks_token_observe_packet(void *self, void *packet) {
    CProtoBufMsg_t *msg = self;
    if (!msg || !packet || msg->eMsg != UINT32_C(0x800022c8) ||
        ((const uint8_t *)self)[0x19]) return; // Steam's parse-error flag
    sx_config_t *cfg __attribute__((cleanup(sx_config_release))) = sx_config_acquire();
    if (!cfg || !cfg->token_count) return;
    void **vtable = *(void ***)packet;
    uint32_t size = ((uint32_t (*)(void *))vtable[8])(packet);
    const uint8_t *storage = *(const uint8_t **)((const uint8_t *)packet + 0x10);
    if (!storage || size < 8) return;
    const uint8_t *raw = *(const uint8_t *const *)(storage + 8);
    if (!raw) return;
    uint32_t emsg, header_size;
    memcpy(&emsg, raw, 4);
    memcpy(&header_size, raw + 4, 4);
    if (emsg != UINT32_C(0x800022c8) || header_size > size - 8) return;
    wire_t body = {raw + 8 + header_size, raw + size};
    if (observe_response(body, cfg, 0) == 0) observe_response(body, cfg, 1);
}

static void *orig_BAsyncSend;
typedef uint64_t (*fn_BAsyncSend)(void *self, uint64_t route);

static int aligned(const void *ptr) {
    return ptr && (uintptr_t)ptr % _Alignof(void *) == 0;
}

static void inject_tokens(CProtoBufMsg_t *msg, sx_config_t *cfg) {
    if (!aligned(msg->body)) return;
    PICSProductInfoRequest_t *req = msg->body;
    if (req->app_count < 0 || req->app_capacity < req->app_count) return;
    if (!req->app_count) return;
    PICSAppsRep_t *rep = req->apps;
    if (!aligned(rep) || rep->allocated < req->app_count ||
        rep->allocated > req->app_capacity ||
        (size_t)req->app_count > (SIZE_MAX - sizeof(*rep)) / sizeof(rep->entries[0])) return;
    // Validate the whole vector before writing, so a bad entry cannot leave a partial patch.
    for (int i = 0; i < req->app_count; i++)
        if (!aligned(rep->entries[i])) return;
    for (int i = 0; i < req->app_count; i++) {
        PICSApp_t *app = rep->entries[i];
        if (!(app->has_bits & 2) || app->app_id > INT_MAX ||
            !sx_config_has_app(cfg, (int)app->app_id)) continue;
        uint64_t token = sx_config_get_token(cfg, app->app_id);
        if (!token) continue;
        app->access_token = token;
        app->has_bits |= 1;
        SX_LOG_ONCE_KEY(app->app_id, "PICS: applied configured access token for app=%u", app->app_id);
    }
}

static uint64_t hook_BAsyncSend(void *self, uint64_t route) {
    if (aligned(self) && !sx_hook_passthrough("BAsyncSend")) {
        CProtoBufMsg_t *msg = self;
        if (msg->eMsg == (UINT32_C(0x80000000) | 8903)) {
            sx_config_t *cfg __attribute__((cleanup(sx_config_release))) = sx_config_acquire();
            if (cfg && cfg->token_count) inject_tokens(msg, cfg);
        }
    }
    return ((fn_BAsyncSend)orig_BAsyncSend)(self, route);
}

static sx_hook_def_t g_hooks[] = {
    {
        .name = "BAsyncSend",
        .sig_name = "CProtoBufMsg::BAsyncSend",
        .hook_fn = (void *)hook_BAsyncSend,
        .orig_fn = &orig_BAsyncSend,
    },
};

int sx_hooks_token_count(void) { return (int)(sizeof(g_hooks) / sizeof(g_hooks[0])); }
sx_hook_def_t *sx_hooks_token_defs(void) { return g_hooks; }
