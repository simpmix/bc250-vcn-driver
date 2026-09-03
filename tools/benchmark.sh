#!/bin/bash
# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
# MIT License
# Benchmark encoder performance

echo "Generating test video pattern..."
ffmpeg -y -f lavfi -i testsrc=duration=10:size=1920x1080:rate=60 test_input.y4m &>/dev/null

echo "Benchmarking Software x264..."
time ffmpeg -y -i test_input.y4m -c:v libx264 -preset fast test_out_x264.mp4 &>/dev/null

echo "Benchmarking VA-API (VCN/Compute Shim)..."
# Assuming our compute shim exposes standard vaapi
time ffmpeg -y -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 -hwaccel_output_format vaapi -i test_input.y4m -c:v h264_vaapi test_out_vaapi.mp4 &>/dev/null

echo "Benchmark complete."
