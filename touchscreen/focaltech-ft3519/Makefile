# Makefile for the focaltech touchscreen drivers.

obj-m   +=  focaltech_core_test.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
EXTRA_CFLAGS := -I$(KDIR)/include -I$(KDIR)/drivers/input/touchscreen -fno-sanitize=all
KCOV_INSTRUMENT := n

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	find . -name "*.ko" -exec sudo cp {} /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/ \;
	cd /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/ && sudo find . -name "*.ko" -exec xz -f {} \;
	sudo depmod -a