uncompressed_ramdisk := $(PRODUCT_OUT)/ramdisk.cpio
$(uncompressed_ramdisk): $(INSTALLED_RAMDISK_TARGET)
	$(MINIGZIP) -d < $(INSTALLED_RAMDISK_TARGET) > $@

MASTER_BOOTSTRAP := $(PRODUCT_OUT)/bootstrap.cpio.gz
$(MASTER_BOOTSTRAP): $(uncompressed_ramdisk) $(recovery_uncompressed_ramdisk) $(PRODUCT_OUT)/utilities/flash_image $(PRODUCT_OUT)/utilities/busybox $(PRODUCT_OUT)/utilities/make_ext4fs $(PRODUCT_OUT)/utilities/erase_image
	$(call pretty,"Bootstrap: $@")
	cp -r device/samsung/galaxysl/bootstrap $(PRODUCT_OUT)
	mkdir -p $(PRODUCT_OUT)/bootstrap/stage1
	cp $(PRODUCT_OUT)/utilities/busybox $(PRODUCT_OUT)/bootstrap/stage1/
	cp $(PRODUCT_OUT)/utilities/make_ext4fs $(PRODUCT_OUT)/bootstrap/stage1/
	cp $(uncompressed_ramdisk) $(PRODUCT_OUT)/bootstrap/
	cp $(recovery_uncompressed_ramdisk) $(PRODUCT_OUT)/bootstrap/
	cd $(PRODUCT_OUT)/bootstrap/; find . | cpio -o -H newc | gzip -9 > ../bootstrap.cpio.gz

INSTALLED_BOOTIMAGE_TARGET := $(PRODUCT_OUT)/boot.img
$(INSTALLED_BOOTIMAGE_TARGET): $(INSTALLED_KERNEL_TARGET) $(MASTER_BOOTSTRAP)
	$(call pretty,"Boot image: $@")
	$(HOST_OUT)/bin/mkbootimg --kernel $(PRODUCT_OUT)/kernel --ramdisk $(MASTER_BOOTSTRAP) --base $(BOARD_KERNEL_BASE) --pagesize $(BOARD_KERNEL_PAGESIZE) --output $@

$(INSTALLED_RECOVERYIMAGE_TARGET): $(INSTALLED_BOOTIMAGE_TARGET)
	$(ACP) $(INSTALLED_BOOTIMAGE_TARGET) $@
