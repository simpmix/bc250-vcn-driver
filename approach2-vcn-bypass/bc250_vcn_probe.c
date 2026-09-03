/*
 * AMD BC-250 (Cyan Skillfish) VCN Probing Module
 *
 * Copyright (C) 2024
 * License: GPL-2.0
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include "bc250_vcn_regs.h"

MODULE_AUTHOR("Research Team");
MODULE_DESCRIPTION("AMD BC-250 VCN Register Probing Module");
MODULE_LICENSE("GPL v2");

static bool force = false;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Force hardware writes (DANGEROUS)");

static bool dry_run = true;
module_param(dry_run, bool, 0644);
MODULE_PARM_DESC(dry_run, "Log writes without executing them");

static struct bc250_vcn_dev *vcn_dev;

static int bc250_vcn_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err;
	u32 status, power_status;

	dev_info(&pdev->dev, "Probing AMD BC-250 VCN...\n");

	vcn_dev = devm_kzalloc(&pdev->dev, sizeof(*vcn_dev), GFP_KERNEL);
	if (!vcn_dev)
		return -ENOMEM;

	vcn_dev->pdev = pdev;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, BIT(0), "bc250_vcn");
	if (err)
		return err;

	vcn_dev->mmio = pcim_iomap_table(pdev)[0];

	/* Safe read-only probing */
	status = ioread32(vcn_dev->mmio + mmUVD_STATUS * 4);
	power_status = ioread32(vcn_dev->mmio + mmUVD_POWER_STATUS * 4);

	dev_info(&pdev->dev, "UVD_STATUS: 0x%08x\n", status);
	dev_info(&pdev->dev, "UVD_POWER_STATUS: 0x%08x\n", power_status);

	if (status == 0xFFFFFFFF || status == 0xDEADBEEF) {
		dev_warn(&pdev->dev, "VCN block does not seem to respond!\n");
	}

	vcn_dev->debugfs_root = debugfs_create_dir("bc250_vcn", NULL);

	bc250_vcn_init_hw(vcn_dev, force, dry_run);

	return 0;
}

static void bc250_vcn_remove(struct pci_dev *pdev)
{
	if (vcn_dev) {
		bc250_vcn_ring_fini(vcn_dev);
		bc250_vcn_fini_hw(vcn_dev);
		debugfs_remove_recursive(vcn_dev->debugfs_root);
	}
	dev_info(&pdev->dev, "BC-250 VCN module removed\n");
}

static const struct pci_device_id bc250_vcn_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_CYAN_SKILLFISH) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, bc250_vcn_id_table);

static struct pci_driver bc250_vcn_driver = {
	.name = "bc250_vcn",
	.id_table = bc250_vcn_id_table,
	.probe = bc250_vcn_probe,
	.remove = bc250_vcn_remove,
};

module_pci_driver(bc250_vcn_driver);
