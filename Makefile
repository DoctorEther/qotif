# Qotif - Quake map editor for X11/Motif and OpenGL 1.x
#
#   make           build ./qotif (runs in place: ./qotif)
#   make dist      build a self-contained qotif-portable/ directory that
#                  carries its own libXm and can be copied anywhere
#   make clean
#
# Qotif is portable: it finds data/ next to its executable and keeps its
# settings in config/ next to it as well.  Nothing needs to be installed.
#
# Needs a C99 compiler and the Motif (libXm), Xt, X11 and OpenGL headers,
# e.g. on Debian/Ubuntu:  apt install libmotif-dev libgl-dev
#      on Fedora:         dnf install motif-devel mesa-libGL-devel
#      on Arch/Artix:     pacman -S openmotif mesa
#
# Motif/X11 outside the default search path (BSDs, a Motif built from
# source, ...) is selected with INCDIRS and LIBDIRS, e.g.
#   make INCDIRS="-I/usr/X11R7/include -I/usr/pkg/include" \
#        LIBDIRS="-L/usr/X11R7/lib -L/usr/pkg/lib"
# Headers and library must come from the SAME Motif: gcc searches
# /usr/local/include before /usr/include, so a Motif built from source in
# /usr/local shadows the packaged headers.

CC        ?= cc
CFLAGS    ?= -O2 -g
WARNFLAGS  = -Wall -Wextra -Wno-unused-parameter
INCDIRS   ?=
LIBDIRS   ?=
DISTDIR   ?= qotif-portable

# XMSTRINGDEFINES/XTSTRINGDEFINES turn resource names (XmNlabelString, ...)
# into string literals instead of offsets into libXm's string table, so a
# header/library version mismatch cannot scramble every resource.
CPPFLAGS_ALL = -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_BSD_SOURCE \
               -DXMSTRINGDEFINES -DXTSTRINGDEFINES \
               $(INCDIRS) $(CPPFLAGS)
# $ORIGIN/lib lets a copy of libXm placed in lib/ next to the executable
# be found without installing anything
LDFLAGS_ALL  = -Wl,-rpath,'$$ORIGIN/lib' $(LDFLAGS)
LIBS_ALL     = $(LIBDIRS) -lXm -lXt -lX11 -lGL -lm $(LIBS)

OBJDIR    ?= obj

SRCS = actions brush common eclass editor game ini lexer main map mathlib \
       render textures ui_dialogs ui_icons ui_main undo vfs view
OBJS = $(SRCS:%=$(OBJDIR)/%.o)

HDRS = src/actions.h src/brush.h src/common.h src/eclass.h src/editor.h \
       src/game.h src/ini.h src/lexer.h src/map.h src/mathlib.h src/render.h \
       src/textures.h src/ui.h src/ui_internal.h src/vfs.h src/view.h

all: qotif

qotif: $(OBJS)
	$(CC) $(LDFLAGS_ALL) -o $@ $(OBJS) $(LIBS_ALL)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) -std=c99 $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS_ALL) -c $< -o $@

$(OBJDIR):
	mkdir -p $@

$(OBJS): $(HDRS) Makefile

run: qotif
	./qotif

# portable bundle: executable, data and the libXm it was linked against
dist: qotif
	rm -rf $(DISTDIR)
	mkdir -p $(DISTDIR)/lib $(DISTDIR)/data/games $(DISTDIR)/config
	cp qotif README.md $(DISTDIR)/
	cp data/games/* $(DISTDIR)/data/games/
	for lib in `ldd qotif | awk '/libXm\.so/ { print $$3 }'`; do \
		cp -L "$$lib" $(DISTDIR)/lib/; done
	@echo "portable bundle in $(DISTDIR)/ (run $(DISTDIR)/qotif)"

clean:
	rm -f qotif
	rm -rf $(OBJDIR) $(DISTDIR)

.PHONY: all run dist clean
