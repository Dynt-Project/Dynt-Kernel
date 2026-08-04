#
# Created by epaxgaming on 31.07.26.
#
# this Makefile builds the kernel as a multiboot2 ELF that grub can load
# and packs it into a bootable ISO with grub-mkrescue.
# run "./test.sh" or "make iso" and boot the iso in qemu with a display.
#

CXX        := g++
CC         := gcc

ARCH       := arch/x86_64
BUILD_DIR  := build
ISO_DIR    := $(BUILD_DIR)/iso

KERNEL     := $(BUILD_DIR)/dynt-kernel
ISO        := $(BUILD_DIR)/dynt-kernel.iso

CXXFLAGS   := -std=c++20 \
              -ffreestanding \
              -fno-exceptions \
              -fno-rtti \
              -fno-pie \
              -fno-pic \
              -fno-stack-protector \
              -mno-red-zone \
              -mcmodel=large \
              -O2 \
              -Wall \
              -Wextra \
              -I. \
              -I$(ARCH)

ASFLAGS    :=

LDFLAGS    := -ffreestanding \
              -nostdlib \
              -no-pie \
              -lgcc

C_SOURCES  := main.c \
              $(shell find init driver examples $(ARCH) -name "*.c" 2>/dev/null)

ASM_SOURCES:= $(shell find $(ARCH) -name "*.S" 2>/dev/null)

C_OBJECTS  := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
A_OBJECTS  := $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))
OBJECTS    := $(C_OBJECTS) $(A_OBJECTS)

.PHONY: all kernel iso run clean help

all: kernel

# =================== build ===================

kernel: $(KERNEL)

$(KERNEL): $(OBJECTS) linker.ld
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(LDFLAGS) -T linker.ld -o $@ $(OBJECTS)
	@echo ""
	@echo "==============================="
	@echo "kernel built: $(KERNEL)"
	@echo "==============================="

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

# =================== iso ===================

iso: $(ISO)

$(ISO): $(KERNEL) boot/grub/grub.cfg
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/dynt-kernel
	cp boot/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR) 2>/dev/null
	@echo ""
	@echo "==============================="
	@echo "iso built: $(ISO)"
	@echo "==============================="

# =================== run ===================

run: iso
	qemu-system-x86_64 -m 128M -cdrom $(ISO) -boot d -serial stdio -no-reboot -no-shutdown

# =================== clean ===================

clean:
	-rm -rf $(BUILD_DIR)

help:
	@echo "make kernel  - build the multiboot2 kernel elf"
	@echo "make iso     - build the grub iso"
	@echo "make run     - build and run it in qemu"
	@echo "make clean   - remove build output"
