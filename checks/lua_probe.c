#include "config/config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_file(const char *dir, const char *name, const char *text) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

int main(void) {
    char dir[] = "/tmp/macsteam-lua.XXXXXX";
    assert(mkdtemp(dir));
    sx_config_t *cfg = calloc(1, sizeof(*cfg));
    assert(cfg);
    write_file(dir, "config.yaml", "Apps:\n  - 42\nPackageIds:\n  - 20200\nHideWhatsNew: true\n");
    char path[1024];
    snprintf(path, sizeof(path), "%s/config.yaml", dir);
    assert(sx_config_load(path, cfg) == 0);
    write_file(dir, "a.lua", "-- comment\naddappid(42)\n addappid(100, 0); -- game\naddappid(101, 0, \"ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789\")\naddappid(200)\naddappid(201, 1, '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef')\n");
    write_file(dir, "b.lua", "AddAppId(100)\n");
    assert(sx_config_load_lua_dir(dir, cfg) == 0);
    assert(cfg->app_count == 3 && sx_config_has_app(cfg, 42) && sx_config_has_app(cfg, 100) && sx_config_has_app(cfg, 200));
    assert(!sx_config_has_app(cfg, 101));
    assert(cfg->pkg_count == 1 && cfg->hide_whats_new);
    int depots[4];
    assert(sx_config_app_depots(cfg, 100, depots, 4) == 1 && depots[0] == 101);
    assert(sx_config_app_depots(cfg, 200, depots, 4) == 1 && depots[0] == 201);
    char key[65];
    assert(sx_config_get_depot_key_any(cfg, 101, key, sizeof(key)) == 0);
    assert(strcmp(key, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789") == 0);
    const char *invalid[] = {
        "addappid(300)\naddappid(0)\n",
        "addappid(300)\naddappid(999999999999999999999999)\n",
        "addappid(300)\naddappid(301, 0, \"bad\")\n",
        "addappid(300)\naddappid(301, 0, \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef')\n",
        "if false then\naddappid(300)\nend\n",
        "addappid(300)\nsetManifestid(301, \"invalid\")\n",
        "setManifestid(301, '9')\naddappid(0)\n",
        "setManifestid(301, '18446744073709551616')\n",
        "setManifestid(301, '9', 18446744073709551616)\n",
        "setManifestid(2147483648, '9')\n",
        "setManifestid(0, '9')\n",
        "setManifestid(301, '0')\n",
        "setManifestid(301, '-1')\n",
        "setManifestid(301, \"9')\n"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        write_file(dir, "bad.lua", invalid[i]);
        assert(sx_config_load_lua_dir(dir, cfg) == -1);
        assert(!sx_config_has_app(cfg, 300));
        assert(cfg->app_count == 3 && cfg->dk_count == 2);
        assert(cfg->manifest_count == 0);
    }
    snprintf(path, sizeof(path), "%s/bad.lua", dir);
    assert(unlink(path) == 0);
    write_file(dir, "generated.lua", "addappid(1091500, 1, \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\")\naddappid(1460472, 1, \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\")\nsetManifestid(1460472, \"1234567890123456789\", 65191070252)\naddtoken(1091500, '1234567890123456789')\n");
    assert(sx_config_load_lua_dir(dir, cfg) == 0);
    assert(sx_config_has_app(cfg, 1091500));
    assert(!sx_config_has_app(cfg, 1460472));
    assert(sx_config_app_depots(cfg, 1091500, depots, 4) == 2);
    assert(sx_config_get_depot_key_any(cfg, 1460472, key, sizeof(key)) == 0);
    assert(cfg->manifest_count == 1);
    assert(sx_config_get_manifest(cfg, 1460472) == UINT64_C(1234567890123456789));
    assert(sx_config_get_manifest(cfg, 1) == 0 && sx_config_get_manifest(NULL, 1) == 0);
    write_file(dir, "z.lua", "SetManifestID(1460472, '18446744073709551615', 0);\nsetmanifestid(1460472, \"9\")\nSETMANIFESTID(201, '18446744073709551615', 18446744073709551615)\n");
    assert(sx_config_load_lua_dir(dir, cfg) == 0);
    assert(cfg->manifest_count == 2 && sx_config_get_manifest(cfg, 1460472) == 9);
    assert(sx_config_get_manifest(cfg, 201) == UINT64_MAX);
    snprintf(path, sizeof(path), "%s/z.lua", dir);
    assert(unlink(path) == 0);
    // Reloads rebuild a fresh snapshot: removing the winning file restores the earlier pin.
    sx_config_free(cfg);
    snprintf(path, sizeof(path), "%s/config.yaml", dir);
    assert(sx_config_load(path, cfg) == 0 && sx_config_load_lua_dir(dir, cfg) == 0);
    assert(cfg->manifest_count == 1 && sx_config_get_manifest(cfg, 201) == 0);
    assert(sx_config_get_manifest(cfg, 1460472) == UINT64_C(1234567890123456789));
    const char *names[] = {"a.lua", "b.lua", "generated.lua", "config.yaml"};
    for (size_t i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        assert(unlink(path) == 0);
    }
    cfg->manifest_count = SX_CONFIG_MAX_MANIFESTS;
    for (int i = 0; i < cfg->manifest_count; i++) {
        cfg->manifests[i].depot_id = (uint32_t)i + 1;
        cfg->manifests[i].gid = 1;
    }
    write_file(dir, "capacity.lua", "setManifestid(1, '2')\n");
    assert(sx_config_load_lua_dir(dir, cfg) == 0 && sx_config_get_manifest(cfg, 1) == 2);
    write_file(dir, "capacity.lua", "setManifestid(1, '3')\nsetManifestid(2147483647, '4')\n");
    assert(sx_config_load_lua_dir(dir, cfg) == -1);
    assert(cfg->manifest_count == SX_CONFIG_MAX_MANIFESTS && sx_config_get_manifest(cfg, 1) == 2);
    assert(sx_config_get_manifest(cfg, 2147483647) == 0);
    snprintf(path, sizeof(path), "%s/capacity.lua", dir);
    assert(unlink(path) == 0);
    assert(rmdir(dir) == 0);
    sx_config_free(cfg);
    free(cfg);
    puts("Lua config checks passed");
    return 0;
}
