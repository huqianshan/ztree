CXX=g++
PLATFORM_LDFLAGS= -lpthread -lrt -ldl -luring -lsnappy -lgflags -lz -lbz2 -llz4 -lzstd -lnuma -ltbb
PLATFORM_CXXFLAGS=-std=c++17 -faligned-new -DHAVE_ALIGNED_NEW -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_IOURING_PRESENT -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DNUMA -DTBB -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -DROCKSDB_AUXV_GETAUXVAL_PRESENT -march=native -DHAVE_UINT128_EXTENSION
EXEC_LDFLAGS=-ldl

.PHONY: clean librocksdb

all: rocksdb_benchmark wiredtiger_benchmark

rocksdb_benchmark: librocksdb rocksdb_benchmark.cpp bench_util.h
	$(CXX) $(CXXFLAGS) $@.cpp -o $@ ../rocksdb/librocksdb.a -O2 $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS) -lzbd

wiredtiger_benchmark: wiredtiger_benchmark.cpp ./wiredtiger/wt_util.h ../zbtree/config.h bench_util.h
	$(CXX) -std=c++17 $@.cpp -o $@ -O2  -lpthread  -lwiredtiger


clean:
	rm -rf ./rocksdb_benchmark ./wiredtiger_benchmark
# g++ -fno-rtti rocksdb_benchmark.cpp -o rocksdb_zen ../rocksdb/librocksdb.a -O2 -std=c++17 -lpthread -lrt -ldl -luring -lsnappy -lgflags -lz -lbz2 -llz4 -lzstd -lnuma -ltbb -std=c++17 -faligned-new -DHAVE_ALIGNED_NEW -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_IOURING_PRESENT -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DNUMA -DTBB -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -DROCKSDB_AUXV_GETAUXVAL_PRESENT -march=native -DHAVE_UINT128_EXTENSION -DROCKSDB_JEMALLOC -DJEMALLOC_NO_DEMANGLE -ldl -lpthread -lzbd
# librocksdb: 
	# cd ../rocksdb && DEBUG_LEVEL=0 ROCKSDB_PLUGINS=zenfs $(MAKE) -j static_lib