CC=gcc
CXX=g++

ifdef DEBUG
CFLAGS?=-O0 -g -DIMDEBUG
CXXFLAGS?=-O0 -g -DIMDEBUG
else
CFLAGS?=-O2
CXXFLAGS?=-O2
endif

CFLAGS+=-Wall -Wno-parentheses
CXXFLAGS+=-Wall -Wno-parentheses -std=c++11
LD=g++
LDFLAGS=
LIBS=

ifeq ($(shell uname -m),armv7l)
LIBS+=-L/opt/vc/lib -lGLESv2 -lEGL
CFLAGS+=-DHAVE_OPENGLES2 -pthread
CXXFLAGS+=-DHAVE_OPENGLES2 -pthread
LDFLAGS+=-Wl,-Bsymbolic
EXE=
else
ifeq ($(shell uname -o),Msys)
LIBS+=-lglew32 -lopengl32
LDFLAGS+=-Wl,-Bsymbolic -pthread -mthreads -Wl,-subsystem,console
EXE=.exe
else
ifeq ($(shell uname),Darwin)
LIBS+=-framework OpenGL
EXE=
endif
endif
endif

SOURCES_CXX = \
	imdialog.cpp \
	imgui/imgui.cpp \
	imgui/imgui_draw.cpp

OBJECTS = $(SOURCES_CXX:%.cpp=%.o)

SHADERS = imgui.vs.glsl imgui.fs.glsl

all:	imdialog$(EXE)

imdialog$(EXE): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^ `sdl2-config --libs` $(LIBS)

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean install

clean:
	rm -rf $(OBJECTS) $(ALL)

rebuild: clean $(ALL)

