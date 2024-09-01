#/bin/bash

PERF=perf
FLAME=/home/hjl/Tools/FlameGraph/
# sudo sysctl kernel.perf_event_paranoid=-1
${PERF} record -g -F 1000 "$@"
${PERF} script >tmp.perf
${FLAME}stackcollapse-perf.pl tmp.perf >tmp.folded
${FLAME}flamegraph.pl tmp.folded >graph_perf"_"${2}"_"${3}"trds_"${4}"M".svg
rm tmp.perf tmp.folded perf.data
