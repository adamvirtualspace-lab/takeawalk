"""Read files out of the game's .scs archives (HashFS version 2) by path.

    python tools/hashfs.py ls <game dir or archive> <directory>
    python tools/hashfs.py get <game dir or archive> <path> <output file>
    python tools/hashfs.py tree <game dir or archive> <directory> [depth]

With a game directory every .scs in it is searched, so a path is found whichever
archive holds it. Paths are written as the game does: /vehicle/driver/...
Only plain files and directories are handled (no textures), with zlib or no compression.
"""
import glob
import os
import struct
import sys
import zlib

M = 0xFFFFFFFFFFFFFFFF
K0, K1, K2, K3 = 0xc3a5c85c97cb3127, 0xb492b66fbe98f273, 0x9ae16a3b2f90404f, 0xc949d7c7509e6557


def _f64(s, i):
    return struct.unpack_from('<Q', s, i)[0]


def _f32(s, i):
    return struct.unpack_from('<I', s, i)[0]


def _rot(v, s):
    return ((v >> s) | (v << (64 - s))) & M if s else v


def _mix(v):
    return v ^ (v >> 47)


def _h16(u, v):
    mul = 0x9ddfea08eb382d69
    a = ((u ^ v) * mul) & M
    a ^= a >> 47
    b = ((v ^ a) * mul) & M
    b ^= b >> 47
    return (b * mul) & M


def _weak(s, i, a, b):
    w, x, y, z = _f64(s, i), _f64(s, i + 8), _f64(s, i + 16), _f64(s, i + 24)
    a = (a + w) & M
    b = _rot((b + a + z) & M, 21)
    c = a
    a = (a + x + y) & M
    b = (b + _rot(a, 44)) & M
    return (a + z) & M, (b + c) & M


def city64(s):
    """CityHash64 v1.0.3, which the archives use for path hashes."""
    n = len(s)
    if n <= 16:
        if n > 8:
            a, b = _f64(s, 0), _f64(s, n - 8)
            return _h16(a, _rot((b + n) & M, n)) ^ b
        if n >= 4:
            return _h16((n + (_f32(s, 0) << 3)) & M, _f32(s, n - 4))
        if n > 0:
            y = (s[0] + (s[n >> 1] << 8)) & 0xFFFFFFFF
            z = (n + (s[n - 1] << 2)) & 0xFFFFFFFF
            return (_mix((y * K2 ^ z * K3) & M) * K2) & M
        return K2
    if n <= 32:
        a = (_f64(s, 0) * K1) & M
        b = _f64(s, 8)
        c = (_f64(s, n - 8) * K2) & M
        d = (_f64(s, n - 16) * K0) & M
        return _h16((_rot((a - b) & M, 43) + _rot(c, 30) + d) & M, (a + _rot(b ^ K3, 20) - c + n) & M)
    if n <= 64:
        z = _f64(s, 24)
        a = (_f64(s, 0) + (n + _f64(s, n - 16)) * K0) & M
        b = _rot((a + z) & M, 52)
        c = _rot(a, 37)
        a = (a + _f64(s, 8)) & M
        c = (c + _rot(a, 7)) & M
        a = (a + _f64(s, 16)) & M
        vf = (a + z) & M
        vs = (b + _rot(a, 31) + c) & M
        a = (_f64(s, 16) + _f64(s, n - 32)) & M
        z = _f64(s, n - 8)
        b = _rot((a + z) & M, 52)
        c = _rot(a, 37)
        a = (a + _f64(s, n - 24)) & M
        c = (c + _rot(a, 7)) & M
        a = (a + _f64(s, n - 16)) & M
        wf = (a + z) & M
        ws = (b + _rot(a, 31) + c) & M
        r = _mix(((vf + ws) * K2 + (wf + vs) * K0) & M)
        return (_mix((r * K0 + vs) & M) * K2) & M
    # Longer strings: the end is hashed first, then 64 bytes at a time.
    x = _f64(s, n - 40)
    y = (_f64(s, n - 16) + _f64(s, n - 56)) & M
    z = _h16((_f64(s, n - 48) + n) & M, _f64(s, n - 24))
    v = _weak(s, n - 64, n, z)
    w = _weak(s, n - 32, (y + K1) & M, x)
    x = (x * K1 + _f64(s, 0)) & M
    left = (n - 1) & ~63
    pos = 0
    while left:
        x = (_rot((x + y + v[0] + _f64(s, pos + 8)) & M, 37) * K1) & M
        y = (_rot((y + v[1] + _f64(s, pos + 48)) & M, 42) * K1) & M
        x ^= w[1]
        y = (y + v[0] + _f64(s, pos + 40)) & M
        z = (_rot((z + w[0]) & M, 33) * K1) & M
        v = _weak(s, pos, (v[1] * K1) & M, (x + w[0]) & M)
        w = _weak(s, pos + 32, (z + w[1]) & M, (y + _f64(s, pos + 16)) & M)
        z, x = x, z
        pos += 64
        left -= 64
    return _h16((_h16(v[0], w[0]) + _mix(y) * K1 + z) & M, (_h16(v[1], w[1]) + x) & M)


PLAIN, DIRECTORY = 128, 129


class Archive:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        head = self.f.read(0x30)
        if head[:4] != b'SCS#' or struct.unpack_from('<H', head, 4)[0] != 2:
            raise ValueError('not a HashFS v2 archive')
        self.salt = struct.unpack_from('<H', head, 6)[0]
        count, entry_size, meta_count, meta_size, entry_at, meta_at = struct.unpack_from('<IIIIQQ', head, 12)
        self.f.seek(entry_at)
        entries = zlib.decompress(self.f.read(entry_size))
        self.f.seek(meta_at)
        self.meta = zlib.decompress(self.f.read(meta_size))
        self.entries = {}
        for i in range(count):
            h, index, chunks, flags = struct.unpack_from('<QIHB', entries, i * 16)
            self.entries[h] = (index, chunks, flags)

    def _hash(self, path):
        path = path.strip('/')
        if self.salt:
            path = '%d%s' % (self.salt, path)
        return city64(path.encode())

    def read(self, path):
        """Contents of a file or raw listing of a directory; None if the archive does not hold it."""
        entry = self.entries.get(self._hash(path))
        if not entry:
            return None
        index, chunks, flags = entry
        for c in range(chunks):
            word = struct.unpack_from('<I', self.meta, (index + c) * 4)[0]
            kind, at = word >> 24, (word & 0xFFFFFF) * 4
            if kind in (PLAIN, DIRECTORY):
                packed, size, _, block = struct.unpack_from('<IIII', self.meta, at)
                method = packed >> 28
                packed &= 0xFFFFFF
                size &= 0xFFFFFF
                self.f.seek(block * 16)
                data = self.f.read(packed)
                if method == 1:
                    data = zlib.decompress(data)
                elif method != 0:
                    raise ValueError('compression method %d is not handled' % method)
                return data
        return None

    def listing(self, path):
        """(directories, files) of a directory, or None."""
        data = self.read(path)
        if data is None:
            return None
        count = struct.unpack_from('<I', data, 0)[0]
        lengths = data[4:4 + count]
        at = 4 + count
        dirs, files = [], []
        for n in lengths:
            name = data[at:at + n].decode('utf-8', 'replace')
            at += n
            (dirs if name.startswith('/') else files).append(name.lstrip('/'))
        return dirs, files


def archives(where):
    paths = sorted(glob.glob(os.path.join(where, '*.scs'))) if os.path.isdir(where) else [where]
    out = []
    for p in paths:
        try:
            out.append(Archive(p))
        except (ValueError, zlib.error, struct.error):
            pass
    return out


def merged_listing(found, path):
    dirs, files = set(), set()
    hit = False
    for a in found:
        l = a.listing(path)
        if l:
            hit = True
            dirs.update(l[0])
            files.update(l[1])
    return (sorted(dirs), sorted(files)) if hit else None


def tree(found, path, depth, indent=0):
    l = merged_listing(found, path)
    if not l:
        return
    for d in l[0]:
        print('  ' * indent + d + '/')
        if depth > 1:
            tree(found, path.rstrip('/') + '/' + d, depth - 1, indent + 1)
    for f in l[1]:
        print('  ' * indent + f)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    what, where, path = sys.argv[1:4]
    found = archives(where)
    if what == 'ls':
        l = merged_listing(found, path)
        if not l:
            sys.exit('not found: ' + path)
        for d in l[0]:
            print(d + '/')
        for f in l[1]:
            print(f)
    elif what == 'tree':
        tree(found, path, int(sys.argv[4]) if len(sys.argv) > 4 else 2)
    elif what == 'get':
        for a in reversed(found):
            data = a.read(path)
            if data is not None:
                with open(sys.argv[4], 'wb') as f:
                    f.write(data)
                print('%s: %d bytes from %s' % (path, len(data), os.path.basename(a.path)))
                return
        sys.exit('not found: ' + path)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
