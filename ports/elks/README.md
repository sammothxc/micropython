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

    make            # build/micropython, about 270 KB
    make HOST=1     # build-host/micropython: same port built with the host gcc

The host build exists to test the port layer and to produce reference output
for comparing against the ELKS build.

## Testing

All of these boot a copy of an ELKS disk image in QEMU (default
`~/elks-qemu/hd32-fat.img`, or set `ELKS_IMG`):

- `tools/qemu-run.sh script.py` runs one script in `/root`, prints its output
  and exits with its exit status.  With `ICOUNT=1` QEMU's clock counts
  instructions, so timings are reproducible; code then takes roughly 30 to 35
  times as long on a 4.77 MHz 8088.  `MPYHEAP` is passed on to micropython.
- `tools/run-tests.py` runs `tests/basics`, `tests/float`, `tests/io` and
  `tests/extmod` (or the test files given) on ELKS and with the host build,
  and compares the outputs.  780 of 798 tests give identical output.  The 18
  that differ are listed in the script with the reason: 2-byte `int` in
  `array` and `struct`, 30-bit small integers, a feature the port lacks, a
  test that prints its own path, a `\r` the serial console cannot show, and
  the floating-point issues under "Known limitations".
- `tools/bench.py` times common operations:
  `ICOUNT=1 tools/qemu-run.sh tools/bench.py`.

QEMU is run with `-d nochain`, which works around a QEMU bug (seen in 10.0):
with chained translation blocks, a near call whose 16-bit target wraps past
the end of a code segment is not wrapped, so a build can crash in QEMU and
still run correctly on a real 8086.  See `tools/qemu-run.sh`.

## How it fits on an 8086

- **Segments.**  The ELKS kernel loads at most `MAX_SEGS` (5) segments per
  program.  The build has four code segments (about 207 KB; the fourth is a
  third full, so about 40 KB of code can still be added) and the default
  data segment (47 KB of 64 KB used).  All data is kept in the default data
  segment, since another segment is not possible (no `-zt`).  `tools/checkmap.py` checks
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
`bytearray`, `array`, `struct`, `collections`, `sys` (with `stdin`, `stdout`,
`stderr`), `gc`, `micropython`, `input()`, `open()` and file objects, `io`
(`StringIO`, `BytesIO`), `os`, `errno`, `math`, `json`, `re`, `random`,
`time` (`time()`, `sleep*()`, `ticks_*()` with the kernel's 10 ms
resolution, and `gmtime()`, `localtime()`, `mktime()` in UTC, from 1970),
importing `.py` and `.mpy` files from the script's directory and from
`/lib/micropython`.

The terminal is in its normal mode while Python code runs (so
`sys.stdin.readline()` and programs started by `os.system()` behave as
usual) and in raw mode only while the REPL or `input()` reads a line.

### Files and `os`

`os` has `listdir`, `ilistdir`, `stat`, `mkdir`, `rmdir`, `remove`,
`rename`, `chdir`, `getcwd`, `getenv`, `putenv`, `unsetenv`, `system` and
`errno`.  ELKS specifics:

- `rename()` of a file on a FAT disk copies it and removes the original,
  as ELKS's `mv` does, because the kernel cannot rename on FAT.  Directories
  cannot be renamed on FAT.
- `ilistdir()` gives 0 as the entry type (ELKS does not report it).
- `file.flush()` writes all buffered disk blocks (`sync`).
- `os.system()` needs free memory for `sh` and the command.  With the
  default heap a 480 KB machine can run shell builtins but not external
  commands; set `MPYHEAP` (for example `MPYHEAP=20000`) to leave room.

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
  rules for NaN: `inf - inf` and `inf * 0` give `0.0`, `nan == nan` is
  true, and negative zero is not produced.  The `math` functions handle
  infinity, NaN and domain errors themselves (`elks_libm.c`), but
  arithmetic on such values, as in `math.radians(inf)`, is still wrong.
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
- `py/gc.c`: with `MICROPY_ENABLE_FINALISER`, the size of the allocation
  table was computed as heap size times 8 in `size_t`, which overflowed for
  heaps over 8 KB where `size_t` is 16 bits (a 60 KB heap ended up 2.5 KB).
- `shared/timeutils/timeutils.c`: the days-per-100- and 400-year constants
  overflowed a 16-bit `int`, so dates came out wrong (`gmtime(0)` was 1611).
- `py/reader.c`: the POSIX file reader is left out only when the VFS reader
  replaces it (`MICROPY_READER_VFS`), not whenever the POSIX VFS is enabled,
  so this port keeps its 512-byte reads.
- `py/mpconfig.h`: a port can define `MICROPY_FLOAT_C_FUN` to route the C
  math functions through its own wrappers (here `elks_libm.c`).
- `extmod/modjson.c`, `extmod/modre.c`, `lib/re1.5/re1.5.h`: Open Watcom
  syntax (an initializer with the address of a local, zero-length arrays).

and the speed options listed under "Speed", which are off by default:
`MICROPY_OPT_VM_SMALL_INT_FAST_PATH`, `MICROPY_OPT_VM_SELECTIVE_EXC_IP` and
`MICROPY_OPT_VM_SWITCH_MULTI_OPCODES` (`py/vm.c`, `py/mpconfig.h`) and
`MICROPY_READER_POSIX_BUF_SIZE` (`py/reader.c`).  Also changed for speed:
`py/map.c` reduces hashes to `size_t` before the modulo and uses the lookup
cache without a division, `py/smallint.c` divides with 16-bit instructions
when both operands fit, and the VM decodes bytecode operands as `size_t`.

The port's own workarounds for the ELKS C library are in `elks_compat.c`
(`stat`, `rename`, `system`, `fsync`) and `elks_libm.c` (math functions),
with the reasons in their comments.

## TODO

Open items, roughly in order of importance.

### Before v1.0

- Test the file, `os` and terminal changes on real hardware (they have only
  run under QEMU): the REPL and `input()` over the console and over telnet,
  `sys.stdin.readline()`, Ctrl-C, file I/O on a MINIX and a FAT disk,
  `os.rename()`, and `os.system()` with external commands.
- Find out how much memory `os.system()` leaves for external commands on a
  640 KB machine with the default heap.  If too little, consider a smaller
  default heap, or freeing memory before the shell runs.
- Check that the ept package installs a build with these features as
  `/bin/upython`.

### Known gaps

- `os.rename()` of a directory fails on FAT (EPERM); only files are copied.
- `os.ilistdir()` reports entry type 0; ELKS's `readdir()` has no `d_type`.
  A `stat()` per entry would fix it but slow down `listdir()`.
- `os.statvfs()`, `os.uname()`, `os.urandom()`, `os.dupterm()` are not
  provided.
- `time.localtime()` is UTC; ELKS keeps no time zone.  Times past 2038 do not
  fit `mp_int_t` (as on other 32-bit ports).
- Module-level `re.split()` does not exist (as in upstream MicroPython; use
  `re.compile(...).split()`).
- Not enabled: `select`, `socket` (ELKS has TCP/IP), `binascii`, `heapq`,
  `hashlib`, `asyncio`, complex numbers (`cmath`), the special math
  functions (`math.log2`, `sinh`, `gamma`, ...).  About 40 KB of code space
  and 16 KB of data space remain, and no more segments can be added.

### Floating point

- Open Watcom's soft-float arithmetic and comparisons do not follow IEEE for
  infinity, NaN and negative zero (see "Known limitations").  Fixing it
  means replacing the soft-float helper routines, which costs code space
  and speed.
- Float parsing loses precision in the last digits for long inputs.

### Size and speed

- More segments are not possible (`MAX_SEGS` is 5).  To make room, look for
  unused core features to disable, or move constant tables out of the data
  segment.
- The recursion limit is about 50 Python calls (16 KB C stack).  Stackless
  calls raised it to about 140 but made calls 11% slower and were dropped;
  a cheaper approach would help recursive programs.
- Compiling `.py` files is slow on an 8088; `.mpy` files are about 50 times
  faster to load.  Frozen modules or caching compiled `.mpy` files next to
  their sources could help.

### Tooling

- `tools/run-tests.py` copies only the test file and its `data/`
  directory, so tests that import modules from their own directory (for
  example `tests/import`) fail on ELKS.
- The QEMU image starts the clock at 1970, because it skips the boot
  scripts that set it.
