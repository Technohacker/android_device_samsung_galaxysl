#!/tmp/busybox sh
    # don't create LVM partitions if they already exist
    /tmp/busybox test -e /dev/lvpool/system && exit 0
    
    # ui_print by Chainfire
    OUTFD=$(/tmp/busybox ps | /tmp/busybox grep -v "grep" | /tmp/busybox grep -o -E "update_binary(.*)" | /tmp/busybox cut -d " " -f 3);
    ui_print() {
        if [ $OUTFD != "" ]; then
            echo "ui_print ${1} " 1>&$OUTFD;
            echo "ui_print " 1>&$OUTFD;
        else
            echo "${1}";
        fi;
    }
    
    if ! /tmp/busybox test -e /tmp/.accept_data_wipe ; then
        touch /tmp/.accept_data_wipe
        ui_print "*****************************************"
        ui_print "* This update WILL ERASE YOUR INTERNAL  *"
        ui_print "*     SD CARD. Please make a backup     *"
        ui_print "*  of your data in the internal SD card *"
        ui_print "* (photos, music, documents, etc.) and  *"
        ui_print "*    reflash this update to confirm     *"
        ui_print "*****************************************"
        exit 3
    fi
    
    # Data wipe accepted
    rm /tmp/.accept_data_wipe

    # create physical volumes
    /lvm/sbin/lvm pvcreate /dev/block/mmcblk0p1 /dev/block/mmcblk0p2 /dev/block/mmcblk0p3

    # create the volume group
    /lvm/sbin/lvm vgcreate lvpool /dev/block/mmcblk0p1 /dev/block/mmcblk0p2 /dev/block/mmcblk0p3

    # create logical volumes
    /lvm/sbin/lvm lvcreate -L 670M -n system lvpool
    /lvm/sbin/lvm lvcreate -L 30M -n cache lvpool
    /lvm/sbin/lvm lvcreate -L 2.98G -n data lvpool
    
    # format the logical volumes
    /tmp/make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /system /dev/lvpool/system
    /tmp/make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /cache /dev/lvpool/cache
    /tmp/make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /data /dev/lvpool/data

exit 0 
