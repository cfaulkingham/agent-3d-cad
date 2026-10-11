"""Independent stdlib PNG decode of the native encoder; no product runtime."""
import binascii
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

with tempfile.TemporaryDirectory(prefix="cad-png-oracle-") as folder:
    path = pathlib.Path(folder) / "native.png"
    subprocess.run([sys.argv[1], str(path)], check=True)
    data = path.read_bytes()
assert data[:8] == b"\x89PNG\r\n\x1a\n"
cursor = 8
compressed = b""
while cursor < len(data):
    size = struct.unpack_from(">I", data, cursor)[0]
    kind = data[cursor+4:cursor+8]
    body = data[cursor+8:cursor+8+size]
    assert binascii.crc32(kind+body) == struct.unpack_from(">I", data, cursor+8+size)[0]
    if kind == b"IHDR":
        assert struct.unpack(">IIBBBBB", body) == (128, 97, 8, 6, 0, 0, 0)
    if kind == b"IDAT":
        compressed += body
    cursor += 12 + size
assert cursor == len(data) and kind == b"IEND"
raw = zlib.decompress(compressed)
expected = b"".join(b"\0" + bytes((x*17+y*31+c*53)&255 for x in range(128) for c in range(4)) for y in range(97))
assert raw == expected
print("Independent PNG oracle: all 49,664 native pixel channels and CRC/zlib pass")
