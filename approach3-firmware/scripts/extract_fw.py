# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#!/usr/bin/env python3
# MIT License
import os
import sys
import struct
import argparse
from pathlib import Path

# AMD Firmware Common Header format
# struct common_firmware_header {
#     uint32_t size_bytes;      /* size of the entire header+image(s) in bytes */
#     uint32_t header_size_bytes; /* size of just the header in bytes */
#     uint16_t header_version_major;
#     uint16_t header_version_minor;
#     uint16_t ip_version_major;
#     uint16_t ip_version_minor;
#     uint32_t ucode_version;
#     uint32_t ucode_size_bytes;
#     uint32_t ucode_array_offset_bytes;
#     uint32_t crc32;
# } __packed;

def parse_header(data):
    if len(data) < 32:
        return None
    header = struct.unpack('<IIHHHHIIII', data[:32])
    return {
        'size_bytes': header[0],
        'header_size_bytes': header[1],
        'header_version_major': header[2],
        'header_version_minor': header[3],
        'ip_version_major': header[4],
        'ip_version_minor': header[5],
        'ucode_version': header[6],
        'ucode_size_bytes': header[7],
        'ucode_array_offset_bytes': header[8],
        'crc32': header[9]
    }

def list_firmwares(fw_dir="/lib/firmware/amdgpu"):
    print(f"Scanning for VCN firmwares in {fw_dir}...")
    fw_path = Path(fw_dir)
    if not fw_path.exists():
        print(f"Error: {fw_dir} does not exist.")
        return
    
    for f in fw_path.glob('*_vcn.bin'):
        print(f"Found: {f.name}")

def extract_fw(fw_file, out_dir):
    try:
        with open(fw_file, 'rb') as f:
            data = f.read()
            
        header = parse_header(data)
        if not header:
            print(f"Failed to parse header for {fw_file}")
            return
            
        print(f"Header for {fw_file}:")
        for k, v in header.items():
            print(f"  {k}: {hex(v) if 'crc' in k or 'version' in k else v}")
            
        ucode_offset = header['ucode_array_offset_bytes']
        ucode_size = header['ucode_size_bytes']
        
        ucode_data = data[ucode_offset:ucode_offset+ucode_size]
        
        out_path = Path(out_dir) / f"{Path(fw_file).stem}_extracted.bin"
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, 'wb') as out_f:
            out_f.write(ucode_data)
            
        print(f"Extracted {ucode_size} bytes to {out_path}")
            
    except Exception as e:
        print(f"Error extracting {fw_file}: {e}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="AMD VCN Firmware Extractor")
    parser.add_argument('--list', action='store_true', help='List available VCN firmwares')
    parser.add_argument('--extract', type=str, help='Extract specific firmware file')
    parser.add_argument('--out', type=str, default='out', help='Output directory for extraction')
    
    args = parser.parse_args()
    
    if args.list:
        list_firmwares()
    elif args.extract:
        extract_fw(args.extract, args.out)
    else:
        parser.print_help()
