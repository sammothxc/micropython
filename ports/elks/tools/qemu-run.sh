#!/usr/bin/env bash
# Run a Python script on ELKS under QEMU and print what it writes.
#
#   tools/qemu-run.sh script.py [seconds] [extra files to put next to it ...]
#
# A copy of a bootable ELKS FAT hard-disk image gets build/micropython as
# /bin/micropython and the script as /root/t.py, and boots with a shell script
# as init that runs the script and then prints an end marker.  QEMU is stopped
# at the marker, or after the given number of seconds (default 60), and this
# script exits with the script's exit status.
#
# Environment:
#   ELKS_IMG  bootable ELKS FAT disk image (default ~/elks-qemu/hd32-fat.img);
#             its kernel must be built with CONFIG_EXEC_OS2
#   MPY       MicroPython binary (default build/micropython next to this port)
#   ICOUNT    set to 1 to clock QEMU by instructions executed instead of by
#             host time.  Timings from time.ticks_ms() are then reproducible,
#             and code takes roughly 30 to 35 times as long on a 4.77 MHz 8088.
set -euo pipefail

if [ $# -lt 1 ]; then
    sed -n '2,5p' "$0"
    exit 2
fi
here=$(cd "$(dirname "$0")/.." && pwd)
script=$1
seconds=${2:-60}
shift
[ $# -gt 0 ] && shift
img=${ELKS_IMG:-$HOME/elks-qemu/hd32-fat.img}
mpy=${MPY:-$here/build/micropython}
icount=()
[ "${ICOUNT:-0}" = 1 ] && icount=(-icount shift=7,sleep=off)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$img" "$work/hd.img"

# /bootopts must start with "##".  A line without "=" is passed to init as an
# argument.  The file is padded because the kernel reads a fixed-size block.
{
    printf '## bootopts\nconsole=ttyS0,19200\nhma=kernel\ninit=/bin/sh\n/root/run.sh\n'
    for _ in $(seq 60); do echo '#############'; done
} > "$work/bootopts"
printf '/bin/micropython /root/t.py\necho "##END## $?"\n' > "$work/run.sh"
mcopy -o -i "$work/hd.img" "$work/bootopts" ::/bootopts
mcopy -o -i "$work/hd.img" "$work/run.sh" ::/root/run.sh
mcopy -o -i "$work/hd.img" "$mpy" ::/bin/micropython
mcopy -o -i "$work/hd.img" "$script" ::/root/t.py
for f in "$@"; do
    mcopy -o -i "$work/hd.img" "$f" "::/root/$(basename "$f")"
done

# "-d nochain" works around a QEMU bug (seen in 10.0): when QEMU chains
# translated blocks, a near CALL or JMP whose 16-bit target wraps past the end
# of the code segment is not wrapped if the unwrapped address is on the same
# 4 KB page, so execution continues at CS:IP+64K.  The linker turns far calls
# within a segment into "push cs; call near", which can wrap like that, so
# without this option a build can crash in QEMU and still run correctly on a
# real 8086.  Chaining only affects QEMU's speed, not what the guest computes.
touch "$work/out"
timeout "$seconds" qemu-system-i386 -M isapc -cpu 486 -m 1M \
    -drive "file=$work/hd.img,format=raw,if=ide" -boot c -d nochain "${icount[@]}" \
    -display none -serial "file:$work/out" -monitor none </dev/null >/dev/null 2>&1 &
qemu=$!
# Show the output as it arrives, from the end of the kernel's boot messages
# up to the end marker; stop QEMU at the marker.
tail -s 0.2 -f --pid=$qemu "$work/out" |
    while IFS= read -r line || [ -n "$line" ]; do
        line=${line//$'\r'/}
        if [ -z "${started:-}" ]; then
            case $line in *'VFS: Mounted'*) started=1 ;; esac
            continue
        fi
        case $line in
        *'##END## '*)
            printf '%s' "${line%%##END## *}"
            [ -n "${line%%##END## *}" ] && echo
            echo "${line##*##END## }" > "$work/status"
            kill $qemu 2>/dev/null || true
            ;;
        *)
            printf '%s\n' "$line"
            ;;
        esac
    done || true
wait $qemu 2>/dev/null || true
if [ -s "$work/status" ]; then
    exit "$(cat "$work/status")"
fi
echo "[stopped after $seconds seconds]" >&2
exit 124
