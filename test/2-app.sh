#!/bin/bash

# cd ..

# git clone -b v8.10.0 https://github.com/facebook/rocksdb.git --depth=1
# cd rocksdb
# git clone https://github.com/westerndigitalcorporation/zenfs plugin/zenfs
# sudo DEBUG_LEVEL=0 ROCKSDB_PLUGINS=zenfs make -j db_bench install
# cd plugin/zenfs/util
# sudo apt-get install libgflags-dev libsnappy-devd liburing-dev libbz2-dev liblz4-dev
# make -j
# pushd
# sudo echo mq-deadline | sudo tee /sys/block/nvme3n2/queue/scheduler
## log dir
# mkdir -p /data/public/hjl/bbtree/rocksdb
# rm -f /data/public/hjl/bbtree/rocksdb/*
# cd /home/hjl/Academical/BBTree-RocksDB/rocksdb
# sudo ./plugin/zenfs/util/zenfs mkfs --zbd=nvme0n2 --aux_path=/data/public/hjl/bbtree/rocksdb
# ./db_bench --fs_uri=zenfs://dev:nvme2n2 --benchmarks=fillrandom --use_direct_io_for_flush_and_compaction

TREE_NAME=$1
WORKLOADS=(ycsba ycsbb ycsbc ycsbd ycsbe real)
NUMS=4
TREE_EXE=None
OUTPUT_FILE=None
rm_color="s/\x1B\[[0-9;]*[a-zA-Z]//g"
THREAD=50

if [[ "$TREE_NAME" == "rocksdb" ]]; then
    echo "rocksdb bench"
    TREE_EXE=./rocksdb_benchmark
    OUTPUT_FILE=./result/app-rocksdb-${NUMS}m.log
elif [[ "$TREE_NAME" == "wiredtiger" ]]; then
    echo "wiredtiger bench"
    TREE_EXE=./wiredtiger_benchmark
    OUTPUT_FILE=./result/app-wiredtiger-${NUMS}m.log
elif [[ "$TREE_NAME" == "ztree" ]]; then
    echo "ztree bench"
    TREE_EXE=./build/test/ztree
    OUTPUT_FILE=./test/result/app-ztree-${NUMS}m-final.log
fi

for WORKLOAD in ${WORKLOADS[@]}; do
    echo "${WORKLOAD}"
    sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
done
awk '/run,/ {printf "%s,", $3}' ${OUTPUT_FILE}
