CC       := clang
CXX      := clang++
ARCH     := arm64
MIN_VER  := 15.0

CFLAGS   := -arch $(ARCH) -mmacosx-version-min=$(MIN_VER) \
            -std=c17 -O2 -Wall -Wextra -Wno-unused-parameter \
            -fPIC -MMD -MP \
            -Isrc -Ivendor -Ivendor/dobby/include
LDFLAGS  := -arch $(ARCH) -mmacosx-version-min=$(MIN_VER) \
            -dynamiclib -install_name @rpath/macsteam.dylib

FRAMEWORKS := -framework CoreFoundation -framework CFNetwork

DOBBY_REV := 5dfc8546954ce3b3198132ab13fddb89ee92cdd7
DOBBY_DIR := build
DOBBY_STAMP := $(DOBBY_DIR)/.dobby-revision
DOBBY_LIBS := $(DOBBY_DIR)/libdobby.a \
              $(DOBBY_DIR)/builtin-plugin/SymbolResolver/libdobby_symbol_resolver.a \
              $(DOBBY_DIR)/builtin-plugin/SymbolResolver/libmacho_ctx_kit.a \
              $(DOBBY_DIR)/builtin-plugin/SymbolResolver/libshared_cache_ctx_kit.a \
              $(DOBBY_DIR)/external/osbase/libosbase.a \
              $(DOBBY_DIR)/external/logging/liblogging.a

SRCS := src/core/loader.c \
        src/core/ctx.c \
        src/core/macho.c \
        src/core/reconcile.c \
        src/core/session.c \
        src/core/stats_cache.c \
        src/feats/apps.c \
        src/feats/dlc.c \
        src/feats/package.c \
        src/feats/license.c \
        src/feats/schema_owners.c \
        src/feats/depot.c \
        src/feats/ticket.c \
        src/util/log.c \
        src/util/file.c \
        src/util/hex.c \
        src/config/config.c \
        src/config/lua.c \
        src/resolver/aob.c \
        src/resolver/anchor.c \
        src/resolver/sigdb.c \
        src/resolver/resolver.c \
        src/hooks/hooks.c \
        src/hooks/hook_apps.c \
        src/hooks/hook_depot.c \
        src/hooks/hook_dlc.c \
        src/hooks/hook_package.c \
        src/hooks/hook_license.c \
        src/hooks/hook_manifest.c \
        src/hooks/hook_relaunch.c \
        src/hooks/hook_stats.c \
        src/hooks/hook_ticket.c \
        src/hooks/hook_whatsnew.c \
        vendor/cJSON.c

OUT_DIR  := out
ARM64_DYLIB := $(OUT_DIR)/macsteam.arm64.dylib
X86_STUB    := $(OUT_DIR)/macsteam.x86_64.dylib
TARGET      := $(OUT_DIR)/macsteam.dylib
OBJS     := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

.PHONY: all clean rebuild test test-standalone dist

all: $(TARGET)

$(ARM64_DYLIB): $(OBJS) $(DOBBY_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(DOBBY_LIBS) $(FRAMEWORKS) -lc++

$(X86_STUB): src/stub_x86_64.c
	@mkdir -p $(dir $@)
	$(CC) -arch x86_64 -mmacosx-version-min=$(MIN_VER) \
		-dynamiclib -install_name @rpath/macsteam.dylib \
		-o $@ $<

$(TARGET): $(ARM64_DYLIB) $(X86_STUB)
	lipo -create $^ -output $@
	codesign -fs - $@
	@echo "==> Built: $@"

$(OUT_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT_DIR)

rebuild: clean all

test: test-standalone

test-standalone:
	@mkdir -p $(OUT_DIR)/checks
	$(CC) -std=c17 -Wall -Wextra -Werror -Isrc -o $(OUT_DIR)/checks/lua_probe checks/lua_probe.c src/config/config.c src/config/lua.c src/util/log.c src/util/file.c
	$(OUT_DIR)/checks/lua_probe
	bash checks/install_probe.sh

dist: $(TARGET)
	mkdir -p $(OUT_DIR)/macsteam-standalone/scripts $(OUT_DIR)/macsteam-standalone/examples
	cp $(TARGET) README.md LICENSE $(OUT_DIR)/macsteam-standalone/
	cp scripts/install.sh scripts/remove.sh scripts/install-common.sh $(OUT_DIR)/macsteam-standalone/scripts/
	cp examples/config.yaml examples/example.lua $(OUT_DIR)/macsteam-standalone/examples/
	cp -R signatures $(OUT_DIR)/macsteam-standalone/
	tar -czf $(OUT_DIR)/macsteam-standalone.tar.gz -C $(OUT_DIR) macsteam-standalone

-include $(DEPS)

.PHONY: dobby
$(DOBBY_STAMP): dobby
	@test -f "$@"

dobby:
	@if [ ! -d vendor/dobby/.git ]; then \
		echo "==> Cloning Dobby..."; \
		git clone https://github.com/jmpews/Dobby.git vendor/dobby; \
	fi
	@if [ "$$(git -C vendor/dobby rev-parse HEAD)" != "$(DOBBY_REV)" ]; then \
		echo "==> Checking out pinned Dobby revision $(DOBBY_REV)..."; \
		git -C vendor/dobby fetch --depth=1 origin $(DOBBY_REV); \
		git -C vendor/dobby checkout --detach $(DOBBY_REV); \
	fi
	@if [ ! -f "$(DOBBY_DIR)/libdobby.a" ] || \
	   [ "$$(cat "$(DOBBY_STAMP)" 2>/dev/null)" != "$(DOBBY_REV)" ]; then \
		echo "==> Building Dobby from source..."; \
		cmake -S vendor/dobby -B "$(DOBBY_DIR)" \
			-DCMAKE_OSX_ARCHITECTURES=arm64 \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MIN_VER) \
			-DDOBBY_DEBUG=OFF \
			-G "Unix Makefiles"; \
		$(MAKE) -C "$(DOBBY_DIR)" -j$$(sysctl -n hw.ncpu); \
		echo "$(DOBBY_REV)" > "$(DOBBY_STAMP)"; \
	fi
