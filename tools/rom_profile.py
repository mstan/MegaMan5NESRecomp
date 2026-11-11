"""Validate the supported original USA cartridge payload before generation."""
from pathlib import Path
import hashlib
import sys


def main():
    data = Path(sys.argv[1]).read_bytes()
    if len(data) < 16 or data[:4] != b'NES\x1a':
        raise SystemExit('An iNES Mega Man 5 (USA) ROM is required')
    start = 16 + (512 if data[6] & 4 else 0)
    mapper = (data[6] >> 4) | (data[7] & 0xf0)
    if mapper != 4 or data[4] != 16 or data[5] != 32 or len(data) != start + 524288:
        raise SystemExit('Mega Man 5 requires the original MMC3 PRG/CHR-ROM cartridge layout')
    if hashlib.sha1(data[start:]).hexdigest() != '36842491bb513c80beb9f2fb2aa30edaa1121ca4':
        raise SystemExit('Unsupported cartridge payload; supply Mega Man 5 (USA)')
    print('NTSC;usa')


if __name__ == '__main__':
    main()
