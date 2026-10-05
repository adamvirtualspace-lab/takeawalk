"""Helpers for static analysis of the game executable (PE64).

Used from short throwaway scripts:

    from exe_analysis import Image
    img = Image(r'...\\eurotrucks2.exe')
    vtable = img.vtables('.?AVNpScene@physx@@')[0]
    print(img.disasm(img.qword(vtable), 40))
"""
import re
import struct

import capstone
import numpy as np


class Image:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        pe = struct.unpack_from('<I', self.data, 0x3C)[0]
        count = struct.unpack_from('<H', self.data, pe + 6)[0]
        opt_size = struct.unpack_from('<H', self.data, pe + 20)[0]
        self.base = struct.unpack_from('<Q', self.data, pe + 24 + 24)[0]
        table = pe + 24 + opt_size
        self.sections = []
        for i in range(count):
            name, vsize, va, rsize, raw, _, _, _, _, flags = struct.unpack_from('<8sIIIIIIHHI', self.data, table + i * 40)
            self.sections.append((name.rstrip(b'\0').decode(), va, vsize, raw, rsize, flags))
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = False

    # Address conversion. "rva" is relative to the image base, "off" is a file offset.

    def off(self, rva):
        for _, va, vsize, raw, rsize, _ in self.sections:
            if va <= rva < va + max(vsize, rsize):
                return raw + rva - va if rva - va < rsize else None
        return None

    def rva(self, off):
        for _, va, _, raw, rsize, _ in self.sections:
            if raw <= off < raw + rsize:
                return va + off - raw
        return None

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        raise KeyError(name)

    def is_code(self, rva):
        for _, va, vsize, _, _, flags in self.sections:
            if va <= rva < va + vsize:
                return bool(flags & 0x20000000)
        return False

    def bytes(self, rva, size):
        off = self.off(rva)
        return self.data[off:off + size]

    def qword(self, rva):
        """Pointer stored at rva, returned as an rva."""
        return struct.unpack('<Q', self.bytes(rva, 8))[0] - self.base

    def dword(self, rva):
        return struct.unpack('<I', self.bytes(rva, 4))[0]

    # Searching.

    def find(self, needle):
        """RVAs of every occurrence of a byte string."""
        return [self.rva(m.start()) for m in re.finditer(re.escape(needle), self.data)]

    def xrefs(self, target):
        """(rva, tail) of every 4-byte displacement in .text which addresses `target`
        RIP-relatively. `tail` is the number of immediate bytes following the displacement,
        so the instruction ends at rva + 4 + tail."""
        _, va, _, raw, rsize, _ = self.section('.text')
        text = self.data[raw:raw + rsize]
        hits = []
        want = target - va
        arr = np.frombuffer(text, dtype=np.uint8)
        disp = (arr[:-3].astype(np.int64) | (arr[1:-2].astype(np.int64) << 8) | (arr[2:-1].astype(np.int64) << 16) | (arr[3:].astype(np.int64) << 24))
        disp = np.where(disp >= 0x80000000, disp - 0x100000000, disp)
        idx = np.arange(len(disp), dtype=np.int64)
        for tail in (0, 1, 2, 4):
            for p in np.nonzero(idx + 4 + tail + disp == want)[0]:
                hits.append((va + int(p), tail))
        return sorted(set(hits))

    def function_start(self, rva, limit=0x4000):
        """Best guess at the start of the function containing rva: the nearest preceding
        address that follows int3 padding."""
        off = self.off(rva)
        for back in range(1, limit):
            if self.data[off - back] == 0xCC and self.data[off - back + 1] != 0xCC:
                return rva - back + 1
        return None

    # RTTI.

    def vtables(self, mangled):
        """RVAs of the vtables of the class with the given RTTI name (primary one first)."""
        result = []
        for name_rva in self.find(mangled.encode() + b'\0'):
            descriptor = name_rva - 0x10
            for off in (m.start() for m in re.finditer(re.escape(struct.pack('<I', descriptor)), self.data)):
                locator = off - 12
                signature, offset, _, td, _, self_rva = struct.unpack_from('<IIIIII', self.data, locator)
                locator_rva = self.rva(locator)
                if signature != 1 or td != descriptor or self_rva != locator_rva:
                    continue
                for ref in self.find(struct.pack('<Q', self.base + locator_rva)):
                    result.append((offset, ref + 8))
        return [v for _, v in sorted(result)]

    def vtable_entries(self, vtable, limit=400):
        entries = []
        for i in range(limit):
            target = self.qword(vtable + i * 8)
            if not self.is_code(target):
                break
            entries.append(target)
        return entries

    # Disassembly.

    def disasm(self, rva, count=30):
        lines = []
        code = self.bytes(rva, count * 15)
        for i, ins in enumerate(self.md.disasm(code, self.base + rva)):
            if i >= count:
                break
            lines.append(f'{ins.address - self.base:08X}  {ins.mnemonic} {ins.op_str}')
        return '\n'.join(lines)
