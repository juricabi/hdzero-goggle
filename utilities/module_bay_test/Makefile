# Host-side tests for the module bay code (plain gcc, not part of the firmware build).
#   make -C utilities/module_bay_test test

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99
SRC = ../../src/module
OUT ?= /tmp/module_bay_test

all: $(OUT)/test_trued_uart

$(OUT)/test_trued_uart: test_trued_uart.c $(SRC)/bitbang_uart.c $(SRC)/trued_proto.c $(SRC)/bitbang_uart.h $(SRC)/trued_proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ test_trued_uart.c $(SRC)/bitbang_uart.c $(SRC)/trued_proto.c

test: $(OUT)/test_trued_uart
	$(OUT)/test_trued_uart

clean:
	rm -f $(OUT)/test_trued_uart

.PHONY: all test clean
