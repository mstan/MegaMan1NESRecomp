"""Identify the supported retail payload; an iNES region byte is insufficient."""
from pathlib import Path
import sys
import zlib

PROFILES={0x6EE4BB0A:('NTSC','usa'),0x94476A70:('PAL','europe')}

def main():
    data=Path(sys.argv[1]).read_bytes()
    if data[:4]!=b'NES\x1a' or len(data)<16:
        raise ValueError('An iNES Mega Man ROM is required')
    start=16+(512 if data[6]&4 else 0)
    length=data[4]*16384+data[5]*8192
    if len(data)!=start+length:
        raise ValueError('Unexpected ROM length')
    crc=zlib.crc32(data[start:])
    if crc not in PROFILES:
        raise ValueError(f'Unknown Mega Man payload CRC{crc:08X}; expected USA6EE4BB0A or Europe94476A70')
    print(';'.join(PROFILES[crc]))

if __name__=='__main__':
    main()
