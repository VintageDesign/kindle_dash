# Kindle build (default):   make
# Desktop test build:       make HOST=1 && ./build/host/kindledash kual/kindledash/dash.ini.example
# Package for KUAL:         make dist      -> dist/kindledash/
# Copy to a mounted Kindle: make deploy KINDLE=/media/$(USER)/Kindle

# arm-kindlepw2-linux-gnueabi (soft-float) for firmware < 5.16.3, e.g. this PW4 on 5.13.6;
# arm-kindlehf-linux-gnueabihf for 5.16.3 and newer.
CROSS_TC ?= arm-kindlepw2-linux-gnueabi
SYSROOT  ?= $(HOME)/x-tools/$(CROSS_TC)/$(CROSS_TC)/sysroot
KINDLE   ?= /media/$(USER)/Kindle

PKGS   := gtk+-2.0 pangocairo
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-unused-local-typedefs

ifeq ($(HOST),1)
BUILD      := build/host
CC         := cc
PKG_CONFIG := pkg-config
CFLAGS     += -DKINDLEDASH_HOST -g
else
BUILD      := build/kindle
CC         := $(HOME)/x-tools/$(CROSS_TC)/bin/$(CROSS_TC)-gcc
PKG_CONFIG := PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) \
              PKG_CONFIG_LIBDIR=$(SYSROOT)/usr/lib/pkgconfig:$(SYSROOT)/usr/share/pkgconfig \
              pkg-config
CFLAGS     += --sysroot=$(SYSROOT)
# The firmware libs reference newer glibc symbols than the toolchain ships; the Kindle has them.
LDFLAGS    += --sysroot=$(SYSROOT) -Wl,--allow-shlib-undefined
endif

PKG_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))

SRCS := src/main.c src/pages.c src/ui.c src/config.c src/http_json.c \
        src/pages/clock_page.c src/pages/weather_page.c src/pages/svg_page.c \
        third_party/cJSON.c
OBJS := $(SRCS:%.c=$(BUILD)/%.o)
BIN  := $(BUILD)/kindledash

all: check-pkgs $(BIN)

check-pkgs:
	@$(PKG_CONFIG) --exists $(PKGS) || { \
	  echo "error: $(PKGS) not found via pkg-config$(if $(HOST),, in $(SYSROOT))."; \
	  echo "       The Kindle build needs GTK2/pango/cairo headers + libs in the sysroot (see README)."; \
	  exit 1; }

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LIBS) -lm

$(BUILD)/%.o: %.c $(wildcard src/*.h src/pages/*.h third_party/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c -o $@ $<

cacert.pem:
	curl -fsSL -o $@ https://curl.se/ca/cacert.pem

dist: all cacert.pem
	rm -rf dist/kindledash "dist/Kindle Dash.sh"
	mkdir -p dist/kindledash/bin
	cp -r kual/kindledash/. dist/kindledash/
	cp "scriptlets/Kindle Dash.sh" dist/
	cp $(BIN) dist/kindledash/bin/kindledash
	cp cacert.pem dist/kindledash/
	chmod +x dist/kindledash/bin/*

# Copies the extension but never overwrites a dash.ini already on the device.
deploy: dist
	@test -d $(KINDLE)/system || { echo "error: $(KINDLE) does not look like a mounted Kindle"; exit 1; }
	mkdir -p $(KINDLE)/extensions/kindledash
	cp -r dist/kindledash/bin dist/kindledash/config.xml dist/kindledash/menu.json \
	      dist/kindledash/dash.ini.example dist/kindledash/cacert.pem \
	      $(patsubst kual/%,dist/%,$(wildcard kual/kindledash/*.svg)) $(KINDLE)/extensions/kindledash/
	@test -f $(KINDLE)/extensions/kindledash/dash.ini || \
	  cp dist/kindledash/dash.ini.example $(KINDLE)/extensions/kindledash/dash.ini
	cp "dist/Kindle Dash.sh" $(KINDLE)/documents/
	sync

clean:
	rm -rf build dist

.PHONY: all check-pkgs dist deploy clean
