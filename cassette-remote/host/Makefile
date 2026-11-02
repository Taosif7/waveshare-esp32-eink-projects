# Host checks. Does not flash. `pio run` once so U8g2 is in .pio/libdeps.
CXX = g++
CC = gcc
U8 = ../.pio/libdeps/esp32-c3-mini/U8g2/src

CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I../src -I$(U8)
CFLAGS = -std=c11 -O2 -I$(U8)/clib

U8_SRC = \
	$(U8)/clib/u8g2_buffer.c \
	$(U8)/clib/u8g2_box.c \
	$(U8)/clib/u8g2_font.c \
	$(U8)/clib/u8g2_kerning.c \
	$(U8)/clib/u8x8_8x8.c \
	$(U8)/clib/u8x8_capture.c \
	$(U8)/clib/u8x8_u16toa.c \
	$(U8)/clib/u8g2_circle.c \
	$(U8)/clib/u8g2_hvline.c \
	$(U8)/clib/u8g2_intersection.c \
	$(U8)/clib/u8g2_line.c \
	$(U8)/clib/u8g2_ll_hvline.c \
	$(U8)/clib/u8g2_polygon.c \
	$(U8)/clib/u8g2_setup.c \
	$(U8)/clib/u8g2_d_memory.c \
	$(U8)/clib/u8x8_cad.c \
	$(U8)/clib/u8x8_byte.c \
	$(U8)/clib/u8x8_display.c \
	$(U8)/clib/u8x8_gpio.c \
	$(U8)/clib/u8x8_setup.c \
	$(U8)/clib/u8x8_d_ssd1306_128x64_noname.c

.PHONY: test preview clean

test: build/test_logic
	./build/test_logic

preview: build/preview
	./build/preview
	python3 pack_images.py

build/test_logic: test_logic.cpp ../src/input_logic.cpp ../src/transport.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Werror -o $@ test_logic.cpp ../src/input_logic.cpp ../src/transport.cpp

build/preview: preview.cpp ../src/ui.cpp ../src/transport.cpp $(U8_SRC)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ preview.cpp ../src/ui.cpp ../src/transport.cpp $(U8_SRC)

clean:
	rm -rf build
