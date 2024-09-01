#ifndef __WORK_QUEUE_H
#define __WORK_QUEUE_H
#include <atomic>
#include <vector>

#include "config.h"
#include "rwlatch.h"
#include "thread_pool.h"

namespace btreeolc {
struct work_queue {
  using q_type = std::vector<work>;
  const int _THREAD_SIZE;
  std::atomic_uint _worker;
  std::atomic_uint _index;
  std::atomic_uint _size;
  std::atomic_uint _done;
  q_type _queue;
  ReaderWriterLatch _rdlock;
  BTree* device_tree;

  work_queue(int n, BTree* tree);
  ~work_queue();

  inline bool do_work() {
    if (_index >= _size || _done >= _size) return false;
    int idx = _index.fetch_add(1);
    if (idx >= _size) return false;
    _rdlock.RLock();
    int work_cnt = _worker.fetch_add(1);
    work& w = _queue[idx];
#ifdef BATCH_INSERT
    device_tree->BatchInsert(w.keys, w.values, w.count);
#else
    for (int i = 0; i < w.count; i++) {
      device_tree->Insert(w.keys[i], w.values[i]);
    }
#endif

    _done.fetch_add(1);
    _worker.fetch_sub(1);
    _rdlock.RUnlock();
    return true;
  }
  void do_all();
  void queue(q_type& w);
  void yield();

  bool get(KeyType key, ValueType& value);
  uint64_t scan(KeyType key, int range, ValueType* values);
};
}  // namespace btreeolc

#endif