# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
obj-m := bc250_vcn.o
bc250_vcn-objs := bc250_vcn_probe.o bc250_vcn_init.o bc250_vcn_ring.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
