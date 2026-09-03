# Safely Testing on BC-250 Hardware

When working with experimental drivers on the BC-250:

1. **Use a secondary machine via SSH.** If the GPU hangs, the display will freeze. SSH allows you to safely reboot (`sudo reboot`) or capture `dmesg` logs.
2. **Monitor temperatures.** Use `watch -n 1 sensors` or `amdgpu_top`.
3. **Module Loading.** Always unload standard drivers before loading custom ones if they conflict:
   ```bash
   sudo rmmod bc250_audio_fix
   sudo insmod bc250_audio_fix.ko
   ```
4. **Recovery.** If a hard lock occurs, hold the power button or use SysRq keys (REISUB) to safely shut down.

<!-- bc250-vcn-driver v0.1.0 -->
