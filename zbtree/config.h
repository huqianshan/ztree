#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <immintrin.h>
#include <string.h>

#include <atomic>
#include <cassert>
#include <chrono>  // NOLINT
#include <cstdint>
#include <iostream>
#include <mutex>
#include <utility>  //std::pair

#include "tsc.h"

// #define UNITTEST_
#define READ_ONLY (true)
#define WRITE_FLAG (false)
// namespace BTree {
using u8 = uint8_t;
using u64 = uint64_t;
using u32 = uint32_t;
using i32 = int32_t;

// size of a data page in byte
#ifdef MULT_PAGE_SIZE
#define PAGE_SIZE (DSIZE)
#else
#define PAGE_SIZE (4096)
#endif

// config for buffer_btree
// how many leaf nodes can be kept in memory

#ifdef MULT_BUFFER_SIZE
#define MAX_BUFFER_LEAF_MB (DSIZE)
#else
#define MAX_BUFFER_LEAF_MB (2)
#endif

#ifdef MULT_FIFO_SIZE

#define MAX_FIFO_SIZE_MB (DSIZE)
#define MAX_READ_CACHE_MB (14 - DSIZE)

#else

#define MAX_FIFO_SIZE_MB (2)
#define MAX_READ_CACHE_MB ((int64_t)16 - MAX_BUFFER_LEAF_MB - MAX_FIFO_SIZE_MB)

#endif

// #define MULT_VALUE_SIZE
// #define DSIZE 2
#ifdef MULT_VALUE_SIZE

class ValueBase {
 private:
  /* data */
  u64 items[DSIZE];

 public:
  ValueBase(/* args */);
  ~ValueBase();

  ValueBase &operator=(const ValueBase &other) {
    // Guard self assignment
    if (this == &other) return *this;

    // std::copy(other.items, other.mArray + other.size, mArray);
    memcpy(items, other.items, sizeof(u64) * DSIZE);
    return *this;
  };

  friend std::ostream &operator<<(std::ostream &os, const ValueBase &other);
};
std::ostream &operator<<(std::ostream &os, const ValueBase &other) {
  for (int i = 0; i < DSIZE; i++) {
    os << other.items[i];
  }
  return os;
}

#define KeyType u64
#define ValueType ValueBase

#else
typedef u64 KeyType;
typedef u64 ValueType;
#endif

typedef char *bytes_t;
typedef u64 page_id_t;
typedef u64 offset_t;

typedef u64 zns_id_t;  // 16 bit for zone id, 48 bit for zone offset
typedef std::pair<KeyType, ValueType> PairType;
static constexpr page_id_t INVALID_PAGE_ID = -1;  // invalid page id

constexpr int32_t BATCH_SIZE = 4;
const int32_t MAX_RESEVER_THR = 1;
constexpr int MAX_DO_JOBS = 16;

constexpr int64_t MAX_KEEP_LEAF_MB = MAX_BUFFER_LEAF_MB / 2;
constexpr int64_t max_leaf_count =
    MAX_BUFFER_LEAF_MB * 1024ull * 1024 / PAGE_SIZE;
constexpr int64_t keep_leaf_count =
    MAX_KEEP_LEAF_MB * 1024ull * 1024 / PAGE_SIZE + 128 + 64;

static int _ = []() {
  static_assert(keep_leaf_count < max_leaf_count,
                "keep_leaf_count < max_leaf_count");
  return 0;
}();

const u32 MAX_BG_FLUSH_THREADS = 14;
#define MAX_NUMS_ZONE (14)
// #define NO_BUFFER_POOL
#define ZNS_BUFFER_POOL
// #define RAW_BUFFER_POOL

#define MAX_CACHED_PAGES_PER_ZONE \
  (MAX_FIFO_SIZE_MB * 1024 * 1024 / PAGE_SIZE / MAX_NUMS_ZONE)
#define MAX_READ_CACHE_PAGES \
  (MAX_READ_CACHE_MB * 1024 * 1024 / PAGE_SIZE / MAX_NUMS_ZONE)

// #define USE_THREAD_POOL
// #define USE_LRU_BUFFER
#define USE_SIEVE
#define BATCH_INSERT

#ifdef USE_LRU_BUFFER
#define LRU_BUFFER_SIZE MAX_READ_CACHE_PAGES
#endif
#define HASH_MAP_SIZE 149
#ifdef USE_SIEVE
#define SIEVE_SIZE MAX_READ_CACHE_PAGES
#endif

const u32 BUFFER_POOL_SIZE = 1024 * 1024 * 16;  // in Bytes
const u32 INSTANCE_SIZE = 64;
const u32 PAGES_SIZE = BUFFER_POOL_SIZE / (INSTANCE_SIZE * PAGE_SIZE);

/**
 * debug plot dot
 */
#define WORK_DIR "/home/hjl/Code/Academic/BBTree/build/"
const std::string DOTFILE_NAME_BEFORE = WORK_DIR "bbtree-before.dot";
const std::string DOTFILE_NAME_AFTER = WORK_DIR "bbtree-after.dot";

/**
 * stoage
 */
#define REGURLAR_DEVICE "/dev/nvme1n1"
#define ZNS_DEVICE "/dev/nvme0n2"
#define WT_ZNS_DEVICE "/dev/nvme1n2"  // wiredtiger
const u32 WAL_INSTANCE = 64;
const std::string WAL_NAME = "/data/public/hjl/bbtree/bbtree.wal";
const std::string SNAPSHOT_EXT = ".snp";
const std::string SNAPSHOT_PATH = "/data/public/hjl/bbtree/";
const std::string SNAPSHOT_PATH_BTREE = "/data/public/hjl/bbtree/btree/";
const std::string FILE_NAME = "/data/public/hjl/bbtree/f2fs_conv_ssd/bbtree.db";

const KeyType MIN_KEY = std::numeric_limits<KeyType>::min();
const ValueType INVALID_VALUE = std::numeric_limits<ValueType>::max();

#define GET_ZONE_ID(zid_) ((zid_) >> 48)
#define GET_ZONE_OFFSET(zid_) ((zid_) & 0x0000FFFFFFFFFFFF)
#define MAKE_PAGE_ID(zone_id, zone_offset) \
  (((zone_id) << 48) | ((zone_offset) & 0x0000FFFFFFFFFFFF))

const u32 SPIN_LIMIT = 6;
#define ATOMIC_SPIN_UNTIL(ptr, status)                    \
  u32 atomic_step_ = 0;                                   \
  while (ptr != status) {                                 \
    auto limit = 1 << std::min(atomic_step_, SPIN_LIMIT); \
    for (uint32_t v_ = 0; v_ < limit; v_++) {             \
      _mm_pause();                                        \
    };                                                    \
    atomic_step_++;                                       \
  }

enum NodeType { INNERNODE = 0, LEAFNODE, ROOTNODE, INVALIDNODE };
enum TreeOpType {
  TREE_OP_FIND = 0,
  TREE_OP_INSERT,
  TREE_OP_REMOVE,
  TREE_OP_UPDATE,
  TREE_OP_SCAN,
};

enum LatchMode {
  LATCH_MODE_READ = 0,
  LATCH_MODE_WRITE,
  LATCH_MODE_DELETE,
  LATCH_MODE_UPDATE,  // not supported
  LATCH_MODE_SCAN,    // not supported
  LATCH_MODE_NOP,     // not supported
};

enum PageStatus {
  ACTIVE = 0,
  EVICTED,
  FLUSHED,
};

class Node;
class InnerNode;
class LeafNode;

#ifndef COLOR_MACRO
#define COLOR_MACRO
#define KNRM "\x1B[0m"
#define KBOLD "\e[1m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"
#define KYEL_ITA_BOLD "\e[1;3;33m"
#define KBLK_GRN_ITA \
  "\033[3;30;42m"  // #"black green italic" = black text with green background,
                   // italic text
#define KBLK_RED_ITA \
  "\033[9;30;41m"  // #"black red strike" = black text with red background,
                   // strikethrough line through the text
#define KRESET "\033[0m"

// 1 = bold; 5 = slow blink; 31 = foreground color red
// 34 = foreground color blue
// See:
// https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_(Select_Graphic_Rendition)_parameters
#define COLOR_BOLD_SLOW_BLINKING "\e[1;5m"
#define COLOR_BOLD_SLOW_BLINKING_RED "\e[1;5;31m"
#define COLOR_BOLD_BLUE "\e[1;34m"
#endif

#define SAFE_DELETE(ptr) \
  do {                   \
    delete (ptr);        \
    (ptr) = nullptr;     \
  } while (0)

#define FATAL_PRINT(fmt, args...)                                          \
  fprintf(stderr, KRED "[%s:%d@%s()]: " fmt, __FILE__, __LINE__, __func__, \
          ##args);                                                         \
  fprintf(stderr, KRESET);                                                 \
  fflush(stderr);                                                          \
  exit(-1)

#define CHECK_OR_EXIT(ptr, msg) \
  do {                          \
    if ((ptr) == nullptr) {     \
      FATAL_PRINT(msg);         \
      exit(-1);                 \
    }                           \
  } while (0)

// Macros to disable copying and moving
#define DISALLOW_COPY(cname)     \
  cname(const cname &) = delete; \
  cname &operator=(const cname &) = delete;

#define DISALLOW_MOVE(cname) \
  cname(cname &&) = delete;  \
  cname &operator=(cname &&) = delete;

#define DISALLOW_COPY_AND_MOVE(cname) \
  DISALLOW_COPY(cname);               \
  DISALLOW_MOVE(cname);

#ifdef NDEBUG
#define VERIFY(expression) ((void)(expression))
#else
#define VERIFY(expression) assert(expression)
#endif

#ifdef DEBUG

#define INFO_PRINT(fmt, args...)
#define DEBUG_PRINT(fmt, args...)
// #define FATAL_PRINT(fmt, args...)

#else

#define INFO_PRINT(fmt, args...) \
  fprintf(stdout, fmt, ##args);  \
  fprintf(stdout, KRESET);       \
  fflush(stdout);

#define DEBUG_PRINT(fmt, args...)                                           \
  fprintf(stdout, KNRM "[%s:%d @%s()]: " fmt, __FILE__, __LINE__, __func__, \
          ##args);                                                          \
  fprintf(stdout, KRESET);                                                  \
  fflush(stdout);
#endif

// }  // namespace BTree
#endif