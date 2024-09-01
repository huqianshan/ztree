#!/bin/bash
# using sudo mode
lscpu | grep -i node
for nvme in $(nvme list | sed 1,2d | awk '{print $1}' | awk -F "/" '{print $NF}'); do
    echo $nvme
    busid=$(readlink -f /sys/block/$nvme | cut -d '/' -f 6)
    echo "busid = $busid"
    lspci -s $busid -vv | grep -i node
    echo "======================================"
done
