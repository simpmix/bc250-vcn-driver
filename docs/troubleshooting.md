# AMD BC-250 Driver Troubleshooting & Diagnostic Guide

This guide covers solutions to all known issues when using the AMD BC-250 (Cyan Skillfish) custom VA-API compute driver and audio fix on Linux distributions (Bazzite, Fedora, Ubuntu, Arch, CachyOS, ChimeraOS).

---

## Quick Diagnostic Check

Before troubleshooting individual issues, run the built-in diagnostic tool from your terminal:

```bash
cd /path/to/bc250-vcn-driver
./tools/bc250_diagnose.sh
```

This automated script checks your APU identification, active Compute Units (CUs), audio driver status, VA-API profile queries, and runs a live 100-frame encode benchmark.

---

## 1. DisplayPort / HDMI Audio Issues ("Drunk" or Stuttering Audio)

### Symptoms
* Audio over DisplayPort or HDMI sounds distorted, robotic, pitched down, or stutters ("drunk audio").
* Audio drops out completely when switching resolutions.

### Cause
The Cyan Skillfish display controller uses a non-standard clock divisor in the `amdgpu` display engine (`dc`), calculating the wrong sample clock for standard 48kHz audio.

### Solution
Install the DKMS audio fix module, which dynamically applies the correct clock configuration:

```bash
cd audio-fix
sudo ./install_dkms.sh
```

**Verify the fix is active:**
```bash
lsmod | grep bc250_audio_fix
```
If active, you will see `bc250_audio_fix` listed. Because this is registered with **DKMS**, it will automatically rebuild and persist whenever you update your Linux kernel!

---

## 2. "vaInitialize failed: driver bc250 not found"

### Symptoms
* Running `vainfo` or launching OBS reports:
  ```
  vaInitialize failed with error code -1 (unknown libva error),exit
  ```
  or
  ```
  vaGetDriverNameByIndex() failed with unknown libva error, driver_name = (null)
  ```

### Cause
The VA-API loader (`libva`) looks for driver libraries in specific DRI directories depending on your Linux distribution (`/usr/lib64/dri/`, `/usr/lib/x86_64-linux-gnu/dri/`, or `/usr/lib/dri/`). If `bc250_drv_video.so` is missing from the directory expected by your distro, it will fail to load.

### Solution
1. Ensure `LIBVA_DRIVER_NAME=bc250` is set in your environment:
   ```bash
   export LIBVA_DRIVER_NAME=bc250
   ```
2. Re-link the driver into all common system DRI paths:
   ```bash
   sudo mkdir -p /usr/lib/x86_64-linux-gnu/dri /usr/lib64/dri /usr/lib/dri
   sudo cp -f approach1-compute-encoder/build/bc250_drv_video.so /usr/lib/x86_64-linux-gnu/dri/
   sudo cp -f approach1-compute-encoder/build/bc250_drv_video.so /usr/lib64/dri/
   sudo cp -f approach1-compute-encoder/build/bc250_drv_video.so /usr/lib/dri/
   ```
3. Test again:
   ```bash
   LIBVA_DRIVER_NAME=bc250 vainfo
   ```

---

## 3. Permission Denied on `/dev/dri/renderD128`

### Symptoms
* `vainfo` or FFmpeg fails with:
  ```
  Failed to open /dev/dri/renderD128: Permission denied
  ```
* Sunshine reports encoder initialization error when launched as a non-root user.

### Cause
Your user account does not have read/write access to the GPU render node.

### Solution
Add your user account to the `video` and `render` groups:
```bash
sudo usermod -a -G video,render $USER
```
Then log out and log back in (or reboot) for the permissions to take effect. Verify with:
```bash
groups
```
Ensure `render` is listed in the output.

---

## 4. Sunshine / Moonlight Streaming Issues

### Symptom A: Moonlight shows black screen or immediate disconnect
* **Solution 1:** In the Sunshine Web UI (**Configuration -> Audio/Video**):
  * Set **Video Encoder** to `VA-API`.
  * Ensure **Resolution** matches a standard 16:9 ratio (1280x720, 1920x1080, or 2560x1440).
* **Solution 2:** Enable the low-overhead gaming mode in your environment:
  ```bash
  export BC250_FAST_MODE=1
  ```
  This bypasses the deblock filter and uses 2:1 subsampled motion estimation, keeping frame latency below 5ms!

### Symptom B: Stream drops frames when game graphics are demanding
* The BC-250 driver automatically prioritizes the GPU's dedicated **Async Compute Engine (ACE)** queues so encoding does not stall graphics rendering.
* Ensure you are running in fast mode:
  ```bash
  export BC250_FAST_MODE=1
  ```
  This keeps encoder GPU load under **3–5% of the 40 CUs**, leaving the rest of the APU for the game.

---

## 5. OBS Studio: "Failed to open video codec"

### Symptoms
* Clicking **Start Recording** or **Start Streaming** in OBS results in:
  ```
  Starting the output failed. Please check the log for details.
  Error: Failed to open video codec: Function not implemented (-40)
  ```

### Solution
1. Launch OBS from the terminal with the driver specified:
   ```bash
   LIBVA_DRIVER_NAME=bc250 obs
   ```
2. In OBS:
   * Go to **Settings -> Output -> Output Mode: Advanced**.
   * Under the **Streaming** or **Recording** tab, select **FFmpeg VAAPI**.
   * Set **VAAPI Device** to `/dev/dri/renderD128`.
   * Under **Profile**, select **Main** or **High**.

---

## 6. Verifying 40 Compute Units (CUs) vs 24 CUs

### Check Status
Run:
```bash
cat /sys/class/drm/card0/device/current_compute_units 2>/dev/null || dmesg | grep -i "compute units"
```
* **Expected:** 40 active CUs.
* **If it reports 24 CUs:** Your kernel or BIOS is limiting the APU to its mining board default. You will need to apply the community `bc250-40cu-unlock` kernel patch or install an APU-optimized distribution like **Bazzite** or **SkillFishOS** which bundles the 40 CU unlock out of the box.

---

## 7. How to Completely Uninstall the Driver

If you ever wish to remove the driver:

```bash
# 1. Remove DRI libraries
sudo rm -f /usr/lib*/dri/bc250_drv_video.so
sudo rm -f /usr/lib/x86_64-linux-gnu/dri/bc250_drv_video.so

# 2. Remove shaders
sudo rm -rf /usr/share/bc250

# 3. Remove environment configs
sudo rm -f /etc/environment.d/99-bc250.conf

# 4. Uninstall audio fix from DKMS
cd audio-fix && sudo ./uninstall_dkms.sh
```
