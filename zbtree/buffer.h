#pragma once
#include <algorithm>
#include <list>
#include <mutex>
#include <mutex>  // NOLINT
#include <unordered_map>
#include <vector>

#include "../zns/zone_device.h"
#include "ThreadSafeMap.h"
#include "config.h"
#include "lru_buffer.h"
#include "page.h"
#include "replacer.h"
#include "storage.h"

/**
 * BufferPoolManager reads disk pages to and from its internal buffer pool.
 */
class BufferPoolManager {
 public:
  using Mutex = std::mutex;
  using Lock_guard = std::lock_guard<Mutex>;
  friend class ParallelBufferPoolManager;

  /**
   * Creates a new BufferPoolManager.
   * @param pool_size the size of the buffer pool
   * @param disk_manager the disk manager
   * @param log_manager the log manager (for testing only: nullptr = disable
   * logging)
   */
  BufferPoolManager(size_t pool_size, DiskManager *disk_manager);
  /**
   * Creates a new BufferPoolManager.
   * @param pool_size the size of the buffer pool
   * @param num_instances total number of BPIs in parallel BPM
   * @param instance_index index of this BPI in the parallel BPM
   * @param disk_manager the disk manager
   * @param log_manager the log manager (for testing only: nullptr = disable
   * logging)
   */
  BufferPoolManager(size_t pool_size, uint32_t num_instances,
                    uint32_t instance_index, DiskManager *disk_manager);

  /**
   * Destroys an existing BufferPoolManager.
   */
  ~BufferPoolManager();

  /** @return size of the buffer pool */
  size_t GetPoolSize() { return pool_size_; }

  /** @return pointer to all the pages in the buffer pool */
  Page *GetPages() { return pages_; }

 protected:
  /**
   * Fetch the requested page from the buffer pool.
   * @param page_id id of page to be fetched
   * @return the requested page
   */
  Page *FetchPage(page_id_t page_id);

  /**
   * Unpin the target page from the buffer pool. Add the frame of pageid to
   * replacer.
   * @param page_id id of page to be unpinned
   * @param is_dirty true if the page should be marked as dirty, false otherwise
   * @return false if the page pin count is <= 0 before this call, true
   * otherwise
   */
  bool UnpinPage(page_id_t page_id, bool is_dirty);

  /**
   * Flushes the target page to disk.
   * @param page_id id of page to be flushed, cannot be INVALID_PAGE_ID
   * @return false if the page could not be found in the page table, true
   * otherwise
   */
  bool FlushPage(page_id_t page_id);

  /**
   * Creates a new page in the buffer pool.
   * @param[out] page_id id of created page
   * @return nullptr if no new pages could be created, otherwise pointer to new
   * page
   */
  Page *NewPage(page_id_t *page_id);

  /**
   * Deletes a page from the buffer pool. caller after Unpin. add frame of
   * pageid to free_list
   * @param page_id id of page to be deleted
   * @return false if the page exists but could not be deleted, true if the page
   * didn't exist or deletion succeeded
   */
  bool DeletePage(page_id_t page_id);

  /**
   * Flushes all the pages in the buffer pool to disk.
   */
  void FlushAllPages();

  /**
   * Allocate a page on disk.∂
   * @return the id of the allocated page
   */
  page_id_t AllocatePage();

  /**
   * Deallocate a page on disk.
   * @param page_id id of the page to deallocate
   */
  // void DeallocatePage(__attribute__((unused)) page_id_t page_id) {
  //   // This is a no-nop right now without a more complex data structure to
  //   track
  //   // deallocated pages
  // }

  /**
   * Validate that the page_id being used is accessible to this BPI. This can be
   * used in all of the functions to validate input data and ensure that a
   * parallel BPM is routing requests to the correct BPI
   * @param page_id
   */
  void ValidatePageId(page_id_t page_id) const;

  Page *GrabPageFrame();

  void GrabLock();
  void ReleaseLock();

  void WaitForFreeFrame();
  void SignalForFreeFrame();

  void Print();

  /** Number of pages in the buffer pool. */
  const size_t pool_size_;
  /** How many instances are in the parallel BPM (if present, otherwise just 1
   * BPI) */
  const uint32_t num_instances_ = 1;
  /** Index of this BPI in the parallel BPM (if present, otherwise just 0) */
  const uint32_t instance_index_ = 0;
  /** Each BPI maintains its own counter for page_ids to hand out, must ensure
   * they mod back to its instance_index_ */
  std::atomic<page_id_t> next_page_id_;

  /** Array of buffer pool pages in memory. */
  Page *pages_;
  /** Page-aligned data */
  char *page_data_;
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_;
  /** Page table for keeping track of buffer pool pages. */
  std::unordered_map<page_id_t, frame_id_t> page_table_;
  std::unordered_map<page_id_t, frame_id_t> page_id_counts_;
  /** Replacer to find unpinned pages for replacement. */
  LRUReplacer *replacer_;
  /** List of free pages. */
  std::list<frame_id_t> free_list_;
  /** This latch protects shared data structures. We recommend updating this
   * comment to describe what it protects. */
  // std::mutex latch_;

  // big lock for buffer pool manager instance
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;
  // count
  u64 count_;
  u64 hit_;
  u64 miss_;
};

class ParallelBufferPoolManager {
 public:
  /**
   * Creates a new ParallelBufferPoolManager.
   * @param num_instances the number of individual BufferPoolManagerInstances to
   * store
   * @param pool_size the pool size of each BufferPoolManager
   * @param disk_manager the disk manager
   * @param log_manager the log manager (for testing only: nullptr = disable
   * logging)
   */
  ParallelBufferPoolManager(size_t num_instances, size_t pool_size,
                            DiskManager *disk_manager);

  ParallelBufferPoolManager(size_t num_instances, size_t pool_size,
                            std::string file_name, bool is_one_file = true);
  ~ParallelBufferPoolManager();

  //  protected:
  /**
   * @param page_id id of page
   * @return pointer to the BufferPoolManager responsible for handling given
   * page id
   */
  BufferPoolManager *GetBufferPoolManager(page_id_t page_id);
  DiskManager *GetDiskManager(page_id_t page_id);
  // allocate a new page_id
  DiskManager *GetDiskManager(page_id_t *page_id);
  /**
   * Fetch the requested page from the buffer pool.
   * @param page_id id of page to be fetched
   * @return the requested page
   */
  Page *FetchPage(page_id_t page_id);

  /**
   * Unpin the target page from the buffer pool.
   * @param page_id id of page to be unpinned
   * @param is_dirty true if the page should be marked as dirty, false otherwise
   * @return false if the page pin count is <= 0 before this call, true
   * otherwise
   */
  bool UnpinPage(page_id_t page_id, bool is_dirty);

  /**
   * Flushes the target page to disk.
   * @param page_id id of page to be flushed, cannot be INVALID_PAGE_ID
   * @return false if the page could not be found in the page table, true
   * otherwise
   */
  bool FlushPage(page_id_t page_id);

  /**
   * Creates a new page in the buffer pool.
   * @param[out] page_id id of created page
   * @return nullptr if no new pages could be created, otherwise pointer to new
   * page
   */
  Page *NewPage(page_id_t *page_id);

  /**
   * Deletes a page from the buffer pool.
   * @param page_id id of page to be deleted
   * @return false if the page exists but could not be deleted, true if the page
   * didn't exist or deletion succeeded
   */
  bool DeletePage(page_id_t page_id);

  /**
   * Flushes all the pages in the buffer pool to disk.
   */
  void FlushAllPages();

  /** @return size of the buffer pool */
  u64 GetPoolSize();
  u64 GetFileSize();
  u64 GetReadCount();
  u64 GetWriteCount();

  void Print();
  void Close() {};

 public:
  size_t num_instances_;
  std::atomic<size_t> index_;
  DiskManager *disk_manager_;
  std::vector<BufferPoolManager *> bpmis_;
  bool in_one_file_;
};

struct Item {
  frame_id_t frame_id_;
  u32 length;
  Item *next_;
  Item *prev_;
  char *data_;
  Item(frame_id_t frame_id, u32 length, char *addr)
      : frame_id_(frame_id),
        length(length),
        data_(addr),
        next_(nullptr),
        prev_(nullptr) {};
  Item() = default;
};

template <typename T>
class CircleBuffer {
  // https://github.com/cameron314/concurrentqueue
 public:
  CircleBuffer(u64 max_size)
      : head_(0), tail_(0), size_(0), max_size_(max_size) {
    buffer_ = new T[max_size_];
  }
  ~CircleBuffer() {}

  bool Push(const T &item) {
    u64 current_size = size_.load();
    if (current_size == max_size_) {
      return false;
    }
    buffer_[head_] = item;
    u64 new_head = (head_ + 1) % max_size_;
    head_.exchange(new_head);
    size_.fetch_add(1);
    return true;
  }

  bool Pop(T &item) {
    u64 current_size = size_.load();
    if (current_size == 0) {
      return false;
    }
    item = buffer_[tail_];
    u64 new_tail = (tail_ + 1) % max_size_;
    tail_.exchange(new_tail);
    size_.fetch_sub(1);
    return true;
  }

  bool Front(T &item) {
    u64 current_size = size_.load();
    if (current_size == 0) {
      return false;
    }
    item = buffer_[head_];
    return true;
  }

  bool Back(T &item) {
    u64 current_size = size_.load();
    if (current_size == 0) {
      return false;
    }
    item = buffer_[tail_];
    return true;
  }

  bool IsEmpty() { return size_.load() == 0; }
  bool IsFull() { return size_.load() == max_size_; }
  u64 GetTail() { return tail_.load(); }
  u64 GetHead() { return head_.load(); }
  T At(u64 index) { return buffer_[index]; }
  T Set(u64 index, T item) { buffer_[index] = item; }
  u64 Size() { return size_.load(); }

 private:
  const u64 max_size_;
  T *buffer_;
  std::atomic<u64> head_;
  std::atomic<u64> tail_;
  std::atomic<u64> size_;
};

class FIFOBatchReplacer {
 public:
  /**
   * Create a new FIFOReplacer.
   * @param num_nodes the maximum number of items the FIFOReplacer will be
   * required to store
   */
  explicit FIFOBatchReplacer(size_t num_nodes);
  FIFOBatchReplacer() = delete;
  ~FIFOBatchReplacer();

  bool Add(frame_id_t frame_id, u32 length, char *data);
  bool Victim(Item **item);
  bool IsFull();

  u64 Size();
  void Print();

  Item *head_;
  Item *tail_;
  u32 max_size_;
  u32 cur_size_;
  // std::vector<frame_id_t> *fifo_list_;
  // u32 fifo_head_;
  // u32 fifo_tail_;
};
class ZoneManagerPool;
typedef std::pair<Page *, u64> Slot;
class ZoneManager {
 public:
  ZoneManager() = delete;
  ZoneManager(Zone *z, u64 max_size = MAX_CACHED_PAGES_PER_ZONE);
  ~ZoneManager();

  void SetPoolPtr(ZoneManagerPool *zmp) { zmp_ = zmp; }
  bool AllocatePageId(page_id_t *page_id);
  // get page from hash map
  Page *GetPageImp(page_id_t page_id);
  Page *AllocateSeqPage(u64 length = 1);
  Page *GrabPageFrameImp(u64 length = 1);
  bool ReadPageFromZNSImp(bytes_t *data, zns_id_t zid, u32 page_nums = 1);
  // no lock
  Page *NewPageImp(page_id_t *page_id, u64 length = 1);

  /* allocate a page in zns
   * if length > 1 pageid will be consecutive in a zone*/
  Page *NewPage(page_id_t *page_id, u64 length = 1);
  /* rewrite a existed page into a new page in CoW-style*/
  Page *UpdatePage(page_id_t *page_id);
  /* read a existed page in zns*/
  Page *FetchPage(page_id_t page_id);
  /* unpin a page in circle buffer or read cache */
  bool UnpinPage(page_id_t page_id, bool is_dirty);
  void FlushAllPages();

  void FlushIfFull() {
    WriteLockGuard guard(rw_lock_);
    if (replacer_->IsFull()) {
      for (int i = 0; i < BATCH_SIZE; ++i) {
        Item *item = nullptr;
        if (!replacer_->Victim(&item)) break;
        Page *page = (Page *)item->data_;
        AppendPage(page);
      }
      FlushBatchedPage();
    }
  }

  void AppendPage(Page *page);
  void FlushBatchedPage();

  void AddPage(Slot slot);

  void RMPage(Page *page);

  void Print();
  bool IsFull(u64 length = 1);

  void HitHelper() {
    count_++;
    hit_++;
  }

  void MissHelper() {
    count_++;
    miss_++;
  }

 public:
  ZoneManagerPool *zmp_;
  // total number of pages of this zone
  // fixed bug
  // todo
  // wp_实际上就是zone内的偏移量（以page为单位）
  // 所以wp_高16位没卵用
  offset_t wp_;
  // remained pages of this zone
  offset_t cap_;
  // the end of this zone
  offset_t end_;
  Zone *zone_;
  int32_t buffer_pages_ = 0;
  char *pages_buffer_ = nullptr;
  // the zone id in the zns device
  zns_id_t zone_id_;
  std::mutex m_;

  std::unordered_map<page_id_t, Page *> page_table_;

  void *read_cache_;
  /* FIFO Write & Read Cacche */
  FIFOBatchReplacer *replacer_;
  u64 max_size_;
  /* RingFlusher */
  // CircleBuffer<Slot> *flusher_;

  // Helper variables
  u64 count_ = 0;
  u64 hit_ = 0;
  u64 miss_ = 0;
  std::unordered_map<page_id_t, frame_id_t> page_id_counts_;

  ReaderWriterLatch *rw_lock_;
#ifdef USE_LRU_BUFFER
  lru_buffer lru_buffer_;
#endif
#ifdef USE_SIEVE
  sieve_buffer sieve_;
#endif
};

class ZoneManagerPool {
 public:
  ZoneManagerPool() = delete;
  ZoneManagerPool(u32 pool_size, u32 num_instances_, const char *db_file);
  ~ZoneManagerPool();

  /* allocate a page in zns
   * if length > 1 pageid will be consecutive in a zone*/
  Page *NewPage(page_id_t *page_id, u64 length = 1);
  Page *NewPageFrom(page_id_t *page_id, u64 length, page_id_t from);
  /* rewrite a existed page into a new page in CoW-style*/
  Page *UpdatePage(page_id_t *page_id);
  /* read a existed page in zns*/
  Page *FetchPage(page_id_t page_id);

  bool UnpinPage(page_id_t page_id, bool is_dirty);

  void FlushAllPages();
  void FlushIfFull(page_id_t page_id) {
    zns_id_t zid = GET_ZONE_ID(page_id);
    return zone_buffers_[zone_table_[zid]]->FlushIfFull();
  }

  ZoneManager *GetZone(page_id_t page_id) {
    zns_id_t zid = GET_ZONE_ID(page_id);
    return zone_buffers_[zone_table_[zid]];
  }

  /**
   * Helper Function
   */
  u64 GetPoolSize();
  u64 GetFileSize();
  u64 GetReadCount();
  u64 GetWriteCount();

  void Print();
  void Close();
  void PrintReadCache();

 public:
  const u32 pool_size_;
  // total number of zones
  const u32 num_instances_;
  // round robin index for zone buffer
  std::atomic<zns_id_t> index_;
  std::mutex m_;
  ZnsManager *zns_;
  std::vector<ZoneManager *> zone_buffers_;
  // key is zone id, value is the offset in zone_buffers_
  // std::unordered_map<zns_id_t, u32> zone_table_;
  ThreadSafeMap<zns_id_t, u32> zone_table_;
};

class NodeRAII {
 public:
  NodeRAII(void *buffer_pool_manager, page_id_t &page_id,
           bool read_only = READ_ONLY)
      : buffer_pool_manager_(buffer_pool_manager), page_id_(page_id) {
    page_ = nullptr;
    dirty_ = false;
#ifdef NO_BUFFER_POOL
    disk_manager_ = ((ParallelBufferPoolManager *)buffer_pool_manager_)
                        ->GetDiskManager(page_id_);
    node_ = (char *)aligned_alloc(PAGE_SIZE, PAGE_SIZE);
    disk_manager_->read_page(page_id_, node_);
#elif defined(RAW_BUFFER_POOL)
    auto bpm = static_cast<ParallelBufferPoolManager *>(buffer_pool_manager_);
    page_ = bpm->FetchPage(page_id_);
    CheckAndInitPage();
#elif defined(ZNS_BUFFER_POOL)
    ZoneManagerPool *zmp = (ZoneManagerPool *)buffer_pool_manager_;
    if (read_only) {
      page_ = zmp->FetchPage(page_id_);
    } else {
      page_ = zmp->UpdatePage(&page_id_);
      page_id = page_id_;
    }
    // if(page_ != nullptr) {
    //   page_->Pin();
    // }
    CheckAndInitPage();
    read_flag_ = read_only;
#endif
    // DEBUG_PRINT("raii fetch\n");
  }

  NodeRAII(void *buffer_pool_manager, page_id_t *page_id,
           page_id_t from = INVALID_PAGE_ID)
      : buffer_pool_manager_(buffer_pool_manager) {
    page_ = nullptr;
#ifdef NO_BUFFER_POOL
    disk_manager_ = ((ParallelBufferPoolManager *)buffer_pool_manager_)
                        ->GetDiskManager(&page_id_);
    node_ = (char *)aligned_alloc(PAGE_SIZE, PAGE_SIZE);
    // disk_manager_->read_page(page_id_, (char *)node_);
    memset(node_, 0x00, PAGE_SIZE);
    dirty_ = true;
#elif defined(RAW_BUFFER_POOL)
    auto bpm = static_cast<ParallelBufferPoolManager *>(buffer_pool_manager_);
    page_ = bpm->NewPage(&page_id_);
    CheckAndInitPage();
#elif defined(ZNS_BUFFER_POOL)
    ZoneManagerPool *zmp = (ZoneManagerPool *)buffer_pool_manager_;
    if (from != INVALID_PAGE_ID) {
      page_ = zmp->NewPageFrom(&page_id_, 1, from);
    } else {
      page_ = zmp->NewPage(&page_id_);
    }
    read_flag_ = WRITE_FLAG;
    CheckAndInitPage();
#endif
    *page_id = page_id_;
  }

  void CheckAndInitPage() {
    if (page_ != nullptr) {
      node_ = reinterpret_cast<char *>(page_->GetData());
    } else {
      std::cout << "allocte error, out of memory for buffer pool" << std::endl;
      exit(-1);
    }
    dirty_ = false;
  }

  ~NodeRAII() {
#ifdef NO_BUFFER_POOL
    if (node_ != nullptr) {
      if (dirty_) {
        disk_manager_->write_page(page_id_, reinterpret_cast<char *>(node_));
      }
      free(node_);
    }
#elif defined(RAW_BUFFER_POOL)
    if (page_ != nullptr) {
      auto bpm = static_cast<ParallelBufferPoolManager *>(buffer_pool_manager_);
      bpm->UnpinPage(page_id_, dirty_);
      // DEBUG_PRINT("raii unpin\n");
    }
#elif defined(ZNS_BUFFER_POOL)
#if defined(USE_LRU_BUFFER) || defined(USE_SIEVE)
    if (page_ != nullptr) {
      ZoneManagerPool *zmp = (ZoneManagerPool *)buffer_pool_manager_;
      zmp->UnpinPage(page_id_, dirty_);
    }
#else
    if (page_ != nullptr) {
      page_->Unpin();
      if (page_->pin_count_ == 0) {
        DESTROY_PAGE(page_);
      }
    }
#endif
#endif
  }

  void *GetNode() { return node_; }
  Page *GetPage() { return page_; }
  page_id_t GetPageId() { return page_id_; }
  void SetDirty(bool is_dirty) { dirty_ = is_dirty; }
  bool SetLeafPtr(void *leaf_ptr) {
    if (page_ != nullptr) {
      page_->SetLeafPtr(leaf_ptr);
      return true;
    }
    return false;
  }

 private:
  void *buffer_pool_manager_;
  DiskManager *disk_manager_;
  page_id_t page_id_;
  Page *page_ = nullptr;
  char *node_ = nullptr;
  bool dirty_ = false;
  bool read_flag_ = false;
};
