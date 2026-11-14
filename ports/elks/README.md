# MicroPython for ELKS (8086/8088)

An experimental port of MicroPython to [ELKS](https://github.com/ghaerr/elks),
the Linux-like OS for 8086-class PCs.  It is based on MicroPython v1.24.1 and
on the 16-bit core changes of [Py86](https://github.com/ARMoir/micropython/tree/tandypy-dos16-port),
the MS-DOS port by ARMoir.

    micropython              interactive REPL (Ctrl-D exits)
    micropython script.py    run a file
    micropython -c "code"    run a string

Status: runs under QEMU.  It has not been run on real hardware yet.

## Building

The 8086 has 16-bit registers and 64 KB segments, so the interpreter is built
in the *large* memory model (far code and far data pointers).  The usual ELKS
compiler, `ia16-elf-gcc`, has no such model; the build uses Open Watcom v2.

Requirements:

- Open Watcom v2 (a snapshot unpacked into `~/open-watcom-v2`, or set `WATCOM`)
- an ELKS source tree (`~/elks`, or set `ELKS`) with its C library built for
  Open Watcom large model:

      cd ~/elks && . ./env.sh
      export WATCOM=~/open-watcom-v2 PATH=$WATCOM/binl64:$PATH
      cd libc && make -f watcom.mk MODEL=l

- an ELKS kernel configured with `CONFIG_EXEC_OS2=y` (the binary is an OS/2
  NE executable, the format ELKS uses for multi-segment programs)

Then:

    make            # build/micropython, about 220 KB
    make HOST=1     # build-host/micropython: same port built with the host gcc

The host build exists to test the port layer and to produce reference output
for comparing against the ELKS build.

## Testing

`tools/qemu-run.sh script.py` boots a copy of an ELKS disk image in QEMU with
MicroPython as init and prints the script's output; see the comments at the
top of that script.

With that runner, the `tests/basics` and `tests/float` suites were run on ELKS
and compared with the host build: 589 of 604 tests give identical output.  Of
the 15 that differ, 9 are expected for a 16-bit target or the feature set
(2-byte `int` in `array` and `struct`, 30-bit small integers, no `open()`),
one is an artifact of the serial console, and 5 are the floating-point issues
listed under "Known limitations".

## How it fits on an 8086

- **Segments.**  The ELKS kernel loads at most `MAX_SEGS` (5) segments per
  program.  The build has three code segments (about 176 KB) and the
  default data segment, which leaves room for one more code segment.  All data
  is kept in the default data segment for that reason (no `-zt`).  `tools/checkmap.py` checks
  the count after linking.
- **Python heap.**  One far block of up to 60,000 bytes from the kernel
  (`fmemalloc`), outside the program's own segments.  The size is chosen
  automatically; `MPYHEAP=bytes` in the environment sets it.  A far pointer
  cannot cross a segment, so one block cannot exceed 64 KB.
- **Object words** are 32-bit (`long`) so they can hold a far pointer.  `int`
  and `size_t` are 16-bit.
- **Even addresses.**  MicroPython tells object pointers from small integers
  by the lowest bit, so every object must be at an even address.  Open Watcom
  only arranges that when optimizing for time (`-ot`), which is why the build
  does not use `-Os`.  `tools/checkmap.py` checks this too.
- **Garbage collection** scans the stack at every 2-byte position, because the
  stack is 2-byte aligned while a far pointer is 4 bytes.

## Enabled features

Compiler and REPL (line editing, history, auto-indent), arbitrary-precision
integers, floats (software double precision), `str` with Unicode, `bytes`,
`bytearray`, `array`, `struct`, `collections`, `sys`, `gc`, `micropython`,
`input()`, importing `.py` files from the script's directory and from
`/lib/micropython`.

Not enabled yet: `open()` and file I/O, `os`, `time`, `math`, `json`, `re`.

## Known limitations

- **NaN handling.**  Open Watcom's soft-float routines do not follow IEEE
  rules for NaN: `inf - inf` and `inf * 0` give `0.0`, and `nan == nan` is
  true.
- **Float parsing** of numbers with very many digits or large exponents can be
  off in the last few digits.
- **16-bit sizes.**  `array('i')`, `array('I')` and the native `struct` codes
  `i`/`I` are 2 bytes.  Lengths above 65,535 are not representable; repeating
  a sequence or growing an allocation past that raises `MemoryError`, but not
  every size computation in the core has been checked for 16-bit overflow.
  A list grows by doubling, so appending stops at 8,192 items.
- **Small integers** are 30-bit; larger values become arbitrary-precision
  integers, which allocate on the heap.
- QEMU runs at host speed.  On a 4.77 MHz 8088 expect everything to be slow,
  floats especially.

## Changes to the core

`py/` carries the Py86 compatibility patch (Open Watcom C syntax workarounds,
2-byte-aligned GC scanning, parse-node alignment, 32-bit constant fixes) plus
four fixes found while testing this port:

- `py/mpprint.c`: `%ld` arguments were read as `int` wherever `long` is wider
  than `int` but the model is not LP64.
- `py/mpz.c`: converting a negative big integer to bytes lost the carry with
  8-bit digits.
- `py/sequence.c`: `mp_seq_multiply_len()` raises `MemoryError` when a
  sequence repeat does not fit in `size_t`, instead of overflowing.
- `py/misc.h`, `py/malloc.c`: the `m_new`/`m_renew` macros check their size
  arithmetic where `size_t` is 16 bits, so an oversized request raises
  `MemoryError` instead of wrapping around to a small allocation.
