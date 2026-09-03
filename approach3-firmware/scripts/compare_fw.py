# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#!/usr/bin/env python3
# MIT License
import os
import sys
import argparse
from pathlib import Path

def compare_fw(fw1, fw2):
    print(f"Comparing {fw1} and {fw2}...")
    
    try:
        with open(fw1, 'rb') as f1, open(fw2, 'rb') as f2:
            data1 = f1.read()
            data2 = f2.read()
            
        print(f"{fw1} size: {len(data1)}")
        print(f"{fw2} size: {len(data2)}")
        
        min_len = min(len(data1), len(data2))
        diff_count = 0
        
        # Simple byte-by-byte comparison to find diffs
        # In a real tool we'd do block/chunk alignment matching
        for i in range(min_len):
            if data1[i] != data2[i]:
                diff_count += 1
                
        diff_percent = (diff_count / min_len) * 100 if min_len > 0 else 0
        print(f"Found {diff_count} differing bytes in the overlapping region ({diff_percent:.2f}% difference)")
        
        if len(data1) != len(data2):
             print(f"Size difference: {abs(len(data1) - len(data2))} bytes")
             
    except Exception as e:
        print(f"Error comparing firmwares: {e}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="AMD VCN Firmware Comparator")
    parser.add_argument('fw1', type=str, help='First firmware file')
    parser.add_argument('fw2', type=str, help='Second firmware file')
    args = parser.parse_args()
    compare_fw(args.fw1, args.fw2)
