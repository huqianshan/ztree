#/bin/bash
#### run in root directory and build btree-olc
log_dir=./test/result/ycsb-3/
exe_dir=./build/test/

tree_list=(ztree f2fsbtree convbtree cowbtree)
bench_list=(ycsba ycsbb ycsbc ycsbd ycsbe ycsbf ycsbg ycsbh ycsbi ycsbj) # ycsbde)
thread_list=(1 8 16 24 32 40 48 56) #
# maxium ops
threshold_list=(4)

test_name=$1
clean=$2

# rm_color="s/\x1B\[([0-9]{1,3}(;[0-9]{1,2};?)?)?[mGK]//g"
rm_color="s/\x1B\[[0-9;]*[a-zA-Z]//g"
for t in ${tree_list[@]}; do
    for load in ${bench_list[@]}; do
        rlog=${log_dir}${test_name}-${t}-${load}.log
        if [[ "$clean" == "clean" ]]; then
            rm -f ${rlog}
        fi
        # 如果rlog文件不存在，则创建它
        if [ ! -f ${rlog} ]; then
            touch ${rlog}
        fi
        for tid in ${thread_list[@]}; do
            for threshold in ${threshold_list[@]}; do
                # echo "------------------------------------------------" >>${rlog}
                echo "" >>${rlog}
                numactl -N 1 sudo ${exe_dir}${t} ${load} ${tid} ${threshold} 2>&1 | sed -r ${rm_color} | tee -a ${rlog}
                echo "" >>${rlog}
                # echo "----------------------------------------\n\n" >>${rlog}
                # echo "------------------------------------------------\n"
            done
            # sudo ${exe_dir}zns_test reset 0 14
        done
    done
done
