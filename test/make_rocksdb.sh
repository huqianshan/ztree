#!/bin/bash

LOG_DIR=/data/public/hjl/bbtree/rocksdb
if [ ! -d $LOG_DIR ]; then
    mkdir -p $LOG_DIR
fi
rm -rf $LOG_DIR/*

pushd $PWD > /dev/null

# mount zenfs
# cd /home/hjl/Academical/BBTree-RocksDB/rocksdb
# sudo ./plugin/zenfs/util/zenfs mkfs --zbd=nvme0n2 --aux_path=/data/public/hjl/bbtree/rocksdb

cd /home/hjl/Academical/BBTree-RocksDB/test
make rocksdb_benchmark
popd > /dev/null