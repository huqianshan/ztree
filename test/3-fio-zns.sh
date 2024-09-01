#!/bin/bash

# run in root directory it seems read in libaio faster than psync
# 4kib write 774MiB 198K read libaio IOPS=185k, BW=724MiB/s (759MB/s)  psync IOPS=142k, BW=554MiB/s
# 8Kib write 1440MiB read psync IOPS=119k, BW=932MiB/s

log_dir=./test/result/log/
FNAME=${1}
ZNS_DEVICE=${2}

BS_LISTS=(4k) # 8k 16k 32k 64k 128k)

# raw device
FIO_COMMAD="fio --ioengine=psync --direct=1 --rw=write --group_reporting --zonemode=zbd \
--name=write --offset_increment=4z --numjobs=14 --job_max_open_zones=1 --runtime=120m"

# raw device random read
# IOMODE=randread

# FIO_COMMAD="fio --ioengine=psync --direct=1 --rw=${IOMODE} --group_reporting --zonemode=zbd \
# --name=randomread --offset_increment=4z --size=4z --numjobs=64 --runtime=10"

# FIO_COMMAD="fio --ioengine=io_uring --direct=1 --rw=randread \
#   --size=4z --zonemode=zbd --name=randread --iodepth=32"

# Mixed read-write performance test
# FIO_COMMAND="fio --ioengine=psync --direct=1 --rw=rw --rwmixread=80 --group_reporting \
# --name=mixed --size=6G --numjobs=14"
# sudo blkzone reset /dev/${2}
for bs in ${BS_LISTS[@]}; do
  rlog=${log_dir}${FNAME}-${bs}.log
  # 如果rlog文件不存在，则创建它
  if [ ! -f ${rlog} ]; then
    touch ${rlog}
  fi
  sudo ${FIO_COMMAD} --filename=${ZNS_DEVICE} --bs=${bs} 2>&1 | tee -a ${rlog}

done
