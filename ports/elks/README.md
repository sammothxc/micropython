# MicroPython for ELKS (8086/8088)

An experimental port of MicroPython to [ELKS](https://github.com/ghaerr/elks),
the Linux-like OS for 8086-class PCs.  It is based on MicroPython v1.24.1 and
on the 16-bit core changes of [Py86](https://github.com/ARMoir/micropython/tree/tandypy-dos16-port),
the MS-DOS port by ARMoir.

    micropython              interactive REPL (Ctrl-D exits)
    micropython script.py    run a file
    micropython script.mpy   run a file precompiled with mpy-cross
    micropython -c "code"    run a string

Status: runs on a Compaq Portable (4.77 MHz 8088) and under QEMU.

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

    make            # build/micropython, about 225 KB
    make HOST=1     # build-host/micropython: same port built with the host gcc

The host build exists to test the port layer and to produce reference output
for comparing against the ELKS build.

## Testing

All of these boot a copy of an ELKS disk image in QEMU (default
`~/elks-qemu/hd32-fat.img`, or set `ELKS_IMG`):

- `tools/qemu-run.sh script.py` runs one script, prints its output and exits
  with its exit status.  With `ICOUNT=1` QEMU's clock counts instructions, so
  timings are reproducible; code then takes roughly 30 to 35 times as long on
  a 4.77 MHz 8088.
- `tools/run-tests.py` runs `tests/basics` and `tests/float` (or the test
  files given) on ELKS and with the host build, and compares the outputs.
  589 of 604 tests give identical output.  The 15 that differ are listed in
  the script with the reason: 2-byte `int` in `array` and `struct`, 30-bit
  small integers, a feature the port lacks, a `\r` the serial console cannot
  show, and the floating-point issues under "Known limitations".
- `tools/bench.py` times common operations:
  `ICOUNT=1 tools/qemu-run.sh tools/bench.py`.

QEMU is run with `-d nochain`, which works around a QEMU bug (seen in 10.0):
with chained translation blocks, a near call whose 16-bit target wraps past
the end of a code segment is not wrapped, so a build can crash in QEMU and
still run correctly on a real 8086.  See `tools/qemu-run.sh`.

## How it fits on an 8086

- **Segments.**  The ELKS kernel loads at most `MAX_SEGS` (5) segments per
  program.  The build has three code segments (about 182 KB) and the
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
`input()`, `time` (`time()`, `sleep()`, `sleep_ms()`, `sleep_us()` and the
`ticks_*()` functions, with the kernel's 10 ms resolution), importing `.py` and
`.mpy` files from the script's directory and from `/lib/micropython`.

Not enabled yet: `open()` and file I/O, `os`, `math`, `json`, `re`.

## Speed

Compiling Python source is slow on an 8088: a 220-line module takes about
52 seconds to import from `.py` on a 4.77 MHz machine, and 1.1 seconds from
`.mpy`.  Precompile modules (and the main script, if it is large) on another
machine with `mpy-cross` from this tree:

    make -C ../../mpy-cross
    ../../mpy-cross/build/mpy-cross -msmall-int-bits=30 module.py

`-msmall-int-bits=30` is required; a `.mpy` built for another integer size is
rejected with "incompatible .mpy file".  When both `module.py` and
`module.mpy` exist, `import module` uses the `.py`.

The port enables these speed options (see `mpconfigport.h`), which matter more
on an 8088 than on other targets:

- `MICROPY_OPT_MAP_LOOKUP_CACHE`: remembers where names were last found.
- `MICROPY_OPT_VM_SMALL_INT_FAST_PATH`: comparisons, `+`, `-`, `%`, `//`,
  `&` and `|` on small integers are done in the VM without a call.
- `MICROPY_OPT_VM_SELECTIVE_EXC_IP`: the VM saves its bytecode position only
  in opcodes that can raise an exception.
- `MICROPY_OPT_VM_SWITCH_MULTI_OPCODES`: every opcode value has its own case
  in the VM's switch, with its operand built in.
- `MICROPY_READER_POSIX_BUF_SIZE` 512: source files are read a block at a time.

On the Compaq, a build with these options ran the loops of a small benchmark
1.4 to 2.5 times faster than one without them, and a Python function call
took 4.2 ms, down from 35 ms before the call-state fix below.

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
- **Recursion** is limited to about 50 levels of Python calls by the 16 KB C
  stack.

## Changes to the core

`py/` carries the Py86 compatibility patch (Open Watcom C syntax workarounds,
2-byte-aligned GC scanning, parse-node alignment, 32-bit constant fixes) plus
these fixes found while testing this port:

- `py/parse.c`: a parse node with 256 or more children (a module or function
  body of 256+ statements, a long list or dict literal) lost all but the
  count modulo 256 where `size_t` is 16 bits, so the rest of the code was
  silently dropped.  The parser's rule stack also kept only 8 bits of the
  source line on such targets.
- `py/objfun.c`, `py/objboundmeth.c`: with `MICROPY_NO_ALLOCA` every Python
  function call took its state (and every bound-method call its argument
  array) from the heap and left it for the garbage collector.  They now use a
  fixed buffer on the C stack when the state is small.
- `py/mpprint.c`: `%ld` arguments were read as `int` wherever `long` is wider
  than `int` but the model is not LP64.
- `py/mpz.c`: converting a negative big integer to bytes lost the carry with
  8-bit digits.
- `py/sequence.c`: `mp_seq_multiply_len()` raises `MemoryError` when a
  sequence repeat does not fit in `size_t`, instead of overflowing.
- `py/misc.h`, `py/malloc.c`: the `m_new`/`m_renew` macros check their size
  arithmetic where `size_t` is 16 bits, so an oversized request raises
  `MemoryError` instead of wrapping around to a small allocation.

and the speed options listed under "Speed", which are off by default:
`MICROPY_OPT_VM_SMALL_INT_FAST_PATH`, `MICROPY_OPT_VM_SELECTIVE_EXC_IP` and
`MICROPY_OPT_VM_SWITCH_MULTI_OPCODES` (`py/vm.c`, `py/mpconfig.h`) and
`MICROPY_READER_POSIX_BUF_SIZE` (`py/reader.c`).  Also changed for speed:
`py/map.c` reduces hashes to `size_t` before the modulo and uses the lookup
cache without a division, `py/smallint.c` divides with 16-bit instructions
when both operands fit, and the VM decodes bytecode operands as `size_t`.
