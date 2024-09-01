#include "work_queue.h"

namespace btreeolc {
work_queue::work_queue(int n, btreeolc::BTree* tree)
    : _THREAD_SIZE(n),
      _worker(0),
      _index(0),
      _size(0),
      _rdlock(),
      _queue(),
      device_tree(tree),
      _done(0) {}

work_queue::~work_queue() {
  _rdlock.WLock();
  while (_index < _size) {
    work& w = _queue[_index];
    device_tree->BatchInsert(w.keys, w.values, w.count);
    delete[] w.keys;
    delete w.values;
    _index.fetch_add(1);
    _done.fetch_add(1);
  }
  _rdlock.WUnlock();
}

// inline void work_queue::do_work() {
// redo:
//   if (_index >= _size) return;
//   int idx = _index.fetch_add(1);
//   if (idx >= _size) return;
//   _rdlock.RLock();
//   int work_cnt = _worker.fetch_add(1);
//   work& w = _queue[idx];
//   device_tree->BatchInsert(w.keys, w.values, w.count);
//   _worker.fetch_sub(1);
//   _rdlock.RUnlock();
//   // if (_THREAD_SIZE == 1) return;
//   // if (work_cnt < _THREAD_SIZE) goto redo;
// }

void work_queue::do_all() {
  while (_done < _size) {
    do_work();
  }
}

void work_queue::queue(q_type& w) {
  while (_done < _size) {
    do_work();
    if (_index >= _size) yield();
  }
  _rdlock.WLock();
  _queue.swap(w);
  _size = _queue.size();
  _index = 0;
  _done = 0;
  _rdlock.WUnlock();
  for (work& wk : w) {
    delete[] wk.keys;
    delete[] wk.values;
    // wk.keys = nullptr;
    // wk.values = nullptr;
  }
}

void work_queue::yield() { _mm_pause(); }

bool work_queue::get(KeyType key, ValueType& value) {
  if (_done == _size) return false;
  _rdlock.RLock();
  auto wp = std::lower_bound(
      _queue.begin(), _queue.end(), key,
      [](const work& w, KeyType k) { return w.keys[w.count - 1] < k; });
  if (wp == _queue.end()) {
    _rdlock.RUnlock();
    return false;
  }
  work& w = *wp;
  if (key >= w.keys[0] && key <= w.keys[w.count - 1]) {
    auto pos = std::lower_bound(w.keys, w.keys + w.count, key) - w.keys;
    if (w.keys[pos] == key) {
      value = w.values[pos];
      // if (value != key) {
      //   std::cout << "error while get key: " << key << " value: " << value
      //             << std::endl;
      // }
      _rdlock.RUnlock();
      return true;
    }
    _rdlock.RUnlock();
    return false;
  }
  _rdlock.RUnlock();
  return false;
}

uint64_t work_queue::scan(KeyType key, int range, ValueType* values) {
  uint64_t count = 0;
  if (_done >= _size) return count;
  _rdlock.RLock();
  auto wp = std::lower_bound(
      _queue.begin(), _queue.end(), key,
      [](const work& w, KeyType k) { return w.keys[w.count - 1] < k; });
  if (wp == _queue.end()) {
    _rdlock.RUnlock();
    return count;
  }
  while (wp != _queue.end() && count < range) {
    work& w = *wp;
    auto start = std::lower_bound(w.keys, w.keys + w.count, key) - w.keys;
    for (int i = start; i < w.count && count < range; i++) {
      values[count++] = w.values[i];
    }
    wp = std::next(wp);
  }
  _rdlock.RUnlock();
  return count;
}

}  // namespace btreeolc