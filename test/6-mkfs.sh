#!/bin/bash

USER=hjl
ZNS_NAME="nvme1n2"
ZNS_PATH="/dev/"${ZNS_NAME}
REGULAR_PATH="/dev/nvme2n1p1"
EXT4_SSD_PATH="/dev/nvme2n1p4"
ZNS_FS_PATH="/data/public/${USER}/bbtree/f2fs"
EXT4_FS_PATH="/data/public/${USER}/bbtree/ext4"

FS=$1
if [[ "$FS" == "ext4" ]]; then
    # mkfs
    mkfs.ext4 -F ${EXT4_SSD_PATH}
    mount -t ext4 ${EXT4_SSD_PATH} ${EXT4_FS_PATH}
    chown ${USER}:${USER} ${EXT4_FS_PATH}
elif [[ "$FS" == "f2fs" ]]; then
    ####################################################
    ##### Mount F2FS on a zone namespace SSD device#####
    ####################################################
    # change to mq deadline scheduler
    echo mq-deadline >/sys/block/${ZNS_NAME}/queue/scheduler
    # mkfs
    mkfs.f2fs -f -m -c ${ZNS_PATH} ${REGULAR_PATH}
    # mount
    mount -t f2fs ${REGULAR_PATH} ${ZNS_FS_PATH}
    chown ${USER}:${USER} ${ZNS_FS_PATH}
elif [[ "$FS" == "zenfs" ]]; then
    ####################################################
    ##### Mount ZENFS on a zone namespace SSD device#####
    ####################################################
    # change to mq deadline scheduler
    ZNS_NAME="nvme0n2"
    ZENFS_SUPERBLOCK_PATH="/data/public/hjl/bbtree/rocksdb"
    sudo echo mq-deadline >/sys/block/${ZNS_NAME}/queue/scheduler
    mkdir -p ${ZENFS_SUPERBLOCK_PATH}
    rm -f ${ZENFS_SUPERBLOCK_PATH}/*
    # mksuperblock
    sudo ./rocksdb/plugin/zenfs/util/zenfs mkfs --zbd=${ZNS_NAME} --aux_path=${ZENFS_SUPERBLOCK_PATH}
fi

# umount ${ZNS_FS_PATH}
