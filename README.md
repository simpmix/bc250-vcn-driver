# 🎮 AMD BC-250 Custom Driver & VA-API Video Encoder

[![Build & Release BC-250 Drivers](https://github.com/simpmix/bc250-vcn-driver/actions/workflows/build.yml/badge.svg)](https://github.com/simpmix/bc250-vcn-driver/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/Driver%20License-MIT-blue.svg)](LICENSE)
[![Kernel Module: GPL-2.0](https://img.shields.io/badge/Audio%20Module-GPL--2.0-green.svg)](audio-fix/README.md)

Hardware-accelerated video encoding and DisplayPort/HDMI audio fixes for the **AMD BC-250 ("Cyan Skillfish" / PS5 "Oberon" APU)** on Linux (Bazzite, CachyOS, Fedora, Ubuntu, Arch, ChimeraOS).

> [!NOTE]
> ### 📢 BC-250 Hardware Testers Wanted!
> Software emulation, CAVLC bitstream generation, and FFmpeg decode oracles have all been mathematically verified in CI. If you own a physical BC-250 APU board, help us validate live on-metal performance!
> 1. Download the [v0.2.0 Pre-Built Release](../../releases) or build from source.
> 2. Run `./tools/bc250_diagnose.sh` on your BC-250 system.
> 3. Submit your results using our [Hardware & Benchmark Report](../../issues/new?template=hardware_report.md) template!

---

## 📖 Why Does This Project Exist?

The **AMD BC-250** is a repurposed PlayStation 5 APU equipped with an 8-core Zen 2 CPU and **40 unlocked RDNA 2 Compute Units** (2,560 stream processors, ~10 TFLOPs of GPU power). It has quickly become a budget favorite for building powerful living room gaming consoles.

However, Sony permanently disabled the physical **VCN 3.0 (Video Core Next)** hardware video encoder on these chips using cryptographic security processor (PSP) fuses. Without a video encoder, applications like **Sunshine, Moonlight, OBS Studio, and Steam Link** cannot stream or record gameplay.

### The Solution:
This project solves video encoding without touching the locked VCN silicon:
1. **Vulkan Compute VA-API Driver (`bc250_drv_video.so`):** Emulates a hardware video encoder by running high-performance GLSL compute shaders across the APU's **40 unlocked RDNA 2 Compute Units**.
2. **Audio Clock Fix (`bc250_audio_fix`):** Fixes the notorious "drunk" or stuttering audio over DisplayPort and HDMI with DKMS persistence across Linux kernel updates.

To Linux applications, **it behaves exactly like a standard hardware VA-API encoder!**

---

## 🚀 Quick Start & Installation

You have two easy ways to install the driver:

### Option A: Pre-Built Release (Easiest — No Compiling Needed!)
If you just want to play games and don't want to install compilers or development libraries:

1. Go to the **[Actions Tab](../../actions/workflows/build.yml)** of this repository.
2. Click on the latest workflow run (with a green checkmark).
3. Scroll down to **Artifacts** and download `bc250-driver-linux-x86_64.tar.gz`.
4. Open your terminal in the download folder and extract:
   ```bash
   tar -xzvf bc250-driver-linux-x86_64.tar.gz
   cd bc250-driver
   sudo ./build_and_install.sh
   ```
*(That's it! Shaders, libraries, and audio fixes are installed automatically.)*

---

### Option B: Build from Source (One Simple Command)
If you prefer compiling locally, the automated setup script handles everything (including automatically installing missing packages for Ubuntu, Fedora, Arch, and openSUSE):

```bash
git clone https://github.com/simpmix/bc250-vcn-driver.git
cd bc250-vcn-driver
chmod +x build_and_install.sh tools/*.sh
./build_and_install.sh
```

---

## 🔍 Verifying Your Setup

Run the built-in diagnostic and benchmark tool:

```bash
./tools/bc250_diagnose.sh
```

This will automatically check:
* ✅ APU identification (`1002:13fe`)
* ✅ 40 active Compute Units (CUs)
* ✅ DisplayPort/HDMI audio fix status
* ✅ VA-API driver loading
* ⚡ **Live 100-frame 1080p60 encode benchmark** (reports frame latency and FPS throughput!)

To test with `vainfo`:
```bash
export LIBVA_DRIVER_NAME=bc250
vainfo
```
You will see `VAProfileH264ConstrainedBaseline`, `VAProfileH264Baseline`, `VAProfileH264Main`, and `VAProfileH264High` listed with `VAEntrypointEncSlice` support!

---

## ⚡ Ultra-Low Overhead Gaming Mode (Keep 60 FPS in Games!)

When playing games on your BC-250 console while streaming to Moonlight or recording with OBS, you don't want the encoder taking compute power away from your game.

Add this to your environment (automatically installed by `build_and_install.sh` into `/etc/environment.d/99-bc250.conf`):

```bash
export BC250_FAST_MODE=1
```

* **What it does:** Uses 2:1 checkerboard subsampled motion estimation, early diamond termination, and bypasses the in-loop deblocking filter pass.
* **The Result:** Keeps GPU compute overhead **under 3–5% of the 40 CUs**, allowing your games to run at full 60 FPS with no frame drops!

---

## 🕹️ Application Setup Guides

### 1. Sunshine / Moonlight (Game Streaming to Handhelds & TVs)
1. Ensure the driver is installed:
   ```bash
   export LIBVA_DRIVER_NAME=bc250
   export BC250_FAST_MODE=1
   ```
2. Open the **Sunshine Web Configuration** (usually `https://localhost:47990`).
3. Navigate to **Configuration -> Audio/Video**.
4. Set **Video Encoder** to **VA-API**.
5. Set your target resolution (720p, 1080p, or 1440p).
6. Connect Moonlight from your Steam Deck, ROG Ally, phone, or TV and enjoy low-latency 60 FPS remote gaming!

### 2. OBS Studio (Recording Gameplay & Streaming)
1. Launch OBS from your terminal or desktop with the driver active:
   ```bash
   LIBVA_DRIVER_NAME=bc250 BC250_FAST_MODE=1 obs
   ```
2. Go to **Settings -> Output -> Output Mode: Advanced**.
3. Under **Streaming** or **Recording**, set **Video Encoder** to **FFmpeg VAAPI**.
4. Set **VAAPI Device** to `/dev/dri/renderD128`.

### 3. FFmpeg Command Line
To transcode or encode any video via GPU compute:
```bash
export LIBVA_DRIVER_NAME=bc250
ffmpeg -vaapi_device /dev/dri/renderD128 -i input.mp4 -vf 'format=nv12,hwupload' -c:v h264_vaapi -b:v 8M output.mp4
```

---

## 🔊 Audio Fix (DisplayPort & HDMI)

The BC-250 APU suffers from a known audio clock issue where audio over DisplayPort or HDMI sounds robotic or stutters ("drunk audio"). 

We provide an automated **DKMS kernel module** that fixes the audio clocks and automatically persists across Linux kernel updates:

```bash
cd audio-fix
sudo ./install_dkms.sh
```

To uninstall:
```bash
cd audio-fix
sudo ./uninstall_dkms.sh
```

---

## 🛠️ GitHub Actions CI/CD Pipeline (`build.yml`)

The repository includes an automated GitHub Actions workflow [`.github/workflows/build.yml`](.github/workflows/build.yml):

### For Users:
* Every commit and release automatically builds on Ubuntu runners.
* You can download pre-built release archives without installing any compilers on your gaming console:
  * Click **Actions** at the top of the repository.
  * Click the latest workflow run.
  * Scroll down to **Artifacts** to download `bc250-driver-linux-x86_64.tar.gz`.

### For Maintainers (Creating Official Releases):
To publish a new tagged release:
```bash
git tag v0.2.0
git push origin v0.2.0
```
GitHub Actions will automatically build the driver, run the test suite, package the `.tar.gz` bundle, and publish it directly to the **Releases** tab on GitHub!

---

## ❓ Troubleshooting & FAQs

Have an issue? We've written a dedicated, comprehensive guide:
👉 **[Read the Full Troubleshooting Guide (docs/troubleshooting.md)](docs/troubleshooting.md)**

Common quick fixes:
* **"Permission denied on /dev/dri/renderD128":**
  ```bash
  sudo usermod -a -G video,render $USER
  ```
  *(Log out and back in for permissions to take effect)*
* **"Audio still stuttering":**
  Ensure the module is loaded with `lsmod | grep bc250_audio_fix`. If not, run `sudo modprobe bc250_audio_fix`.
* **"Driver bc250 not found":**
  Verify `export LIBVA_DRIVER_NAME=bc250` is in your shell environment.

---

## 📜 License
* Userspace compute driver, shaders, and tools: **MIT**
* Audio fix kernel module: **GPL-2.0**

<!-- bc250-vcn-driver v0.2.0 -->
