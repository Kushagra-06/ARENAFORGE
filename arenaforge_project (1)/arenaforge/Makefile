# ArenaForge build
#   make test   native build of the C engine + C++ layer, runs the test suite (with sanitizers)
#   make wasm   compile the same C + C++ sources to WebAssembly (needs Emscripten)
#   make site   wasm + inline everything into one self-contained web/arenaforge.html

CC      := gcc
CXX     := g++
CFLAGS  := -std=c11   -O1 -g -Wall -Wextra -Iengine
CXXFLAGS:= -std=c++17 -O1 -g -Wall -Wextra -Iengine -Iapp
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer

C_SRC   := $(wildcard engine/*.c)
CXX_SRC := app/tournament.cpp app/arena_system.cpp app/snapshot.cpp

build/%.o: engine/%.c | build
	$(CC) $(CFLAGS) $(SAN) -c $< -o $@

build:
	mkdir -p build

C_OBJ := $(patsubst engine/%.c,build/%.o,$(C_SRC))

build/arenaforge_tests: $(C_OBJ) $(CXX_SRC) tests/tests.cpp app/*.hpp engine/arena_engine.h
	$(CXX) $(CXXFLAGS) $(SAN) $(C_OBJ) $(CXX_SRC) tests/tests.cpp -o $@

test: build/arenaforge_tests
	./build/arenaforge_tests

# ---- WebAssembly: C engine is compiled as C, application layer as C++ ----
WASM_FLAGS := -O2 -fexceptions -sMODULARIZE=1 -sEXPORT_NAME=ArenaModule -sSINGLE_FILE=1 \
              -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,node \
              -sEXPORTED_RUNTIME_METHODS=ccall,FS

wasm: build/arena.js build/arena_js.js

build/arena.js: $(C_SRC) $(CXX_SRC) app/web_api.cpp app/*.hpp engine/arena_engine.h
	mkdir -p build/wasm
	for f in $(C_SRC); do emcc -std=c11 -O2 -Iengine -c $$f -o build/wasm/$$(basename $$f .c).o || exit 1; done
	for f in $(CXX_SRC) app/web_api.cpp; do em++ -std=c++17 -O2 -fexceptions -Iengine -Iapp -c $$f -o build/wasm/$$(basename $$f .cpp).o || exit 1; done
	em++ $(WASM_FLAGS) build/wasm/*.o -o $@

site: build/arena.js build/arena_js.js
	python3 web/build_site.py

# Same C + C++ sources compiled to plain JavaScript: the page falls back to this
# when its host does not allow WebAssembly (content-security-policy).
JS_FLAGS := -O2 -fexceptions -sWASM=0 -sMODULARIZE=1 -sEXPORT_NAME=ArenaModuleJS -sSINGLE_FILE=1 \
            -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,node \
            -sEXPORTED_RUNTIME_METHODS=ccall,FS

build/arena_js.js: build/arena.js
	em++ $(JS_FLAGS) build/wasm/*.o -o $@

clean:
	rm -rf build

.PHONY: test wasm site clean
