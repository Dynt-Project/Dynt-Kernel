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
USR_DIR    := userspace
USR_BUILD  := $(BUILD_DIR)/userspace

KERNEL     := $(BUILD_DIR)/dynt-kernel
ISO        := $(BUILD_DIR)/dynt-kernel.iso
FAT_IMG    := $(BUILD_DIR)/fat32.img
FAT_PART   := $(BUILD_DIR)/fat32-part.img
USR_INIT   := $(USR_BUILD)/init

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

# Userspace build flags - PIE so the kernel loader can map each instance
# at its own base and apply the RELATIVE relocations
USR_CFLAGS := -nostdlib -ffreestanding -fPIE -fno-stack-protector \
              -mno-red-zone -O2 -Wall -Wextra -I$(USR_DIR)

USR_LDFLAGS := -nostdlib -pie -T $(USR_DIR)/linker.ld

C_SOURCES  := main.c \
              $(shell find init driver mem fs process scheduler exec libc $(ARCH) -name "*.c" 2>/dev/null)

ASM_SOURCES:= $(shell find $(ARCH) -name "*.S" 2>/dev/null)

C_OBJECTS  := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
A_OBJECTS  := $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))
OBJECTS    := $(C_OBJECTS) $(A_OBJECTS)

.PHONY: all kernel iso fat32 run clean help userspace

all: kernel

# =================== kernel build ===================

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

# =================== userspace build ===================

userspace: $(USR_INIT)

$(USR_INIT): $(USR_DIR)/busybox.c $(USR_DIR)/linker.ld $(USR_DIR)/syscall.h $(USR_DIR)/string.h $(USR_DIR)/stdio.h
	@mkdir -p $(USR_BUILD)
	$(CC) $(USR_CFLAGS) -c $(USR_DIR)/busybox.c -o $(USR_BUILD)/busybox.o
	$(LD) $(USR_LDFLAGS) -o $@ $(USR_BUILD)/busybox.o
	@echo ""
	@echo "==============================="
	@echo "userspace init built: $(USR_INIT)"
	@echo "==============================="

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

# =================== fat32 disk image ===================

fat32: $(FAT_IMG)

$(FAT_IMG): $(USR_INIT)
	@mkdir -p $(BUILD_DIR)
	@echo "Creating partitioned FAT32 disk image..."
	# Create the FAT32 partition image (63MB)
	truncate -s 63M $(FAT_PART)
	mkfs.fat -F 32 -n DYNTDISK $(FAT_PART)
	# Copy userspace init program to the partition
	mcopy -i $(FAT_PART) $(USR_INIT) ::/init
	# Create the full 64MB disk image
	truncate -s 64M $(FAT_IMG)
	# Write MBR partition table (bootable, type 0x0C FAT32 LBA, start sector 2048)
	printf '\x80\xfe\xff\xff\x0c\xfe\xff\xff\x00\x08\x00\x00\x00\xf8\x01\x00' | dd of=$(FAT_IMG) bs=1 seek=446 conv=notrunc 2>/dev/null
	# Write boot signature
	printf '\x55\xaa' | dd of=$(FAT_IMG) bs=1 seek=510 conv=notrunc 2>/dev/null
	# Copy the FAT32 partition into the disk image at sector 2048
	dd if=$(FAT_PART) of=$(FAT_IMG) bs=512 seek=2048 conv=notrunc 2>/dev/null
	rm -f $(FAT_PART)
	@echo ""
	@echo "==============================="
	@echo "fat32 image built: $(FAT_IMG)"
	@echo "  (MBR partition, 1 partition at sector 2048)"
	@echo "  (userspace init at /init)"
	@echo "==============================="

# =================== run ===================

run: iso fat32
	qemu-system-x86_64 -m 256M -smp 4 -cdrom $(ISO) -drive file=$(FAT_IMG),format=raw,if=ide,index=0,media=disk -boot d -serial stdio -no-reboot -no-shutdown

# =================== clean ===================

clean:
	-rm -rf $(BUILD_DIR)

help:
	@echo "make kernel    - build the multiboot2 kernel elf"
	@echo "make iso       - build the grub iso"
	@echo "make userspace - build the userspace init program"
	@echo "make fat32     - build the qemu fat32 disk image with userspace"
	@echo "make run       - build and run it in qemu"
	@echo "make clean     - remove build output"