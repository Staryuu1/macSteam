// Config file parser
#include "config.h"
#include "../util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

static sx_config_t *g_current;
static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;

sx_config_t *sx_config_acquire(void) {
    pthread_mutex_lock(&g_config_lock);
    sx_config_t *cfg = g_current;
    if (cfg) cfg->references++;
    pthread_mutex_unlock(&g_config_lock);
    return cfg;
}

void sx_config_release(sx_config_t **ptr) {
    sx_config_t *cfg = *ptr;
    if (!cfg) return;
    pthread_mutex_lock(&g_config_lock);
    int destroy = --cfg->references == 0;
    pthread_mutex_unlock(&g_config_lock);
    if (destroy) { sx_config_free(cfg); free(cfg); }
    *ptr = NULL;
}

void sx_config_publish(sx_config_t *cfg) {
    pthread_mutex_lock(&g_config_lock);
    sx_config_t *old = g_current;
    if (cfg) cfg->references = 1;
    g_current = cfg;
    pthread_mutex_unlock(&g_config_lock);
    sx_config_release(&old);
}

enum parse_section {
    SEC_NONE = 0,
    SEC_APPS,
    SEC_PACKAGE_IDS,
    SEC_DEPOT_KEYS,
};

static int indent_of(const char *line) {
    int n = 0;
    while (line[n] == ' ') n++;
    return n;
}

static void strip_trailing(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == ' '  || s[len-1] == '\t'))
        s[--len] = '\0';
}

static const char *value_after_colon(const char *line) {
    const char *colon = strchr(line, ':');
    if (!colon) return NULL;
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    if (*colon == '\0' || *colon == '\n') return NULL;
    return colon;
}

static int section_is(const char *trimmed, const char *key) {
    size_t klen = strlen(key);
    return strncmp(trimmed, key, klen) == 0 && trimmed[klen] == ':';
}

static int key_before_colon(const char *line, char *buf, size_t buf_sz) {
    while (*line == ' ' || *line == '\t') line++;
    const char *colon = strchr(line, ':');
    if (!colon) return -1;
    size_t klen = (size_t)(colon - line);
    if (klen == 0 || klen >= buf_sz) return -1;
    memcpy(buf, line, klen);
    buf[klen] = '\0';
    while (klen > 0 && buf[klen-1] == ' ') buf[--klen] = '\0';
    return 0;
}

static void strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len-1] == '"') ||
                     (s[0] == '\'' && s[len-1] == '\''))) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static int append_int(int **arr, int *count, int *cap, int value) {
    if (*count >= *cap) {
        int new_cap = (*cap == 0) ? 32 : (*cap * 2);
        int *new_arr = (int *)realloc(*arr, (size_t)new_cap * sizeof(int));
        if (!new_arr) return -1;
        *arr = new_arr;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = value;
    return 0;
}

static int parse_id(const char *s, int *out) {
    errno = 0;
    char *end;
    long value = strtol(s, &end, 10);
    if (errno || end == s || *end || value <= 0 || value > INT_MAX) return -1;
    *out = (int)value;
    return 0;
}

int sx_config_load(const char *path, sx_config_t *cfg) {
    if (!path || !cfg) return -1;
    memset(cfg, 0, sizeof(*cfg));
    FILE *f = fopen(path, "r");
    if (!f) { SX_ERR("config: cannot open '%s'", path); return -1; }

    char line[1024];
    enum parse_section section = SEC_NONE;
    int app_cap = 0, pkg_cap = 0, dk_current_app = 0, number = 0, rc = 0;
    while (fgets(line, sizeof(line), f)) {
        number++;
        if (!strchr(line, '\n') && !feof(f)) { rc = -1; break; }
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        strip_trailing(line);
        int indent = indent_of(line);
        const char *trimmed = line + indent;
        if (!*trimmed) continue;
        if (indent == 0) {
            section = SEC_NONE;
            dk_current_app = 0;
            const char *val = value_after_colon(trimmed);
            if (section_is(trimmed, "Apps")) section = SEC_APPS;
            else if (section_is(trimmed, "PackageIds")) section = SEC_PACKAGE_IDS;
            else if (section_is(trimmed, "DepotKeys")) section = SEC_DEPOT_KEYS;
            else if (section_is(trimmed, "HideWhatsNew") && val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "yes") || !strcmp(val, "1")) cfg->hide_whats_new = 1;
                else if (strcasecmp(val, "false") && strcasecmp(val, "no") && strcmp(val, "0")) { rc = -1; break; }
                continue;
            } else { rc = -1; break; }
            if (val && strcmp(val, section == SEC_DEPOT_KEYS ? "{}" : "[]")) { rc = -1; break; }
            continue;
        }
        if (indent == 2 && (section == SEC_APPS || section == SEC_PACKAGE_IDS)) {
            int id;
            if (*trimmed != '-' || !isspace((unsigned char)trimmed[1]) || parse_id(trimmed + 2, &id)) { rc = -1; break; }
            rc = section == SEC_APPS
                ? append_int(&cfg->app_ids, &cfg->app_count, &app_cap, id)
                : append_int(&cfg->package_ids, &cfg->pkg_count, &pkg_cap, id);
            if (rc) break;
            continue;
        }
        if (indent == 2 && section == SEC_DEPOT_KEYS) {
            char key[64];
            if (key_before_colon(line, key, sizeof(key)) || parse_id(key, &dk_current_app) || value_after_colon(line)) { rc = -1; break; }
            continue;
        }
        if (indent == 4 && section == SEC_DEPOT_KEYS && dk_current_app) {
            char key[64], hex[67];
            int depot;
            const char *val = value_after_colon(line);
            if (key_before_colon(line, key, sizeof(key)) || parse_id(key, &depot) || !val || strlen(val) >= sizeof(hex)) { rc = -1; break; }
            strcpy(hex, val);
            strip_quotes(hex);
            if (strlen(hex) != 64) { rc = -1; break; }
            for (int i = 0; i < 64; i++) if (!isxdigit((unsigned char)hex[i])) rc = -1;
            if (rc) break;
            int i;
            for (i = 0; i < cfg->dk_count; i++) if (cfg->depot_keys[i].app_id == dk_current_app) break;
            if (i == cfg->dk_count) {
                if (i == SX_CONFIG_MAX_DK) { rc = -1; break; }
                cfg->depot_keys[cfg->dk_count++].app_id = dk_current_app;
            }
            int j = cfg->depot_keys[i].depot_count;
            if (j == SX_CONFIG_MAX_DEPOTS) { rc = -1; break; }
            cfg->depot_keys[i].depots[j].depot_id = depot;
            memcpy(cfg->depot_keys[i].depots[j].key, hex, 65);
            cfg->depot_keys[i].depot_count++;
            continue;
        }
        rc = -1;
        break;
    }
    if (ferror(f)) rc = -1;
    fclose(f);
    if (rc) {
        SX_ERR("config: rejected '%s' at line %d (invalid syntax/value or capacity exceeded)", path, number);
        sx_config_free(cfg);
        memset(cfg, 0, sizeof(*cfg));
        return -1;
    }
    SX_LOG("config: loaded '%s'. %d apps, %d pkgs, %d depot_key groups", path, cfg->app_count, cfg->pkg_count, cfg->dk_count);
    return 0;
}

void sx_config_free(sx_config_t *cfg) {
    if (!cfg) return;
    free(cfg->app_ids); cfg->app_ids = NULL; cfg->app_count = 0;
    free(cfg->package_ids); cfg->package_ids = NULL; cfg->pkg_count = 0;
}

static int contains_int(const int *values, int count, int target) {
    for (int i = 0; i < count; i++)
        if (values[i] == target) return 1;
    return 0;
}

int sx_config_has_app(sx_config_t *cfg, int app_id) {
    return cfg ? contains_int(cfg->app_ids, cfg->app_count, app_id) : 0;
}

int sx_config_has_package(sx_config_t *cfg, int package_id) {
    return cfg ? contains_int(cfg->package_ids, cfg->pkg_count, package_id) : 0;
}

uint64_t sx_config_get_manifest(sx_config_t *cfg, uint32_t depot_id) {
    if (!cfg) return 0;
    for (int i = 0; i < cfg->manifest_count; i++)
        if (cfg->manifests[i].depot_id == depot_id) return cfg->manifests[i].gid;
    return 0;
}

int sx_config_app_depots(sx_config_t *cfg, int app_id, int *out, int max) {
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < cfg->dk_count && n < max; i++) {
        if (cfg->depot_keys[i].app_id != app_id) continue;
        for (int j = 0; j < cfg->depot_keys[i].depot_count && n < max; j++)
            out[n++] = cfg->depot_keys[i].depots[j].depot_id;
    }
    return n;
}

static int copy_key_checked(char *dst, size_t dst_sz, const char *src) {
    size_t n = strlen(src);
    if (dst_sz == 0 || n >= dst_sz) return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

int sx_config_get_depot_key_any(sx_config_t *cfg, int depot_id, char *key_out, size_t key_out_sz) {
    if (!cfg || !key_out) return -1;
    for (int i = 0; i < cfg->dk_count; i++) {
        for (int j = 0; j < cfg->depot_keys[i].depot_count; j++) {
            if (cfg->depot_keys[i].depots[j].depot_id == depot_id)
                return copy_key_checked(key_out, key_out_sz, cfg->depot_keys[i].depots[j].key);
        }
    }
    return -1;
}

uint64_t sx_config_get_token(sx_config_t *cfg, uint32_t app_id) {
    if (!cfg) return 0;
    for (int i = 0; i < cfg->token_count; i++)
        if (cfg->tokens[i].app_id == app_id) return cfg->tokens[i].value;
    return 0;
}
