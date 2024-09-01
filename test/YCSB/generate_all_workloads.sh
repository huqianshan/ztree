#!/bin/bash

KEY_TYPE=randint
uniform_workloads=(a b c d e f g h i j k)
# zipf_workloads=(f g h i j)
for WORKLOAD_TYPE in ${uniform_workloads[@]}; do
  echo "----------${WORKLOAD_TYPE}----------\n"
  echo workload${WORKLOAD_TYPE} >workload_config.inp
  echo ${KEY_TYPE} >>workload_config.inp
  python2 gen_workload.py workload_config.inp
done
