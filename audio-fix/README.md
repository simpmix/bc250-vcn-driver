# AMD BC-250 Audio Fix

This directory contains a kernel module and patches to fix the DisplayPort/HDMI audio issues on the AMD BC-250 (Cyan Skillfish).

## The Issue
The DP/HDMI audio on Cyan Skillfish suffers from a "drunk" or stuttering playback. This is due to an incorrect audio clock divisor calculation in the `amdgpu` display codebase (`dc`), which derives the audio sample rate clock incorrectly for this specific APU configuration.

## Approaches

We provide two ways to fix this:
1. **Loadable Kernel Module (`bc250_audio_fix.ko`)**: Dynamically patches the audio configuration registers. Safe, doesn't require recompiling the kernel.
2. **Mainline Kernel Patches (`patches/`)**: Direct patches for the `amdgpu` driver. Requires recompiling your kernel but provides a permanent, native fix.

## Automated Installation via DKMS (Recommended)
This ensures the audio fix automatically survives Linux kernel updates:
```bash
sudo ./install_dkms.sh
```
To uninstall:
```bash
sudo ./uninstall_dkms.sh
```

## Manual Module Build
```bash
make
sudo insmod bc250_audio_fix.ko sample_rate=48000 channels=2
```

## Runtime Monitoring & Tuning
Once the module is loaded, you can check the live audio status and register states:
```bash
cat /proc/bc250_audio_status
```
Example output:
```
===============================================
      AMD BC-250 Audio Fix & Status Monitor     
===============================================
Graphics Device:  0000:01:00.0 [1002:13fe]
HDA Audio Device: 0000:01:00.1 [1002:1637]
HDA MMIO BAR 0:   0xfce80000 (length: 16384 bytes)
MMIO Mapping:     Mapped Active
HDA Controller:   CRST=Running, Codec Wake=0x1
HDA Stream Caps:  4 Out Streams, 4 In Streams
-----------------------------------------------
Configured Rate:  48000 Hz
Channels:         2
DTO Phase:        96000
DTO Modulo:       390625
DTO Effective MCLK: 24576000 Hz
===============================================
```

You can also dynamically change the audio sample rate at runtime without unloading the module:
```bash
# Switch to 44.1 kHz (CD audio standard)
echo 44100 | sudo tee /proc/bc250_audio_status

# Switch back to 48 kHz (DVD / video standard)
echo 48000 | sudo tee /proc/bc250_audio_status
```

## Applying Patches
If you prefer to patch your kernel directly:
```bash
cd /usr/src/linux
patch -p1 < /path/to/0001-dp-audio-clock-fix.patch
patch -p1 < /path/to/0002-hdmi-audio-adapter-compat.patch
make && sudo make modules_install install
```

<!-- bc250-vcn-driver v0.1.0 -->
