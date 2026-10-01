"""Pull text files out of an .scs archive by keyword, without a real extractor.

Scans the archive for zlib streams, inflates each one, and writes those containing
any of the given keywords to OUTDIR (named by byte offset, since the archive only
stores hashed paths). Stopgap for small archives like def.scs; use a proper
extractor for anything else.

    python tools/scs_scan.py <archive.scs> <outdir> <keyword> [keyword ...]
"""
import os
import re
import sys
import zlib

MAX_STREAM = 4_000_000


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, outdir = sys.argv[1], sys.argv[2]
    keywords = [k.encode() for k in sys.argv[3:]]
    os.makedirs(outdir, exist_ok=True)

    with open(src, 'rb') as f:
        data = f.read()

    streams = hits = 0
    for m in re.finditer(rb'\x78[\x01\x5e\x9c\xda]', data):
        off = m.start()
        try:
            d = zlib.decompressobj()
            out = d.decompress(data[off:off + MAX_STREAM])
        except zlib.error:
            continue
        if not d.eof or len(out) < 16:
            continue
        streams += 1
        if any(k in out for k in keywords):
            hits += 1
            with open(os.path.join(outdir, f'{off:010d}.txt'), 'wb') as f:
                f.write(out)
    print(f'{streams} streams, {hits} hits -> {outdir}')


if __name__ == '__main__':
    main()
