#
# Userspace build system.
#
# Every subfolder of userspace/ is one application (except "mlibc", which is
# the mlibc submodule). All *.c / *.cpp sources below an app folder - including
# subfolders - are compiled against mlibc and linked into a static PIE ELF.
#
# The final stripped ELF lands at  build/userspace/<app>/<app>.elf
# The app named "init" is placed at /init on the drive, all others at /<name>.
#

USR_ROOT   := $(dir $(lastword $(MAKEFILE_LIST)))
MLIBC_DIR  := $(USR_ROOT)mlibc
MLIBC_BUILD := $(MLIBC_DIR)/build-dynt
BUILD_DIR  := $(USR_ROOT)../build/userspace

CC    := gcc
CXX   := g++
STRIP := strip

LDSCRIPT := $(USR_ROOT)linker.ld

LIBC_A := $(MLIBC_BUILD)/libc.a
CRT1_O := $(MLIBC_BUILD)/sysdeps/dynt/crt1.o
EXTRA_LIBS := $(wildcard $(MLIBC_BUILD)/libm.a $(MLIBC_BUILD)/libpthread.a $(MLIBC_BUILD)/libdl.a)

# App folders: every subdir except mlibc
APP_DIRS  := $(filter-out $(MLIBC_DIR)/,$(wildcard $(USR_ROOT)*/))
APP_NAMES := $(notdir $(patsubst %/,%,$(APP_DIRS)))

# mlibc include dirs (source + generated config headers)
MLIBC_INC := \
	-I$(MLIBC_DIR)/options/ansi \
	-I$(MLIBC_DIR)/options/internal/include \
	-I$(MLIBC_DIR)/options/elf/include \
	-I$(MLIBC_DIR)/options/rtld/include \
	-I$(MLIBC_DIR)/options/rtld/x86_64 \
	-I$(MLIBC_DIR)/options/internal/x86_64-include \
	-I$(MLIBC_DIR)/sysdeps/generic-helpers/include \
	-I$(MLIBC_DIR)/sysdeps/dynt/include \
	-I$(MLIBC_DIR)/options/ansi/include \
	-I$(MLIBC_DIR)/options/posix/include \
	-I$(MLIBC_DIR)/subprojects/frigg/include \
	-I$(MLIBC_BUILD)

USR_CFLAGS   := -nostdlib -ffreestanding -fPIE -fno-stack-protector -mno-red-zone \
		-O2 -Wall -Wextra -D_GNU_SOURCE $(MLIBC_INC)
USR_CXXFLAGS := $(USR_CFLAGS) -fno-exceptions -fno-rtti -fpermissive
USR_LDFLAGS  := -nostdlib -pie -T $(LDSCRIPT)
# frigg declares frg_panic/frg_log as weak in its headers, so the archive
# member defining them is never pulled in (weak references do not force
# extraction). Force them undefined so the strong definitions from
# mlibc's frigg.cpp land in every app; otherwise a user-side frigg
# assertion calls through a NULL GOT slot and dies in a bare #UD.
USR_LDFLAGS  += -u frg_panic -u frg_log

.PHONY: all apps mlibc clean

all: apps

# =================== mlibc ===================

$(LIBC_A) $(CRT1_O):
	@cd $(MLIBC_DIR) && \
	if [ ! -f build-dynt/build.ninja ]; then \
		echo "[userspace] meson setup mlibc..." ; \
		meson setup --cross-file dynt-x86_64.cross -Ddefault_library=static build-dynt . ; \
	fi && \
	echo "[userspace] building mlibc..." && \
	ninja -C build-dynt libc.a sysdeps/dynt/crt1.o

mlibc: $(LIBC_A) $(CRT1_O)

# =================== apps ===================

define APP_template
SOURCES_$(1) := $$(shell find $(USR_ROOT)$(1) -type f \( -name '*.c' -o -name '*.cpp' \))
OBJS_$(1)    := $$(patsubst $(USR_ROOT)$(1)/%.c,$(BUILD_DIR)/$(1)/%.o,$$(filter %.c,$$(SOURCES_$(1)))) \
                $$(patsubst $(USR_ROOT)$(1)/%.cpp,$(BUILD_DIR)/$(1)/%.o,$$(filter %.cpp,$$(SOURCES_$(1))))

$(BUILD_DIR)/$(1)/$(1).elf: $$(OBJS_$(1)) $(CRT1_O) $(LIBC_A) $(LDSCRIPT)
	@mkdir -p $$(dir $$@)
	$(CXX) $(USR_LDFLAGS) -o $$@.unstripped $(CRT1_O) $$(OBJS_$(1)) \
		-Wl,--start-group $(LIBC_A) $(EXTRA_LIBS) -Wl,--end-group -lgcc
	$(STRIP) -o $$@ $$@.unstripped
	rm -f $$@.unstripped
	@echo "[userspace] built: $$@"

$(BUILD_DIR)/$(1)/%.o: $(USR_ROOT)$(1)/%.c
	@mkdir -p $$(dir $$@)
	$(CC) $(USR_CFLAGS) -c $$< -o $$@

$(BUILD_DIR)/$(1)/%.o: $(USR_ROOT)$(1)/%.cpp
	@mkdir -p $$(dir $$@)
	$(CXX) $(USR_CXXFLAGS) -c $$< -o $$@

APPS += $(BUILD_DIR)/$(1)/$(1).elf
endef

$(foreach a,$(APP_NAMES),$(eval $(call APP_template,$(a))))

apps: mlibc $(APPS)

clean:
	-rm -rf $(BUILD_DIR)

.PHONY: all apps mlibc clean
