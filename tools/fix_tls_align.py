#!/usr/bin/env python3
# Copyright 2026 The ZCat-GLES Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Raise the PT_TLS alignment of an ELF32 ARM executable to 32 bytes.

Static bionic binaries produced by the NDK end up with an 8-byte aligned TLS
segment, while bionic's start-up code requires 32 bytes for ARM.  The check
aborts under qemu-user.  The TLS image bytes only depend on p_offset and
p_filesz, so moving p_vaddr/p_paddr up to the next 32-byte boundary and setting
p_align is safe for a single-threaded test executable.
"""

import struct
import sys


def main(path):
  data = bytearray(open(path, 'rb').read())
  if data[:4] != b'\x7fELF' or data[4] != 1:
    raise SystemExit('not an ELF32 file: ' + path)
  phoff = struct.unpack_from('<I', data, 28)[0]
  phentsize = struct.unpack_from('<H', data, 42)[0]
  phnum = struct.unpack_from('<H', data, 44)[0]
  patched = False
  for i in range(phnum):
    offset = phoff + i * phentsize
    if struct.unpack_from('<I', data, offset)[0] != 7:  # PT_TLS
      continue
    align = struct.unpack_from('<I', data, offset + 28)[0]
    if align >= 32:
      return False
    vaddr = struct.unpack_from('<I', data, offset + 8)[0]
    new_vaddr = (vaddr + 31) & ~31
    struct.pack_into('<I', data, offset + 8, new_vaddr)
    struct.pack_into('<I', data, offset + 12, new_vaddr)
    struct.pack_into('<I', data, offset + 28, 32)
    patched = True
    print('PT_TLS 0x%x -> 0x%x, align %d -> 32' % (vaddr, new_vaddr, align))
  if patched:
    with open(path, 'wb') as output:
      output.write(data)
  return patched


if __name__ == '__main__':
  main(sys.argv[1])
