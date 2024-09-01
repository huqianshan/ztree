#positive read
# ./generator UNIFORM 200000000 200000000 0 8 1 0 0 0 PiBench1
# #negative read
# ./generator UNIFORM 200000000 200000000 0 8 1 0 0 1 PiBench2
# #insert
# ./generator UNIFORM 0 200000000 0 8 0 1 0 1 PiBench3
# # remove
# ./generator UNIFORM 200000000 200000000 0 8 0 0 1 0 PiBench4

# #positive read
# ./generator SELFSIMILAR 200000000 200000000 0.2 8 1 0 0 0 PiBench5
# #negative read
# ./generator SELFSIMILAR 200000000 200000000 0.2 8 1 0 0 1 PiBench6
# #insert
# ./generator SELFSIMILAR 0 200000000 0.2 8 0 1 0 0 PiBench7
# #remove
# ./generator SELFSIMILAR 200000000 200000000 0.2 8 0 0 1 0 PiBench8

arrs=(0 1 2 3 4 5)
workloads=(a b c d e f)
skewness=(0.5 0.6 0.7 0.8 0.9 0.99)
load_size=10000000
run_size=10000000
for i in ${arrs[@]}; do
    ./generator ZIPFIAN ${load_size} ${run_size} ${skewness[$i]} 8 0.8 0.2 0 0 zipfian-${workloads[$i]}
    mv zipfian-${workloads[$i]}.load ./workloads/zipfian_load_workload${workloads[$i]}
    mv zipfian-${workloads[$i]}.run ./workloads/zipfian_run_workload${workloads[$i]}
done
