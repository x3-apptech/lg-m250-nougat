#
# Makefile for the FC8180 drivers.
#

# Each configuration option enables a list of files.
# LGE

obj-y		+= broadcast_fc8180_drv_if.o
obj-y		+= broadcast_fc8180.o
obj-y		+= drv/

#MTK
EXTRA_CFLAGS += -I$(srctree)/drivers/misc/mediatek/broadcast/oneseg
EXTRA_CFLAGS += -I$(srctree)/drivers/misc/mediatek/broadcast/oneseg/fc8180/drv

#QCT
#EXTRA_CFLAGS += -Idrivers/broadcast/oneseg
#EXTRA_CFLAGS += -Idrivers/broadcast/oneseg/fc8180/drv

ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
EXTRA_CFLAGS += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
endif