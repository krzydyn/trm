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

CXX:=g++
CXXFLAGS:=$(DEBUG) -I$(INC_DIR) -fPIC -std=c++11
CXXFLAGS+=-Wall -Wconversion -Werror

LDFLAGS:=-rdynamic
LDFLAGS+=-lpthread $(shell pkg-config --libs uhd)

ifeq ($(HOST_OS),Linux)
LDFLAGS+=-ldl
endif

SRCS_TRM:=./trm.cpp ./MobileStation.cpp ./RadioDevice.cpp
SRCS_TRXCOM:=./trxcom.cpp ./Transcom.cpp
OBJS_TRM:=$(patsubst $(SOURCE_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS_TRM))
OBJS_TRXCOM:=$(patsubst $(SOURCE_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS_TRXCOM))
TARGETS:=$(BUILD_DIR)/trm $(BUILD_DIR)/trxcom

$(BUILD_DIR)/trm: $(OBJS_TRM) $(JRELIB)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/trxcom: $(OBJS_TRXCOM) $(JRELIB)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	@#printf "cpp $(CXX) -c $(CXXFLAGS) $< -o $@\n"
	$(CXX) -c $(CXXFLAGS) $< -o $@


build: create-$(BUILD_DIR) $(TARGETS)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: create-$(BUILD_DIR)
create-$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

