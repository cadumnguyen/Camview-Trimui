# Build trong toolchain tg5040 (aarch64). FFmpeg được build tĩnh bởi build_ffmpeg.sh vào ./ffmpeg-install
FF ?= $(CURDIR)/ffmpeg-install
CC ?= $(CROSS_COMPILE)gcc
SDLFLAGS := $(shell sdl2-config --cflags --libs 2>/dev/null)
ifeq ($(SDLFLAGS),)
SDLFLAGS := -lSDL2
endif

camview: main.c
	$(CC) -O2 -I$(FF)/include -o camview main.c $(SDLFLAGS) -L$(FF)/lib \
		-lavformat -lavcodec -lswscale -lswresample -lavutil -lpthread -lm

clean:
	rm -f camview
