"""Show the skeleton of a model (.pmg) or what skeleton an animation (.pma) is made for.

    python tools/skeleton_info.py <game dir> <path in the archives> [...]
    python tools/skeleton_info.py <game dir> --dir <directory>     every .pmg/.pma in it, one line each

An animation only fits a model whose skeleton hash is the same.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hashfs

LETTERS = '\0' + '0123456789abcdefghijklmnopqrstuvwxyz_'


def token(value):
    out = ''
    while value:
        out += LETTERS[value % 38]
        value //= 38
    return out


def pmg(data):
    version = data[0]
    if data[1:4] != b'gmP':
        return None
    pieces, parts, bones, weight_width, locators, skeleton = struct.unpack_from('<IIIiIQ', data, 4)
    # The hash is followed by the bounds (40 bytes), then the offsets of the sections.
    skeleton_at = struct.unpack_from('<i', data, 0x48)[0]
    names = []
    for i in range(bones):
        at = skeleton_at + i * 200
        name = token(struct.unpack_from('<Q', data, at)[0])
        translation = struct.unpack_from('<3f', data, at + 8 + 64 + 64 + 16 + 16)
        parent = struct.unpack_from('<b', data, at + 196)[0]
        names.append((name, parent, translation))
    return {'version': version, 'pieces': pieces, 'bones': bones, 'locators': locators, 'skeleton': skeleton, 'names': names}


def pma(data):
    # Version 5: frame count and flags (16 bits each), bone count, a hash, bounds, then the offsets of the frame
    # lengths, of one byte per bone, and of the frames (56 bytes per bone and frame).
    version, frames, flags, bones, hash_ = struct.unpack_from('<IHHIQ', data, 0)
    lengths_at, bones_at = struct.unpack_from('<II', data, 0x40)
    length = sum(struct.unpack_from('<%df' % frames, data, lengths_at))
    return {'version': version, 'frames': frames, 'bones': bones, 'length': length, 'skeleton': hash_, 'map': data[bones_at:bones_at + bones]}


def read(found, path):
    for a in reversed(found):
        data = a.read(path)
        if data is not None:
            return data
    return None


def one_line(found, path):
    data = read(found, path)
    if data is None:
        return '%s: not found' % path
    if path.endswith('.pmg'):
        info = pmg(data)
        return '%-70s model      skeleton %016X  %3d bones  %d pieces' % (path, info['skeleton'], info['bones'], info['pieces'])
    info = pma(data)
    return '%-70s animation  skeleton %016X  %3d bones  %4d frames  %.2f s' % (path, info['skeleton'], info['bones'], info['frames'], info['length'])


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    found = hashfs.archives(sys.argv[1])
    if sys.argv[2] == '--dir':
        directory = sys.argv[3].rstrip('/')
        listing = hashfs.merged_listing(found, directory)
        if not listing:
            sys.exit('not found: ' + directory)
        for name in listing[1]:
            if name.endswith(('.pmg', '.pma')):
                print(one_line(found, directory + '/' + name))
        return
    for path in sys.argv[2:]:
        print(one_line(found, path))
        if path.endswith('.pmg'):
            data = read(found, path)
            if data:
                for i, (name, parent, translation) in enumerate(pmg(data)['names']):
                    print('    %3d %-14s parent %3d  at (%.3f, %.3f, %.3f)' % ((i, name, parent) + translation))


if __name__ == '__main__':
    main()
