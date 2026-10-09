#include "../src/hooks/hook_manifest.c"
#include <assert.h>

static uint32_t original_result = 1;
static int calls, passthrough;
static char manager, app_config, install_state;
static uint32_t build_id;
static uint8_t used_public;
static CUtlVecDepot_t *expected_depots, *expected_shared;

int sx_hook_passthrough(const char *name) {
    assert(strcmp(name, "BuildDepotDependency") == 0);
    return passthrough;
}

static uint32_t original(void *self, uint32_t app_id, void *config,
    CUtlVecDepot_t *depots, CUtlVecDepot_t *shared, void *state,
    uint32_t *build, uint8_t *public) {
    assert(self == &manager && (app_id == 42 || app_id == 99));
    assert(config == &app_config && state == &install_state);
    assert(build == &build_id && public == &used_public);
    assert(depots == expected_depots && shared == expected_shared);
    *build = 123;
    *public = 1;
    calls++;
    return original_result;
}

static uint32_t invoke(uint32_t app, CUtlVecDepot_t *depots, CUtlVecDepot_t *shared) {
    int before = calls;
    expected_depots = depots;
    expected_shared = shared;
    uint32_t result = hook_BuildDepotDependency(&manager, app, &app_config,
        depots, shared, &install_state, &build_id, &used_public);
    assert(calls == before + 1 && build_id == 123 && used_public == 1);
    return result;
}

int main(void) {
    orig_BuildDepotDependency = (void *)original;
    sx_config_t *cfg = calloc(1, sizeof(*cfg));
    assert(cfg);
    cfg->app_ids = malloc(sizeof(*cfg->app_ids));
    assert(cfg->app_ids);
    cfg->app_ids[0] = 42;
    cfg->app_count = 1;
    cfg->manifests[0].depot_id = 101;
    cfg->manifests[0].gid = UINT64_MAX;
    cfg->manifest_count = 1;
    sx_config_publish(cfg);

    DepotInfo_t entries[2] = {
        {.depot_id = 101, .app_id = 42, .manifest_id = 11, .size = 1234, ._tail = {1, 2, 3}},
        {.depot_id = 102, .app_id = 42, .manifest_id = 22, .size = 5678, ._tail = {4, 5, 6}},
    };
    DepotInfo_t expected[2];
    memcpy(expected, entries, sizeof(entries));
    expected[0].manifest_id = UINT64_MAX;
    CUtlVecDepot_t depots = {.base = entries, .cap = 2, .count = 2};
    DepotInfo_t shared_entry = entries[0], shared_before = shared_entry;
    CUtlVecDepot_t shared = {.base = &shared_entry, .cap = 1, .count = 1};
    sx_log_file = tmpfile();
    assert(sx_log_file);
    entries[0].manifest_id = UINT64_MAX;
    assert(invoke(42, &depots, &shared) == 1);
    rewind(sx_log_file);
    char line[512];
    assert(fgets(line, sizeof(line), sx_log_file));
    assert(strstr(line, "pinned manifest=18446744073709551615 (was=18446744073709551615)"));
    fclose(sx_log_file);
    sx_log_file = NULL;
    entries[0].manifest_id = 11;
    assert(invoke(42, &depots, &shared) == 1);
    assert(memcmp(expected, entries, sizeof(entries)) == 0);
    assert(memcmp(&shared_before, &shared_entry, sizeof(shared_entry)) == 0);

    entries[0].manifest_id = 11;
    original_result = 0;
    assert(invoke(42, &depots, &shared) == 0 && entries[0].manifest_id == 11);
    original_result = 1;
    assert(invoke(99, &depots, &shared) == 1 && entries[0].manifest_id == 11);
    passthrough = 1;
    assert(invoke(42, &depots, &shared) == 1 && entries[0].manifest_id == 11);
    passthrough = 0;
    assert(invoke(42, NULL, NULL) == 1);
    depots.count = 3;
    assert(invoke(42, &depots, NULL) == 1 && entries[0].manifest_id == 11);
    depots.count = -1;
    assert(invoke(42, &depots, NULL) == 1 && entries[0].manifest_id == 11);
    depots.count = 1;
    depots.base = NULL;
    assert(invoke(42, &depots, NULL) == 1);
    depots.count = 0;
    assert(invoke(42, &depots, NULL) == 1);
    depots.count = 2;
    depots.base = entries;

    sx_config_t *next = calloc(1, sizeof(*next));
    assert(next);
    next->app_ids = malloc(sizeof(*next->app_ids));
    assert(next->app_ids);
    next->app_ids[0] = 42;
    next->app_count = 1;
    sx_config_publish(next);
    assert(invoke(42, &depots, &shared) == 1 && entries[0].manifest_id == 11);
    sx_config_publish(NULL);
    assert(invoke(42, &depots, &shared) == 1 && entries[0].manifest_id == 11);
    puts("Manifest hook checks passed (Steam calls mocked)");
    return 0;
}
