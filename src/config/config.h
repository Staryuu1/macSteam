// Config file parser
#ifndef MACSTEAM_CONFIG_CONFIG_H
#define MACSTEAM_CONFIG_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define SX_CONFIG_MAX_TOKENS   16384
#define SX_CONFIG_MAX_DK       256
#define SX_CONFIG_MAX_DEPOTS   64
#define SX_CONFIG_MAX_MANIFESTS (SX_CONFIG_MAX_DK * SX_CONFIG_MAX_DEPOTS)

typedef struct sx_config {
    int  *app_ids;
    int   app_count;

    int  *package_ids;
    int   pkg_count;

    struct {
        int app_id;
        struct {
            int  depot_id;
            char key[65];
        } depots[SX_CONFIG_MAX_DEPOTS];
        int depot_count;
    } depot_keys[SX_CONFIG_MAX_DK];
    int dk_count;

    struct {
        uint32_t depot_id;
        uint64_t gid;
    } manifests[SX_CONFIG_MAX_MANIFESTS];
    int manifest_count;

    struct {
        uint32_t app_id;
        uint64_t value;
    } tokens[SX_CONFIG_MAX_TOKENS];
    int token_count;

    int hide_whats_new;
    unsigned references; // Protected by the config mutex; published snapshots are immutable.
} sx_config_t;

// Readers release their snapshot; publish takes ownership of a fresh heap config.
sx_config_t *sx_config_acquire(void);
void sx_config_release(sx_config_t **cfg);
void sx_config_publish(sx_config_t *cfg);

int sx_config_load(const char *path, sx_config_t *cfg);
int sx_config_load_lua_dir(const char *path, sx_config_t *cfg);
void sx_config_free(sx_config_t *cfg);
int sx_config_has_app(sx_config_t *cfg, int app_id);
int sx_config_has_package(sx_config_t *cfg, int package_id);
int sx_config_app_depots(sx_config_t *cfg, int app_id, int *out, int max);
int sx_config_get_depot_key_any(sx_config_t *cfg, int depot_id, char *key_out, size_t key_out_sz);
uint64_t sx_config_get_token(sx_config_t *cfg, uint32_t app_id);
uint64_t sx_config_get_manifest(sx_config_t *cfg, uint32_t depot_id);

#endif // MACSTEAM_CONFIG_CONFIG_H
