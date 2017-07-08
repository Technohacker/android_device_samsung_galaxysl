#!/tmp/busybox sh
    # don't create LVM partitions if they already exist
    /tmp/busybox test -e /dev/lvpool/system && exit 0

    # create physical volumes
    /lvm/sbin/lvm pvcreate /dev/block/mmcblk0p1 /dev/block/mmcblk0p2 /dev/block/mmcblk0p3

    # create the volume group
    /lvm/sbin/lvm vgcreate lvpool /dev/block/mmcblk0p1 /dev/block/mmcblk0p2 /dev/block/mmcblk0p3

    # create logical volumes
    /lvm/sbin/lvm lvcreate -L 680M -n system lvpool
    /lvm/sbin/lvm lvcreate -L 20M -n cache lvpool
    /lvm/sbin/lvm lvcreate -L 2.98G -n data lvpool
    
    # format the logical volumes
    /tmp/make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /system /dev/lvpool/system
    /tmp/make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /cache /dev/lvpool/cache
    /tmp/make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /data /dev/lvpool/data

exit 0 
