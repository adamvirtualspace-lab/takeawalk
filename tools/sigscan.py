"""Search an executable on disk for byte signatures ("48 8B 05 ?? ?? ?? ??").

Prints file offset and RVA for each match (first 10 per pattern), so a signature can
be checked against a new game build without running the game.

    python tools/sigscan.py <exe> "<pattern>" ["<pattern>" ...]
"""
import re
import struct
import sys


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt_size = struct.unpack_from('<H', data, pe + 20)[0]
    table = pe + 24 + opt_size
    for i in range(count):
        name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', data, table + i * 40)
        yield name.rstrip(b'\0').decode(), va, vsize, raw, rsize


def to_regex(pattern):
    parts = []
    for tok in pattern.split():
        parts.append(b'.' if tok in ('?', '??') else re.escape(bytes([int(tok, 16)])))
    return re.compile(b''.join(parts), re.DOTALL)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[1], 'rb') as f:
        data = f.read()
    secs = list(sections(data))

    def rva(off):
        for name, va, vsize, raw, rsize in secs:
            if raw <= off < raw + rsize:
                return name, va + off - raw
        return '?', 0

    for pattern in sys.argv[2:]:
        hits = [m.start() for m in to_regex(pattern).finditer(data)]
        print(f'{pattern}\n  {len(hits)} match(es)')
        for off in hits[:10]:
            name, addr = rva(off)
            print(f'  file 0x{off:X}  {name} rva 0x{addr:X}  bytes {data[off:off + 24].hex(" ")}')


if __name__ == '__main__':
    main()
