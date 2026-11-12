# Host-side tests for the module bay code (plain gcc, not part of the firmware build).
#   make -C utilities/module_bay_test test

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99
SRC = ../../src/module
OUT ?= /tmp/module_bay_test

# module.c built as for the Goggle 2 emulator, against recording module types
ROOT = ../..
WORKER_CFLAGS = -O1 -g -std=gnu99 -DEMULATOR_BUILD -DHDZGOGGLE2=1 -D_GNU_SOURCE \
	-I$(ROOT)/src -I$(ROOT)/src/core -I$(ROOT)/lib/log/include -I$(ROOT)/lib/lvgl -I$(ROOT)/lib/minIni/src

all: $(OUT)/test_trued_uart $(OUT)/test_module_worker

$(OUT)/test_trued_uart: test_trued_uart.c $(SRC)/bitbang_uart.c $(SRC)/trued_proto.c $(SRC)/bitbang_uart.h $(SRC)/trued_proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ test_trued_uart.c $(SRC)/bitbang_uart.c $(SRC)/trued_proto.c

$(OUT)/test_module_worker: test_module_worker.c $(SRC)/module.c $(SRC)/module.h $(SRC)/module_def.h
	mkdir -p $(OUT)
	$(CC) $(WORKER_CFLAGS) -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -o $@ test_module_worker.c -lpthread

test: $(OUT)/test_trued_uart $(OUT)/test_module_worker
	$(OUT)/test_trued_uart
	$(OUT)/test_module_worker

clean:
	rm -f $(OUT)/test_trued_uart $(OUT)/test_module_worker

.PHONY: all test clean
