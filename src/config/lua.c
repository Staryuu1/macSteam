#include "config.h"
#include "../util/log.h"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int add_app(sx_config_t *cfg, int id) {
    if (sx_config_has_app(cfg, id)) return 0;
    int *ids = realloc(cfg->app_ids, (size_t)(cfg->app_count + 1) * sizeof(int));
    if (!ids) return -1;
    cfg->app_ids = ids;
    cfg->app_ids[cfg->app_count++] = id;
    return 0;
}

static int add_key(sx_config_t *cfg, int owner, int depot, const char *key) {
    int i;
    for (i = 0; i < cfg->dk_count; i++)
        if (cfg->depot_keys[i].app_id == owner) break;
    if (i == cfg->dk_count) {
        if (i == SX_CONFIG_MAX_DK) return -1;
        cfg->depot_keys[cfg->dk_count++].app_id = owner;
    }
    int j;
    for (j = 0; j < cfg->depot_keys[i].depot_count; j++)
        if (cfg->depot_keys[i].depots[j].depot_id == depot) break;
    if (j == cfg->depot_keys[i].depot_count) {
        if (j == SX_CONFIG_MAX_DEPOTS) return -1;
        cfg->depot_keys[i].depot_count++;
    }
    cfg->depot_keys[i].depots[j].depot_id = depot;
    memcpy(cfg->depot_keys[i].depots[j].key, key, 65);
    return 0;
}

static int parse_u64(const char *p, regmatch_t match, uint64_t *out) {
    const char *start = p + match.rm_so, *end = p + match.rm_eo;
    if (*start == '"' || *start == '\'') { start++; end--; }
    errno = 0;
    char *tail;
    unsigned long long value = strtoull(start, &tail, 10);
    if (errno || tail == start || tail != end || value > UINT64_MAX) return -1;
    *out = (uint64_t)value;
    return 0;
}

static int add_manifest(sx_config_t *cfg, uint32_t depot, uint64_t gid) {
    int i;
    for (i = 0; i < cfg->manifest_count; i++)
        if (cfg->manifests[i].depot_id == depot) break;
    if (i == cfg->manifest_count) {
        if (i == SX_CONFIG_MAX_MANIFESTS) return -1;
        cfg->manifest_count++;
    }
    cfg->manifests[i].depot_id = depot;
    cfg->manifests[i].gid = gid;
    return 0;
}

static int load_lua(const char *path, regex_t *re, regex_t *manifest, regex_t *metadata, sx_config_t *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    int owner = 0, number = 0, rc = 0, ignored = 0;
    while (fgets(line, sizeof(line), f)) {
        number++;
        if (!strchr(line, '\n') && !feof(f)) { rc = -1; break; }
        char *comment = strstr(line, "--");
        if (comment) *comment = '\0';
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (!*p) continue;
        regmatch_t m[7];
        if (regexec(manifest, p, 5, m, 0) == 0) {
            uint64_t depot, gid, size;
            if (parse_u64(p, m[1], &depot) || !depot || depot > INT_MAX ||
                parse_u64(p, m[2], &gid) || !gid ||
                (m[4].rm_so >= 0 && parse_u64(p, m[4], &size))) { rc = -1; break; }
            // Match the reference consumer: preserve Steam's size, even when supplied.
            rc = add_manifest(cfg, (uint32_t)depot, gid);
            if (rc) break;
            continue;
        }
        if (regexec(metadata, p, 0, NULL, 0) == 0) {
            ignored++;
            continue;
        }
        if (regexec(re, p, 7, m, 0) != 0) { rc = -1; break; }
        errno = 0;
        long id = strtol(p + m[1].rm_so, NULL, 10);
        if (errno || id <= 0 || id > INT_MAX) { rc = -1; break; }
        // Generated files may attach a key to the first (main app) entry too.
        if (!owner) {
            owner = (int)id;
            rc = add_app(cfg, owner);
            if (rc) break;
        }
        if (m[5].rm_so >= 0) {
            // A keyed depot belongs to the most recent unkeyed app in this file.
            if (p[m[5].rm_so - 1] != p[m[5].rm_eo]) { rc = -1; break; }
            char key[65];
            memcpy(key, p + m[5].rm_so, 64);
            key[64] = '\0';
            for (int i = 0; i < 64; i++) key[i] = (char)tolower((unsigned char)key[i]);
            rc = add_key(cfg, owner, (int)id, key);
        } else {
            owner = (int)id;
            rc = add_app(cfg, owner);
        }
        if (rc) break;
    }
    if (ferror(f)) rc = -1;
    fclose(f);
    if (rc) SX_ERR("Lua: rejected %s at line %d (unsupported syntax, invalid value, or capacity exceeded)", path, number);
    else if (ignored) SX_WARN("Lua: %s: ignored %d addtoken declarations (access tokens are not implemented)", path, ignored);
    return rc;
}

int sx_config_load_lua_dir(const char *path, sx_config_t *cfg) {
    if (!path || !cfg) return -1;
    char pattern[1024];
    if (snprintf(pattern, sizeof(pattern), "%s/*.lua", path) >= (int)sizeof(pattern)) return -1;
    glob_t files = {0};
    int rc = glob(pattern, 0, NULL, &files);
    if (rc == GLOB_NOMATCH) { globfree(&files); return 0; }
    if (rc) { globfree(&files); return -1; }
    regex_t re, manifest, metadata;
    // shortcut: declarative calls only, use a Lua runtime if scripts need execution.
    const char *syntax = "^[[:space:]]*addappid[[:space:]]*\\([[:space:]]*([0-9]+)[[:space:]]*(,[[:space:]]*([0-9]+)[[:space:]]*(,[[:space:]]*[\"']([[:xdigit:]]{64})[\"'][[:space:]]*)?)?\\)[[:space:]]*;?[[:space:]]*$";
    if (regcomp(&re, syntax, REG_EXTENDED | REG_ICASE) != 0) { globfree(&files); return -1; }
    const char *manifest_syntax = "^[[:space:]]*setManifestid[[:space:]]*\\([[:space:]]*([0-9]+)[[:space:]]*,[[:space:]]*(\"[0-9]+\"|'[0-9]+')[[:space:]]*(,[[:space:]]*([0-9]+)[[:space:]]*)?\\)[[:space:]]*;?[[:space:]]*$";
    if (regcomp(&manifest, manifest_syntax, REG_EXTENDED | REG_ICASE) != 0) { regfree(&re); globfree(&files); return -1; }
    const char *meta_syntax = "^[[:space:]]*addtoken[[:space:]]*\\([[:space:]]*[1-9][0-9]*[[:space:]]*,[[:space:]]*(\"[0-9]+\"|'[0-9]+')[[:space:]]*\\)[[:space:]]*;?[[:space:]]*$";
    if (regcomp(&metadata, meta_syntax, REG_EXTENDED | REG_ICASE) != 0) { regfree(&manifest); regfree(&re); globfree(&files); return -1; }
    int result = 0;
    for (size_t i = 0; i < files.gl_pathc; i++) {
        struct stat st;
        if (stat(files.gl_pathv[i], &st) != 0 || !S_ISREG(st.st_mode)) { result = -1; continue; }
        // Parse into a copy so a bad file never leaves a partially applied config.
        sx_config_t *next = malloc(sizeof(*next));
        if (!next) { result = -1; break; }
        *next = *cfg;
        next->app_ids = malloc((size_t)(cfg->app_count + 1) * sizeof(int));
        next->package_ids = NULL;
        if (!next->app_ids) { free(next); result = -1; break; }
        if (cfg->app_count) memcpy(next->app_ids, cfg->app_ids, (size_t)cfg->app_count * sizeof(int));
        if (load_lua(files.gl_pathv[i], &re, &manifest, &metadata, next) == 0) {
            free(cfg->app_ids);
            cfg->app_ids = next->app_ids;
            cfg->app_count = next->app_count;
            cfg->dk_count = next->dk_count;
            memcpy(cfg->depot_keys, next->depot_keys, sizeof(cfg->depot_keys));
            cfg->manifest_count = next->manifest_count;
            memcpy(cfg->manifests, next->manifests, sizeof(cfg->manifests));
            SX_LOG("Lua: loaded %s", files.gl_pathv[i]);
        } else {
            free(next->app_ids);
            result = -1;
        }
        free(next);
    }
    regfree(&re);
    regfree(&manifest);
    regfree(&metadata);
    globfree(&files);
    return result;
}
