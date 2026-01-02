TARGET ?= aesdchar

ifneq ($(KERNELRELEASE),)
	obj-m := $(TARGET).o

	$(TARGET)-y := main.o aesd-circular-buffer.o
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: default

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
