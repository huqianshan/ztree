#/bin/bash
TREE_DIR=./build/test/
TREE_BASE=f2fsbtree
TREE_EXE=${TREE_DIR}${TREE_BASE}
LOG_DIR=./test/result/
WORKLOAD=ycsbd
THREAD=56
NUMS=4
rm_color="s/\x1B\[[0-9;]*[a-zA-Z]//g"
MODE=$1
# latency
if [[ ${MODE} == 'latency' ]]; then
    echo "latency"
    WORKLOADS=(ycsba ycsbd) # ycsbb ycsbc
    #  wired tiger should exected in test dir and modify the LOG_DIR to ../test/result
    FORESTS=(./build/test/ztree ./build/test/convbtree ./build/test/cowbtree ./build/test/f2fsbtree) #    ./wiredtiger_benchmark ./test/rocksdb_benchmark
    THREAD=50
    NUMS=4
    for WORKLOAD in ${WORKLOADS[@]}; do
        OUTPUT_FILE=${LOG_DIR}${WORKLOAD}-latency-2.log
        for TREE_EXE in ${FORESTS[@]}; do
            # echo
            sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
        done
        awk '/LatencyValue:/ {sub(/.*LatencyValue:/, "", $0); print $0}' ${OUTPUT_FILE}
    done
    # sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
    # awk '/LatencyValue:/ {printf "%s,", $2}' ${OUTPUT_FILE}
elif [[ ${MODE} == 'breakdown' ]]; then
    echo "breakdown"
    WORKLOAD=ycsbd
    OUTPUT_FILE=${LOG_DIR}breakdown.log
    # >OUTPUT_FILE
    # TREE_EXE=./build/test/$2
    sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
elif [[ ${MODE} == 'skewed' ]]; then
    echo "skewed ${2}"
    WORKLOADS=(zipfa zipfb zipfc zipfd zipfe zipff)
    NUMS=(8)
    FORESTS=(ztree f2fsbtree cowbtree convbtree)
    for TREE_EXE in ${FORESTS[@]}; do
        OUTPUT_FILE=${LOG_DIR}skewed-${TREE_EXE}.log
        for WORKLOAD in ${WORKLOADS[@]}; do
            for NUM in ${NUMS[@]}; do
                sudo numactl -N 1 ${TREE_DIR}${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUM} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
            done
        done
        #  run iops
        awk '/run,/ {printf "%s,", $3}' ${OUTPUT_FILE}
        # zone buffer pool hit ratio
        awk '/ZoneBufferPool] count/ {printf "%s,", $9}' ${OUTPUT_FILE}
    done
    echo ""
elif [[ ${MODE} == 'fifosize' ]]; then
    echo "fifosize ${2}"
    SIZES=(1 2 4 6 8 10 12)
    OUTPUT_FILE=${LOG_DIR}fifosize-${2}.log
    WORKLOAD=ycsbd
    # TYPE=MULT_FIFO_SIZE

    for SIZE in ${SIZES[@]}; do
        cmake -B ./build -DCMAKE_BUILD_TYPE=Release -DMULT_FIFO_SIZE=on -DTNUM=${SIZE} ./ && cd build && make ztree -j && cd ..
        sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
    done
    #  run iops
    awk '/run,/ {printf "%s,", $3}' ${OUTPUT_FILE}
elif [[ ${MODE} == 'varlen' ]]; then
    echo "varlen ${2}"
    SIZES=(8 16 32 64 128 256 512)
    OUTPUT_FILE=${LOG_DIR}varlen-${2}.log
    WORKLOAD=ycsbd
    NUMS=4

    for SIZE in ${SIZES[@]}; do
        cmake -B ./build -DCMAKE_BUILD_TYPE=Release -DMULT_VALUE_SIZE=on -DTNUM=${SIZE} ./ && cd build && make ztree -j && cd ..
        sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
    done
    awk '/load,/ {printf "%s,", $3}' ${OUTPUT_FILE}
    awk '/run,/ {printf "%s,", $3}' ${OUTPUT_FILE}
elif [[ ${MODE} == 'mergedfilter' ]]; then
    echo "mergedfilter ${2}"
    SIZES=(2 8 16 24 32 40 48 56 64)
    OUTPUT_FILE=${LOG_DIR}mergedfilter-${2}.log
    WORKLOAD=ycsba
    NUMS=4

    for SIZE in ${SIZES[@]}; do
        cmake -B ./build -DCMAKE_BUILD_TYPE=Release -DMULT_BUFFER_SIZE=on -DTNUM=${SIZE} ./ && cd build && make ztree -j && cd ..
        sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
    done
    awk '/load,/ {printf "%s,", $3}' ${OUTPUT_FILE}
    awk '/run,/ {printf "%s,", $3}' ${OUTPUT_FILE}
elif [[ ${MODE} == 'leafsize' ]]; then
    echo "leafsize ${2}"
    SIZES=(4096 8192 16384 32768)
    OUTPUT_FILE=${LOG_DIR}leafsize-${TREE_BASE}.log
    WORKLOAD=ycsba

    OUTPUT_FILE=${LOG_DIR}leafsize-${TREE_BASE}-1.log
    for SIZE in ${SIZES[@]}; do
        cmake -B ./build -DCMAKE_BUILD_TYPE=Release -DMULT_PAGE_SIZE=on -DTNUM=${SIZE} ./ && cd build && make ${TREE_BASE} -j && cd ..
        sudo numactl -N 1 ${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUMS} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
    done
    echo "${TREE_EXE}.${2} iops and write bytes per op:"
    awk '/run,/ {printf "%s,", $3}' ${OUTPUT_FILE}
    #  wa
    awk '/nvme0n2/ {printf "%s,", $12}' ${OUTPUT_FILE}
elif [[ ${MODE} == 'datasize' ]]; then
    echo "size"
    WORKLOAD=ycsba
    NUMS=(1 2 4 8 16 32 64)
    FORESTS=(ztree f2fsbtree)
    for TREE_EXE in ${FORESTS[@]}; do
        OUTPUT_FILE=${LOG_DIR}datasize-${TREE_EXE}.log
        for NUM in ${NUMS[@]}; do
            sudo numactl -N 1 ${TREE_DIR}${TREE_EXE} ${WORKLOAD} ${THREAD} ${NUM} 2>&1 | sed -r ${rm_color} | tee -a ${OUTPUT_FILE}
        done
        echo "${TREE_EXE}.${2} iops and write bytes per op:"
        #  wa
        awk '/nvme0n2/ {printf "%s,", $12}' ${OUTPUT_FILE}
        # iops
        awk '/run,/ {printf "%s,", $3}' ${OUTPUT_FILE}
    done
fi
