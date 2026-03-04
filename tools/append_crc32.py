#!/usr/bin/env python3
"""
append_crc32.py - Post-build CRC32 injection for M1 firmware images.

Calculates an STM32-HAL-compatible CRC32 over the firmware image and injects
the CRC extension block at fixed offsets within the binary file.

STM32 CRC algorithm:
  - Polynomial:    0x04C11DB7
  - Initial value: 0xFFFFFFFF
  - Input:         NOT reflected (fed as 32-bit words, big-endian bit order)
  - Output:        NOT reflected
  - Final XOR:     None

CRC extension block layout (within the 1KB reserved area at 0x080FFC00):
  Offset 0xFFC14: CRC_MAGIC      (0x43524332 = "CRC2")
  Offset 0xFFC18: fw_image_size  (bytes, word-aligned)
  Offset 0xFFC1C: CRC32 value

Usage:
    python append_crc32.py <firmware.bin> [--output <output.bin>] [--verbose]

If --output is not specified, the input file is modified in place.

M1 Project
See COPYING.txt for license details.
"""

import argparse
import struct
import sys
import os

# === Constants ===
FW_CONFIG_OFFSET     = 0x0FFC00   # Offset of FW_CONFIG_RESERVED from flash base (within the .bin)
CRC_EXT_BASE_OFFSET  = 20         # Fixed: right after the 20-byte S_M1_FW_CONFIG_t struct
CRC_EXT_MAGIC_OFFSET = FW_CONFIG_OFFSET + CRC_EXT_BASE_OFFSET + 0   # 0xFFC14
CRC_EXT_SIZE_OFFSET  = FW_CONFIG_OFFSET + CRC_EXT_BASE_OFFSET + 4   # 0xFFC18
CRC_EXT_CRC_OFFSET   = FW_CONFIG_OFFSET + CRC_EXT_BASE_OFFSET + 8   # 0xFFC1C

CRC_EXT_MAGIC_VALUE  = 0x43524332  # "CRC2"
CRC_POLYNOMIAL       = 0x04C11DB7
CRC_INIT             = 0xFFFFFFFF


def stm32_crc32(data: bytes) -> int:
    """
    Compute STM32-HAL-compatible CRC32.

    Matches the hardware CRC peripheral with:
      - Default polynomial (0x04C11DB7)
      - Default init value (0xFFFFFFFF)
      - No input/output inversion
      - 32-bit word input format

    Args:
        data: Firmware binary data (must be word-aligned, len % 4 == 0)

    Returns:
        CRC32 value as unsigned 32-bit integer
    """
    if len(data) % 4 != 0:
        raise ValueError(f"Data length ({len(data)}) is not word-aligned (must be multiple of 4)")

    crc = CRC_INIT

    for offset in range(0, len(data), 4):
        # Read as 32-bit little-endian word (STM32 is little-endian)
        word = struct.unpack_from('<I', data, offset)[0]

        crc ^= word

        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ CRC_POLYNOMIAL) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF

    return crc


def main():
    parser = argparse.ArgumentParser(
        description='Inject STM32-compatible CRC32 into M1 firmware binary'
    )
    parser.add_argument('input', help='Input firmware .bin file')
    parser.add_argument('--output', '-o', help='Output file (default: modify in place)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Print detailed info')
    args = parser.parse_args()

    # Read the input binary
    if not os.path.isfile(args.input):
        print(f"ERROR: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    with open(args.input, 'rb') as f:
        data = bytearray(f.read())

    file_size = len(data)

    if args.verbose:
        print(f"Input file:   {args.input}")
        print(f"File size:    {file_size} bytes (0x{file_size:X})")

    # Validate file size
    if file_size < FW_CONFIG_OFFSET + CRC_EXT_BASE_OFFSET + 12:
        print(f"ERROR: File too small ({file_size} bytes). Expected at least "
              f"{FW_CONFIG_OFFSET + CRC_EXT_BASE_OFFSET + 12} bytes.", file=sys.stderr)
        sys.exit(1)

    # The firmware image to CRC is from 0x0 up to the config area (0xFFC00)
    fw_image_size = FW_CONFIG_OFFSET
    if fw_image_size % 4 != 0:
        print(f"ERROR: Firmware image size (0x{fw_image_size:X}) is not word-aligned.", file=sys.stderr)
        sys.exit(1)

    fw_image_data = bytes(data[:fw_image_size])

    if args.verbose:
        print(f"CRC region:   0x00000000 - 0x{fw_image_size:08X} ({fw_image_size} bytes, {fw_image_size // 4} words)")

    # Calculate the CRC32
    crc_value = stm32_crc32(fw_image_data)

    if args.verbose:
        print(f"CRC32:        0x{crc_value:08X}")
        print(f"Magic offset: 0x{CRC_EXT_MAGIC_OFFSET:06X}")
        print(f"Size offset:  0x{CRC_EXT_SIZE_OFFSET:06X}")
        print(f"CRC offset:   0x{CRC_EXT_CRC_OFFSET:06X}")

    # Inject the CRC extension block into the binary
    struct.pack_into('<I', data, CRC_EXT_MAGIC_OFFSET, CRC_EXT_MAGIC_VALUE)
    struct.pack_into('<I', data, CRC_EXT_SIZE_OFFSET, fw_image_size)
    struct.pack_into('<I', data, CRC_EXT_CRC_OFFSET, crc_value)

    # Write the output
    output_path = args.output if args.output else args.input
    with open(output_path, 'wb') as f:
        f.write(data)

    if args.verbose:
        print(f"Output file:  {output_path}")

    print(f"CRC32 injected: 0x{crc_value:08X} (image: {fw_image_size} bytes) -> {output_path}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
