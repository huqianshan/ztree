#ifndef BUFFER_BTREE_H
#define BUFFER_BTREE_H

#include <immintrin.h>
#include <sched.h>

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_set>

#include "thread_pool.h"
#include "wal.h"
#include "work_queue.h"
#include "zbtree.h"
extern int _num_threads;

// #define DEBUG_BUFFER

namespace btreeolc {
namespace buffer_btree {

enum class PageType : uint8_t { BTreeInner = 1, BTreeLeaf = 2 };

static const uint64_t pageSize = 4 * 1024;
#define LEAF_DELETED_FLAG ((uint32_t)(-1))
struct OptLock {
  // std::atomic<uint64_t> typeVersionLockObsolete{0b100};
  std::atomic_uint64_t typeVersionLockObsolete{0b100};
  // alignas(CacheLineSize) std::atomic<uint64_t> typeVersionLockObsolete;

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
  virtual ~NodeBase() {}
};

struct BTreeLeafBase : public NodeBase {
  static const PageType typeMarker = PageType::BTreeLeaf;
};

template <class Key, class Payload>
struct BTreeLeaf : public BTreeLeafBase {
  struct Entry {
    Key k;
    Payload p;
  };

  static const uint64_t maxEntries =
      // pageSize / (sizeof(Key) + sizeof(Payload));
      (pageSize - sizeof(NodeBase) - sizeof(Key) - sizeof(Payload) -
       sizeof(uint32_t)) /
      (sizeof(Key) + sizeof(Payload));

  // Key keys[maxEntries];
  // Payload payloads[maxEntries];
  // use pointer instead of array
  Key *keys;
  Payload *payloads;
  uint32_t access_count;

  BTreeLeaf() {
    count = 0;
    type = typeMarker;
    access_count = 0;
    keys = nullptr;
    payloads = nullptr;
  }

  bool isFull() { return count == maxEntries; };

  unsigned lowerBound(Key k) {
    unsigned lower = 0;
    unsigned upper = count;
    do {
      unsigned mid = ((upper - lower) / 2) + lower;
      if (k < keys[mid]) {
        upper = mid;
      } else if (k > keys[mid]) {
        lower = mid + 1;
      } else {
        return mid;
      }
    } while (lower < upper);
    return lower;
  }

  void insert(Key k, Payload p) {
    assert(count < maxEntries);
    if (access_count < std::numeric_limits<uint32_t>::max()) access_count++;
    if (count > 0) {
      unsigned pos = lowerBound(k);
      if ((pos < count) && (keys[pos] == k)) {
        // Upsert
        payloads[pos] = p;
        return;
      }
      memmove(keys + pos + 1, keys + pos, sizeof(Key) * (count - pos));
      memmove(payloads + pos + 1, payloads + pos,
              sizeof(Payload) * (count - pos));
      keys[pos] = k;
      payloads[pos] = p;
    } else {
      if (keys == nullptr || payloads == nullptr) {
        assert(keys == nullptr && payloads == nullptr);
        keys = new Key[maxEntries];
        payloads = new Payload[maxEntries];
        count = 0;
      }
      keys[0] = k;
      payloads[0] = p;
    }
    count++;
  }

  void deleteNode() {
    delete[] keys;
    delete[] payloads;
    keys = nullptr;
    payloads = nullptr;
    count = 0;
    access_count = 0;
  }

  BTreeLeaf *split(Key &sep) {
    BTreeLeaf *newLeaf = new BTreeLeaf();
    newLeaf->count = count - (count / 2);
    newLeaf->keys = new Key[maxEntries];
    newLeaf->payloads = new Payload[maxEntries];
    count = count - newLeaf->count;
    memcpy(newLeaf->keys, keys + count, sizeof(Key) * newLeaf->count);
    memcpy(newLeaf->payloads, payloads + count,
           sizeof(Payload) * newLeaf->count);
    sep = keys[count - 1];
    return newLeaf;
  }
  virtual ~BTreeLeaf() {
    delete[] keys;
    delete[] payloads;
    // keys = nullptr;
    // payloads = nullptr;
    count = 0;
    access_count = 0;
  }
};

struct BTreeInnerBase : public NodeBase {
  static const PageType typeMarker = PageType::BTreeInner;
};

template <class Key>
struct BTreeInner : public BTreeInnerBase {
  static const uint64_t maxEntries =
      (pageSize - sizeof(NodeBase)) / (sizeof(Key) + sizeof(NodeBase *));
  NodeBase *children[maxEntries];
  Key keys[maxEntries];

  BTreeInner() {
    count = 0;
    type = typeMarker;
  }

  bool isFull() { return count == (maxEntries - 1); };

  unsigned lowerBoundBF(Key k) {
    auto base = keys;
    unsigned n = count;
    while (n > 1) {
      const unsigned half = n / 2;
      base = (base[half] < k) ? (base + half) : base;
      n -= half;
    }
    return (*base < k) + base - keys;
  }

  unsigned lowerBound(Key k) {
    unsigned lower = 0;
    unsigned upper = count;
    do {
      unsigned mid = ((upper - lower) / 2) + lower;
      if (k < keys[mid]) {
        upper = mid;
      } else if (k > keys[mid]) {
        lower = mid + 1;
      } else {
        return mid;
      }
    } while (lower < upper);
    return lower;
  }

  BTreeInner *split(Key &sep) {
    BTreeInner *newInner = new BTreeInner();
    newInner->count = count - (count / 2);
    count = count - newInner->count - 1;
    sep = keys[count];
    memcpy(newInner->keys, keys + count + 1,
           sizeof(Key) * (newInner->count + 1));
    memcpy(newInner->children, children + count + 1,
           sizeof(NodeBase *) * (newInner->count + 1));
    return newInner;
  }

  void insert(Key k, NodeBase *child) {
    assert(count < maxEntries - 1);
    unsigned pos = lowerBound(k);
    memmove(keys + pos + 1, keys + pos, sizeof(Key) * (count - pos + 1));
    memmove(children + pos + 1, children + pos,
            sizeof(NodeBase *) * (count - pos + 1));
    keys[pos] = k;
    children[pos] = child;
    std::swap(children[pos], children[pos + 1]);
    count++;
  }

  void remove_leaf(NodeBase *leaf_ptr) {
    // 1.1 find the pos of leaf_ptr in children[];
    // cannot use binary search due to unsorted ptr
    size_t pos = 0;
    for (; pos <= count; pos++) {
      if (children[pos] == leaf_ptr) {
        break;
      }
    }
    if (pos < count) {
      // 1.2 move the others
      memmove(keys + pos, keys + pos + 1, sizeof(Key) * (count - pos));
      memmove(children + pos, children + pos + 1,
              sizeof(NodeBase *) * (count - pos));
    }
    leaf_ptr->writeUnlock();
    count--;
    SAFE_DELETE(leaf_ptr);
  }

  virtual ~BTreeInner() {
    for (int i = 0; i <= count; i++) {
      delete children[i];
    }
  }
};

template <class Key, class Value>
struct BufferBTreeImp {
  std::atomic<NodeBase *> root;
  std::atomic<int16_t> leaf_count;
  std::atomic<bool> full;
  BTree *device_tree;
#ifdef USE_THREAD_POOL
  thread_pool pool;
#else
  work_queue _queue;
#endif
  using leaf_type = BTreeLeaf<Key, Value>;
  using inner_type = BTreeInner<Key>;

  // helper
  std::atomic_int64_t _access;
  std::atomic_int64_t _hit;
  uint64_t _total_kvs = 0;
  uint64_t _total_batches = 0;
  uint64_t _total_leaves = 0;
  uint64_t _total_traverse_time = 0;
  uint64_t _total_inbatch_time = 0;

  explicit BufferBTreeImp(BTree *device_tree)
      : device_tree(device_tree),
#ifdef USE_THREAD_POOL
        pool(MAX_BG_FLUSH_THREADS, device_tree),
#else
        _queue(_num_threads, device_tree),
#endif
        _access(0),
        _hit(0) {
    root = new BTreeLeaf<Key, Value>();
    leaf_count.store(0);
    full = false;
  }

  void makeRoot(Key k, NodeBase *leftChild, NodeBase *rightChild) {
    BTreeInner<Key> *inner = new BTreeInner<Key>();
    inner->count = 1;
    inner->keys[0] = k;
    inner->children[0] = leftChild;
    inner->children[1] = rightChild;
    root = inner;
  }

  void yield(int count) {
    if (count > 3)
      sched_yield();
    else
      _mm_pause();
  }

  void insert(Key k, Value v) {
    int restartCount = 0;
  restart:
    if (restartCount++) yield(restartCount);
    bool needRestart = false;

    // Current node
    NodeBase *node = root;
    uint64_t versionNode = node->readLockOrRestart(needRestart);
    if (needRestart || (node != root)) goto restart;

    // Parent of current node
    BTreeInner<Key> *parent = nullptr;
    uint64_t versionParent;

    while (node->type == PageType::BTreeInner) {
      BTreeInner<Key> *inner = (BTreeInner<Key> *)node;

      // Split eagerly if full
      if (inner->isFull()) {
        // Lock
        if (parent) {
          parent->upgradeToWriteLockOrRestart(versionParent, needRestart);
          if (needRestart) goto restart;
        }
        node->upgradeToWriteLockOrRestart(versionNode, needRestart);
        if (needRestart) {
          if (parent) parent->writeUnlock();
          goto restart;
        }
        if (!parent && (node != root)) {  // there's a new parent
          node->writeUnlock();
          goto restart;
        }
        // Split
        Key sep;
        BTreeInner<Key> *newInner = inner->split(sep);
        if (parent)
          parent->insert(sep, newInner);
        else
          makeRoot(sep, inner, newInner);
        // Unlock and restart
        node->writeUnlock();
        if (parent) parent->writeUnlock();
        goto restart;
      }

      if (parent) {
        parent->readUnlockOrRestart(versionParent, needRestart);
        if (needRestart) goto restart;
      }

      parent = inner;
      versionParent = versionNode;

      node = inner->children[inner->lowerBound(k)];
      inner->checkOrRestart(versionNode, needRestart);
      if (needRestart) goto restart;
      versionNode = node->readLockOrRestart(needRestart);
      if (needRestart) goto restart;
    }

    BTreeLeaf<Key, Value> *leaf = (BTreeLeaf<Key, Value> *)node;

    // Split leaf if full
    if (leaf->count == leaf->maxEntries ||
        leaf->access_count == LEAF_DELETED_FLAG) {
      // Lock
      if (parent) {
        parent->upgradeToWriteLockOrRestart(versionParent, needRestart);
        if (needRestart) goto restart;
      }
      node->upgradeToWriteLockOrRestart(versionNode, needRestart);
      if (needRestart) {
        if (parent) parent->writeUnlock();
        goto restart;
      }
      if (!parent && (node != root)) {  // there's a new parent
        node->writeUnlock();
        goto restart;
      }
      // Split
      if (leaf->access_count == LEAF_DELETED_FLAG) {
        parent->remove_leaf(leaf);
      } else {
        Key sep;
        BTreeLeaf<Key, Value> *newLeaf = leaf->split(sep);
        leaf_count.fetch_add(1);
        if (parent)
          parent->insert(sep, newLeaf);
        else
          makeRoot(sep, leaf, newLeaf);
        // Unlock and restart
        node->writeUnlock();
      }
      if (parent) parent->writeUnlock();
      goto restart;
    } else {
      // only lock leaf node
      node->upgradeToWriteLockOrRestart(versionNode, needRestart);
      if (needRestart) goto restart;
      if (parent) {
        parent->readUnlockOrRestart(versionParent, needRestart);
        if (needRestart) {
          node->writeUnlock();
          goto restart;
        }
      }
      if (leaf->keys == nullptr) {
        leaf_count.fetch_add(1);
        // printf("triger nullptr insert\n");
      }
      leaf->insert(k, v);
      node->writeUnlock();
    }

    if (leaf_count.load() >= max_leaf_count) {
      full = true;
    }
  }

  struct LeafCompare {
    bool operator()(const leaf_type *lhs, const leaf_type *rhs) const {
      return lhs->access_count < rhs->access_count;
    }
  };
  struct LeafCompareGreater {
    bool operator()(const leaf_type *lhs, const leaf_type *rhs) const {
      return lhs->access_count > rhs->access_count;
    }
  };
  // max heap
  using vec_type = std::vector<leaf_type *>;
  using pq_type = std::priority_queue<leaf_type *, vec_type, LeafCompare>;

  struct LeafCompareKeys {
    bool operator()(const leaf_type *lhs, const leaf_type *rhs) const {
      return lhs->keys[0] < rhs->keys[0];
    }
  };
  vec_type flush() {
    // 1. travers phase
    auto start = bench_start();
    std::pair<vec_type, vec_type> ret = distinguish_leaves(root);
    vec_type flush_leaf = std::move(ret.first);
    std::sort(flush_leaf.begin(), flush_leaf.end(), LeafCompareKeys());
    auto end = bench_end();
    _total_traverse_time += end - start;

    // 2. emplace in batch phase
    start = bench_start();
    thread_pool::q_type q;
    for (leaf_type *leaf : flush_leaf) {
      bool restart = false;
    retry:
      leaf->writeLockOrRestart(restart);
      if (restart) {
        yield(0);
        goto retry;
      }
      q.emplace_back(leaf->keys, leaf->payloads, leaf->count);
      this->_total_kvs += leaf->count;

      leaf->keys = nullptr;
      leaf->payloads = nullptr;
      leaf->count = 0;
      // means this leaf is deleted
      leaf->access_count = LEAF_DELETED_FLAG;
      leaf->writeUnlock();
    }
    this->_total_batches += 1;
    this->_total_leaves += flush_leaf.size();
    leaf_count.fetch_sub(flush_leaf.size());
#ifdef USE_THREAD_POOL
    pool.queue(q);
#else
    _queue.queue(q);
    // for (auto leaf : q) {
    //   device_tree->BatchInsert(leaf.keys, leaf.values, leaf.count);
    //   delete[] leaf.keys;
    //   delete[] leaf.values;
    // }
#endif
    end = bench_end();
    _total_inbatch_time += end - start;

    full = false;
    return {};
  }

  /*
   * Author: chenbo
   * Time: 2024-03-19 15:52:49
   * Description: 这个函数应该不会被调用了
   */
  void flush_page(leaf_type *leaf) {
    assert(false);
    auto keys = leaf->keys;
    if (keys == nullptr) return;
    auto values = leaf->payloads;
    auto num = leaf->count;
    leaf->keys = nullptr;
    leaf->payloads = nullptr;
    leaf->count = 0;
    leaf->access_count = 0;
    leaf_count.fetch_sub(1);
    /*
     * Author: chenbo
     * Time: 2024-03-19 14:58:39
     * Description: 由thread_pool::queue调用
     */
    device_tree->BatchInsert(keys, values, num);
  }

  void FlushAll() {
    thread_pool::q_type q;
    std::function<void(NodeBase *)> dfs = [&](NodeBase *node) {
      if (node == nullptr) return;
      if (node->type == PageType::BTreeInner) {
        inner_type *inner = (inner_type *)node;
        for (int i = 0; i <= inner->count; i++) {
          dfs(inner->children[i]);
        }
      } else {
        leaf_type *leaf = (leaf_type *)node;
        if (leaf->keys == nullptr) {
          assert(leaf->count == 0 && leaf->payloads == nullptr);
          return;
        }
        /*
         * Author: chenbo
         * Time: 2024-03-19 15:40:44
         * Description: 将所有的leaf放在一个队列中，然后交给线程池处理
         */
        q.emplace_back(leaf->keys, leaf->payloads, leaf->count);
        leaf->keys = nullptr;
        leaf->payloads = nullptr;
        leaf->count = 0;
        leaf->access_count = 0;
        leaf_count.fetch_sub(1);
        // delete_node(leaf);
      }
    };
    dfs(root.load());
    if (q.size() != 0) {
#ifdef USE_THREAD_POOL
      pool.queue(q);
      while (pool.start) yield(4);
#else
      _queue.queue(q);
      _queue.do_all();
      // for (auto leaf : q) {
      //   device_tree->BatchInsert(leaf.keys, leaf.values, leaf.count);
      //   // for(int i = 0; i < leaf.count; i++)
      //   // {
      //   //   device_tree->Insert(leaf.keys[i], leaf.values[i]);
      //   // }
      //   delete[] leaf.keys;
      //   delete[] leaf.values;
      // }
#endif
    }
  }

  void GetNodeNums() const {
    u64 innerNodeCount = 0;
    u64 leafNodeCount = 0;
    u64 emptyLeafNodeCount = 0;
    double avgInnerNodeKeys = 0;
    double avgLeafNodeKeys = 0;
    u32 height = 0;

    if (root.load() == nullptr) {
      INFO_PRINT("Root node is Null\n");
      return;
    }

    std::stack<std::pair<NodeBase *, u32>> nodeStack;
    nodeStack.push({root.load(), 1});

    while (!nodeStack.empty()) {
      NodeBase *node = nodeStack.top().first;
      u32 depth = nodeStack.top().second;
      nodeStack.pop();

      height = std::max(height, depth);

      if (node->type == PageType::BTreeLeaf) {
        leaf_type *leafNode = reinterpret_cast<leaf_type *>(node);
        emptyLeafNodeCount += (leafNode->keys == nullptr ? 1 : 0);
        leafNodeCount++;
        avgLeafNodeKeys += leafNode->count;
      } else {
        inner_type *innerNode = reinterpret_cast<inner_type *>(node);
        innerNodeCount++;
        avgInnerNodeKeys += innerNode->count;

        // count begin from zero and the ptr is always one more than the count
        for (int i = 0; i <= innerNode->count; i++) {
          nodeStack.push({innerNode->children[i], depth + 1});
        }
      }
    }

    if (innerNodeCount > 0) {
      avgInnerNodeKeys /= innerNodeCount;
    }

    if (leaf_count.load() > 0) {
      avgLeafNodeKeys /= leaf_count.load();
    }
    INFO_PRINT(
        "[BufferTree] Tree Height: %2u InnerNodeCount: %4lu LeafNodeCount: "
        "%6lu "
        "EmptyLeafNodeCount: %6lu ValidLeafNodeCount: %6hd "
        "Avg Inner Node pairs: %3.1lf Avg Leaf Node pairs: %3.1lf\n",
        height, innerNodeCount, leafNodeCount, emptyLeafNodeCount,
        leaf_count.load(), avgInnerNodeKeys, avgLeafNodeKeys);
  }

 private:
  // return flush_leaf and keep_leaf
  // now we keep keep_leaf empty because we only delete flush_leaf
  std::pair<vec_type, vec_type> distinguish_leaves(NodeBase *node) {
    pq_type pq;
    vec_type another;
#ifdef TRAVERSE_GREATER
    traverse_greater(node, pq, another);
    while (!pq.empty()) {
      // keep_leaf.push_back(pq.top());
      pq.top()->access_count /= 2;
      pq.pop();
    }
    return std::make_pair(std::move(another), vec_type{});
#else
    traverse(node, pq, another);
    vec_type flush_leaf;
    while (!pq.empty()) {
      flush_leaf.push_back(pq.top());
      pq.pop();
    }
    return std::make_pair(std::move(flush_leaf), vec_type{});
#endif
  }
  // using max-heap with size of (max_leaf_count - keep_leaf_count)
  // which means time O(nlogk) is smaller
  void traverse(NodeBase *node, pq_type &pq /*flush*/, vec_type &another) {
    if (node == nullptr) return;
    if (node->type == PageType::BTreeInner) {
      inner_type *inner = (inner_type *)node;
      for (int i = 0; i <= inner->count; i++) {
        traverse(inner->children[i], pq, another);
      }
    } else {
      leaf_type *leaf = (leaf_type *)node;
      if (leaf->keys == nullptr) return;
      pq.push(leaf);
      if (pq.size() > max_leaf_count - keep_leaf_count) {
        auto top = pq.top();
        top->access_count /= 2;
        pq.pop();
      }
    }
  }
  // using min-heap with size of keep_leaf_count
  // which means time O(nlogk) is smaller
  // and now we have not half access count
  void traverse_greater(NodeBase *node, pq_type &pq /*keep*/,
                        vec_type &another) {
    if (node == nullptr) return;
    if (node->type == PageType::BTreeInner) {
      inner_type *inner = (inner_type *)node;
      for (int i = 0; i <= inner->count; i++) {
        traverse_greater(inner->children[i], pq, another);
      }
    } else {
      leaf_type *leaf = (leaf_type *)node;
      if (leaf->keys == nullptr) return;
      pq.push(leaf);
      if (pq.size() > keep_leaf_count) {
        another.push_back(pq.top());
        auto top = pq.top();
        pq.pop();
      }
    }
  }

  void delete_nodes(vec_type &nodes) {
    for (auto node : nodes) {
      delete_node(node);
    }
  }
  void delete_node(leaf_type *leaf) {
    if (leaf->keys == nullptr) return;
    leaf_count.fetch_sub(1);
    assert(leaf_count.load() >= 0);
    leaf->deleteNode();
  }

 public:
  bool lookup(Key k, Value &result) {
    _access++;
    int restartCount = 0;
  restart:
    if (restartCount++) yield(restartCount);
    bool needRestart = false;

    NodeBase *node = root;
    uint64_t versionNode = node->readLockOrRestart(needRestart);
    if (needRestart || (node != root)) goto restart;

    // Parent of current node
    BTreeInner<Key> *parent = nullptr;
    uint64_t versionParent;

    while (node->type == PageType::BTreeInner) {
      auto inner = static_cast<BTreeInner<Key> *>(node);

      if (parent) {
        parent->readUnlockOrRestart(versionParent, needRestart);
        if (needRestart) goto restart;
      }

      parent = inner;
      versionParent = versionNode;

      node = inner->children[inner->lowerBound(k)];
      inner->checkOrRestart(versionNode, needRestart);
      if (needRestart) goto restart;
      versionNode = node->readLockOrRestart(needRestart);
      if (needRestart) goto restart;
    }

    BTreeLeaf<Key, Value> *leaf = static_cast<BTreeLeaf<Key, Value> *>(node);
    // if(leaf->count == 0) return false;
    if (leaf->keys == nullptr || leaf->count == 0) {
#ifdef USE_THREAD_POOL
      if (!pool.get(k, result))
#else
      if (!_queue.get(k, result))
#endif
        return device_tree->Get(k, result);
      return true;
    }
    // if(!(k>= leaf->keys[0] && k<= leaf->keys[leaf->count-1]))
    // {
    //   printf("finding %ld in leaf %ld %ld\n", k, leaf->keys[0],
    //   leaf->keys[leaf->count-1]);
    // }
    unsigned pos = leaf->lowerBound(k);
    bool success = false;
    if ((pos < leaf->count) && (leaf->keys[pos] == k)) {
      success = true;
      _hit++;
      result = leaf->payloads[pos];
    }
    if (parent) {
      parent->readUnlockOrRestart(versionParent, needRestart);
      if (needRestart) goto restart;
    }
    node->readUnlockOrRestart(versionNode, needRestart);
    if (needRestart) goto restart;

    if (!success) {
#ifdef USE_THREAD_POOL
      if (!pool.get(k, result))
#else
      if (!_queue.get(k, result))
#endif
        return device_tree->Get(k, result);
      return true;
    }
    return success;
  }

  uint64_t scan(Key k, int range, Value *output) {
    int restartCount = 0;
    int count = 0;
    bool next_scan = false;
  restart:
    bool right_most = true;
    if (!next_scan && restartCount++) yield(restartCount);
    next_scan = false;
    bool needRestart = false;

    NodeBase *node = root;
    uint64_t versionNode = node->readLockOrRestart(needRestart);
    if (needRestart || (node != root)) goto restart;

    // Parent of current node
    BTreeInner<Key> *parent = nullptr;
    uint64_t versionParent;

    int p = -1;
    while (node->type == PageType::BTreeInner) {
      auto inner = static_cast<BTreeInner<Key> *>(node);

      if (parent) {
        parent->readUnlockOrRestart(versionParent, needRestart);
        if (needRestart) goto restart;
      }

      parent = inner;
      versionParent = versionNode;

      p = inner->lowerBound(k);
      if (p != inner->count) right_most = false;
      node = inner->children[p];
      inner->checkOrRestart(versionNode, needRestart);
      if (needRestart) goto restart;
      versionNode = node->readLockOrRestart(needRestart);
      if (needRestart) goto restart;
    }
    while (count != range && p <= parent->count) {
      auto node1 = parent->children[p];
      BTreeLeaf<Key, Value> *leaf = static_cast<BTreeLeaf<Key, Value> *>(node1);
      if (leaf->keys == nullptr) {
        p++;
        continue;
      }
      unsigned pos = leaf->lowerBound(k);
      for (unsigned i = pos; i < leaf->count; i++) {
        if (count == range) break;
        output[count++] = leaf->payloads[i];
      }
      k = output[count - 1] + 1;
      p++;
    }

    if (parent) {
      parent->readUnlockOrRestart(versionParent, needRestart);
      if (needRestart) goto restart;
    }
    node->readUnlockOrRestart(versionNode, needRestart);
    if (needRestart) goto restart;
    // if (count != range && !right_most) {
    //   k = output[count - 1] + 1;
    //   next_scan = true;
    //   goto restart;
    // }

    return count;
  }
  virtual ~BufferBTreeImp() {
    INFO_PRINT(
        "[BufferBTree] lookup hit: %ld miss: %ld hit ratio: %f%%\n",
        _hit.load(), _access.load() - _hit.load(),
        _hit.load() == 0 ? 0.0 : _hit.load() / (double)_access.load() * 100);
    delete root.load();
    root = nullptr;
  }
};

/*
 *  ZBTree is a thread-safe B+ tree
 */
template <typename Key, typename Value>
struct ZBTree {
  BufferBTreeImp<Key, Value> *current;
  std::shared_mutex mtx;
  BTree *device_tree;
  WAL *wal_;

  ZBTree(BTree *device_tree) : device_tree(device_tree) {
    current = new BufferBTreeImp<Key, Value>(device_tree);
    wal_ = new WAL(WAL_NAME.c_str(), WAL_INSTANCE);
  }

  ~ZBTree() {
    FlushAll();
    Print();
    delete wal_;
    delete current;
    current = nullptr;
    // delete device_tree;
  }

  // std::atomic_int flush_thread = 0;

  void Insert(Key k, Value v) {
    wal_->Append(k, v);
    int done = 0;
    int current_jobs = 0;
  restart_insert:

    if (current->leaf_count >= max_leaf_count) {
      // if (mtx.try_lock()) {
      mtx.lock();
      if (current->leaf_count >= max_leaf_count) {
        auto keep_leaf = std::move(current->flush());
      }
      mtx.unlock();
      // }
#ifndef USE_THREAD_POOL
      while (current->_queue.do_work() &&
             current->_queue._worker < MAX_BG_FLUSH_THREADS &&
             current_jobs < MAX_DO_JOBS) {
        current_jobs++;
        done = true;
      }
#endif
      if (current_jobs >= MAX_DO_JOBS) {
        // yield();
        current->insert(k, v);
        return;
      }
      yield();
      goto restart_insert;
    } else {
      current->insert(k, v);
      if (current->leaf_count >= max_leaf_count) {
        // if (mtx.try_lock()) {
        mtx.lock();
        if (current->leaf_count >= max_leaf_count) {
          auto keep_leaf = std::move(current->flush());
        }
        mtx.unlock();
        // }
      }
    }
#ifndef USE_THREAD_POOL
    if (!done || current->_queue._worker < MAX_BG_FLUSH_THREADS) {
      while (current->_queue.do_work() &&
             current->_queue._worker < MAX_BG_FLUSH_THREADS &&
             current_jobs < MAX_DO_JOBS) {
        current_jobs++;
      }
    }
#endif
  }

  bool Get(Key k, Value &result) {
    bool done = false;
    int current_jobs = 0;
  restart_lookup:
    // std::shared_lock<std::shared_mutex> share_lock(mtx);
    if (current->leaf_count >= max_leaf_count) {
      // if (mtx.try_lock()) {
      mtx.lock();
      if (current->leaf_count >= max_leaf_count) {
        auto keep_leaf = std::move(current->flush());
      }
      mtx.unlock();
      // }
#ifndef USE_THREAD_POOL
      while (current->_queue.do_work() &&
             current->_queue._worker < MAX_BG_FLUSH_THREADS &&
             current_jobs < MAX_DO_JOBS) {
        done = true;
        current_jobs++;
      }
#endif
      yield();
      if (current_jobs >= MAX_DO_JOBS) {
        return current->lookup(k, result);
      }
      goto restart_lookup;
    } else {
      return current->lookup(k, result);
    }
#ifndef USE_THREAD_POOL
    if (!done || current->_queue._worker < MAX_BG_FLUSH_THREADS) {
      while (current->_queue.do_work() &&
             current->_queue._worker < MAX_BG_FLUSH_THREADS &&
             current_jobs < MAX_DO_JOBS) {
        current_jobs++;
      }
    }
#endif
  }

  uint64_t Scan(Key k, int range, Value *output) {
    bool done = false;
    int current_jobs = 0;
  restart_scan:
    // std::shared_lock<std::shared_mutex> share_lock(mtx);
    if (current->leaf_count >= max_leaf_count) {
      // if (mtx.try_lock()) {
      mtx.lock();
        if (current->leaf_count >= max_leaf_count) {
          auto keep_leaf = std::move(current->flush());
        }
        mtx.unlock();
      // }
// share_lock.unlock();
#ifndef USE_THREAD_POOL
      while (current->_queue.do_work() &&
             current->_queue._worker < MAX_NUMS_ZONE &&
             current_jobs < MAX_DO_JOBS) {
        done = true;
        current_jobs++;
      }
#endif
      yield();
      if (current_jobs >= MAX_DO_JOBS) {
        goto do_scan;
      }
      goto restart_scan;
    } else {
    do_scan:
      Value buffer_btree_output[range];
      Value thread_pool_output[range];
      Value device_tree_output[range];
      // fix ok
      uint64_t buffer_btree_cnt = current->scan(k, range, buffer_btree_output);
#ifdef USE_THREAD_POOL
      uint64_t thread_pool_cnt =
          current->pool.scan(k, range, thread_pool_output);
#else
      // fix ok
      uint64_t thread_pool_cnt =
          current->_queue.scan(k, range, thread_pool_output);
#endif
      uint64_t device_tree_cnt =
          device_tree->Scan(k, range, device_tree_output);
      uint64_t cnt = 0;
      uint64_t a = 0, b = 0, c = 0;
      vector<Value> tmp;
      tmp.reserve(3 * range);
      for (int i = 0; i < buffer_btree_cnt; ++i)
        tmp.push_back(buffer_btree_output[i]);
      for (int i = 0; i < thread_pool_cnt; ++i)
        tmp.push_back(thread_pool_output[i]);
      for (int i = 0; i < device_tree_cnt; ++i)
        tmp.push_back(device_tree_output[i]);
      sort(tmp.begin(), tmp.end());
      tmp.resize(unique(tmp.begin(), tmp.end()) - tmp.begin());
      for (int i = 0; i < tmp.size() && i < range; ++i) {
        output[i] = tmp[i];
      }
      return min(tmp.size(), (uint64_t)range);
    }
#ifndef USE_THREAD_POOL
    if (!done || current->_queue._worker < MAX_BG_FLUSH_THREADS) {
      while (current->_queue.do_work() &&
             current->_queue._worker < MAX_BG_FLUSH_THREADS &&
             current_jobs < MAX_DO_JOBS) {
        current_jobs++;
      }
    }
#endif
  }

  void yield() {
    usleep(1);
    // sched_yield();
    _mm_pause();
  }

  void FlushAll() {
    std::unique_lock<std::shared_mutex> u_lock(mtx);
    wal_->FlushAll();
    current->FlushAll();
#ifdef USE_THREAD_POOL
    int cnt = 0;
    while (current->pool.start.load()) {
      cnt++;
      yield();
      if (cnt > 1000) {
        printf("%s waiting for pool\n", __func__);
      }
    }
// printf("%s\n", __func__);
#else
    current->_queue.do_all();
#endif
  }

  void Print() {
    current->GetNodeNums();
    // if (current->_total_batches == 0) {
    // return;
    // }
    double avg_traverse_time = cycles_to_us(current->_total_traverse_time) *
                               1.0 / current->_total_batches;
    double avg_inbatch_time = cycles_to_us(current->_total_inbatch_time) * 1.0 /
                              current->_total_batches;
    double avg_kvs_per_batch =
        current->_total_kvs * 1.0 / current->_total_batches;
    double avg_kvs_per_leaf =
        current->_total_kvs * 1.0 / current->_total_leaves;
    INFO_PRINT(
        "[BatchMerge->Traverse] total traverse time: %3.2fs, avg:%8.2lfus, "
        "flush %4ld\n",
        cycles_to_sec(current->_total_traverse_time), avg_traverse_time,
        current->_total_batches);
    INFO_PRINT(
        "[BatchMerge->InBatch]  total inBatch  time: %3.2fs, avg:%8.2lfus, "
        "flush %4ld, kvs per batch:%3.2lf  kvs per leaf:" KRED
        "%3.2lf\n" KRESET,
        cycles_to_sec(current->_total_inbatch_time), avg_inbatch_time,
        current->_total_batches, avg_kvs_per_batch, avg_kvs_per_leaf);
  }
};

}  // namespace buffer_btree

template <class Key, class Value>
using ZBTree = buffer_btree::ZBTree<Key, Value>;
}  // namespace btreeolc

#endif