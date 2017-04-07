#!/stage1/busybox sh
_PATH="$PATH"
export PATH=/stage1

busybox cd /
busybox date >>boot.txt
exec >>boot.txt 2>&1
busybox rm -fr init
busybox mount -t proc proc /proc
busybox mount -t sysfs sysfs /sys

busybox mount -t yaffs2 /dev/block/mtdblock2 /system

RAMDISK=ramdisk.cpio.xz

if busybox test -e /cache/.startrecovery || busybox grep -q bootmode=2 /proc/cmdline || busybox grep -q androidboot.mode=reboot_recovery /proc/cmdline ; then
	# recovery boot
	busybox rm -fr /cache/.startrecovery
	RAMDISK=ramdisk-recovery.cpio.xz

elif ! busybox test -e /system/build.prop ; then
	# emergency boot

	# wait for the internal memory
	busybox sleep 3

	make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /cache /dev/block/mmcblk0p2
	busybox mount -t ext4 /dev/block/mmcblk0p2 /cache
	busybox mkdir /cache/recovery

	busybox mkdir -p /sdcard
	busybox mount -t vfat /dev/block/mmcblk0p1 /sdcard

	UPDATE=$(busybox cat /sdcard/cyanogenmod.cfg)

	if busybox test -n "$UPDATE" && busybox test -e "$UPDATE"; then
		busybox echo "install_zip(\"`echo $UPDATE`\");" > /cache/recovery/extendedcommand
	fi

	RAMDISK=ramdisk-recovery.cpio.xz

	busybox umount /sdcard
fi

busybox umount /system

busybox unxz -c ${RAMDISK} | busybox cpio -i

if busybox grep -q bootmode=5 /proc/cmdline || busybox grep -q androidboot.mode=usb_charger /proc/cmdline ; then
	# charging mode
	busybox cp lpm.rc init.rc
	busybox rm init.latona.rc
fi

busybox umount /sys
busybox umount /proc
busybox date >>boot.txt

busybox rm -fr ramdisk.cpio.xz || true
busybox rm -fr ramdisk-recovery.cpio.xz || true

busybox rm -fr /stage1 /dev/*

export PATH="${_PATH}"
exec /init
