/*
 * AMD BC-250 (Cyan Skillfish) VCN Register-Level Bypass Module
 *
 * Copyright (C) 2024
 * License: GPL-2.0
 */

#ifndef __BC250_VCN_REGS_H__
#define __BC250_VCN_REGS_H__

#include <linux/types.h>

/* Cyan Skillfish PCI IDs */
#define PCI_VENDOR_ID_ATI		0x1002
#define PCI_DEVICE_ID_CYAN_SKILLFISH	0x13FE

/* VCN 3.0 IP Base Address (Relative to MMIO BAR, from amdgpu kernel source) */
#define VCN_BASE_OFFSET			0x00007E00

/* 
 * VCN Registers
 * These registers control the UVD (Unified Video Decoder) block within VCN.
 */
#define mmUVD_STATUS				0x000F
#define mmUVD_POWER_STATUS			0x0010
#define mmUVD_RBC_IB_SIZE			0x0011
#define mmUVD_RBC_IB_BASE			0x0012
#define mmUVD_CONTEXT_ID			0x0013
#define mmUVD_ENGINE_CNTL			0x0014
#define mmUVD_SOFT_RESET			0x0015
#define mmUVD_LMI_CTRL				0x0016
#define mmUVD_LMI_STATUS			0x0017
#define mmUVD_LMI_CTRL2				0x0018
#define mmUVD_RBC_RB_BASE			0x0018 // Note: offsets can overlap depending on IP version. Using distinct names.
#define mmUVD_RBC_RB_WPTR			0x0019
#define mmUVD_RBC_RB_RPTR			0x001A
#define mmUVD_RBC_RB_CNTL			0x001B

/* VCPU Control and Interrupts */
#define mmUVD_VCPU_CNTL				0x001C
#define mmUVD_MASTINT_EN			0x001D

/* Clock Gating Control Registers */
#define mmUVD_CGC_CTRL				0x0022
#define mmUVD_CGC_STATUS			0x0023
#define mmUVD_SUVD_CGC_CTRL			0x0024

/* VCN Encode Ring Registers */
#define mmUVD_RB_BASE_LO			0x0025
#define mmUVD_RB_BASE_HI			0x0026
#define mmUVD_RB_SIZE				0x0027
#define mmUVD_RB_WPTR				0x0028
#define mmUVD_RB_RPTR				0x0029

/* Scratch Registers for Firmware Communication */
#define mmUVD_SCRATCH0				0x0040
#define mmUVD_SCRATCH1				0x0041
#define mmUVD_SCRATCH2				0x0042
#define mmUVD_GP_SCRATCH0			0x0048
#define mmUVD_GP_SCRATCH1			0x0049

/* Dynamic Power Gating (DPG) Registers */
#define mmUVD_DPG_CTRL				0x0050
#define mmUVD_DPG_STATUS			0x0051

/* VCN JPEG Decode Registers */
#define mmVCN_JPEG_SYS_STATUS		0x0060
#define mmVCN_JPEG_CGC_CTRL			0x0061
#define mmVCN_JPEG_RB_BASE			0x0062
#define mmVCN_JPEG_RB_SIZE			0x0063
#define mmVCN_JPEG_RB_WPTR			0x0064

/* MMHUB VM Registers for VCN Memory Translation */
#define mmVCN_MMHUB_CTRL			0x0080
#define mmVCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32 0x0081
#define mmVCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32 0x0082
#define mmVCN_VM_CONTEXT0_CNTL		0x0083

/* Field Definitions and Masks */
#define UVD_STATUS__IDLE_MASK			0x00000001
#define UVD_STATUS__VCPU_REPORT_MASK	0x00000002

#define UVD_POWER_STATUS__UVD_POWER_EN_MASK	0x00000001

#define UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK	0x00000001
#define UVD_SOFT_RESET__LMI_SOFT_RESET_MASK	    0x00000002
#define UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK 0x00000004

#define UVD_VCPU_CNTL__CLK_EN_MASK      0x00000001
#define UVD_VCPU_CNTL__RB_EN_MASK       0x00000002

#define UVD_MASTINT_EN__VCPU_EN_MASK    0x00000001
#define UVD_MASTINT_EN__SYS_EN_MASK     0x00000002

#define UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK 0x00000001
#define UVD_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK 0x00003F00
#define UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT 8

struct bc250_vcn_dev {
	struct pci_dev *pdev;
	void __iomem *mmio;
	struct dentry *debugfs_root;
	bool is_initialized;
};

/* Function prototypes */
int bc250_vcn_init_hw(struct bc250_vcn_dev *vdev, bool force, bool dry_run);
void bc250_vcn_fini_hw(struct bc250_vcn_dev *vdev);
int bc250_vcn_ring_init(struct bc250_vcn_dev *vdev);
void bc250_vcn_ring_fini(struct bc250_vcn_dev *vdev);

#endif /* __BC250_VCN_REGS_H__ */
