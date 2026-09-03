#!/usr/bin/env python3
# MIT License
import os
import sys
import struct
import hashlib
import argparse
from pathlib import Path

def compute_hash(data):
    return hashlib.sha256(data).hexdigest()

def hexdump(data, length=64):
    for i in range(0, min(len(data), length), 16):
        chunk = data[i:i+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"{i:04x}  {hex_str:<48}  |{ascii_str}|")

def analyze_fw(fw_file):
    print(f"Analyzing {fw_file}...")
    try:
        with open(fw_file, 'rb') as f:
            data = f.read()
            
        print(f"File size: {len(data)} bytes")
        print(f"SHA256: {compute_hash(data)}")
        
        # Look for standard headers or signature blocks
        # This is a generic analysis that looks for common structures
        if data.startswith(b'\x00\x00\x00\x00'): # Some magic checks
             print("Warning: Null bytes at start, might be encrypted or raw ucode.")
             
        # Look for strings
        print("Looking for ASCII strings >= 8 chars:")
        current_str = []
        strings = []
        for b in data:
            if 32 <= b < 127:
                current_str.append(chr(b))
            else:
                if len(current_str) >= 8:
                    strings.append(''.join(current_str))
                current_str = []
        
        for s in strings:
             print(f"  String found: {s}")
             
        print("\nFirst 64 bytes:")
        hexdump(data, 64)
            
    except Exception as e:
        print(f"Error analyzing {fw_file}: {e}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="AMD VCN Firmware Analyzer")
    parser.add_argument('file', type=str, help='Firmware file to analyze')
    args = parser.parse_args()
    analyze_fw(args.file)
