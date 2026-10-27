"""Packs PNG files into one Windows .ico (PNG entries, which Windows Vista and newer read).

Usage: python3 tools/make_ico.py OUT.ico IN_16.png IN_32.png ...
The PNGs come from `Mapsmith --write-icon DIR`; the result is src/app/resources/mapsmith.ico.
"""

import struct
import sys
from pathlib import Path


def png_size(data: bytes) -> tuple[int, int]:
    """Width and height from a PNG's IHDR chunk."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    return struct.unpack(">II", data[16:24])


def main() -> None:
    out, inputs = Path(sys.argv[1]), [Path(p) for p in sys.argv[2:]]
    images = [path.read_bytes() for path in inputs]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for data in images:
        width, height = png_size(data)
        # 256 is written as 0 in the one-byte size fields.
        entries += struct.pack("<BBBBHHII", width % 256, height % 256, 0, 0, 1, 32, len(data), offset)
        blobs += data
        offset += len(data)
    out.write_bytes(header + entries + blobs)


if __name__ == "__main__":
    main()
