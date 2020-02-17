# Makefile by giann

COMPILER = /path/to/compiler
KDIR = /path/to/kernel
PWD := $(shell pwd)

all:
	make -C ${KDIR} M = ${PWD} ARCH=${ARCH} CROSS_COMPILE=${COMPILER} modules
clean:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(COMPILER) clean
