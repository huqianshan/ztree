cmake_minimum_required(VERSION 3.14)
project(TestBBTree)

include_directories(../zbtree)
include_directories(../zns)
file(GLOB ZBTREE_CPPS "../zbtree/*.cpp")
file(GLOB ZNS_CPPS "../zns/*.cpp")

# set(CMAKE_CXX_FLAGS "-std=c++2a ")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mavx512f -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g3 -march=native") # -fsanitize=leak,address,undefined ")

include(FetchContent)
FetchContent_Declare(
  googletest
  # Specify the commit you depend on and update it regularly.
  URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(UnitTests insert_delete_test.cpp ${ZBTREE_CPPS} ${ZNS_CPPS})
target_link_libraries(UnitTests gtest_main pthread zbd)
add_test(NAME runUnitTests COMMAND UnitTests)
target_compile_definitions(UnitTests PRIVATE UNITTEST_)




add_executable(ztree benchmark.cpp ${ZBTREE_CPPS} ${ZNS_CPPS})
target_link_libraries(ztree  jemalloc pthread  zbd ${Boost_LIBRARIES})

if("${MULT_PAGE_SIZE}" STREQUAL on)
target_compile_definitions(ztree PRIVATE MULT_PAGE_SIZE)
target_compile_definitions(ztree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_BUFFER_SIZE}" STREQUAL on)
target_compile_definitions(ztree PRIVATE MULT_BUFFER_SIZE)
target_compile_definitions(ztree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_FIFO_SIZE}" STREQUAL on)
target_compile_definitions(ztree PRIVATE MULT_FIFO_SIZE)
target_compile_definitions(ztree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_VALUE_SIZE}" STREQUAL on)
target_compile_definitions(ztree PRIVATE MULT_VALUE_SIZE)
target_compile_definitions(ztree PRIVATE -DDSIZE=${TNUM})
endif()


add_executable(cowbtree benchmark.cpp ${ZBTREE_CPPS} ${ZNS_CPPS})
target_link_libraries(cowbtree   jemalloc pthread  zbd ${Boost_LIBRARIES})

if("${MULT_PAGE_SIZE}" STREQUAL on)
target_compile_definitions(cowbtree PRIVATE MULT_PAGE_SIZE)
target_compile_definitions(cowbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_BUFFER_SIZE}" STREQUAL on)
target_compile_definitions(cowbtree PRIVATE MULT_BUFFER_SIZE)
target_compile_definitions(cowbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_FIFO_SIZE}" STREQUAL on)
target_compile_definitions(cowbtree PRIVATE MULT_FIFO_SIZE)
target_compile_definitions(cowbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_VALUE_SIZE}" STREQUAL on)
target_compile_definitions(cowbtree PRIVATE MULT_VALUE_SIZE)
target_compile_definitions(cowbtree PRIVATE -DDSIZE=${TNUM})
endif()


add_executable(f2fsbtree benchmark.cpp ${ZBTREE_CPPS} ${ZNS_CPPS})
target_link_libraries(f2fsbtree   jemalloc pthread  zbd ${Boost_LIBRARIES})

if("${MULT_PAGE_SIZE}" STREQUAL on)
target_compile_definitions(f2fsbtree PRIVATE MULT_PAGE_SIZE)
target_compile_definitions(f2fsbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_BUFFER_SIZE}" STREQUAL on)
target_compile_definitions(f2fsbtree PRIVATE MULT_BUFFER_SIZE)
target_compile_definitions(f2fsbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_FIFO_SIZE}" STREQUAL on)
target_compile_definitions(f2fsbtree PRIVATE MULT_FIFO_SIZE)
target_compile_definitions(f2fsbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_VALUE_SIZE}" STREQUAL on)
target_compile_definitions(f2fsbtree PRIVATE MULT_VALUE_SIZE)
target_compile_definitions(f2fsbtree PRIVATE -DDSIZE=${TNUM})
endif()


add_executable(convbtree benchmark.cpp ${ZBTREE_CPPS} ${ZNS_CPPS})
target_link_libraries(convbtree   jemalloc pthread  zbd ${Boost_LIBRARIES})

if("${MULT_PAGE_SIZE}" STREQUAL on)
target_compile_definitions(convbtree PRIVATE MULT_PAGE_SIZE)
target_compile_definitions(convbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_BUFFER_SIZE}" STREQUAL on)
target_compile_definitions(convbtree PRIVATE MULT_BUFFER_SIZE)
target_compile_definitions(convbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_FIFO_SIZE}" STREQUAL on)
target_compile_definitions(convbtree PRIVATE MULT_FIFO_SIZE)
target_compile_definitions(convbtree PRIVATE -DDSIZE=${TNUM})
endif()

if("${MULT_VALUE_SIZE}" STREQUAL on)
target_compile_definitions(convbtree PRIVATE MULT_VALUE_SIZE)
target_compile_definitions(convbtree PRIVATE -DDSIZE=${TNUM})
endif()
# 宏定义修改
# if("${ZBTREE}" STREQUAL on)
# target_compile_definitions(zbtree PRIVATE ZBTREE_ON_ZNS)
# endif()


# add_library(rocksdb_static_lib STATIC IMPORTED)
# set_target_properties(rocksdb_static_lib PROPERTIES
#   IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/librocksdb.a
# )
# add_executable(rocksdb_zen rocksdb_benchmark.cpp ${ZBTREE_CPPS} ${ZNS_CPPS} ${ROCKSDB_LIB})
# target_compile_options(rocksdb_zen PRIVATE -O2 -faligned-new -DHAVE_ALIGNED_NEW -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_IOURING_PRESENT -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DNUMA -DTBB -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -DROCKSDB_AUXV_GETAUXVAL_PRESENT -march=native -DHAVE_UINT128_EXTENSION)
# set(ROCKSDB_LIB jemalloc pthread rt uring snappy gflags z bz2 lz4 zstd numa tbb zbd dl)
# target_link_libraries(rocksdb_zen
#         rocksdb_static_lib ${ROCKSDB_LIB})
        


add_executable(zns_test ztest.cpp ${ZNS_CPPS} )
target_link_libraries(zns_test
        jemalloc
        pthread
        zbd)

add_executable(recovery_test benchmark_rec.cpp ${ZBTREE_CPPS} ${ZNS_CPPS})
target_link_libraries(recovery_test jemalloc pthread  zbd ${Boost_LIBRARIES})
        


# add_executable(smart smart.cpp )
