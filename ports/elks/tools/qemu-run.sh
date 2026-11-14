#!/usr/bin/env bash
# Run a Python script on ELKS under QEMU and print what it writes.
#
#   tools/qemu-run.sh script.py [seconds] [extra files to put next to it ...]
#
# A copy of a bootable ELKS FAT hard-disk image gets build/micropython as
# /bin/micropython and the script as /root/t.py, and boots with MicroPython as
# the init program on the serial console.  Nothing powers the machine off when
# the script ends, so QEMU is stopped after the given number of seconds
# (default 10).  QEMU runs at host speed: this tests correctness, not 8088
# performance.
#
# Environment:
#   ELKS_IMG  bootable ELKS FAT disk image (default ~/elks-qemu/hd32-fat.img);
#             its kernel must be built with CONFIG_EXEC_OS2
#   MPY       MicroPython binary (default build/micropython next to this port)
set -euo pipefail

if [ $# -lt 1 ]; then
    sed -n '2,5p' "$0"
    exit 2
fi
here=$(cd "$(dirname "$0")/.." && pwd)
script=$1
seconds=${2:-10}
shift
[ $# -gt 0 ] && shift
img=${ELKS_IMG:-$HOME/elks-qemu/hd32-fat.img}
mpy=${MPY:-$here/build/micropython}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$img" "$work/hd.img"

# /bootopts must start with "##".  A line without "=" is passed to init as an
# argument.  The file is padded because the kernel reads a fixed-size block.
{
    printf '## bootopts\nconsole=ttyS0,19200\nhma=kernel\ninit=/bin/micropython\n/root/t.py\n'
    for _ in $(seq 60); do echo '#############'; done
} > "$work/bootopts"
mcopy -o -i "$work/hd.img" "$work/bootopts" ::/bootopts
mcopy -o -i "$work/hd.img" "$mpy" ::/bin/micropython
mcopy -o -i "$work/hd.img" "$script" ::/root/t.py
for f in "$@"; do
    mcopy -o -i "$work/hd.img" "$f" "::/root/$(basename "$f")"
done

timeout "$seconds" qemu-system-i386 -M isapc -cpu 486 -m 1M \
    -drive "file=$work/hd.img,format=raw,if=ide" -boot c \
    -display none -serial stdio -monitor none 2>&1 |
    sed -n '/VFS: Mounted/,$p' | grep -a -v 'VFS: Mounted\|terminating on signal' || true
