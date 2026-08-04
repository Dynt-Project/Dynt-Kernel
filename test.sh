#!/usr/bin/env bash
#
# Created by epaxgaming on 31.07.26.
#
# builds the kernel iso with grub and starts it in qemu with a display.
# serial output (COM1) is shown in the terminal.
# boot via: ./test.sh
#
set -euo pipefail
cd "$(dirname "$0")"

make iso

exec qemu-system-x86_64 \
    -m 128M \
    -cdrom build/dynt-kernel.iso \
    -boot d \
    -device VGA \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -no-shutdown
