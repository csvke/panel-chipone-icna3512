obj-m := panel-chipone-icna3512.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
EXTRA_CFLAGS := -I$(KDIR)/include -I$(KDIR)/drivers/gpu/drm -fno-sanitize=all
KCOV_INSTRUMENT := n

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo cp panel-chipone-icna3512.ko /lib/modules/$(shell uname -r)/kernel/drivers/gpu/drm/panel/
	sudo depmod -a