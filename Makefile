### flying V6.20 - Makefile for modern Linux (g++ / clang++, X11)
###
### Requirements: a C++ compiler and the X11/Xext development headers
### (Debian/Ubuntu: libx11-dev libxext-dev, Fedora: libX11-devel libXext-devel).
###
### Overridable from the command line, e.g.
###   make X_INCLUDE=/opt/x11/include LIBS="-l:libX11.so.6 -lm"

### directory for writing temporary data and pixmap files
DATA_DIRECTORY = '"/tmp"'
BUILD_DIRECTORY = bin

### compiler section
### for debugging, there might be the additional flags -DDEBUG and -DSTATISTICS
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wno-unused -Wno-misleading-indentation -Wno-parentheses -Wno-narrowing -Iinclude

### parameter type of select-parameters (empty on Linux)
FDS_TYPE = ''

### X11 include/lib directories. Use pkg-config when available, otherwise
### fall back to the standard system locations.
PKG_CONFIG ?= pkg-config
X_CFLAGS   := $(shell $(PKG_CONFIG) --cflags x11 xext 2>/dev/null)
X_LIBS     := $(shell $(PKG_CONFIG) --libs x11 xext 2>/dev/null)
ifeq ($(X_LIBS),)
X_LIBS := -lX11 -lXext
endif
ifdef X_INCLUDE
X_CFLAGS += -I$(X_INCLUDE)
endif
ifdef XLIB_DIR
LDFLAGS  += -L$(XLIB_DIR)
endif

###################################################################

CPPFLAGS += $(X_CFLAGS) -DDATA_DIRECTORY=$(DATA_DIRECTORY) -DFDS_TYPE=$(FDS_TYPE)
LIBS     ?= $(X_LIBS) -lm

.SUFFIXES: src/.C .o

bin/%.o: src/%.C
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

OBJS	= bin/arcs.o \
		bin/ball.o \
		bin/billard.o \
		bin/cannon.o \
		bin/carrom.o \
		bin/curling.o \
		bin/dynobj.o \
		bin/game.o \
		bin/global.o \
		bin/goal.o \
		bin/graph.o \
		bin/hockey.o \
		bin/keeper.o \
		bin/logfile.o \
		bin/main.o \
		bin/mover.o \
		bin/object.o \
		bin/objtab.o \
		bin/pball.o \
		bin/pcon.o \
		bin/pocket.o \
		bin/pool.o \
		bin/pool8.o \
		bin/real.o \
		bin/rgb.o \
		bin/snooker.o \
		bin/vec2.o \
		bin/vec3.o \
		bin/vector.o \
		bin/wall.o \
		bin/xbench.o \
		bin/xsound.o

PROGRAM		= flying
LINKS		=	$(BUILD_DIRECTORY)/xpool 		\
				$(BUILD_DIRECTORY)/xsnooker 	\
				$(BUILD_DIRECTORY)/xcarrom 		\
				$(BUILD_DIRECTORY)/xhockey 		\
				$(BUILD_DIRECTORY)/xcurling 	\
				$(BUILD_DIRECTORY)/xcannon

all:			$(BUILD_DIRECTORY)					\
				$(BUILD_DIRECTORY)/$(PROGRAM)		\
				$(LINKS)

$(BUILD_DIRECTORY):
	mkdir -p $(BUILD_DIRECTORY)

$(BUILD_DIRECTORY)/$(PROGRAM): $(OBJS)
		$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -o $(BUILD_DIRECTORY)/$(PROGRAM)

$(LINKS): $(BUILD_DIRECTORY)/$(PROGRAM)
		ln -sf $(PROGRAM) $@

clean:
		-rm -f $(LINKS) $(BUILD_DIRECTORY)/$(PROGRAM) $(OBJS)
		-rm -f fly.dta/*.*

.PHONY: all clean

### dependencies ###

bin/arcs.o: include/global.h   \
	  include/arcs.h include/object.h include/real.h \
	include/vec2.h include/vector.h include/ball.h include/dynobj.h include/objtab.h include/game.h include/keeper.h
bin/ball.o: include/global.h   \
	  include/ball.h include/arcs.h include/object.h \
	include/real.h include/vec2.h include/vector.h include/dynobj.h include/objtab.h include/game.h include/keeper.h include/mover.h \
	include/graph.h include/xgraph.h  \
	include/xsound.h    \
	include/xmover.h  include/pball.h include/xpball.h 
bin/billard.o: include/global.h   \
	 include/billard.h include/real.h include/game.h include/vec2.h \
	 include/vector.h include/keeper.h include/wall.h include/object.h include/graph.h \
	include/xgraph.h   \
	 include/xsound.h  \
	  include/mover.h include/xmover.h
bin/cannon.o: include/global.h   \
	 include/cannon.h include/real.h include/billard.h include/game.h include/vec2.h \
	 include/vector.h include/keeper.h include/ball.h include/arcs.h include/object.h include/dynobj.h \
	include/objtab.h include/wall.h include/graph.h include/xgraph.h  \
	include/xsound.h    \
	include/mover.h include/xmover.h 
bin/carrom.o: include/global.h   \
	 include/carrom.h include/real.h include/vec2.h  \
	include/vector.h include/game.h include/keeper.h include/wall.h include/object.h include/graph.h include/xgraph.h \
	 include/xsound.h  \
	  include/mover.h include/xmover.h \
	 include/arcs.h include/pocket.h include/ball.h include/dynobj.h include/objtab.h
bin/curling.o: include/global.h   \
	 include/curling.h include/vec2.h  include/real.h \
	include/vector.h include/game.h include/keeper.h include/arcs.h include/object.h include/ball.h include/dynobj.h include/objtab.h \
	include/goal.h include/wall.h include/graph.h include/xgraph.h  \
	include/xsound.h    \
	include/mover.h include/xmover.h 
bin/dynobj.o: include/global.h   \
	 include/dynobj.h include/real.h include/objtab.h include/object.h include/ball.h include/arcs.h \
	include/vec2.h  include/vector.h
bin/game.o: include/global.h   \
	 include/game.h include/vec2.h  include/real.h \
	include/vector.h include/keeper.h include/wall.h include/object.h include/ball.h include/arcs.h include/dynobj.h include/objtab.h \
	include/graph.h include/xgraph.h  \
	include/xsound.h   
bin/global.o: include/global.h   \
	  include/real.h include/cannon.h include/billard.h \
	include/game.h include/vec2.h  include/vector.h include/keeper.h include/carrom.h include/goal.h \
	include/wall.h include/object.h include/hockey.h include/curling.h include/pball.h include/xpball.h  include/pool.h \
	include/pool8.h include/snooker.h include/pocket.h include/ball.h include/arcs.h include/dynobj.h include/objtab.h
bin/goal.o: include/global.h   \
	 include/goal.h include/wall.h include/object.h include/real.h include/vec2.h \
	 include/vector.h include/ball.h include/arcs.h include/dynobj.h include/objtab.h include/game.h \
	include/keeper.h
bin/graph.o: include/global.h   \
	 include/object.h include/real.h include/graph.h include/vec2.h \
	 include/vector.h include/xgraph.h  \
	include/xsound.h    \
	include/pball.h include/xpball.h  src/xgraph.C include/pointer.h include/stipple.h include/game.h \
	include/keeper.h \
	include/rgb.h
bin/hockey.o: include/global.h   \
	 include/hockey.h include/vec2.h  include/real.h \
	include/vector.h include/game.h include/keeper.h include/arcs.h include/object.h include/ball.h include/dynobj.h include/objtab.h \
	include/goal.h include/wall.h include/graph.h include/xgraph.h  \
	include/xsound.h    \
	include/mover.h include/xmover.h 
bin/keeper.o: include/global.h   \
	 include/ball.h include/arcs.h include/object.h include/real.h include/vec2.h \
	 include/vector.h include/dynobj.h include/objtab.h include/wall.h include/keeper.h include/game.h \
	include/graph.h include/xgraph.h  \
	include/xsound.h   
bin/logfile.o:   \
	  include/global.h \
	include/logfile.h
bin/main.o: include/global.h   \
	  include/dynobj.h include/real.h include/objtab.h \
	include/object.h include/graph.h include/vec2.h include/vector.h include/xgraph.h \
	 include/xsound.h  \
	  include/mover.h include/xmover.h \
	 include/game.h include/keeper.h include/snooker.h include/pool.h include/billard.h include/cannon.h \
	include/pool8.h include/carrom.h include/hockey.h include/curling.h include/pball.h include/xpball.h 
bin/mover.o: include/global.h   \
	 include/mover.h include/real.h include/graph.h include/object.h include/vec2.h \
	 include/vector.h include/xgraph.h  \
	include/xsound.h    \
	include/xmover.h  src/xmover.C include/vec3.h
bin/object.o: include/global.h   \
	 include/object.h include/real.h include/ball.h include/arcs.h include/vec2.h \
	 include/vector.h include/dynobj.h include/objtab.h include/graph.h include/xgraph.h \
	 include/xsound.h
bin/objtab.o: include/global.h   \
	 include/objtab.h include/real.h include/object.h
bin/pball.o: include/global.h   \
	 include/pball.h include/vec2.h  include/real.h \
	include/vector.h include/xpball.h  include/ball.h include/arcs.h include/object.h include/dynobj.h include/objtab.h \
	include/graph.h include/xgraph.h  \
	include/xsound.h    \
	include/game.h include/keeper.h src/xpball.C include/cursor.h
bin/pcon.o: include/global.h   \
	 include/pcon.h include/real.h include/pocket.h include/ball.h include/arcs.h include/object.h \
	include/vec2.h  include/vector.h include/dynobj.h include/objtab.h include/keeper.h include/wall.h \
	include/graph.h include/xgraph.h  \
	include/xsound.h   
bin/pocket.o: include/global.h   \
	 include/pocket.h include/ball.h include/arcs.h include/object.h include/real.h include/vec2.h \
	 include/vector.h include/dynobj.h include/objtab.h include/keeper.h include/game.h
bin/pool.o: include/global.h   \
	 include/pool.h include/real.h include/billard.h include/game.h include/vec2.h \
	 include/vector.h include/keeper.h include/pcon.h include/pocket.h include/ball.h include/arcs.h \
	include/object.h include/dynobj.h include/objtab.h include/wall.h include/graph.h include/xgraph.h \
	 include/xsound.h  \
	  include/mover.h include/xmover.h
bin/pool8.o: include/global.h   \
	 include/pool8.h include/pool.h include/real.h include/billard.h include/game.h include/vec2.h \
	 include/vector.h include/keeper.h include/graph.h include/object.h include/xgraph.h \
	 include/xsound.h  \
	  include/mover.h include/xmover.h \
	 include/ball.h include/arcs.h include/dynobj.h include/objtab.h include/wall.h
bin/real.o: include/global.h   \
	 include/real.h 
bin/rgb.o: include/rgb.h 
bin/snooker.o: include/global.h   \
	 include/snooker.h include/vec2.h  include/real.h \
	include/vector.h include/pool.h include/billard.h include/game.h include/keeper.h include/pocket.h include/ball.h include/arcs.h \
	include/object.h include/dynobj.h include/objtab.h include/graph.h include/xgraph.h \
	 include/xsound.h  \
	  include/mover.h include/xmover.h
bin/vec2.o: include/global.h   \
	 include/vec2.h  include/real.h include/vector.h
bin/vec3.o: include/global.h   \
	 include/vec3.h  include/real.h include/vector.h
bin/vector.o: include/global.h   \
	 include/vector.h  include/real.h \
	  include/vec2.h include/vec3.h
bin/wall.o: include/global.h   \
	 include/wall.h include/object.h include/real.h include/vec2.h \
	 include/vector.h include/ball.h include/arcs.h include/dynobj.h include/objtab.h include/game.h \
	include/keeper.h