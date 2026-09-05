/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * bc250_audio_fix.c - AMD BC-250 (Cyan Skillfish) Audio Clock & DTO Fix Driver
 *
 * This kernel module configures the DisplayPort and HDMI audio clock DTO
 * (Discrete Time Oscillator) and Azalia HD Audio controller on AMD BC-250 APUs.
 *
 * On the BC-250, default firmware configurations derive incorrect audio sample
 * clocks for the DCN display engine, causing audio stuttering or slow pitch.
 * This module calculates and applies exact phase/modulo DTO values and provides
 * an interactive procfs interface for monitoring and runtime adjustment.
 *
 * License: GPL-2.0
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define BC250_PCI_VENDOR_ID 0x1002
#define BC250_PCI_DEVICE_ID 0x13fe

/* Common AMD Azalia HD Audio device IDs on APUs */
#define AMD_HDA_DEVICE_ID_1 0x1637
#define AMD_HDA_DEVICE_ID_2 0x15e3
#define AMD_HDA_DEVICE_ID_3 0x157a

/* Standard Intel / AMD HDA Controller Register Offsets (BAR 0) */
#define HDA_REG_GCAP       0x00  /* Global Capabilities */
#define HDA_REG_VMIN       0x02  /* Minor Version */
#define HDA_REG_VMAJ       0x03  /* Major Version */
#define HDA_REG_OUTPAY     0x04  /* Output Payload Capability */
#define HDA_REG_INPAY      0x06  /* Input Payload Capability */
#define HDA_REG_GCTL       0x08  /* Global Control */
#define HDA_REG_WAKEEN     0x0C  /* Wake Enable */
#define HDA_REG_STATESTS   0x0E  /* State Change Status */

/* DCN Audio DTO Register Offsets (relative to display controller BAR) */
#define DCN_DCCG_AUDIO_DTO_SOURCE  0x05E0
#define DCN_DCCG_AUDIO_DTO_PHASE   0x05E4
#define DCN_DCCG_AUDIO_DTO_MODULO  0x05E8

MODULE_AUTHOR("BC-250 Project Contributors");
MODULE_DESCRIPTION("AMD BC-250 Audio Clock & DTO Fix Module");
MODULE_LICENSE("GPL v2");

static int sample_rate = 48000;
module_param(sample_rate, int, 0644);
MODULE_PARM_DESC(sample_rate, "Audio sample rate in Hz (default: 48000, e.g. 44100, 48000, 96000)");

static int channels = 2;
module_param(channels, int, 0644);
MODULE_PARM_DESC(channels, "Audio channels (default: 2)");

static struct pci_dev *gpu_dev = NULL;
static struct pci_dev *hda_dev = NULL;
static void __iomem *hda_mmio = NULL;
static resource_size_t hda_bar_len = 0;
static void __iomem *gpu_mmio = NULL;
static resource_size_t gpu_bar_len = 0;
static int gpu_bar_idx = -1;

static u32 current_phase = 0;
static u32 current_modulo = 0;

/*
 * Calculate Discrete Time Oscillator (DTO) phase and modulo for a target sample rate.
 * Target audio master clock = 512 * sample_rate
 * Reference clock = 100 MHz (100,000,000 Hz)
 */
static void bc250_calc_audio_dto(int rate, u32 *phase, u32 *modulo)
{
    u64 target_hz = (u64)512 * rate;
    u64 ref_hz = 100000000ULL; /* 100 MHz standard AMD reference */

    /* Simplify by common factor of 256 */
    *phase = (u32)(target_hz / 256ULL);
    *modulo = (u32)(ref_hz / 256ULL);
}

/*
 * Apply audio clock tuning to hardware
 */
static void bc250_apply_audio_fix(void)
{
    bc250_calc_audio_dto(sample_rate, &current_phase, &current_modulo);

    pr_info("bc250_audio_fix: Target Sample Rate: %d Hz (DTO Phase: %u, Modulo: %u)\n",
            sample_rate, current_phase, current_modulo);

    /* Program DCN DCCG Audio DTO on APU Graphics controller if MMIO is available */
    if (gpu_mmio && gpu_bar_len >= (DCN_DCCG_AUDIO_DTO_MODULO + 4)) {
        pr_info("bc250_audio_fix: Programming DCN DCCG Audio DTO (Phase: %u, Modulo: %u)...\n",
                current_phase, current_modulo);
        iowrite32(0x01, gpu_mmio + DCN_DCCG_AUDIO_DTO_SOURCE); /* Enable DTO0 */
        iowrite32(current_phase, gpu_mmio + DCN_DCCG_AUDIO_DTO_PHASE);
        iowrite32(current_modulo, gpu_mmio + DCN_DCCG_AUDIO_DTO_MODULO);
    }

    if (hda_mmio && hda_bar_len >= 0x10) {
        /* Read HDA Global Capabilities and Status */
        u16 gcap = ioread16(hda_mmio + HDA_REG_GCAP);
        u32 gctl = ioread32(hda_mmio + HDA_REG_GCTL);
        u16 statests = ioread16(hda_mmio + HDA_REG_STATESTS);

        pr_info("bc250_audio_fix: HDA GCAP=0x%04x (OSS=%u, ISS=%u), GCTL=0x%08x, STATESTS=0x%04x\n",
                gcap, (gcap >> 12) & 0x0F, (gcap >> 8) & 0x0F, gctl, statests);

        /* Ensure Controller is out of reset (GCTL bit 0 = 1) */
        if (!(gctl & 0x01)) {
            pr_info("bc250_audio_fix: Bringing HDA controller out of reset...\n");
            iowrite32(gctl | 0x01, hda_mmio + HDA_REG_GCTL);
        }
    }
}

static int audio_status_show(struct seq_file *m, void *v)
{
    seq_printf(m, "===============================================\n");
    seq_printf(m, "      AMD BC-250 Audio Fix & Status Monitor     \n");
    seq_printf(m, "===============================================\n");

    if (gpu_dev) {
        seq_printf(m, "Graphics Device:  %s [1002:%04x]\n", pci_name(gpu_dev), gpu_dev->device);
        if (gpu_mmio && gpu_bar_idx >= 0) {
            seq_printf(m, "GPU MMIO BAR %d:   0x%llx (length: %llu bytes, Active)\n",
                       gpu_bar_idx,
                       (unsigned long long)pci_resource_start(gpu_dev, gpu_bar_idx),
                       (unsigned long long)gpu_bar_len);
            seq_printf(m, "DCN DCCG DTO HW:  Programmed (0x%04x: Phase=%u, Modulo=%u)\n",
                       DCN_DCCG_AUDIO_DTO_PHASE, current_phase, current_modulo);
        } else {
            seq_printf(m, "GPU MMIO BAR:     Unmapped (Display DTO calculated in software)\n");
        }
    } else {
        seq_printf(m, "Graphics Device:  Not Detected\n");
    }

    if (hda_dev) {
        seq_printf(m, "HDA Audio Device: %s [1002:%04x]\n", pci_name(hda_dev), hda_dev->device);
        seq_printf(m, "HDA MMIO BAR 0:   0x%llx (length: %llu bytes)\n",
                   (unsigned long long)pci_resource_start(hda_dev, 0),
                   (unsigned long long)hda_bar_len);
        seq_printf(m, "MMIO Mapping:     %s\n", hda_mmio ? "Mapped Active" : "Unmapped");

        if (hda_mmio && hda_bar_len >= 0x10) {
            u16 gcap = ioread16(hda_mmio + HDA_REG_GCAP);
            u32 gctl = ioread32(hda_mmio + HDA_REG_GCTL);
            u16 statests = ioread16(hda_mmio + HDA_REG_STATESTS);
            seq_printf(m, "HDA Controller:   CRST=%s, Codec Wake=0x%x\n",
                       (gctl & 1) ? "Running" : "Reset", statests);
            seq_printf(m, "HDA Stream Caps:  %d Out Streams, %d In Streams\n",
                       (gcap >> 12) & 0x0F, (gcap >> 8) & 0x0F);
        }
    } else {
        seq_printf(m, "HDA Audio Device: Standalone / Host Driven\n");
    }

    seq_printf(m, "-----------------------------------------------\n");
    seq_printf(m, "Configured Rate:  %d Hz\n", sample_rate);
    seq_printf(m, "Channels:         %d\n", channels);
    seq_printf(m, "DTO Phase:        %u\n", current_phase);
    seq_printf(m, "DTO Modulo:       %u\n", current_modulo);
    seq_printf(m, "DTO Effective MCLK: %llu Hz\n",
               current_modulo ? ((u64)current_phase * 100000000ULL / current_modulo) : 0ULL);
    seq_printf(m, "===============================================\n");
    seq_printf(m, "Tip: To adjust rate at runtime: echo 44100 > /proc/bc250_audio_status\n");

    return 0;
}

static ssize_t audio_status_write(struct file *file, const char __user *ubuf,
                                  size_t count, loff_t *ppos)
{
    char kbuf[32];
    int new_rate;

    if (count == 0 || count >= sizeof(kbuf)) return -EINVAL;
    if (copy_from_user(kbuf, ubuf, count)) return -EFAULT;
    kbuf[count] = '\0';

    if (sscanf(kbuf, "%d", &new_rate) == 1) {
        if (new_rate >= 32000 && new_rate <= 192000) {
            sample_rate = new_rate;
            bc250_apply_audio_fix();
            pr_info("bc250_audio_fix: Updated sample rate to %d Hz via procfs\n", sample_rate);
        } else {
            pr_warn("bc250_audio_fix: Invalid sample rate %d Hz (valid: 32000-192000)\n", new_rate);
            return -EINVAL;
        }
    }
    return count;
}

static int audio_status_open(struct inode *inode, struct file *file)
{
    return single_open(file, audio_status_show, NULL);
}

static const struct proc_ops audio_status_ops = {
    .proc_open    = audio_status_open,
    .proc_read    = seq_read,
    .proc_write   = audio_status_write,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init bc250_audio_init(void)
{
    pr_info("bc250_audio_fix: Initializing AMD BC-250 Audio Fix Module v0.2.0...\n");

    /* 1. Discover Graphics APU & Map MMIO */
    gpu_dev = pci_get_device(BC250_PCI_VENDOR_ID, BC250_PCI_DEVICE_ID, NULL);
    if (gpu_dev) {
        pr_info("bc250_audio_fix: Detected BC-250 APU at %s\n", pci_name(gpu_dev));
        if (pci_enable_device(gpu_dev) == 0) {
            for (int bar = 5; bar >= 0; bar--) {
                resource_size_t len = pci_resource_len(gpu_dev, bar);
                if ((pci_resource_flags(gpu_dev, bar) & IORESOURCE_MEM) &&
                    len >= 0x1000 && len <= 0x10000000) {
                    gpu_bar_idx = bar;
                    gpu_bar_len = len;
                    break;
                }
            }
            if (gpu_bar_idx >= 0) {
                gpu_mmio = pci_iomap(gpu_dev, gpu_bar_idx, 0);
                if (gpu_mmio) {
                    pr_info("bc250_audio_fix: Successfully mapped GPU MMIO BAR %d (%llu bytes)\n",
                            gpu_bar_idx, (unsigned long long)gpu_bar_len);
                } else {
                    pr_warn("bc250_audio_fix: Failed to map GPU MMIO BAR %d\n", gpu_bar_idx);
                }
            }
        }
    } else {
        pr_info("bc250_audio_fix: Notice: BC-250 APU (1002:13fe) not directly detected on bus.\n");
    }

    /* 2. Discover Companion HDA Controller */
    hda_dev = pci_get_class(PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8, NULL);
    while (hda_dev && hda_dev->vendor != BC250_PCI_VENDOR_ID) {
        hda_dev = pci_get_class(PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8, hda_dev);
    }

    if (!hda_dev) {
        /* Fallback: try common AMD audio device IDs */
        hda_dev = pci_get_device(BC250_PCI_VENDOR_ID, AMD_HDA_DEVICE_ID_1, NULL);
        if (!hda_dev) hda_dev = pci_get_device(BC250_PCI_VENDOR_ID, AMD_HDA_DEVICE_ID_2, NULL);
        if (!hda_dev) hda_dev = pci_get_device(BC250_PCI_VENDOR_ID, AMD_HDA_DEVICE_ID_3, NULL);
    }

    if (hda_dev) {
        pr_info("bc250_audio_fix: Detected AMD HD Audio Controller at %s (1002:%04x)\n",
                pci_name(hda_dev), hda_dev->device);

        if (pci_enable_device(hda_dev) == 0) {
            hda_bar_len = pci_resource_len(hda_dev, 0);
            if (hda_bar_len > 0) {
                hda_mmio = pci_iomap(hda_dev, 0, 0);
                if (hda_mmio) {
                    pr_info("bc250_audio_fix: Successfully mapped HDA BAR 0 (%llu bytes)\n",
                            (unsigned long long)hda_bar_len);
                } else {
                    pr_warn("bc250_audio_fix: Failed to map HDA BAR 0\n");
                }
            }
        }
    } else {
        pr_info("bc250_audio_fix: No dedicated HDA function detected; using display DTO calculations.\n");
    }

    /* 3. Apply clock divisors */
    bc250_apply_audio_fix();

    /* 4. Register proc interface */
    proc_create("bc250_audio_status", 0666, NULL, &audio_status_ops);
    pr_info("bc250_audio_fix: Registered /proc/bc250_audio_status monitor.\n");

    return 0;
}

static void __exit bc250_audio_exit(void)
{
    pr_info("bc250_audio_fix: Unloading module...\n");

    remove_proc_entry("bc250_audio_status", NULL);

    if (hda_mmio && hda_dev) {
        pci_iounmap(hda_dev, hda_mmio);
        hda_mmio = NULL;
    }

    if (hda_dev) {
        pci_disable_device(hda_dev);
        pci_dev_put(hda_dev);
        hda_dev = NULL;
    }

    if (gpu_mmio && gpu_dev && gpu_bar_idx >= 0) {
        pci_iounmap(gpu_dev, gpu_mmio);
        gpu_mmio = NULL;
    }

    if (gpu_dev) {
        pci_disable_device(gpu_dev);
        pci_dev_put(gpu_dev);
        gpu_dev = NULL;
    }

    pr_info("bc250_audio_fix: Teardown complete.\n");
}

module_init(bc250_audio_init);
module_exit(bc250_audio_exit);
