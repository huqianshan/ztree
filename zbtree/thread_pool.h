#ifndef THREAD_WORKER_H
#define THREAD_WORKER_H
#include <xmmintrin.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
// #include <barrier>
#include <boost/thread/barrier.hpp>

#include "rwlatch.h"
#include "zbtree.h"
// using KeyType = int;
// using ValueType = int;
namespace btreeolc {
struct work {
  KeyType* keys;
  ValueType* values;
  int count;
  work(KeyType* k, ValueType* v, int c);
  work(const work& w) {
    keys = w.keys;
    values = w.values;
    count = w.count;
  };
  ~work();
};

struct thread_pool {
  using q_type = std::vector<work>;
  void queue(q_type& w);
  explicit thread_pool(int n, btreeolc::BTree* tree);
  ~thread_pool();
  // private:
  btreeolc::BTree* device_tree;
  q_type work_queue;
  std::vector<std::shared_ptr<std::thread>> threads;
  std::atomic<bool> start;
  std::atomic<bool> end;
  std::mutex wakeup_mutex;
  std::mutex start_mutex;
  ReaderWriterLatch start_rdlock;
  std::condition_variable wakeup_cv;
  boost::barrier syncpoint;
  void yield(int trial);
  void run();
  void do_work(const int index);
  void notify_all();
  bool get(KeyType, ValueType&);
  uint64_t scan(KeyType k, int range, ValueType* values);
};
}  // namespace btreeolc

#endif  // THREAD_WORKER_H