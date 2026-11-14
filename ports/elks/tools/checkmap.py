#!/usr/bin/env python3
"""Check the linker map of the ELKS build for two problems that otherwise
only show up as crashes on the target.

1. Object alignment.  MicroPython stores a pointer to an object and a small
   integer in the same machine word and tells them apart by the lowest bit, so
   every object must sit at an even address.  Open Watcom only guarantees that
   when it optimizes for time (-ot); with -Os data is packed and some objects
   land on odd addresses.

2. Segment count.  The ELKS kernel loads at most MAX_SEGS code and data
   segments per program (5 by default, see elks/include/linuxmt/limits.h).

usage: checkmap.py file.map [max-segments]
"""

import re
import sys


# Byte-sized data that is never used as a MicroPython object, so its address
# may be odd: flags of the soft-float library, and the tables of qstr hashes
# and lengths (one byte per entry, see MICROPY_QSTR_BYTES_IN_HASH/LEN).
IGNORE = {
    "__8087", "__real87", "__chipbug",
    "_mp_qstr_const_hashes", "_mp_qstr_const_lengths",
    "_mp_qstr_const_hashes_static", "_mp_qstr_const_lengths_static",
}


def main():
    map_file = sys.argv[1]
    max_segs = int(sys.argv[2]) if len(sys.argv) > 2 else 5

    code_segs = set()
    all_segs = set()
    odd = []
    module_is_ours = False
    section = None
    for line in open(map_file, errors="replace"):
        line = line.rstrip()
        if line.startswith("Segment ") and "Class" in line:
            section = "segments"
            continue
        if line.startswith("Address ") and "Symbol" in line:
            section = "symbols"
            continue
        if section == "segments":
            m = re.match(r"(\S+)\s+(\S+)\s+(\S+)\s+([0-9a-f]{4}):([0-9a-f]{4})\s+[0-9a-f]{8}$", line)
            if m:
                seg = int(m.group(4), 16)
                all_segs.add(seg)
                if m.group(2) == "CODE":
                    code_segs.add(seg)
        elif section == "symbols":
            if line.startswith("Module: "):
                # Only MicroPython's own objects matter, not C library data.
                module_is_ours = ".lib(" not in line
                continue
            m = re.match(r"([0-9a-f]{4}):([0-9a-f]{4})[+* ]\s+(\S+)$", line)
            if m and module_is_ours:
                seg, off, name = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
                if seg not in code_segs and off % 2 and name not in IGNORE:
                    odd.append((m.group(1) + ":" + m.group(2), name))

    ok = True
    if len(all_segs) > max_segs:
        print("checkmap: %d segments, but ELKS loads at most %d (MAX_SEGS)" % (len(all_segs), max_segs))
        ok = False
    if odd:
        print("checkmap: %d data symbols at odd addresses, for example:" % len(odd))
        for addr, name in odd[:10]:
            print("  %s %s" % (addr, name))
        ok = False
    if not ok:
        sys.exit(1)
    print("checkmap: %d segments, all data symbols at even addresses" % len(all_segs))


main()
