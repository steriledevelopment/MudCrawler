CXXFLAGS_COMMON = -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers
WINCXX  = x86_64-w64-mingw32-g++-posix
WINRES  = x86_64-w64-mingw32-windres
WINFLAGS = $(CXXFLAGS_COMMON) -DUNICODE -D_UNICODE -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
CORE = src/telnet.cpp src/ansi.cpp src/rules.cpp src/mapper.cpp src/store.cpp
WINSRC = $(CORE) src/session.cpp src/ui.cpp src/dialogs.cpp src/main.cpp
EXE = dist/MudCrawler.exe

.PHONY: all test win installer clean
all: test win

test:
	@mkdir -p /tmp/mc_build
	g++ $(CXXFLAGS_COMMON) -o /tmp/mc_build/core tests/test_core.cpp src/telnet.cpp src/ansi.cpp && /tmp/mc_build/core
	g++ $(CXXFLAGS_COMMON) -o /tmp/mc_build/rules tests/test_rules.cpp src/rules.cpp && /tmp/mc_build/rules
	g++ $(CXXFLAGS_COMMON) -o /tmp/mc_build/mapper tests/test_mapper.cpp src/mapper.cpp && /tmp/mc_build/mapper
	g++ $(CXXFLAGS_COMMON) -o /tmp/mc_build/json tests/test_json.cpp && /tmp/mc_build/json
	g++ $(CXXFLAGS_COMMON) -o /tmp/mc_build/store tests/test_store.cpp src/store.cpp && /tmp/mc_build/store

win: $(EXE)

dist/app.res.o: build/app.rc build/app.manifest build/icon.ico build/paypal.png
	@mkdir -p dist
	cd build && $(WINRES) -O coff -o ../dist/app.res.o app.rc

$(EXE): $(WINSRC) src/*.h dist/app.res.o
	$(WINCXX) $(WINFLAGS) -mwindows -static -static-libgcc -static-libstdc++ -o $@ $(WINSRC) dist/app.res.o \
	  -lgdiplus -lole32 -lws2_32 -lcomctl32 -lcomdlg32 -lcrypt32 -lshell32 -lgdi32 -luser32 -lz
	x86_64-w64-mingw32-strip $@

installer: win
	bash store/installer/build.sh

clean:
	rm -rf dist
