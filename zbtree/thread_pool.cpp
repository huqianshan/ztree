#include "thread_pool.h"

std::mutex m;
namespace btreeolc {
work::work(KeyType* k, ValueType* v, int c) : keys(k), values(v), count(c) {}
work::~work() {}
thread_pool::thread_pool(int n, btreeolc::BTree* tree)
    : syncpoint(n, [&]() { start = false; }),
      start(false),
      end(false),
      device_tree(tree) {
  assert(n > 0);
  for (int i = 0; i < n; ++i) {
    threads.push_back(
        std::make_shared<std::thread>(&thread_pool::do_work, this, i));
  }
}
thread_pool::~thread_pool() {
  int trial = 0;
  while (start.load()) {
    yield(trial++);
  }
  end = true;
  notify_all();
  std::unique_lock<std::mutex> lock(m);
  // std::cout << "Joining threads..." << std::endl;
  lock.unlock();
  for (auto& t : threads) {
    t->join();
  }
  // for(auto& wk : work_queue)
  // {
  // 	delete[] wk.keys;
  // 	delete[] wk.values;
  // }
  work_queue.clear();
}

void thread_pool::notify_all() {
  std::unique_lock<std::mutex> lock(wakeup_mutex);
  wakeup_cv.notify_all();
}

void thread_pool::yield(int trial) {
  if (trial < 3) {
    sched_yield();
  } else {
    _mm_pause();
  }
}

void thread_pool::queue(thread_pool::q_type& w) {
  int trial = 0;
  while (start.load()) {
    yield(trial++);
  }
  start_rdlock.WLock();
  work_queue.swap(w);
  run();
  start_rdlock.WUnlock();
  for (auto& wk : w) {
    delete[] wk.keys;
    delete[] wk.values;
  }
}

void thread_pool::run() {
  start = true;
  notify_all();
}

bool thread_pool::get(KeyType key, ValueType& value) {
  if (!start.load()) return false;
  start_rdlock.RLock();
  auto wp = std::lower_bound(
      work_queue.begin(), work_queue.end(), key,
      [](const work& w, KeyType k) { return w.keys[w.count - 1] < k; });
  if (wp == work_queue.end()) {
    start_rdlock.RUnlock();
    return false;
  }
  work w = *wp;
  if (key >= w.keys[0] && key <= w.keys[w.count - 1]) {
    auto pos = std::lower_bound(w.keys, w.keys + w.count, key) - w.keys;
    if (w.keys[pos] == key) {
      value = w.values[pos];
      start_rdlock.RUnlock();
      return true;
    }
    start_rdlock.RUnlock();
    return false;
  }
  start_rdlock.RUnlock();
  return false;
}

uint64_t thread_pool::scan(KeyType key, int range, ValueType* values) {
  uint64_t count = 0;
  if (!start.load()) return count;
  start_rdlock.RLock();
  auto wp = std::lower_bound(
      work_queue.begin(), work_queue.end(), key,
      [](const work& w, KeyType k) { return w.keys[w.count - 1] < k; });
  if (wp == work_queue.end()) {
    start_rdlock.RUnlock();
    return count;
  }
  // while(wp != work_queue.end() && count < range) {
  work w = *wp;
  auto start = std::lower_bound(w.keys, w.keys + w.count, key) - w.keys;
  for (auto i = start; i < w.count && count < range; ++i) {
    values[count++] = w.values[i];
  }
  // ++wp;
  // }
  start_rdlock.RUnlock();
  return count;
}

void thread_pool::do_work(const int index) {
  while (!end.load()) {
    // int trial = 0;
    while (!start.load()) {
      std::unique_lock<std::mutex> lock(wakeup_mutex);
      // wakeup_cv.wait_for(lock, std::chrono::nanoseconds(100), [this]()
      // {return start.load() || end.load();});
      wakeup_cv.wait(lock, [this]() { return start.load() || end.load(); });
      if (end.load()) {
        return;
      }
    }
    start_rdlock.RLock();
    // size_t split = work_queue.size() / threads.size();
    // int start = index * split;
    // int end = std::min(start + split, work_queue.size());
    // for(int i = start; i < end; ++i)
    for (int i = index; i < work_queue.size(); i += threads.size()) {
      device_tree->BatchInsert(work_queue[i].keys, work_queue[i].values,
                               work_queue[i].count);
    }
    syncpoint.count_down_and_wait();
    start = false;
    start_rdlock.RUnlock();
  }
}
}  // namespace btreeolc