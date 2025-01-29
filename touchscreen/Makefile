obj-m += edt-ft5x06.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	@if lsmod | grep -q edt_ft5x06; then sudo rmmod edt_ft5x06; fi
	sudo cp edt-ft5x06.ko /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/
	sudo depmod -a