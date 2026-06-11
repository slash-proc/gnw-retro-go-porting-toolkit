#!/usr/bin/env python3
"""Statistical PC-sampling profiler over SWD (halt -> read pc -> resume).

Usage: profile.py [samples] <binary.out>
Defaults: 200 samples; pass the app/firmware ELF the device is running.

Tallies samples per function via addr2line. ~25ms/sample including the
halt/resume round-trip, so 200 samples ~= 5-8 seconds of wall time.
The halts themselves steal ~1% CPU from the target; fine for a profile.
"""
import struct, subprocess, sys, time, collections
from gnwmanager.ocdbackend.openocd_backend import OpenOCDBackend

N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
if len(sys.argv) < 3:
    sys.exit("usage: profile.py [samples] <binary.out>")
BIN = sys.argv[2]

b = OpenOCDBackend(); b.open()
pcs = []
try:
    for i in range(N):
        b.halt()
        try:
            pcs.append(b.read_register("pc"))
        finally:
            b.resume()
        time.sleep(0.01)
finally:
    b.close()

# batch addr2line
out = subprocess.check_output(
    ["arm-none-eabi-addr2line", "-e", BIN, "-f", "-C"] + [hex(pc) for pc in pcs]
).decode().splitlines()
funcs = [out[i * 2] for i in range(len(pcs))]
locs = [out[i * 2 + 1] for i in range(len(pcs))]

tally = collections.Counter(funcs)
print(f"{len(pcs)} samples:")
for fn, n in tally.most_common(25):
    print(f"  {100*n/len(pcs):5.1f}%  {n:4d}  {fn}")

# top exact lines within the top function
top = tally.most_common(1)[0][0]
linec = collections.Counter(l for f, l in zip(funcs, locs) if f == top)
print(f"\nhot lines in {top}:")
for l, n in linec.most_common(8):
    print(f"  {n:4d}  {l}")
