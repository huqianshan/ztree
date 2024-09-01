#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "storage.h"

#define FLUSH_SIZE (PAGE_SIZE)

// write timestamp at first 8 bytes
// first WAL_TIMESTAMP_SIZE bytes timestamp
// next WAL_LENGTH_SIZE bytes buffer valid data length
#define WAL_TIMESTAMP_OFFSET (wal_area_)
#define WAL_TIMESTAMP_SIZE (sizeof(time_t))
#define WAL_LENGTH_OFFSET (WAL_TIMESTAMP_OFFSET + WAL_TIMESTAMP_SIZE)
#define WAL_LENGTH_SIZE (sizeof(int16_t))

class SingleWAL {
 public:
  SingleWAL(const char* wal_name, u64 length = FLUSH_SIZE);
  SingleWAL(SingleWAL&& other);
  ~SingleWAL();
  void Append(const char* data, size_t size);
  void Append(const u64 key, const u64 val);
  void Flush();
  u64 Size();
  u64 GetOffset() { return offset_; }

 private:
  char* wal_area_;
  u64 offset_;
  //   u64 cur_size_;
  page_id_t page_id_;
  u32 length_;
  DiskManager* disk_manager_;
  std::mutex mutex_;
};

class WAL {
 public:
  WAL(const char* wal_name, u32 instance, u32 length = FLUSH_SIZE);
  ~WAL();
  void Append(const char* data, size_t size);
  void Append(const u64 key, const u64 val);
  void FlushAll();
  u64 Size();
  void Print();

 private:
  std::vector<SingleWAL> wal_list_;
  u32 num_instances_;
  std::atomic<size_t> index_;
  u32 length_;
};