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

## Applying Patches
If you prefer to patch your kernel:
```bash
cd /usr/src/linux
patch -p1 < /path/to/0001-dp-audio-clock-fix.patch
patch -p1 < /path/to/0002-hdmi-audio-adapter-compat.patch
make && sudo make modules_install install
```

<!-- bc250-vcn-driver v0.1.0 -->
