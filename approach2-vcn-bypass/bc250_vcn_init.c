/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * AMD BC-250 (Cyan Skillfish) VCN Direct Initialization
 *
 * Copyright (C) 2024
 * License: GPL-2.0
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include "bc250_vcn_regs.h"

static inline void vcn_write32(struct bc250_vcn_dev *vdev, u32 reg, u32 val, bool force, bool dry_run)
{
	u32 offset = VCN_BASE_OFFSET + reg * 4;
	u32 old_val = ioread32(vdev->mmio + offset);
	
	if (dry_run) {
		dev_info(&vdev->pdev->dev, "[DRY RUN] Would write 0x%08x to reg 0x%04x (old val: 0x%08x)\n", val, offset, old_val);
		return;
	}

	if (!force) {
		dev_info(&vdev->pdev->dev, "[PROTECTED] Attempted write 0x%08x to reg 0x%04x (old val: 0x%08x). Set force=1 to override.\n", val, offset, old_val);
		return;
	}

	iowrite32(val, vdev->mmio + offset);
}

static inline u32 vcn_read32(struct bc250_vcn_dev *vdev, u32 reg)
{
	return ioread32(vdev->mmio + VCN_BASE_OFFSET + reg * 4);
}

static int vcn_poll_register(struct bc250_vcn_dev *vdev, u32 reg, u32 mask, u32 expected, int timeout_us)
{
	int i;
	for (i = 0; i < timeout_us; i += 10) {
		if ((vcn_read32(vdev, reg) & mask) == expected)
			return 0;
		udelay(10);
	}
	return -ETIMEDOUT;
}

// Debugfs file for dumping registers
static int vcn_register_dump_show(struct seq_file *m, void *data)
{
	struct bc250_vcn_dev *vdev = m->private;
	seq_printf(m, "VCN Register Dump (Base 0x%08x):\n", VCN_BASE_OFFSET);
	seq_printf(m, "UVD_STATUS:       0x%08x\n", vcn_read32(vdev, mmUVD_STATUS));
	seq_printf(m, "UVD_POWER_STATUS: 0x%08x\n", vcn_read32(vdev, mmUVD_POWER_STATUS));
	seq_printf(m, "UVD_SOFT_RESET:   0x%08x\n", vcn_read32(vdev, mmUVD_SOFT_RESET));
	seq_printf(m, "UVD_VCPU_CNTL:    0x%08x\n", vcn_read32(vdev, mmUVD_VCPU_CNTL));
	seq_printf(m, "UVD_CGC_CTRL:     0x%08x\n", vcn_read32(vdev, mmUVD_CGC_CTRL));
	seq_printf(m, "UVD_MASTINT_EN:   0x%08x\n", vcn_read32(vdev, mmUVD_MASTINT_EN));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vcn_register_dump);

// Debugfs file for init status
static int vcn_init_status_show(struct seq_file *m, void *data)
{
	struct bc250_vcn_dev *vdev = m->private;
	seq_printf(m, "VCN Initialization Status: %s\n", vdev->is_initialized ? "INITIALIZED" : "NOT INITIALIZED");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vcn_init_status);

static void setup_debugfs(struct bc250_vcn_dev *vdev)
{
	vdev->debugfs_root = debugfs_create_dir("bc250_vcn", NULL);
	debugfs_create_file("register_dump", 0444, vdev->debugfs_root, vdev, &vcn_register_dump_fops);
	debugfs_create_file("init_status", 0444, vdev->debugfs_root, vdev, &vcn_init_status_fops);
}

int bc250_vcn_init_hw(struct bc250_vcn_dev *vdev, bool force, bool dry_run)
{
	u32 status;
	int ret;

	dev_info(&vdev->pdev->dev, "Attempting VCN hardware init (force=%d, dry_run=%d)...\n", force, dry_run);

	if (!vdev->debugfs_root) {
		setup_debugfs(vdev);
	}

	/* Check if already initialized */
	status = vcn_read32(vdev, mmUVD_POWER_STATUS);
	if (status & UVD_POWER_STATUS__UVD_POWER_EN_MASK) {
		dev_warn(&vdev->pdev->dev, "VCN appears to be already powered on. Aborting init to prevent conflict.\n");
		return -EBUSY;
	}

	dev_info(&vdev->pdev->dev, "Step 1: Powering on VCN...\n");
	vcn_write32(vdev, mmUVD_POWER_STATUS, status | UVD_POWER_STATUS__UVD_POWER_EN_MASK, force, dry_run);

	dev_info(&vdev->pdev->dev, "Step 2: VCPU Clock Setup...\n");
	vcn_write32(vdev, mmUVD_VCPU_CNTL, UVD_VCPU_CNTL__CLK_EN_MASK, force, dry_run);

	dev_info(&vdev->pdev->dev, "Step 3: Disabling Clock Gating...\n");
	vcn_write32(vdev, mmUVD_CGC_CTRL, 0, force, dry_run);
	vcn_write32(vdev, mmUVD_SUVD_CGC_CTRL, 0, force, dry_run);

	dev_info(&vdev->pdev->dev, "Step 4: Memory Controller VMID Setup...\n");
	vcn_write32(vdev, mmVCN_VM_CONTEXT0_CNTL, 0, force, dry_run); // Simplified VMID setup

	dev_info(&vdev->pdev->dev, "Step 5: Local Memory Interface (LMI) Soft Reset...\n");
	vcn_write32(vdev, mmUVD_SOFT_RESET, 
				UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK | 
				UVD_SOFT_RESET__LMI_SOFT_RESET_MASK | 
				UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK, 
				force, dry_run);
	
	mdelay(10); // Wait for reset to propagate

	dev_info(&vdev->pdev->dev, "Step 6: Releasing Soft Reset...\n");
	vcn_write32(vdev, mmUVD_SOFT_RESET, 0, force, dry_run);

	if (!dry_run && force) {
		dev_info(&vdev->pdev->dev, "Step 7: Polling UVD Status...\n");
		ret = vcn_poll_register(vdev, mmUVD_STATUS, UVD_STATUS__IDLE_MASK, UVD_STATUS__IDLE_MASK, 100000); // 100ms
		
		if (ret) {
			dev_err(&vdev->pdev->dev, "VCN not idle after reset! UVD_STATUS=0x%08x\n", vcn_read32(vdev, mmUVD_STATUS));
			return -EIO;
		}
	}

	vdev->is_initialized = (!dry_run && force);
	if (vdev->is_initialized) {
		dev_info(&vdev->pdev->dev, "VCN hardware init sequence completed successfully.\n");
		bc250_vcn_ring_init(vdev);
	}

	return 0;
}

void bc250_vcn_fini_hw(struct bc250_vcn_dev *vdev)
{
	if (!vdev->is_initialized)
		return;

	dev_info(&vdev->pdev->dev, "Tearing down VCN hardware...\n");
	
	/* Assert reset to power down safely */
	vcn_write32(vdev, mmUVD_SOFT_RESET, 
				UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK | 
				UVD_SOFT_RESET__LMI_SOFT_RESET_MASK, 
				true, false);
				
	vcn_write32(vdev, mmUVD_POWER_STATUS, 0, true, false);

	if (vdev->debugfs_root) {
		debugfs_remove_recursive(vdev->debugfs_root);
		vdev->debugfs_root = NULL;
	}

	vdev->is_initialized = false;
}
