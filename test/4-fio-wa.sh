#!/bin/bash

# 定义设备路径和日志文件路径
LOG_DIR=./test/result/log/
DEVICE=/dev/nvme2n1

# 定义fio写入的粒度
BLOCK_SIZES=("4k")
# BLOCK_SIZES=("8k" "32k" "256k")
# PRE=4-zns-write-SN540
PRE=6-trim-rand-conv-write-P4510

# 运行fio命令，测试全盘写性能，持续写入数据
FIO_LOG=${LOG_DIR}${PRE}-${DEVICE##*/}-FIO.log
for BLOCK_SIZE in ${BLOCK_SIZES[@]}; do
    IOSTAT_LOG=${LOG_DIR}${PRE}-${DEVICE##*/}-iostat-${BLOCK_SIZE}.log
    SMART_LOG=${LOG_DIR}${PRE}-${DEVICE##*/}-smart-${BLOCK_SIZE}.log
    # 清空文件内容
    >$IOSTAT_LOG
    >$SMART_LOG

    # use blkdiscard to discard the whole disk,ensure the trimmed condition
    # sudo blkdiscard /dev/nvme4n1
    # sudo nvme format /dev/nvme4n1 --ses=1

    # 启动iostat命令，每3s记录一次，输出到日志文件
    sudo iostat -d ${DEVICE} -x 1 -m >>$IOSTAT_LOG &
    IO_PID=$!
    # 启动nvme命令，每10s记录一次，输出到日志文件
    while true; do
        sudo nvme intel smart-log-add ${DEVICE} >>$SMART_LOG
        # sudo nvme smart-log ${DEVICE} >>$SMART_LOG
        sleep 8
    done &
    # 获取后台进程的PID
    PID=$!
    echo "Background PID:${IO_PID} ${PID}"

    sudo fio --ioengine=psync --direct=1 --rw=randwrite --bs=$BLOCK_SIZE --group_reporting \
        --name=write --size=550% --numjobs=14 --filename=${DEVICE} --time_based --runtime=120m 2>&1 | tee -a ${FIO_LOG}

    # zns write
    # sudo numactl -N 1 fio --ioengine=psync --direct=1 --rw=write --group_reporting --zonemode=zbd \
    # --name=write --offset_increment=4z --numjobs=14 --job_max_open_zones=1 --filename=${DEVICE} --time_based --runtime=60m 2>&1 | tee -a ${FIO_LOG}

    # fio --ioengine=io_uring --direct=1 --rw=randwrite --bs=$BLOCK_SIZE --group_reporting \
    # --name=write --size=250% --iodepth=32 --filename=${DEVICE} --time_based --runtime=30m
    # fio --ioengine=psync --direct=1 --rw=write --bs=$BLOCK_SIZE --filename=${DEVICE} --group_reporting --zonemode=zbd --offset_increment=10z --size=10z --name=write --numjobs=14 --job_max_open_zones=1 --time_based --runtime=30m

    # 停止iostat和nvme命令
    killall iostat
    killall "nvme smart-log"
    USER=$(whoami)
    chown ${USER}:${USER} ${FIO_LOG} ${IOSTAT_LOG} ${SMART_LOG}
    # killall nvme
    # 在脚本退出时杀死后台进程
    trap "kill $PID" EXIT
    trap "kill $IO_PID" EXIT
    trap "chown ${USER}:${USER} ${FIO_LOG} ${IOSTAT_LOG} ${SMART_LOG}" EXIT

done

# if [[ "${MODE}" == "iostat" ]]; then
# 提取日志文件中的最后一列数字，并输出到csv文件
awk '/nvme2n1/{bd=$9; print bd}' $IOSTAT_LOG >${IOSTAT_LOG%.log}.csv
# elif [[ "${MODE}" == "smart" ]]; then
# 提取日志文件中的最后一列数字，并输出到csv文件
awk '/nand_bytes_written/{nand=$NF; getline; if ($0 ~ /host_bytes_written/) print nand","$NF}' $SMART_LOG >${SMART_LOG%.log}.csv
# fi
