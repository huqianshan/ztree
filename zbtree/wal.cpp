#include "wal.h"

#include <string.h>

SingleWAL::SingleWAL(const char* wal_name, u64 length) {
  //   wal_area_ = new bytes_t[length];
  wal_area_ = (char*)aligned_alloc(PAGE_SIZE, length);
  length_ = length;
  disk_manager_ = new DiskManager(wal_name);
  offset_ = 0;
  //   cur_size_ = 0;
  page_id_ = 0;
}
// should this be called?
SingleWAL::SingleWAL(SingleWAL&& wal) {
  wal_area_ = wal.wal_area_;
  length_ = wal.length_;
  disk_manager_ = wal.disk_manager_;
  offset_ = wal.offset_;
  page_id_ = wal.page_id_;
  wal.wal_area_ = nullptr;
  wal.disk_manager_ = nullptr;
}

SingleWAL::~SingleWAL() {
  Flush();
  if (wal_area_) {
    free(wal_area_);
  }
  if (disk_manager_) {
    delete disk_manager_;
  }
}

void SingleWAL::Append(const char* data, u64 size) {
  if (__glibc_unlikely(size >
                       length_ - (WAL_TIMESTAMP_SIZE + WAL_LENGTH_SIZE))) {
    std::cerr << "[WAL] insert data size too large: " << size << std::endl;
    assert(false);
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
retry:
  if (__glibc_unlikely(offset_ == 0)) {
    // prefill timestamp and length
    time_t timestamp = (time_t)-1;
    int16_t length = (int16_t)-1;
    memcpy(WAL_TIMESTAMP_OFFSET, &timestamp, WAL_TIMESTAMP_SIZE);
    memcpy(WAL_LENGTH_OFFSET, &length, WAL_LENGTH_SIZE);
    offset_ += WAL_TIMESTAMP_SIZE;
    offset_ += WAL_LENGTH_SIZE;
  }

  char* cur_buf = wal_area_ + offset_;
  size_t new_offset = offset_ + size;
  // can't write beyond the buffer
  if (__glibc_unlikely(new_offset > length_)) {
    Flush();
    goto retry;
  }
  memcpy(cur_buf, data, size);
  offset_ = new_offset;

  // full
  if (offset_ >= length_) {
    Flush();
  }
}

void SingleWAL::Append(const u64 key, const u64 val) {
  std::pair<u64, u64> pair = std::make_pair(key, val);
  this->Append((const char*)&pair, sizeof(pair));
  return;
  std::lock_guard<std::mutex> lock(mutex_);

  char* cur_buf = wal_area_ + offset_;
  assert(offset_ + sizeof(PairType) <= length_);
  PairType* addr = (PairType*)wal_area_;
  addr[offset_ / sizeof(PairType)] = std::make_pair(key, val);
  offset_ += sizeof(PairType);

  if (offset_ >= FLUSH_SIZE) {
    Flush();
  }
}

void SingleWAL::Flush() {
  //   std::lock_guard<std::mutex> lock(mutex_);
  if (offset_ == 0) {
    return;
  }
  time_t timestamp = time(NULL);
  memcpy(WAL_TIMESTAMP_OFFSET, &timestamp, WAL_TIMESTAMP_SIZE);
  int16_t length = offset_;
  memcpy(WAL_LENGTH_OFFSET, &length, WAL_LENGTH_SIZE);
  disk_manager_->write_n_pages(page_id_, length_ / PAGE_SIZE,
                               (const char*)wal_area_);
  page_id_++;
  offset_ = 0;
  memset(wal_area_, 0x00, length_);
}

u64 SingleWAL::Size() { return page_id_; }

WAL::WAL(const char* wal_name, u32 instance, u32 length) {
  num_instances_ = instance;
  length_ = length;
  index_ = 0;

  // wal_list_.resize(num_instances_);
  for (u32 i = 0; i < num_instances_; i++) {
    std::string name_with_suffix =
        std::string(wal_name) + "_" + std::to_string(i);
    // wal_list_[i] = new SingleWAL(name_with_suffix.c_str(), length_);
    // wal_list_.emplace_back(name_with_suffix.c_str(), length_);
    wal_list_.push_back(SingleWAL(name_with_suffix.c_str(), length_));
  }
}

WAL::~WAL() {
  // Print();
  // for (auto& wal : wal_list_) {
  //   delete wal;
  // }
}

void WAL::Append(const char* data, u64 size) {
  size_t loop_index = index_.load();
  index_.store((index_.load() + 1) % num_instances_);
  assert(loop_index < num_instances_);
  // wal_list_[loop_index]->Append(data, size);
  wal_list_[loop_index].Append(data, size);
}

void WAL::Append(const u64 key, const u64 val) {
  size_t loop_index = index_.load();
  index_.store((index_.load() + 1) % num_instances_);
  assert(loop_index < num_instances_);
  // wal_list_[loop_index]->Append(key, val);
  wal_list_[loop_index].Append(key, val);
}

void WAL::FlushAll() {
  for (u32 i = 0; i < num_instances_; i++) {
    // wal_list_[i]->Flush();
    wal_list_[i].Flush();
  }
}

u64 WAL::Size() {
  u64 size = 0;
  for (u32 i = 0; i < num_instances_; i++) {
    // size += (wal_list_[i]->Size() * PAGE_SIZE);
    // size += wal_list_[i]->GetOffset();
    size += (wal_list_[i].Size() * PAGE_SIZE);
    size += wal_list_[i].GetOffset();
  }
  return size;
}

void WAL::Print() {
  std::cout << "WAL size: " << Size() << " bytes " << std::endl;
}