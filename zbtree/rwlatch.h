#pragma once
#include <climits>
#include <condition_variable>  // NOLINT
#include <mutex>               // NOLINT

#include "config.h"

/**
 * Reader-Writer latch backed by std::mutex.
 */
class ReaderWriterLatch {
  using mutex_t = std::mutex;
  using cond_t = std::condition_variable;
  static const uint32_t MAX_READERS = UINT_MAX;

 public:
  ReaderWriterLatch() = default;
  ~ReaderWriterLatch() { std::lock_guard<mutex_t> guard(mutex_); }

  DISALLOW_COPY(ReaderWriterLatch);

  /**
   * Acquire a write latch.
   */
  void WLock() {
    std::unique_lock<mutex_t> latch(mutex_);
    while (writer_entered_) {
      reader_.wait(latch);
    }
    writer_entered_ = true;
    while (reader_count_ > 0) {
      writer_.wait(latch);
    }
  }

  /**
   * Release a write latch.
   */
  void WUnlock() {
    std::lock_guard<mutex_t> guard(mutex_);
    writer_entered_ = false;
    reader_.notify_all();
  }

  /**
   * Acquire a read latch.
   */
  void RLock() {
    std::unique_lock<mutex_t> latch(mutex_);
    while (writer_entered_ || reader_count_ == MAX_READERS) {
      reader_.wait(latch);
    }
    reader_count_++;
  }

  /**
   * Release a read latch.
   */
  void RUnlock() {
    std::lock_guard<mutex_t> guard(mutex_);
    reader_count_--;
    if (writer_entered_) {
      if (reader_count_ == 0) {
        writer_.notify_one();
      }
    } else {
      if (reader_count_ == MAX_READERS - 1) {
        reader_.notify_one();
      }
    }
  }

  /**
   * helper function!
   * Try to get the read lock. If can't get, return false; else
   * return true
   */
  bool TryRLock() {
    std::unique_lock<mutex_t> latch(mutex_);
    if (writer_entered_ || reader_count_ == MAX_READERS) {
      return false;
    }
    reader_count_++;
    return true;
  }

 private:
  mutex_t mutex_;
  cond_t writer_;
  cond_t reader_;
  uint32_t reader_count_{0};
  bool writer_entered_{false};
};

class ReadLockGuard {
 public:
  DISALLOW_COPY_AND_MOVE(ReadLockGuard);

  explicit ReadLockGuard(ReaderWriterLatch* latch) : latch_(latch) {
    latch_->RLock();
  }
  ~ReadLockGuard() { latch_->RUnlock(); }

 private:
  ReaderWriterLatch* latch_;
};

class WriteLockGuard {
 public:
  DISALLOW_COPY_AND_MOVE(WriteLockGuard);

  explicit WriteLockGuard(ReaderWriterLatch* latch) : latch_(latch) {
    latch_->WLock();
  }
  ~WriteLockGuard() { latch_->WUnlock(); }

 private:
  ReaderWriterLatch* latch_;
};
