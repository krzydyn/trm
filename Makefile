.PHONY: all build clean install
all:build

PREFIX?=.
TOP_DIR?=.

BUILD_DIR?=$(TOP_DIR)/build
SOURCE_DIR?=$(PREFIX)
INC_DIR?=../japp/include
JRELIB?=../japp/build/jre.a
DEBUG?=-g2
STATIC=

BUILD_DIR:=$(BUILD_DIR)/bin

HOST_OS:=$(shell uname -s)
GCC_VERSION:=$(shell gcc --version | awk '/^gcc/ {print $$4}')

CC:=g++
CPPFLAGS:=$(DEBUG) -I$(INC_DIR) -fPIC -std=c++11
CPPFLAGS+=-Wall -Wconversion -Werror

LDFLAGS:=-rdynamic
LDFLAGS+=-lpthread

ifeq ($(HOST_OS),Linux)
LDFLAGS+=-ldl
endif

SRCS:=$(wildcard $(SOURCE_DIR)/*.cpp)
OBJS:=$(patsubst $(SOURCE_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
TARGETS:=$(BUILD_DIR)/trm

$(BUILD_DIR)/trm: $(SRCS) $(JRELIB)
	$(CC) $(CPPFLAGS) $^ $(LDFLAGS) -o $@

build: create-$(BUILD_DIR) $(TARGETS)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: create-$(BUILD_DIR)
create-$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

