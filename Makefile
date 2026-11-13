# PocketBook SDK 6.3.0 Makefile
SDK_PATH = /SDK/usr
SYSROOT = $(SDK_PATH)/arm-obreey-linux-gnueabi/sysroot

CC = $(SDK_PATH)/bin/arm-obreey-linux-gnueabi-gcc

APP_NAME = OPDSClient.app
SRC = main.c network.c parser.c ui.c filename.c
OBJ = $(SRC:.c=.o)

# LDFLAGS: Changed -lft2 to -lfreetype
# Added explicit path to the sysroot library folder where libfreetype.so lives
LDFLAGS = -L$(SYSROOT)/usr/lib \
          -linkview -lcurl -lxml2 -lfreetype -lm -ldl

CFLAGS = -Wall -O2 \
         -I$(SYSROOT)/usr/include \
         -I$(SYSROOT)/usr/include/libxml2 \
         -I$(SYSROOT)/usr/include/freetype2

all: $(APP_NAME)

$(APP_NAME): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(APP_NAME)
	rm -rf build

# Host-side tests for the pure-C filename logic (no SDK needed).
# CI also runs them with HOST_CC="cc -fsanitize=address,undefined ...".
HOST_CC ?= cc
HOST_CFLAGS ?= -std=gnu11 -Wall -Wextra -O1 -g

test: build/test_filename
	./build/test_filename

build/test_filename: tests/test_filename.c filename.c filename.h
	mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test_filename.c filename.c

.PHONY: all clean test
