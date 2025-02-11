obj-m += edt-ft5x06.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	@if lsmod | grep -q edt_ft5x06; then \
		echo "Attempting to remove edt_ft5x06 module..."; \
		if ! sudo rmmod -f edt_ft5x06; then \
			echo "Module edt_ft5x06 is in use. Please stop any processes using it and try again."; \
			exit 1; \
		fi \
	fi
	sudo cp edt-ft5x06.ko /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/
	@if [ -f /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/edt-ft5x06.ko.xz ]; then \
		sudo rm /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/edt-ft5x06.ko.xz; \
	fi
	sudo xz -z /lib/modules/$(shell uname -r)/kernel/drivers/input/touchscreen/edt-ft5x06.ko
	sudo depmod -a
	echo "Module edt_ft5x06 installed and compressed successfully."