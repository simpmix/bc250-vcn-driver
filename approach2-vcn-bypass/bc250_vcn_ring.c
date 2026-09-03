/*
 * AMD BC-250 (Cyan Skillfish) VCN Ring Buffer Setup
 *
 * Copyright (C) 2024
 * License: GPL-2.0
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include "bc250_vcn_regs.h"

#define RING_SIZE 0x10000

struct vcn_ring {
	void *cpu_addr;
	dma_addr_t dma_addr;
	u32 size;
};

static struct vcn_ring rb;

int bc250_vcn_ring_init(struct bc250_vcn_dev *vdev)
{
	dev_info(&vdev->pdev->dev, "Initializing VCN ring buffer...\n");

	rb.size = RING_SIZE;
	rb.cpu_addr = dma_alloc_coherent(&vdev->pdev->dev, rb.size, &rb.dma_addr, GFP_KERNEL);
	if (!rb.cpu_addr) {
		dev_err(&vdev->pdev->dev, "Failed to allocate ring buffer\n");
		return -ENOMEM;
	}

	/* Dummy ring programming, simulating the write (since we don't know exact offsets for RB programming yet) */
	dev_info(&vdev->pdev->dev, "Ring buffer allocated at DMA %pad, CPU %p\n", &rb.dma_addr, rb.cpu_addr);

	/* Normally we would write mmUVD_RBC_RB_BASE, SIZE, WPTR, RPTR etc here */
	
	return 0;
}

void bc250_vcn_ring_fini(struct bc250_vcn_dev *vdev)
{
	if (rb.cpu_addr) {
		dma_free_coherent(&vdev->pdev->dev, rb.size, rb.cpu_addr, rb.dma_addr);
		rb.cpu_addr = NULL;
		dev_info(&vdev->pdev->dev, "VCN ring buffer freed\n");
	}
}
