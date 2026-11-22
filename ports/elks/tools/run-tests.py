#!/usr/bin/env python3
"""Run MicroPython tests on ELKS under QEMU and compare with the host build.

    tools/run-tests.py [-j JOBS] [-t SECONDS] [test.py ...]

Without test files it runs the tests in tests/basics, tests/float, tests/io
and tests/extmod (tests of modules the port does not have print SKIP on both
systems, so they count as matching).  Each test runs on ELKS (build/micropython, in a copy of an ELKS disk image booted in
QEMU) and with the host build of this port (build-host/micropython, made with
"make HOST=1"), and the two outputs are compared.  Results go to build-tests/:
DIR/NAME.elks and DIR/NAME.host for each test, and results.txt with one
"ok DIR/NAME" or "DIFF DIR/NAME" line per test.  Tests listed in EXPECTED_DIFF
are known to differ; the exit status is 1 if any other test differs.

The disk image boots /bin/sh as init with a script that runs the test and then
prints an end marker with the exit status, so QEMU is stopped as soon as the
test finishes.  The test file is copied to /root on the image, together with
the data/ directory next to it if there is one, and runs with /root as the
current directory (the host build runs it in its own directory).  Tests that
import other files from their directory fail on ELKS.

Environment: ELKS_IMG (default ~/elks-qemu/hd32-fat.img; its kernel must be
built with CONFIG_EXEC_OS2), MPY and HOST_MPY to test other binaries.
"""

import argparse
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

PORT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOP = os.path.dirname(os.path.dirname(PORT_DIR))
END = b"##END##"

# Tests whose ELKS output is known to differ from the host build's.
EXPECTED_DIFF = {
    # 16-bit int: array('i') and the native struct codes i/I are 2 bytes.
    "basics/array_construct",
    "basics/bytearray_construct_endian",
    "basics/bytes_add_endian",
    "basics/bytes_construct_endian",
    "basics/struct1",
    "basics/struct2",
    "float/float_array",
    # Small integers are 30-bit.
    "basics/int_big_to_small",
    # Feature set: the test needs something the port does not have.
    "basics/sys_path",
    # The test prints its own file name, which is /root/t.py on ELKS.
    "io/argv",
    # The test prints a raw '\r', which the serial console cannot pass through.
    "basics/string_upperlow",
    "extmod/json_loads",
    # Open Watcom's soft-float library: NaN and infinity arithmetic and
    # comparisons, negative zero, and the last digits of long decimal numbers.
    # (The math functions themselves handle special values, see elks_libm.c,
    # but math.degrees(), math.radians() and math.log(x, base) do arithmetic
    # on their results.)
    "float/float1",
    "float/float_parse",
    "float/float_parse_doubleprec",
    "float/inf_nan_arith",
    "float/math_domain",
    "float/math_fun",
}

running = set()
running_lock = threading.Lock()


def mcopy(img, src, dst):
    subprocess.run(["mcopy", "-o", "-i", img, src, dst], check=True)


def make_template(tmp, elks_img, mpy):
    tpl = os.path.join(tmp, "template.img")
    shutil.copyfile(elks_img, tpl)
    mcopy(tpl, mpy, "::/bin/micropython")
    run_sh = os.path.join(tmp, "run.sh")
    with open(run_sh, "w") as f:
        f.write('cd /root\n/bin/micropython /root/t.py\necho "%s $?"\n' % END.decode())
    mcopy(tpl, run_sh, "::/root/run.sh")
    # /bootopts must start with "##".  A line without "=" is passed to init as
    # an argument.  The file is padded because the kernel reads a fixed block.
    bootopts = os.path.join(tmp, "bootopts")
    with open(bootopts, "w") as f:
        f.write("## bootopts\nconsole=ttyS0,19200\nhma=kernel\ninit=/bin/sh\n/root/run.sh\n")
        f.write("#############\n" * 60)
    mcopy(tpl, bootopts, "::/bootopts")
    return tpl


def normalize(out):
    # Tracebacks name the file differently on the two systems.
    return re.sub(rb'File "[^"]*"', b'File "X"', out)


def run_elks(test, tpl, tmp, timeout):
    work = tempfile.mkdtemp(dir=tmp)
    try:
        img = os.path.join(work, "hd.img")
        shutil.copyfile(tpl, img)
        mcopy(img, test, "::/root/t.py")
        data_dir = os.path.join(os.path.dirname(test), "data")
        if os.path.isdir(data_dir):
            subprocess.run(["mcopy", "-s", "-o", "-i", img, data_dir, "::/root/"], check=True)
        out = os.path.join(work, "out")
        # "-d nochain" works around a QEMU bug with near jumps that wrap around
        # the end of a 16-bit code segment; see tools/qemu-run.sh.
        cmd = ["qemu-system-i386", "-M", "isapc", "-cpu", "486", "-m", "1M",
               "-drive", "file=%s,format=raw,if=ide" % img, "-boot", "c",
               "-d", "nochain", "-display", "none", "-monitor", "none",
               "-serial", "file:" + out]
        p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with running_lock:
            running.add(p)
        data = b""
        deadline = time.time() + timeout
        try:
            while time.time() < deadline:
                time.sleep(0.1)
                try:
                    with open(out, "rb") as f:
                        data = f.read()
                except FileNotFoundError:
                    continue
                if END in data:
                    time.sleep(0.1)  # let the exit status arrive
                    with open(out, "rb") as f:
                        data = f.read()
                    break
        finally:
            p.kill()
            p.wait()
            with running_lock:
                running.discard(p)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    data = data.replace(b"\r", b"")
    mark = data.find(b"VFS: Mounted root device")
    if mark >= 0:
        data = data[data.find(b"\n", mark) + 1:]
    i = data.find(END)
    if i < 0:
        return data + b"[timed out after %d s]\n" % timeout, None
    status = data[i + len(END):].split(b"\n")[0].strip()
    return data[:i], status.decode(errors="replace")


def run_host(test, host_mpy, timeout):
    try:
        r = subprocess.run([host_mpy, os.path.basename(test)], cwd=os.path.dirname(test),
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        return r.stdout
    except subprocess.TimeoutExpired as e:
        return (e.stdout or b"") + b"[timed out after %d s]\n" % timeout


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("-t", "--timeout", type=int, default=60, help="seconds per test")
    ap.add_argument("-o", "--out", default=os.path.join(PORT_DIR, "build-tests"))
    ap.add_argument("tests", nargs="*")
    args = ap.parse_args()

    tests = args.tests or [t for d in ("basics", "float", "io", "extmod")
                           for t in sorted(glob.glob(os.path.join(TOP, "tests", d, "*.py")))]
    tests = [os.path.abspath(t) for t in tests]
    elks_img = os.environ.get("ELKS_IMG", os.path.expanduser("~/elks-qemu/hd32-fat.img"))
    mpy = os.environ.get("MPY", os.path.join(PORT_DIR, "build/micropython"))
    host_mpy = os.environ.get("HOST_MPY", os.path.join(PORT_DIR, "build-host/micropython"))
    for f in (elks_img, mpy, host_mpy):
        if not os.path.exists(f):
            sys.exit("missing %s" % f)
    os.makedirs(args.out, exist_ok=True)

    def name(test):
        return os.path.join(os.path.basename(os.path.dirname(test)),
                            os.path.splitext(os.path.basename(test))[0])

    results = {}
    with tempfile.TemporaryDirectory() as tmp:
        tpl = make_template(tmp, elks_img, mpy)

        def one(test):
            n = name(test)
            elks, status = run_elks(test, tpl, tmp, args.timeout)
            elks = normalize(elks)
            host = normalize(run_host(test, host_mpy, args.timeout))
            os.makedirs(os.path.join(args.out, os.path.dirname(n)), exist_ok=True)
            with open(os.path.join(args.out, n + ".elks"), "wb") as f:
                f.write(elks)
            with open(os.path.join(args.out, n + ".host"), "wb") as f:
                f.write(host)
            return n, elks == host

        try:
            with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
                for n, ok in ex.map(one, tests):
                    results[n] = ok
                    print("%s %s" % ("ok" if ok else "DIFF", n), flush=True)
        finally:
            with running_lock:
                for p in running:
                    p.kill()

    with open(os.path.join(args.out, "results.txt"), "w") as f:
        for n in sorted(results):
            f.write("%s %s\n" % ("ok" if results[n] else "DIFF", n))
    failed = sorted(n for n, ok in results.items() if not ok)
    print("%d of %d tests give the same output on ELKS and the host" % (len(results) - len(failed), len(results)))
    unexpected = [n for n in failed if n not in EXPECTED_DIFF]
    fixed = sorted(n for n, ok in results.items() if ok and n in EXPECTED_DIFF)
    if failed:
        print("%d differ as expected" % (len(failed) - len(unexpected)))
    if unexpected:
        print("UNEXPECTED: " + " ".join(unexpected))
    if fixed:
        print("now match (remove from EXPECTED_DIFF): " + " ".join(fixed))
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
