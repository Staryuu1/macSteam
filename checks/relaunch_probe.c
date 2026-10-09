#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

static int fail_alloc;
static void *probe_malloc(size_t size) { return fail_alloc ? NULL : malloc(size); }
#define malloc probe_malloc
#include "../src/hooks/hook_relaunch.c"
#undef malloc

static int calls, installs, expect_insert, expect_rewrite;
static char *const *expected_argv;
static pid_t child;
static posix_spawn_file_actions_t actions;
static posix_spawnattr_t attributes;

static void check_args(char *const argv[], char *const envp[]) {
    if (expect_rewrite) {
        assert(argv != expected_argv && argv[0] == expected_argv[0]);
        assert(strcmp(argv[1], "-c") == 0 && strstr(argv[2], "open -n"));
    } else assert(argv == expected_argv);
    int insert = 0, app = 0;
    for (int i = 0; envp && envp[i]; i++) {
        if (!strncmp(envp[i], "DYLD_INSERT_LIBRARIES=", 22)) insert++;
        if (!strcmp(envp[i], "SteamAppId=4656000")) app++;
    }
    assert(insert == expect_insert);
    assert(!envp || app == 1);
    calls++;
}

static int fake_execve(const char *path, char *const argv[], char *const envp[]) {
    assert(path);
    check_args(argv, envp);
    errno = ENOENT;
    return -1;
}

static int fake_execv(const char *path, char *const argv[]) {
    return fake_execve(path, argv, *_NSGetEnviron());
}

static int fake_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa,
    const posix_spawnattr_t *attr, char *const argv[], char *const envp[]) {
    assert(path && pid == &child && fa == &actions && attr == &attributes);
    check_args(argv, envp);
    return EAGAIN;
}

void *DobbySymbolResolver(const char *image, const char *symbol) {
    (void)image;
    if (!strcmp(symbol, "execve")) return (void *)fake_execve;
    if (!strcmp(symbol, "execv")) return (void *)fake_execv;
    assert(!strcmp(symbol, "posix_spawn"));
    return (void *)fake_spawn;
}

int DobbyHook(void *address, void *replacement, void **original) {
    assert(address && replacement && original);
    *original = address;
    installs++;
    return 0;
}

int main(void) {
    sx_log_level = SX_LVL_ERR;
    sx_hooks_relaunch_install();
    sx_hooks_relaunch_install();
    assert(installs == 3);
    char *env[] = {"DYLD_INSERT_LIBRARIES=/tmp/macsteam.dylib", "SteamAppId=4656000",
                   "STEAM_APP_BUNDLE_PATH=/Applications/Steam.app", NULL};
    char *argv[] = {"launchctl", "load", "steam.ipctool.plist", NULL};
    expected_argv = argv;
    const char *paths[] = {"/bin/launchctl", "/bin/sh", "/games/Game.app/Contents/MacOS/Game",
                           "/Steam/Contents/MacOS/ipcserver", "/Steam/Contents/MacOS/steam_osx"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        expect_insert = i == 4;
        assert(hook_execve(paths[i], argv, env) == -1 && errno == ENOENT);
        assert(hook_posix_spawn(&child, paths[i], &actions, &attributes, argv, env) == EAGAIN);
        assert(!strcmp(env[0], "DYLD_INSERT_LIBRARIES=/tmp/macsteam.dylib"));
    }
    assert(setenv("DYLD_INSERT_LIBRARIES", "/tmp/macsteam.dylib", 1) == 0);
    assert(setenv("SteamAppId", "4656000", 1) == 0);
    expect_insert = 0;
    assert(hook_execv("/bin/launchctl", argv) == -1 && errno == ENOENT);
    assert(getenv("DYLD_INSERT_LIBRARIES"));
    expect_insert = 1;
    assert(hook_execv(paths[4], argv) == -1 && errno == ENOENT);
    char *restart[] = {"sh", "-c", "open -n '/Applications/Steam.app'", NULL};
    expected_argv = restart;
    expect_insert = 0;
    expect_rewrite = 1;
    assert(hook_execve("/bin/sh", restart, env) == -1 && errno == ENOENT);
    assert(hook_posix_spawn(&child, "/bin/sh", &actions, &attributes, restart, env) == EAGAIN);
    expect_rewrite = 0;
    expected_argv = argv;
    assert(hook_execve("/bin/launchctl", argv, NULL) == -1 && errno == ENOENT);
    int before = calls;
    fail_alloc = 1;
    assert(hook_execve("/bin/launchctl", argv, env) == -1 && errno == ENOMEM);
    assert(hook_execv("/bin/launchctl", argv) == -1 && errno == ENOMEM);
    assert(hook_posix_spawn(&child, "/bin/launchctl", &actions, &attributes, argv, env) == ENOMEM);
    assert(calls == before);
    puts("Early child-launch environment checks passed (process calls mocked)");
    return 0;
}
