# Written by [@saphhic](https://github.com/saphhic)  
# Date: 29 July 2026

# Makefile for x86_64 architecture source files
# doesnt compile the kernel, this is just to test
# if the source files compile correctly


CXX     := g++
ASM     := gcc

CXXFLAGS := -std=c++20 \
            -ffreestanding \
            -Wall \
            -Wextra \
            -I.

ASMFLAGS := -x assembler -c

BUILD_DIR := bin

CPP_SOURCES := $(shell find -name "*.c")
ASM_SOURCES := $(shell find -name "*.S")

CPP_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
ASM_OBJECTS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

OBJECTS := $(CPP_OBJECTS) $(ASM_OBJECTS)

all: $(OBJECTS)
	@echo ""
	@echo "==============================="
	@echo "Compiled Successfully"
	@echo "==============================="


$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	-rm -rf $(BUILD_DIR)/*

.PHONY: all clean