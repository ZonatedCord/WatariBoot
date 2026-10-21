#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Find ACPI MADT tables in a guest-RAM dump (QEMU monitor: pmemsave 0x40000000 <size> "<file>", quote the path) and print the GIC subtables.
# Usage: python3 -I m2-madt.py dump.bin   (RAM base 0x40000000)
import sys,struct
d=open(sys.argv[1],'rb').read()
i=0;n=0
while True:
    i=d.find(b'APIC',i)
    if i<0:break
    ln=struct.unpack_from('<I',d,i+4)[0]
    if 40<ln<2000 and d[i+8] in (3,4,5,6) and sum(d[i:i+ln])%256==0:
        print('MADT at',hex(0x40000000+i),'len',ln,'rev',d[i+8],'oem',d[i+10:i+16])
        o=i+44
        while o<i+ln:
            t,l=d[o],d[o+1]
            b=d[o:o+l]
            if t==0x0D:
                _,fid,base,flags,cnt,sb=struct.unpack_from('<HIQIHH',b,2)
                print(' MSI FRAME id',fid,'base',hex(base),'flags',hex(flags),'spi_count',cnt,'spi_base',sb)
            else:
                print(' type',hex(t),'len',l,b[:l].hex()[:70] if t not in(0xb,) else '')
            o+=l
    i+=4
