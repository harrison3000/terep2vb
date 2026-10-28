# terep2 in C++ ("C with classes").
#   make            headless test host, terep2-headless (no dependencies)
#   make sdl        SDL2 version (needs SDL2 development files, OpenGL and the submodules:
#                   host/imgui, host/Nuked-OPL3)
#   make test       replay the Win16 recordings in tests/ and compare the frames
#   make coverage   which lines of src/ the tests run (clang/LLVM); REPLAY="recdir mapdir": a recording's
#   make web        the game as a web page (web/terep2.wasm; serve web/ and open it)
#   make wasm-test  the test host built for WebAssembly (wasm32, WASI) and the replays run on it with node
#   make -j8 ...    in parallel: every file is its own object in build/
CXX      ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=gnu++17 -fwrapv -Wall -Wno-unused-result
DEPFLAGS  = -MMD -MP                    # header (and #embed) dependencies, in the .d files

GAME     := $(wildcard src/*.cpp)
# common/: code the game and the hosts share (the PCX reader), built with the game
GAME_OBJ := $(patsubst src/%.cpp,build/game/%.o,$(GAME)) $(patsubst common/%.cpp,build/game/common_%.o,$(wildcard common/*.cpp))
HOST_OBJ := build/host/common.o
TEST_OBJ := $(patsubst host_test/%.cpp,build/test/%.o,$(wildcard host_test/*.cpp))

all: terep2-headless

build build/game build/host build/test build/sdl build/imgui/backends:
	@mkdir -p $@

# The game and what the hosts share.
build/game/%.o: src/%.cpp | build/game
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
build/game/common_%.o: common/%.cpp | build/game
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
build/host/%.o: host/%.cpp | build/host
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
build/test/%.o: host_test/%.cpp | build/test
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

terep2-headless: $(GAME_OBJ) $(HOST_OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

# The SDL host and its map chooser (OpenGL 3.3); Dear ImGui is the git submodule in host/imgui.
ifeq ($(OS),Windows_NT)
GL_LIBS  := -lopengl32
else
GL_LIBS  := -lGL
endif
IMGUI    := host/imgui
OPL3     := host/Nuked-OPL3
SDL_SRC  := sdl_main launcher prefs voxel_preview opl_sound minimap gui renderer_sw
SDL_OBJ  := $(patsubst %,build/sdl/%.o,$(SDL_SRC))
SDL_FLAGS = $(CXXFLAGS) $(shell sdl2-config --cflags) -I$(IMGUI) -I$(IMGUI)/backends -I$(OPL3)
IMGUI_OBJ := $(patsubst $(IMGUI)/%.cpp,build/imgui/%.o,$(addprefix $(IMGUI)/,imgui.cpp imgui_draw.cpp \
             imgui_tables.cpp imgui_widgets.cpp backends/imgui_impl_sdl2.cpp backends/imgui_impl_opengl3.cpp))

$(IMGUI)/imgui.h:
	@echo "Dear ImGui is missing: git submodule update --init" >&2; false
$(OPL3)/opl3.h:
	@echo "Nuked-OPL3 is missing: git submodule update --init" >&2; false

# The SDL host's flags, kept in a file that only changes when they do: changing them rebuilds what
# they touch, with no need to remove anything by hand.
build/sdl/flags: FORCE | build/sdl
	@echo '$(SDL_FLAGS)' | cmp -s - $@ || echo '$(SDL_FLAGS)' > $@
FORCE:

build/sdl/%.o: host/%.cpp build/sdl/flags | build/sdl $(IMGUI)/imgui.h $(OPL3)/opl3.h
	$(CXX) $(SDL_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/imgui/%.o: $(IMGUI)/%.cpp | build/imgui/backends $(IMGUI)/imgui.h
	$(CXX) -O2 -std=gnu++17 $(shell sdl2-config --cflags) -I$(IMGUI) $(DEPFLAGS) -c -o $@ $<
build/opl3.o: $(OPL3)/opl3.c | build $(OPL3)/opl3.h
	$(CC) -O2 $(DEPFLAGS) -c -o $@ $<

sdl: terep2vb
terep2vb: $(GAME_OBJ) $(HOST_OBJ) $(SDL_OBJ) $(IMGUI_OBJ) build/opl3.o build/sdl/flags
	$(CXX) $(CXXFLAGS) -o $@ $(GAME_OBJ) $(HOST_OBJ) $(SDL_OBJ) $(IMGUI_OBJ) build/opl3.o \
		$(shell sdl2-config --libs) $(GL_LIBS) -lm

test: terep2-headless
	sh tests/run_tests.sh

# Coverage of the game's code: the headless host built with clang's source-based coverage in
# build/cov, then the tests (the recordings in tests/, pass or not), or with
# REPLAY="recdir mapdir" only that recording.  Summary on the terminal, report in build/cov/html.
COV_CXX       ?= clang++
LLVM_PROFDATA ?= llvm-profdata
LLVM_COV      ?= llvm-cov
COV_FLAGS     := -O0 -g -std=gnu++17 -fwrapv -fprofile-instr-generate -fcoverage-mapping
COV_OBJ       := $(patsubst build/%,build/cov/%,$(GAME_OBJ) $(HOST_OBJ) $(TEST_OBJ))
COV_RUN       := LLVM_PROFILE_FILE=$(CURDIR)/build/cov/prof/%p.profraw
COV_SHOW      := build/cov/terep2-headless -instr-profile=build/cov/all.profdata -ignore-filename-regex='host'

build/cov/game/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(COV_CXX) $(COV_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/cov/game/common_%.o: common/%.cpp
	@mkdir -p $(@D)
	$(COV_CXX) $(COV_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/cov/host/%.o: host/%.cpp
	@mkdir -p $(@D)
	$(COV_CXX) $(COV_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/cov/test/%.o: host_test/%.cpp
	@mkdir -p $(@D)
	$(COV_CXX) $(COV_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/cov/terep2-headless: $(COV_OBJ)
	$(COV_CXX) $(COV_FLAGS) -o $@ $^ -lm

coverage: build/cov/terep2-headless
	rm -rf build/cov/prof build/cov/html
ifdef REPLAY
	-$(COV_RUN) build/cov/terep2-headless -C $(REPLAY) | tail -1
else
	-$(COV_RUN) sh tests/run_tests.sh build/cov/terep2-headless
endif
	$(LLVM_PROFDATA) merge -o build/cov/all.profdata build/cov/prof/*.profraw
	$(LLVM_COV) report $(COV_SHOW)
	$(LLVM_COV) show $(COV_SHOW) -format=html -show-branches=count -output-dir=build/cov/html > /dev/null
	@echo "report: build/cov/html/index.html"

# WebAssembly (32 bits): the test host built with clang for wasm32-wasip1 (WASI libc and libc++,
# no Emscripten) in build/wasm, and the replays run on it with node (tools/wasi_run.mjs).
WASI_SYSROOT ?= /usr/share/wasi-sysroot
WASM_CXX     ?= clang++
WASM_FLAGS   := --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT) -O2 -std=gnu++17 -fwrapv -fno-exceptions \
                -Wall -Wno-unused-result
WASM_OBJ     := $(patsubst build/%,build/wasm/%,$(GAME_OBJ) $(HOST_OBJ) $(TEST_OBJ))

build/wasm/game/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(WASM_CXX) $(WASM_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/wasm/game/common_%.o: common/%.cpp
	@mkdir -p $(@D)
	$(WASM_CXX) $(WASM_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/wasm/host/%.o: host/%.cpp
	@mkdir -p $(@D)
	$(WASM_CXX) $(WASM_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/wasm/test/%.o: host_test/%.cpp
	@mkdir -p $(@D)
	$(WASM_CXX) $(WASM_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/wasm/terep2-headless.wasm: $(WASM_OBJ)
	$(WASM_CXX) $(WASM_FLAGS) -o $@ $^
# run_tests.sh takes a program: this one starts the .wasm with node
build/wasm/terep2-headless: build/wasm/terep2-headless.wasm tools/wasi_run.mjs
	printf '#!/bin/sh\nexec node --no-warnings "%s/tools/wasi_run.mjs" "%s/$<" "$$@"\n' "$(CURDIR)" "$(CURDIR)" > $@
	chmod +x $@

# The web page (web/, what is served): the game as a WebAssembly "reactor" for the browser, with
# host_web/web_main.cpp as its host.  Serve the folder (python3 -m http.server -d web) and open it.
# The OpenGL renderer there: host_web/gl_renderer.cpp and host_web/rz, the web's own copy of the
# renderizeitor (WebGL 2, its calls imports of the module done by web/webgl.js).
WASM_CC      ?= clang
RZ_WEB_OBJ   := $(patsubst host_web/rz/%.cpp,build/wasm/rz/%.o,$(wildcard host_web/rz/*.cpp))
RZ_WEB_FLAGS := --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT) -O2 -std=c++23 -fno-exceptions -fno-rtti \
                -Wall -Wextra
WEB_OBJ      := $(patsubst build/%,build/wasm/%,$(GAME_OBJ)) build/wasm/web/web_main.o \
                build/wasm/web/gl_renderer.o build/wasm/opl3.o $(RZ_WEB_OBJ)
build/wasm/web/gl_renderer.o: WEB_WARN := -Wno-vla-cxx-extension -Wno-missing-braces
build/wasm/web/%.o: host_web/%.cpp | $(OPL3)/opl3.h
	@mkdir -p $(@D)
	$(WASM_CXX) $(WASM_FLAGS) $(WEB_WARN) -I$(OPL3) $(DEPFLAGS) -c -o $@ $<
build/wasm/rz/%.o: host_web/rz/%.cpp
	@mkdir -p $(@D)
	$(WASM_CXX) $(RZ_WEB_FLAGS) $(DEPFLAGS) -c -o $@ $<
build/wasm/opl3.o: $(OPL3)/opl3.c | $(OPL3)/opl3.h
	@mkdir -p $(@D)
	$(WASM_CC) --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT) -O2 -DOPL_WF_TABLE_RUNTIME=1 $(DEPFLAGS) -c -o $@ $<
web/terep2.wasm: $(WEB_OBJ)
	$(WASM_CXX) $(WASM_FLAGS) -mexec-model=reactor -Wl,--strip-debug -o $@ $^
web: web/terep2.wasm

wasm-test: build/wasm/terep2-headless
	sh tests/run_tests.sh build/wasm/terep2-headless

clean:
	rm -f terep2vb terep2-headless
	rm -rf build/game build/host build/test build/sdl build/imgui build/opl3.o build/opl3.d build/cov build/wasm
	rm -f web/terep2.wasm

-include $(wildcard build/*/*.d build/*.d build/imgui/backends/*.d build/cov/*/*.d build/wasm/*/*.d)

.PHONY: all sdl test coverage web wasm-test clean FORCE
