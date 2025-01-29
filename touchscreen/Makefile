obj-m += edt-ft5x06.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo cp edt-ft5x06.ko /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/
	sudo depmod -a