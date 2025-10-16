override METAG_VERSION_NEEDED := 2.8.1.0.3
override MIPS_VERSION_NEEDED := 2014.07-1
override PDVFS_COM_HOST := 1
override PDVFS_COM_AP := 2
override PDVFS_COM_PMC := 3
override PDVFS_COM := PDVFS_COM_HOST
override PVRSRV_MODNAME := pvrsrvkm
override PVRSYNC_MODNAME := pvr_sync
override PVR_BUILD_DIR := sunxi_linux
override PVR_HANDLE_BACKEND := idr
override PVR_SYSTEM := rgx_sunxi
override PVR_USE_FENCE_SYNC_MODEL := 1
override RGX_TIMECORR_CLOCK := mono
override SUPPORT_DMABUF_BRIDGE := 1
override SUPPORT_DMA_FENCE := 1
override SUPPORT_NATIVE_FENCE_SYNC := 1
override SUPPORT_PHYSMEM_TEST := 1
override SUPPORT_RGX := 1
override SUPPORT_SERVER_SYNC_IMPL := 1
override VMM_TYPE := stub
override undefine SUPPORT_DISPLAY_CLASS
ifeq ($(CONFIG_DRM_POWERVR_ROGUE_DEBUG),y)
override BUILD := debug
override PVRSRV_ENABLE_GPU_MEMORY_INFO := 1
override PVR_BUILD_TYPE := debug
else
override BUILD := release
override PVR_BUILD_TYPE := release
endif
