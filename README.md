# BBTree

## Pre

`sudo apt-get install -y libzbd-dev libjemalloc-dev libgflags-dev`

## Generate YCSB workloads

```bash
cd test/YCSB
wget https://github.com/brianfrankcooper/YCSB/releases/download/0.17.0/ycsb-0.17.0.tar.gz
tar -xvf ycsb-0.17.0.tar.gz
mv ycsb-0.17.0 YCSB
#Then run workload generator
mkdir workloads
bash ./generate_all_workloads.sh
```

## Generate Zipfian workloads
```bash
cd PiBench
make
./auto_gene.sh
```

## Compiler

```
see ./test/CMakeLists.txt
Sources File: ./test/benchmark.cpp and ./zbtree/config.h
```

## format

```zsh
find . \( -iname "*.h" -o -iname "*.cpp" \) -print0 | xargs -0 clang-format -i -style=file
```
[clang-format-Ref](https://juejin.cn/post/7252500978556649528)