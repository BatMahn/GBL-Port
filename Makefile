# Ghost Bear's Legacy port (Linux / macOS / Windows via MinGW-w64): `make gbl`, the tools with `make`
CC      ?= cc
CFLAGS  ?= -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Wconversion
LDLIBS  = -lpthread -lm
UNAME_S := $(shell uname -s)
# SDL3 through pkg-config (sdl3.pc): `make PKG_CONFIG_PATH=/opt/sdl3/lib/pkgconfig`, or a whole command in SDL3_CONFIG
# that answers --cflags / --libs. Windows: MSYS2's MINGW64 shell (uname MINGW64_NT-...), or cross-compiling from Linux
# with SDL3's MinGW development package:
#   make gbl WINDOWS=1 CC=x86_64-w64-mingw32-gcc PKG_CONFIG_PATH=/path/to/SDL3-x.y.z/x86_64-w64-mingw32/lib/pkgconfig
PKG_CONFIG ?= pkg-config
# (PKG_CONFIG_PATH given to make on its command line reaches pkg-config too: make 4.3's $(shell) has no command-line
# variables in its environment)
SDL3_CONFIG ?= $(if $(PKG_CONFIG_PATH),PKG_CONFIG_PATH='$(PKG_CONFIG_PATH)' )$(PKG_CONFIG) sdl3
SDL_CFLAGS = $(shell $(SDL3_CONFIG) --cflags 2>/dev/null)
SDL_LIBS = $(shell $(SDL3_CONFIG) --libs 2>/dev/null)
# RPATH: where the executable looks for libSDL3 first, e.g. the .deb's  make gbl RPATH='$$ORIGIN/../lib/gblport'
ifdef RPATH
  LDFLAGS += -Wl,-rpath,'$(RPATH)'
endif
ifneq (,$(findstring MINGW,$(UNAME_S)))
  WINDOWS = 1
  ifeq ($(origin CC),default)
    CC = gcc
  endif
endif
ifeq ($(WINDOWS),1)
  UNAME_S = Windows
  CFLAGS += -include src/wincompat.h
  LDLIBS = -lm
  DLLIB =
  WINSRC = src/wincompat.c
  WINLIBS = -lole32 -lshell32 -mwindows   # system DLLs: the folder picker (wincompat.c mw2_pick_folder); a GUI program
  WINRES = build/gblres.o
  WINDRES ?= $(patsubst %gcc,%windres,$(CC))
else
  DLLIB = -ldl
  WINSRC =
  WINRES =
  WINLIBS =
endif
ifeq ($(UNAME_S),Linux)
  LDLIBS += -ldl
endif
ifeq ($(UNAME_S),Darwin)
  LDLIBS += -framework CoreFoundation -framework CoreAudio -framework AudioToolbox
endif

LIB_SRC = src/prj.c src/datapath.c src/cdaudio.c src/mek.c
BIN     = prjtool cdtool mektool bwdtool mechview cdrip test_port shelltool test_titanium test_gamefolder
GLBIN   = glview

all: $(BIN)

# Windows: the exe's version information, and its icon when packaging/windows/gblport.ico is there
build/gblres.o: packaging/windows/gblport.rc
	@mkdir -p build
	$(WINDRES) -I packaging/windows $(if $(wildcard packaging/windows/gblport.ico),-DGBL_ICON) $< -O coff -o $@

build/miniaudio.o: third_party/miniaudio.c third_party/miniaudio.h
	@mkdir -p build
	$(CC) $(filter-out -std=% -include src/wincompat.h,$(CFLAGS)) -O2 -w -c $< -o $@

shelltool: tools/shelltool.c src/shelldb.c src/smk.c src/shelldb.h src/smk.h
	$(CC) $(CFLAGS) -Isrc $(filter %.c,$^) -o $@

prjtool: tools/prjtool.c src/prj.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@

cdrip: tools/cdrip.c
	$(CC) $(CFLAGS) $^ -o $@

HDRS = $(wildcard src/*.h tools/*.h)
# the renderer: the world (glr.c), the HUD (hud.c) and the render backend on SDL_GPU (rb_gpu.c; the shaders compiled
# for Vulkan, Direct3D 12 and Metal in rb_shaders.c - generated, checked in: "make shaders" remakes it). SDL loads the
# GPU API (Vulkan / Direct3D 12 / Metal) at run time: nothing graphics-specific to link
GL3D_SRC = src/rb_gpu.c src/rb_shaders.c src/hud.c src/shp.c src/glr.c src/skygnd.c src/anim.c src/mek.c src/mtbl.c src/msim.c src/fx.c src/combat.c src/ai.c src/mech3d.c src/render.c src/tex.c src/wtb.c src/bwd.c src/prj.c src/datapath.c

missionsim: tools/missionsim.c src/msim.c src/fx.c src/ai.c src/combat.c src/mtbl.c src/anim.c src/mek.c src/mech3d.c src/wtb.c src/bwd.c src/prj.c src/datapath.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-conversion -Isrc $(filter %.c,$^) -o $@ -lm


# gbl: MechWarrior 2: Ghost Bear's Legacy - its own executable (GBL.EXE was), the GBL shell + the same simulation
# (no MIDI: GBL's shell never plays menu music, docs/gbl/A_frame.md 1.5; CD audio stays)
GBL_SHELL_SRC = tools/gblshell.c tools/gbl/gbl_common.c tools/gbl/gbl_frame.c tools/gbl/gbl_pilots.c tools/gbl/gbl_campaign.c tools/gbl/gbl_lab.c
gbl: $(WINRES) $(GBL_SHELL_SRC) tools/glview.c src/shelldb.c src/smk.c src/vidfx.c src/lance.c src/ttext.c src/cockpitcfg.c src/sfx.c src/portcfg.c src/cdaudio.c build/miniaudio.o $(GL3D_SRC) $(WINSRC) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -Wno-conversion -Isrc -Ithird_party $(SDL_CFLAGS) -c tools/glview.c -Dmain=glview_main -o build/glview_sim.o
	$(CC) $(CFLAGS) -Wno-conversion -Isrc -Ithird_party -Itools -c tools/gblshell.c $(SDL_CFLAGS) -o build/gblshell_one.o
	$(CC) $(CFLAGS) -Wno-conversion -Isrc -Ithird_party $(SDL_CFLAGS) build/gblshell_one.o build/glview_sim.o $(sort $(filter src/%.c,$^)) build/miniaudio.o $(WINRES) -o $@ $(LDFLAGS) $(SDL_LIBS) -lm -lpthread $(DLLIB) $(WINLIBS)

glview: tools/glview.c src/sfx.c src/portcfg.c src/cdaudio.c build/miniaudio.o $(GL3D_SRC) $(HDRS)
	$(CC) $(CFLAGS) -Wno-conversion -Isrc -Ithird_party $(SDL_CFLAGS) $(filter %.c,$^) $(filter %.o,$^) -o $@ $(LDFLAGS) $(SDL_LIBS) -lm -lpthread $(DLLIB)

gl: $(GLBIN)

mechview: tools/mechview.c src/anim.c src/mek.c src/mech3d.c src/render.c src/wtb.c src/bwd.c src/prj.c src/datapath.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@ -lm

bwdtool: tools/bwdtool.c src/mtbl.c src/bwd.c src/prj.c src/datapath.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@

mektool: tools/mektool.c src/mek.c src/datapath.c src/prj.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@

cdtool: tools/cdtool.c src/cdaudio.c build/miniaudio.o
	$(CC) $(CFLAGS) -Isrc $^ -o $@ $(LDLIBS)

test_port: tests/test_port.c src/cdaudio.c src/datapath.c build/miniaudio.o
	$(CC) $(CFLAGS) -Wno-conversion -Isrc $^ -o $@ $(LDLIBS)

test_titanium: tests/test_titanium.c src/tex.c src/skygnd.c src/portcfg.c src/prj.c src/datapath.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@ -lm

test_gamefolder: tests/test_gamefolder.c src/portcfg.c src/portcfg.h
	$(CC) $(CFLAGS) -Isrc tests/test_gamefolder.c src/portcfg.c -o $@

test_cockpitcfg: tests/test_cockpitcfg.c src/cockpitcfg.c src/cockpitcfg.h
	$(CC) $(CFLAGS) -Isrc tests/test_cockpitcfg.c src/cockpitcfg.c -o $@

test_inputmap: tests/test_inputmap.c tools/inputmap.h tools/inputmap_defaults.h src/datapath.c src/datapath.h
	$(CC) -std=c99 -D_DEFAULT_SOURCE -Itools -Isrc $(SDL_CFLAGS) tests/test_inputmap.c src/datapath.c $(LDFLAGS) $(SDL_LIBS) -o $@

# make check GBL=/path/to/GBL-game/install [ISO=/path/to/gbl_cd.iso] [TITANIUM=/path/to/GBL-game/titanium]
TITANIUM ?= $(GBL)/../titanium
check: all glview
	./prjtool verify $(GBL)/MW2.PRJ
	./mektool $(GBL)/MW2.PRJ list | tail -1
	./bwdtool $(GBL)/MW2.PRJ verify
	./mechview $(GBL)/MW2.PRJ verify
	MW2_INSTALL_DIR=$(GBL) ./bwdtool $(GBL)/MW2.PRJ missions | tail -1
	./test_port $(GBL) $(ISO)
	./test_titanium $(TITANIUM)
	./test_gamefolder
	$(MAKE) test_inputmap && ./test_inputmap $(GBL)
	$(MAKE) test_cockpitcfg && ./test_cockpitcfg $(GBL) $(COCKPIT_REF)
	python3 tests/gbl/gbl_hud_check.py $(GBL)/..

clean:
	rm -rf build $(BIN) $(GLBIN) gbl gbl.exe sweep_objectives sweep_ai missionsim

.PHONY: all check clean

# the shaders (shaders/*.vert, *.frag) for every backend into src/rb_shaders.c / .h (checked in; needs glslangValidator,
# spirv-cross and dxc - on PATH or in SHADER_TOOLS=<folder>/bin; see scripts/gen_shaders.py)
shaders:
	python3 scripts/gen_shaders.py $(if $(SHADER_TOOLS),--tools $(SHADER_TOOLS))
.PHONY: shaders

# A self-contained folder to run from anywhere: the launcher, the simulation, the font (found next to the
# executables), the documentation. Needs SDL3 from the system (and a Vulkan driver: the GPU's).
DIST = dist/gblport
dist: gbl
	rm -rf $(DIST)
	mkdir -p $(DIST)/assets/fonts
	cp gbl $(DIST)/
	cp assets/fonts/LiberationSans-Bold.ttf assets/fonts/README.md $(DIST)/assets/fonts/
	cp README.md FOLDER_SETUP.md THIRD_PARTY.md $(DIST)/
	cd dist && tar czf gblport-linux-x86_64.tar.gz gblport
	@echo "dist/gblport-linux-x86_64.tar.gz"

# The objective sweep: every mission run headless by a perfect player (tools/sweep_objectives.c).
# make sweep GBL=/path/to/install   (SWEEP_ARGS="-v BASSSCN1" for one scene, verbose)
SWEEP_SRC = tools/sweep_objectives.c src/lance.c src/msim.c src/fx.c src/ai.c src/combat.c src/mtbl.c src/anim.c src/mek.c src/mech3d.c src/wtb.c src/bwd.c src/prj.c src/datapath.c
sweep_objectives: $(SWEEP_SRC) $(HDRS)
	$(CC) $(CFLAGS) -Wno-conversion -Isrc $(SWEEP_SRC) -o $@ -lm
sweep_objectives_asan: $(SWEEP_SRC) $(HDRS)
	$(CC) -std=c99 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc $(SWEEP_SRC) -o $@ -lm
sweep: sweep_objectives
	MW2_INSTALL_DIR=$(GBL) ./sweep_objectives $(GBL)/MW2.PRJ $(SWEEP_ARGS)
.PHONY: sweep

# walking sweep (tests only): tools/sweep_walk.py drives glview (MW2_WALK_TRACE hooks); sweep_ai reports stuck AI mechs
sweep_ai: tools/sweep_ai.c src/msim.c src/fx.c src/ai.c src/combat.c src/mtbl.c src/anim.c src/mek.c src/mech3d.c src/wtb.c src/bwd.c src/prj.c src/datapath.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-conversion -Isrc $(filter %.c,$^) -o $@ -lm
GBL_GAME ?= ../GBL-game
sweep-walk: glview sweep_ai bwdtool
	GBL_GAME=$(GBL_GAME) python3 tools/sweep_walk.py -o /tmp/walk $(WALK_ARGS)
	for m in $$(./bwdtool $(GBL_GAME)/install/MW2.PRJ missions | awk '/SCN/{print $$1}'); do MW2_INSTALL_DIR=$${MW2_INSTALL_DIR:-$(GBL_GAME)/install} ./sweep_ai $(GBL_GAME)/install/MW2.PRJ $$m 300 attack; done | grep aistuck
.PHONY: sweep-walk
