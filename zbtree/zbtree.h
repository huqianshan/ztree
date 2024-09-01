#pragma once

#include <immintrin.h>
#include <sched.h>

#include <atomic>
#include <cassert>
#include <cstring>
#include <fstream>
#include <stack>
#include <utility>

#include "buffer.h"
#include "config.h"
#include "page.h"

namespace btreeolc {

static const uint64_t InnerNodeSize = 4096;
// static const uint64_t LeafNodeSize = 4096;
static const uint64_t LeafNodeSize = (PAGE_SIZE);

using KeyValueType = PairType;
using Key = KeyType;
using Value = ValueType;

enum class PageType : uint8_t { BTreeInner = 1, BTreeLeaf = 2 };

static const uint64_t BaseNodeSize = 24;
static const uint64_t CacheLineSize = 64;

// This is the element numbers of the leaf node
static const uint64_t LeafNodeMaxEntries =
    (LeafNodeSize - BaseNodeSize - sizeof(uint64_t) - sizeof(void *)) /
    (sizeof(KeyValueType));

static const uint64_t InnerNodeMaxEntries =
    (InnerNodeSize - BaseNodeSize) / (sizeof(Key) + sizeof(void *)) - 2;

struct OptLock {
  std::atomic<uint64_t> typeVersionLockObsolete{0b100};

  bool isLocked(uint64_t version) { return ((version & 0b10) == 0b10); }

  uint64_t readLockOrRestart(bool &needRestart) {
    uint64_t version;
    version = typeVersionLockObsolete.load();
    if (isLocked(version) || isObsolete(version)) {
      _mm_pause();
      needRestart = true;
    }
    return version;
  }

  void writeLockOrRestart(bool &needRestart) {
    uint64_t version;
    version = readLockOrRestart(needRestart);
    if (needRestart) return;

    upgradeToWriteLockOrRestart(version, needRestart);
    if (needRestart) return;
  }

  void upgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart) {
    if (typeVersionLockObsolete.compare_exchange_strong(version,
                                                        version + 0b10)) {
      version = version + 0b10;
    } else {
      _mm_pause();
      needRestart = true;
    }
  }

  void writeUnlock() { typeVersionLockObsolete.fetch_add(0b10); }

  bool isObsolete(uint64_t version) { return (version & 1) == 1; }

  void checkOrRestart(uint64_t startRead, bool &needRestart) const {
    readUnlockOrRestart(startRead, needRestart);
  }

  void readUnlockOrRestart(uint64_t startRead, bool &needRestart) const {
    needRestart = (startRead != typeVersionLockObsolete.load());
  }

  void writeUnlockObsolete() { typeVersionLockObsolete.fetch_add(0b11); }
};

struct NodeBase : public OptLock {
  PageType type;
  uint16_t count;
  virtual ~NodeBase() = default;
};

struct BTreeLeafBase : public NodeBase {
  static const PageType typeMarker = PageType::BTreeLeaf;
};

// template <class Key, class Payload>

struct BTreeLeaf : public BTreeLeafBase {
  page_id_t page_id;
  // This is the array that we perform search on
  KeyValueType *data = nullptr;

  BTreeLeaf();

  bool isFull();

  unsigned lowerBound(Key k);

  /**
   * @brief
   *
   * @param k
   * @param p
   * @return true insert
   * @return false  update
   */
  bool insert(Key k, Value p);

  int BatchInsert(Key *keys, Value *values, int num);

  void Init(uint16_t num = 0, page_id_t id = 0);

  BTreeLeaf *split(Key &sep, void *bpm);
  BTreeLeaf *splitFrom(Key &sep, void *bpm, page_id_t from);

  void Print(void *bpm);

  void ToGraph(std::ofstream &out, ParallelBufferPoolManager *bpm);
  virtual ~BTreeLeaf() {
    // delete[] data;
    data = nullptr;
  }

  void ToGraph(std::ofstream &out, void *bpm);
};

struct BTreeInnerBase : public NodeBase {
  static const PageType typeMarker = PageType::BTreeInner;
};

/**
 * @brief * Store n indexed keys and n+1 child pointers (page_id_) within
 * internal page. Pointer PAGE_ID(i) points to a subtree in which all keys K
 * satisfy: K(i-1) <= K < K(i) for i > 0  K<= K(0) for i = 0
 * @note: since the number of keys does not equal to
 * number of child pointers, the **last** key always remains invalid.
 * That is to say, any search/lookup should ignore the last key.
 * k1 k2 k3 ... kn-1 kn NULL
 * p1 p2 p3 ... pn-1 pn pn+1
 * count always equals to number of keys
 */
struct alignas(CacheLineSize) BTreeInner : public BTreeInnerBase {
  // split first so add 2, no doubt that the last key is invalid
  NodeBase *children[InnerNodeMaxEntries + 2];
  Key keys[InnerNodeMaxEntries + 2];

  BTreeInner();

  // ensure correct when maxEntries is even: 3,5
  bool isFull();

  unsigned lowerBoundBF(Key k);

  unsigned lowerBound(Key k);

  BTreeInner *split(Key &sep);

  void insert(Key k, NodeBase *child);

  void Print(void *bpm);

  void ToGraph(std::ofstream &out, ParallelBufferPoolManager *bpm);
  virtual ~BTreeInner() {
    for (int i = 0; i <= count; i++) {
      delete children[i];
      // children[i] = nullptr;
    }
  }

  void ToGraph(std::ofstream &out, void *bpm);
};
// template <class Key, class Value>
struct alignas(CacheLineSize) BTree {
  std::atomic<NodeBase *> root;
  void *bpm;

  BTree(void *buffer);

  virtual ~BTree();

  bool IsEmpty() const;

  void makeRoot(Key k, NodeBase *leftChild, NodeBase *rightChild);

  void yield(int count);

  bool Insert(Key k, Value v);

  void BatchInsert(Key *keys, Value *values, int num);

  bool Get(Key k, Value &result);

  uint64_t Scan(Key k, int range, Value *output);

  void DestroyNode();

  void Print() const;

  void GetNodeNums() const;

  void ToGraph(std::ofstream &out) const;

  void Draw(std::string path) const;
  void FlushAll() const {};
};

/*
 * Author: chenbo
 * Time: 2024-03-20 08:39:46
 * Description: 自动释放内存
 */
struct KVHolder {
  Key *keys;
  Value *values;
  int num;
  explicit KVHolder(Key *k, Value *v, int n) : keys(k), values(v), num(n) {}
  ~KVHolder() {
    delete[] keys;
    delete[] values;
    // keys = nullptr;
    // values = nullptr;
  }
};

}  // namespace btreeolc