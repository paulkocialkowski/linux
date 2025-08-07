img_basedir := $(src)
include $(img_basedir)/config_kernel.mk

obj-$(CONFIG_DRM_POWERVR_ROGUE) += pvrsrvkm.o

ccflags-y += \
 -include config_kernel.h \
 -I$(img_basedir) \
 -I$(img_basedir)/km \
 -I$(img_basedir)/system \
 -I$(img_basedir)/system/$(PVR_SYSTEM) \
 -D__linux__

include $(img_basedir)/pvrsrvkm.mk
