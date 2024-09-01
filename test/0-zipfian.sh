#/bin/bash
#### run in root directory and build btree-olc
log_dir=./test/result/zipfian/
exe_dir=./build/test/

tree_list=("btree-olc")
# bench_list=(ycsba ycsbb ycsbc ycsbd ycsbe ycsbf ycsbg ycsbh ycsbi ycsbj) # ycsbde)
bench_list=(zipfian-a zipfian-b zipfian-c zipfian-d zipfian-e zipfian-f) # ycsbde)
thread_list=(48)                                                         #
# maxium ops
threshold_list=(10)

test_name=$1
clean=$2

# rm_color="s/\x1B\[([0-9]{1,3}(;[0-9]{1,2};?)?)?[mGK]//g"
rm_color="s/\x1B\[[0-9;]*[a-zA-Z]//g"
for t in ${tree_list[@]}; do
    rlog=${log_dir}${t}-${test_name}.log
    if [[ "$clean" == "clean" ]]; then
        rm -f ${rlog}
    fi
    # 如果rlog文件不存在，则创建它
    if [ ! -f ${rlog} ]; then
        touch ${rlog}
    fi
    for load in ${bench_list[@]}; do
        for tid in ${thread_list[@]}; do
            for threshold in ${threshold_list[@]}; do
                echo "------------------------------------------------" >>${rlog}
                numactl -N 1 sudo ${exe_dir}${t} ${load} ${tid} ${threshold} 2>&1 | sed -r ${rm_color} | tee -a ${rlog}
                echo "----------------------------------------\n\n" >>${rlog}
                echo "------------------------------------------------\n"
            done
        done
    done
    # sudo ${exe_dir}zns_test reset 0 14
done

DIR=test/result/zipfian/
CSV_FILE=""
for file in "$DIR"/*.csv; do
    # clear contents in csv
    if [[ -f ${file} ]]; then
        >${file}
        echo ${file}
    fi
done

for file in "$DIR"/*.log; do
    echo "Processing $file"
    if [[ $file == *"conv"* ]]; then
        TREE_NAME=0-conv-btree
    elif [[ $file == *"f2fs"* ]]; then
        TREE_NAME=1-f2fs-btree
    elif [[ $file == *"cow"* ]]; then
        TREE_NAME=2-cow-btree
    elif [[ $file == *"zbtree"* ]]; then
        TREE_NAME=3-zbtree
    fi
    CSV_FILE=${DIR}${TREE_NAME}-ycsb.csv
    file_log=${file##*-}
    awk_output=$(awk '/run,/ {printf "%s,", $3}' ${file})
    echo "${file_log%.*},${awk_output}" | tee -a ${CSV_FILE}
done
