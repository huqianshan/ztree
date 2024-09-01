# 定义日志文件路径和csv文件路径
# DIR=./test/result/log/
# DEVICE=nvme4n1
# NAME=3-trim-rand-${DEVICE}
# MODE=ycsb
# # MODE=smart
# BS=4k
# LOG_FILE=${DIR}${NAME}-${MODE}-${BS}.log
# CSV_FILE=${DIR}${NAME}-${MODE}-${BS}.csv

# if [[ "${MODE}" == "iostat" ]]; then
#     # 提取日志文件中的最后一列数字，并输出到csv文件
#     awk '/nvme4n1/{bd=$9; print bd}' $LOG_FILE >$CSV_FILE
# elif [[ "${MODE}" == "smart" ]]; then
#     # 提取日志文件中的最后一列数字，并输出到csv文件
#     awk '/nand_bytes_written/{nand=$NF; getline; if ($0 ~ /host_bytes_written/) print nand","$NF}' $LOG_FILE >$CSV_FILE
# fi

DIR=test/result/ycsb-3/
CSV_FILE=""
for file in "$DIR"*.csv; do
    # clear contents in csv
    if [[ -f ${file} ]]; then
        >${file}
        echo ${file}
    fi
done

for file in "$DIR"*.log; do
    echo "Processing $file"
    if [[ $file == *"conv"* ]]; then
        TREE_NAME=0-conv-btree
    elif [[ $file == *"f2fs"* ]]; then
        TREE_NAME=1-f2fs-btree
    elif [[ $file == *"cow"* ]]; then
        TREE_NAME=2-cow-btree
    elif [[ $file == *"ztree"* ]]; then
        TREE_NAME=3-ztree
    else
        continue
    fi
    CSV_FILE=${DIR}${TREE_NAME}-ycsb.csv
    file_log=${file##*-}
    awk_output=$(awk '/run,/ {printf "%s,", $3}' ${file})
    echo "${file_log%.*},${awk_output::-1}" | tee -a ${CSV_FILE}
done
