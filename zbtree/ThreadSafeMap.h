#include <initializer_list>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

template <typename K, typename V>
class ThreadSafeMap {
 public:
  ThreadSafeMap() {}

  ThreadSafeMap(std::initializer_list<std::pair<K, V>> init_list) {
    for (auto& pair : init_list) {
      map_[pair.first] = pair.second;
    }
  }

  V& operator[](const K& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_[key];
  }

  bool erase(const K& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.erase(key);
  }

  auto find(const K& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.find(key);
  }

  auto end() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.end();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.size();
  }

 private:
  std::unordered_map<K, V> map_;
  mutable std::mutex mutex_;
};
