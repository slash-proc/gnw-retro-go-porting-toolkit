#!/usr/bin/env python3
"""List the firmware's LittleFS partition from a raw device dump.

The partition is stored "inverted" (block N at partition_end-(N+1)*block_size),
so we reverse the 4096-byte blocks before handing it to littlefs-python. Usage:
    python3 scripts/debug/lfs_list.py <dump.bin>
"""
import sys
from littlefs import LittleFS

BLOCK_SIZE = 4096
BLOCK_COUNT = 2560  # 10 MB partition

data = open(sys.argv[1], "rb").read()
full = BLOCK_SIZE * BLOCK_COUNT
if len(data) < full:
    data = data + b"\xff" * (full - len(data))
data = data[:full]

blocks = [data[i:i+BLOCK_SIZE] for i in range(0, full, BLOCK_SIZE)]
corrected = b"".join(blocks[::-1])

fs = LittleFS(block_size=BLOCK_SIZE, block_count=BLOCK_COUNT)
fs.context.buffer = bytearray(corrected)
fs.mount()
print("mounted OK")

def walk(path):
    for e in fs.scandir(path):
        if e.name in (".", ".."):
            continue
        fp = (path.rstrip("/") + "/" + e.name)
        is_dir = (e.type == 2)
        print(f"  {fp}{'/' if is_dir else ''}  size={e.size}")
        if is_dir:
            walk(fp)

walk("/")
