#include "buffer.h"

BufferPoolManager::BufferPoolManager(size_t pool_size,
                                     DiskManager* disk_manager)
    : BufferPoolManager(pool_size, 1, 0, disk_manager) {
  count_ = hit_ = miss_ = 0;
}

BufferPoolManager::BufferPoolManager(size_t pool_size, uint32_t num_instances,
                                     uint32_t instance_index,
                                     DiskManager* disk_manager)
    : pool_size_(pool_size),
      num_instances_(num_instances),
      instance_index_(instance_index),
      next_page_id_(instance_index),
      disk_manager_(disk_manager) {
  assert(num_instances > 0 &&
         "If BPI is not part of a pool, then the pool size should just be 1");
  assert(instance_index < num_instances &&
         "BPI index cannot be greater than the number of BPIs in the "
         "pool. In non-parallel case, index should just be 1.");
  // We allocate a consecutive memory space for the buffer pool.
  page_data_ = (char*)aligned_alloc(PAGE_SIZE, pool_size_ * PAGE_SIZE);
  assert(((size_t)page_data_ & (PAGE_SIZE - 1)) == 0);
  pages_ = new Page[pool_size_];
  replacer_ = new LRUReplacer(pool_size);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    pages_[i].SetData(page_data_ + i * PAGE_SIZE);
    free_list_.emplace_back(static_cast<frame_id_t>(i));
  }

  int ret = pthread_mutex_init(&mutex_, nullptr);
  VERIFY(!ret);
  ret = pthread_cond_init(&cond_, nullptr);
  VERIFY(!ret);
  count_ = hit_ = miss_ = 0;
  // page_id_counts_.reserve(2000 * 1000);
}

BufferPoolManager::~BufferPoolManager() {
  // Print();

  delete[] pages_;
  free(page_data_);
  delete replacer_;

  int ret = pthread_mutex_destroy(&mutex_);
  VERIFY(!ret);
  ret = pthread_cond_destroy(&cond_);
  VERIFY(!ret);
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
  // Make sure you call DiskManager::WritePage!
  GrabLock();
  if (page_id == INVALID_PAGE_ID ||
      page_table_.find(page_id) == page_table_.end()) {
    ReleaseLock();
    return false;
  }

  frame_id_t cur_frame = page_table_[page_id];
  Page* cur_page = pages_ + cur_frame;
  if (cur_page->is_dirty_) {
    disk_manager_->write_page(page_id, cur_page->GetData());
    cur_page->is_dirty_ = false;
  }
  ReleaseLock();
  return true;
}

// 1. FlushPage内部又有lock guard
void BufferPoolManager::FlushAllPages() {
  // Lock_guard lk(latch_);
  for (const auto& key : page_table_) {
    // INFO_PRINT("%p flush page:%4u :%4u\n", this, key.first, key.second);
    FlushPage(key.first);
    // UnpinPage(key.first, false);
  }
}

Page* BufferPoolManager::NewPage(page_id_t* page_id) {
  // 1.   If all the pages in the buffer pool are pinned, return nullptr.
  // 2.   Pick a victim page P from either the free list or the replacer. Always
  // pick from the free list first.
  // 3.   Update P's metadata, zero out memory and add P to the page table.
  // 4.   Set the page ID output parameter. Return a pointer to P.
  // ValidatePageId(*page_id);

  GrabLock();
  page_id_t tmp_page_id = AllocatePage();

  // 3 pick a victim frame from free_lists or replacer
  Page* ret_page = GrabPageFrame();
  VERIFY(ret_page != nullptr);
  frame_id_t cur_frame_id = static_cast<frame_id_t>(ret_page - pages_);
  page_table_[tmp_page_id] = cur_frame_id;
  // ret_page->pin_count_ = 1;
  ret_page->Pin();
  ret_page->page_id_ = tmp_page_id;
  ret_page->is_dirty_ = true;
  replacer_->Pin(cur_frame_id);
  ret_page->ResetMemory();
  *page_id = tmp_page_id;

  // DEBUG_PRINT("new page_id:%4u frame_id:%4u\n", tmp_page_id, cur_frame_id);
  // new page is not hit, add miss
  page_id_counts_[tmp_page_id] = 1;
  miss_++;
  count_++;

  ReleaseLock();
  return ret_page;
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
  // 1.     Search the page table for the requested page (P).
  // 1.1    If P exists, pin it and return it immediately.
  // 1.2    If P does not exist, find a replacement page (R) from either the
  // free list or the replacer.
  //        Note that pages are always found from the free list first.
  // 2.     If R is dirty, write it back to the disk.
  // 3.     Delete R from the page table and insert P.
  // 4.     Update P's metadata, read in the page content from disk, and then
  // return a pointer to P.
  GrabLock();
  count_++;
  Page* ret_page = nullptr;

  if (page_table_.find(page_id) != page_table_.end()) {
    // 1.1
    frame_id_t cur_frame_id = page_table_[page_id];
    ret_page = pages_ + cur_frame_id;
    // need update pin_count and notify the lru replacer;
    ret_page->pin_count_++;
    replacer_->Pin(cur_frame_id);
    // DEBUG_PRINT("fetch page_id:%4u frame_id:%4u\n", page_id, cur_frame_id);
    page_id_counts_[page_id]++;
    hit_++;

    ReleaseLock();
    return ret_page;
  }
  miss_++;
  // 1.2 2 3
  ret_page = GrabPageFrame();
  if (ret_page == nullptr) {
    ReleaseLock();
    return nullptr;  // no frames found neither in free_list nor replacer for
                     // target page(page_id)
  }

  // 4 update some data
  disk_manager_->read_page(page_id, ret_page->data_);
  page_table_[page_id] = static_cast<frame_id_t>(ret_page - pages_);
  ret_page->page_id_ = page_id;  // update page_id
  ret_page->pin_count_ = 1;
  ret_page->is_dirty_ = false;
  // NOTE: 此时frame ID是否可能出现在replacer中
  // 有可能。deletePage把frame加入free list时，没有将它从replacer移除
  // 但获取frame时，会先从free list里取，然后需要pin，这样就把它从replacer移除了
  // 但是从replacer Victim中取出的页面，自然是删除的页面，所以不会出现这种情况
  replacer_->Pin(ret_page - pages_);
  ReleaseLock();
  return ret_page;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
  // 0.   Make sure you call DeallocatePage!
  // 1.   Search the page table for the requested page (P).
  // 1.   If P does not exist, return true.
  // 2.   If P exists, but has a non-zero pin-count, return false. Someone is
  // using the page.
  // 3.   Otherwise, P can be deleted. Remove P from the page table, reset its
  // metadata and return it to the free list.

  // 0 ? only delete page in BufferPool, not in disk
  GrabLock();

  if (page_table_.find(page_id) == page_table_.end()) {
    ReleaseLock();
    return false;
  }

  // 1 2
  frame_id_t cur_frame_id = page_table_[page_id];
  Page* cur_page = pages_ + cur_frame_id;
  // if (cur_page->GetPinCount() != 0) {
  //   ReleaseLock();
  //   return false;
  // }

  // 3
  page_table_.erase(page_id);
  replacer_->Pin(cur_frame_id);
  cur_page->page_id_ = INVALID_PAGE_ID;
  cur_page->pin_count_ = 0;
  cur_page->is_dirty_ = false;
  cur_page->ResetMemory();
  free_list_.push_back(cur_frame_id);

  SignalForFreeFrame();
  ReleaseLock();
  return true;
}

/***
 * @attention unpin don't delete maping in page_table, so it can fetch the
 * unpinned page in page_table. And the unpin page will be both in page_table
 * and replacer.
 */
bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  GrabLock();

  // 0 Make sure you can unpin page_id
  if (page_table_.find(page_id) == page_table_.end()) {
    ReleaseLock();
    return true;
  }
  // 1 get page object
  frame_id_t cur_frame_id = page_table_[page_id];
  Page* cur_page = pages_ + cur_frame_id;

  // DEBUG_PRINT("unpin page_id:%4u frame_id:%4u\n", page_id, cur_frame_id);

  if (is_dirty) {
    // if page is dirty but is_dirty indicates non-dirty,
    // it also should be dirty
    cur_page->is_dirty_ |= is_dirty;
  }

  if (cur_page->GetPinCount() > 0) {
    cur_page->pin_count_--;
  }
  if (cur_page->GetPinCount() == 0) {
    replacer_->Unpin(cur_frame_id);
    // page_table_.erase(page_id);
    SignalForFreeFrame();
  }
  ReleaseLock();
  return true;
}

page_id_t BufferPoolManager::AllocatePage() {
  const page_id_t next_page_id = next_page_id_;
  next_page_id_ += num_instances_;
  ValidatePageId(next_page_id);
  return next_page_id;
}

void BufferPoolManager::ValidatePageId(const page_id_t page_id) const {
  // allocated pages mod back to this BPI
  //   CHECK(page_id % num_instances_ ==
  // instance_index_);
  VERIFY(page_id % num_instances_ == instance_index_);
}

Page* BufferPoolManager::GrabPageFrame() {
  Page* ret_page = nullptr;
  frame_id_t cur_frame_id = -1;
  while (true) {
    if (!free_list_.empty()) {
      // 1. first find in free-lists
      cur_frame_id = free_list_.front();
      free_list_.pop_front();
      ret_page = pages_ + cur_frame_id;
      // DEBUG_PRINT("grab in free lists page_id:%4u frame_id:%4u\n",
      // ret_page->GetPageId(), cur_frame_id);
    } else if (replacer_->Victim(&cur_frame_id)) {
      // 2. then find in replacer
      ret_page = pages_ + cur_frame_id;
      if (ret_page->is_dirty_) {
        // flush raw page
        disk_manager_->write_page(ret_page->GetPageId(), ret_page->GetData());
      }
      // delete old page_id -> frame_id map
      page_table_.erase(ret_page->GetPageId());
      // DEBUG_PRINT("grab in replacer page_id:%4u frame_id:%4u\n",
      //             ret_page->GetPageId(), cur_frame_id);
    }
    if (ret_page != nullptr) {
      break;
    }
    // XXX: timed wait?
    // reset the next page id?
    WaitForFreeFrame();
  }
  return ret_page;
}

void BufferPoolManager::GrabLock() {
  int ret = pthread_mutex_lock(&mutex_);
  VERIFY(!ret);
}

void BufferPoolManager::ReleaseLock() {
  int ret = pthread_mutex_unlock(&mutex_);
  VERIFY(!ret);
}

void BufferPoolManager::SignalForFreeFrame() {
  int ret = pthread_cond_signal(&cond_);
  VERIFY(!ret);
}

void BufferPoolManager::WaitForFreeFrame() {
  int ret = pthread_cond_wait(&cond_, &mutex_);
  VERIFY(!ret);
}

void BufferPoolManager::Print() {
  INFO_PRINT(
      "Instance ID:%2u Page table size:%2lu Replacer size: %2lu"
      " Free list size: %2lu \n",
      instance_index_, page_table_.size(), replacer_->Size(),
      free_list_.size());
}

ParallelBufferPoolManager::ParallelBufferPoolManager(
    size_t num_instances, size_t pool_size, DiskManager* disk_manager) {
  // Allocate and create individual BufferPoolManagerInstances
  bpmis_.resize(num_instances);
  num_instances_ = num_instances;
  index_ = 0;
  for (size_t i = 0; i < num_instances_; i++) {
    bpmis_[i] =
        new BufferPoolManager(pool_size, num_instances_, i, disk_manager);
  }
  disk_manager_ = disk_manager;
}

ParallelBufferPoolManager::ParallelBufferPoolManager(size_t num_instances,
                                                     size_t pool_size,
                                                     std::string file_name,
                                                     bool is_one_file) {
  // Allocate and create individual BufferPoolManagerInstances
  bpmis_.resize(num_instances);
  num_instances_ = num_instances;
  index_ = 0;
  in_one_file_ = is_one_file;
  if (is_one_file) {
    auto tmp_file_name = file_name;
    auto disk_manager = new DiskManager(tmp_file_name.c_str(), 1);
    for (size_t i = 0; i < num_instances_; i++) {
      bpmis_[i] =
          new BufferPoolManager(pool_size, num_instances_, i, disk_manager);
    }
    disk_manager_ = disk_manager;
  } else {
    for (size_t i = 0; i < num_instances_; i++) {
      auto tmp_file_name = file_name + "_" + std::to_string(i);
      auto disk_manager = new DiskManager(tmp_file_name.c_str(), num_instances);
      bpmis_[i] =
          new BufferPoolManager(pool_size, num_instances_, i, disk_manager);
    }
    disk_manager_ = nullptr;
  }
}

// Update constructor to destruct all BufferPoolManagerInstances and
// deallocate any associated memory
ParallelBufferPoolManager::~ParallelBufferPoolManager() {
  Print();
  FlushAllPages();

  if (disk_manager_) {
    delete disk_manager_;
  } else {
    for (auto& buffer : bpmis_) {
      if (buffer->disk_manager_) {
        delete buffer->disk_manager_;
      }
      buffer->disk_manager_ = nullptr;
    }
  }

  for (auto& buffer : bpmis_) {
    delete buffer;
  }
}

u64 ParallelBufferPoolManager::GetPoolSize() {
  // Get size of all BufferPoolManagerInstances
  size_t total = 0;
  for (auto& buffer : bpmis_) {
    total += buffer->GetPoolSize();
  }
  return total;
}

u64 ParallelBufferPoolManager::GetFileSize() {
  if (in_one_file_) {
    return bpmis_[0]->disk_manager_->get_file_size();
  }

  u64 total = 0;
  for (auto& buffer : bpmis_) {
    total += buffer->disk_manager_->get_file_size();
  }
  return total;
};
u64 ParallelBufferPoolManager::GetReadCount() {
  if (in_one_file_) {
    return bpmis_[0]->disk_manager_->get_read_count();
  }

  u64 total = 0;
  for (auto& buffer : bpmis_) {
    total += buffer->disk_manager_->get_read_count();
  }
  return total;
};
u64 ParallelBufferPoolManager::GetWriteCount() {
  if (in_one_file_) {
    return bpmis_[0]->disk_manager_->get_write_count();
  }

  u64 total = 0;
  for (auto& buffer : bpmis_) {
    total += buffer->disk_manager_->get_write_count();
  }
  return total;
};

BufferPoolManager* ParallelBufferPoolManager::GetBufferPoolManager(
    page_id_t page_id) {
  // Get BufferPoolManager responsible for handling given page id. You can use
  // this method in your other methods.
  return bpmis_[page_id % num_instances_];
}

DiskManager* ParallelBufferPoolManager::GetDiskManager(page_id_t page_id) {
  // Get BufferPoolManager responsible for handling given page id. You can use
  // this method in your other methods.
  return bpmis_[page_id % num_instances_]->disk_manager_;
}

Page* ParallelBufferPoolManager::FetchPage(page_id_t page_id) {
  // Fetch page for page_id from responsible BufferPoolManager
  return dynamic_cast<BufferPoolManager*>(GetBufferPoolManager(page_id))
      ->FetchPage(page_id);
}

bool ParallelBufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  // Unpin page_id from responsible BufferPoolManager
  return dynamic_cast<BufferPoolManager*>(GetBufferPoolManager(page_id))
      ->UnpinPage(page_id, is_dirty);
}

bool ParallelBufferPoolManager::FlushPage(page_id_t page_id) {
  // Flush page_id from responsible BufferPoolManager
  return dynamic_cast<BufferPoolManager*>(GetBufferPoolManager(page_id))
      ->FlushPage(page_id);
}

Page* ParallelBufferPoolManager::NewPage(page_id_t* page_id) {
  // create new page. We will request page allocation in a round robin manner
  // from the underlying BufferPoolManagerInstances
  // 1.   From a starting index of the BPMIs, call NewPageImpl until either 1)
  // success and return 2) looped around to starting index and return nullptr
  // 2.   Bump the starting index (mod number of instances) to start search at a
  // different BPMI each time this function is called

  // fetch_add() returns old value
  // We don't care about unsigned int overflow
  size_t start_index = index_;
  size_t loop_index = index_;
  index_ = (index_ + 1) % num_instances_;
  Page* ret_page = nullptr;
  while (true) {
    ret_page = bpmis_[loop_index]->NewPage(page_id);
    if (ret_page != nullptr) {
      return ret_page;
    }
    loop_index = (loop_index + 1) % num_instances_;
    if (loop_index == start_index) {
      return nullptr;
    }
  }
  return ret_page;
}

DiskManager* ParallelBufferPoolManager::GetDiskManager(page_id_t* page_id) {
  size_t start_index = index_;
  size_t loop_index = index_;
  index_ = (index_ + 1) % num_instances_;
  DiskManager* ret = nullptr;
  while (true) {
    ret = bpmis_[loop_index]->disk_manager_;
    *page_id = bpmis_[loop_index]->AllocatePage();
    if (ret != nullptr) {
      return ret;
    }
    loop_index = (loop_index + 1) % num_instances_;
    if (loop_index == start_index) {
      return nullptr;
    }
  }
  return ret;
}

bool ParallelBufferPoolManager::DeletePage(page_id_t page_id) {
  // Delete page_id from responsible BufferPoolManager
  return dynamic_cast<BufferPoolManager*>(GetBufferPoolManager(page_id))
      ->DeletePage(page_id);
}

void ParallelBufferPoolManager::FlushAllPages() {
  // flush all pages from all BufferPoolManagerInstances
  for (auto& buffer : bpmis_) {
    auto instance = dynamic_cast<BufferPoolManager*>(buffer);
    instance->FlushAllPages();
  }
}

void ParallelBufferPoolManager::Print() {
  u64 page_table_size = 0;
  u64 replacer_size = 0;
  u64 free_list_size = 0;

  u64 count = 0;
  u64 miss = 0;
  u64 hit = 0;

  u64 total = 0;
  for (auto& buffer : bpmis_) {
    auto instance = dynamic_cast<BufferPoolManager*>(buffer);
    // instance->Print();
    page_table_size += instance->page_table_.size();
    replacer_size += instance->replacer_->Size();
    free_list_size += instance->free_list_.size();

    count += instance->count_;
    miss += instance->miss_;
    hit += instance->hit_;
    total += instance->page_id_counts_.size();
  }

  std::vector<page_id_t> page_ids;
  page_ids.reserve(total);
  for (auto& buffer : bpmis_) {
    auto instance = dynamic_cast<BufferPoolManager*>(buffer);
    for (auto& count : instance->page_id_counts_) {
      page_ids.push_back(count.second);
    }
  }
  std::sort(page_ids.begin(), page_ids.end());
  size_t avg = 0;
  for (size_t i = 0; i < total; i++) {
    avg += page_ids[i];
  }
  avg /= total;
  auto sz = total;
  INFO_PRINT("[ParaBufferPool]  Avg Count For a BufferPage: %3lu hits:", avg);
  std::vector<float> percentiles = {0.0, 0.1, 0.2, 0.3, 0.4,   0.5,
                                    0.6, 0.7, 0.8, 0.9, 0.999, 0.9999999};
  INFO_PRINT("  Percentile:")
  for (auto& p : percentiles) {
    INFO_PRINT(" {%2.3lf%%->%4lu}, ", p * 100, page_ids[size_t(p * sz)]);
  }
  INFO_PRINT("\n");

  INFO_PRINT("[ParaBufferPool] Total size: " KMAG "%4.2f" KRESET
             " MB Instance Nums:%2lu Page table "
             "size:%4lu Replacer size: %4lu Free list size: %4lu \n",
             page_table_size * 4.0 / 1024, num_instances_, page_table_size,
             replacer_size, free_list_size);

  double miss_ratio = miss * 100.0 / count;
  double hit_ratio = hit * 100.0 / count;
  printf("[ParaBufferPool] count:%4lu miss: %4lu %2.2lf%% hit: %4lu " KCYN
         "%2.2lf%%" KRESET "\n",
         count, miss, miss_ratio, hit, hit_ratio);
}

/*
 * ===================================================================
 * ===================================================================
 * ======================== FIFO Batch Buffer ========================
 * ===================================================================
 * ===================================================================
 */

FIFOBatchReplacer::FIFOBatchReplacer(size_t num_pages) {
  // fifo_list_ = new std::vector<frame_id_t>(num_pages);
  head_ = new Item(-1, -1, nullptr);
  tail_ = new Item(-1, -1, nullptr);

  head_->next_ = tail_;
  tail_->prev_ = head_;

  max_size_ = num_pages;
  cur_size_ = 0;
}

FIFOBatchReplacer::~FIFOBatchReplacer() {
  while (head_) {
    Item* temp = head_;
    head_ = head_->next_;
    delete temp;
  }
}

bool FIFOBatchReplacer::Add(frame_id_t frame_id, u32 length, char* data) {
  if (cur_size_ == max_size_) {
    return false;
  }
  Item* item = new Item(frame_id, length, data);
  item->prev_ = head_;

  item->next_ = head_->next_;
  head_->next_->prev_ = item;
  head_->next_ = item;

  cur_size_++;
  return true;
};

bool FIFOBatchReplacer::IsFull() { return cur_size_ == max_size_; }

bool FIFOBatchReplacer::Victim(Item** item) {
  if (cur_size_ == 0) {
    return false;
  }

  Item* temp = tail_->prev_;
  temp->prev_->next_ = tail_;
  tail_->prev_ = temp->prev_;
  *item = temp;

  cur_size_--;
  return true;
}

u64 FIFOBatchReplacer::Size() { return cur_size_; }

void FIFOBatchReplacer::Print() {
  Item* temp = head_;
  while (temp) {
    std::cout << temp->frame_id_ << " ";
    temp = temp->next_;
  }
  std::cout << std::endl;
}

/*
 * ===================================================================
 * ===================================================================
 * ======================== ZoneManager   ============================
 * ===================================================================
 * ===================================================================
 */

ZoneManager::ZoneManager(Zone* zone, u64 max_size)
    : zone_(zone),
      zone_id_(zone->GetZoneNr())
#ifdef USE_LRU_BUFFER
      ,
      lru_buffer_(LRU_BUFFER_SIZE)
#endif
#ifdef USE_SIEVE
      ,
      sieve_(SIEVE_SIZE)
#endif
{
  // zone_id_ = zone->GetZoneNr();
  wp_ = zone->wp_ / PAGE_SIZE;
  cap_ = zone->GetCapacityLeft() / PAGE_SIZE;
  end_ = zone->start_ / PAGE_SIZE + zone->GetMaxCapacity() / PAGE_SIZE;

  // printf("[ZoneManager] zone_id:%lu wp:%lu cap:%lu end:%lu\n", zone_id_, wp_,
  //  cap_, end_);
  // zone_->Print();

  replacer_ = new FIFOBatchReplacer(max_size);
  // flusher_ = new CircleBuffer<Slot>(max_size);
  // flusher_ = nullptr;
  rw_lock_ = new ReaderWriterLatch();
  read_cache_ = nullptr;
}
ZoneManager::~ZoneManager() {
  /*
   * Author: chenbo
   * Time: 2024-03-29 18:56:27
   * Description: todo 将未下刷的数据下刷
   */
  FlushAllPages();
  zone_->Counts();
  SAFE_DELETE(rw_lock_);
  // SAFE_DELETE(read_cache_);
  // SAFE_DELETE(zone_);
  SAFE_DELETE(replacer_);
  if (pages_buffer_ != nullptr) {
    free(pages_buffer_);
  }
  // SAFE_DELETE(flusher_);
}
static offset_t get_offset(page_id_t page_id, Zone* zone_) {
  // offset_t zone_offset = zone_->GetMaxCapacity() * GET_ZONE_ID(page_id);
  offset_t zone_offset = 0;
  offset_t offset = zone_offset + GET_ZONE_OFFSET(page_id) * PAGE_SIZE;
  return offset;
}

void ZoneManager::FlushAllPages() {
  WriteLockGuard guard(rw_lock_);
  // printf("%s\n", __func__);
  while (replacer_->Size() > 0) {
    Item* tmp = nullptr;
    replacer_->Victim(&tmp);
    Page* page = reinterpret_cast<Page*>(tmp->data_);
    offset_t offset = get_offset(page->GetPageId(), zone_);
    u64 length = tmp->length;
    AppendPage(page);
    delete (tmp);
  }
  FlushBatchedPage();
}

void ZoneManager::AppendPage(Page* page) {
  if (__glibc_unlikely(pages_buffer_ == nullptr)) {
    // pages_buffer_ = new char[PAGE_SIZE * BATCH_SIZE];
    pages_buffer_ = (char*)aligned_alloc(4096, PAGE_SIZE * BATCH_SIZE);
    memset(pages_buffer_, 0, BATCH_SIZE * PAGE_SIZE);
  }
  page->WLatch();
  memcpy(pages_buffer_ + buffer_pages_ * PAGE_SIZE, page->GetData(), PAGE_SIZE);
  buffer_pages_++;
  if (buffer_pages_ == BATCH_SIZE) {
    FlushBatchedPage();
  }
#ifdef USE_LRU_BUFFER
  auto epage = lru_buffer_.evict_and_insert(page->page_id_, page);
#elif defined(USE_SIEVE)
  auto epage = sieve_.evict_and_insert(page->page_id_, page);
#else
  Page* epage = nullptr;
  page->Unpin();
  if (page->pin_count_ == 0) {
    DESTROY_PAGE(page);
  }
#endif
  if (__glibc_likely(epage != nullptr)) {
    auto cnt = epage->pin_count_.fetch_sub(1);
    epage->SetStatus(EVICTED);
    if (cnt == 1) {
      DESTROY_PAGE(epage);
    }
  }
  page_table_.erase(page->GetPageId());
  page->WUnlatch();
  // }
}

void ZoneManager::FlushBatchedPage() {
  if (buffer_pages_ == 0) return;
  auto ret = zone_->Append(pages_buffer_, buffer_pages_ * PAGE_SIZE);
  if (ret != Code::kOk) {
    DEBUG_PRINT("zone id:%lu append failed wp:%lu cap:%lu end:%lu\n", zone_id_,
                wp_, cap_, end_);
  }
  buffer_pages_ = 0;
}

/**
 * every time add a page to flusher,
 * check to flush the existed page in flusher to zns
 */

void ZoneManager::AddPage(Slot slot) {
  // 1.1 add the current evicted pages to flusher
  // flusher_->Push(slot);
  // 1.2 if no exited page in flusher, just return
  Slot tmp = slot;

  // 1.3 flushed the existed page
  Page* page = tmp.first;
  u64 length = tmp.second;
  // if (length > 0) {
  // length indicates the page not flushed
  // page_id_t page_id = page->GetPageId();
  // uint64_t offset = get_offset(page_id, zone_);
  // if(offset != this->zone_->wp_) {
  //   printf("[ZoneManager] page_id:%lu zone_offset:%lu wp:%lu zoneid:%lu
  //   thiszoneid:%lu\n", page_id, offset, this->zone_->wp_,
  //   this->zone_->GetZoneNr(), this->zone_id_);
  //   // printf("[ZoneManager] page_id:%lu zone_offset:%lu wp:%lu\n", page_id,
  //   offset, this->zone_->wp_);
  // }
  // assert(offset == this->zone_->wp_);
  zone_->Append(page->GetData(), length * PAGE_SIZE);

  // small batch buffer that merge multi page into big write
  // AppendPage(page);
  // zone_->Append(page, buffer_pages_ * PAGE_SIZE);

  // 2. after flushed to zns, it will be safe to umap the items
  //
  for (u64 i = 0; i < length; i++) {
    // DEBUG_PRINT("%lu pin count %d \n", i, page[i].GetPinCount());
    // buffer_pool_manager_->page_table_.erase(page[i].GetPageId());
    page_table_.erase(page[i].GetPageId());
    page[i].Unpin();
    page[i].SetStatus(FLUSHED);
    // ATOMIC_SPIN_UNTIL(page[i].GetReadCount(), 0);
    // page[i].SetStatus(FLUSHED);
  }

  if (page->GetReadCount() < MAX_RESEVER_THR && page->GetPinCount() == 0) {
    // 1.1 the page is not hot, just destroy it
    DESTROY_PAGE(page);
  } else if (page->GetReadCount() >= MAX_RESEVER_THR) {
    // 1.2 if the page is hot add it to read cache
    page->Pin();
#ifdef USE_LRU_BUFFER
    auto epage = lru_buffer_.evict_and_insert(page->page_id_, page);
#elif defined(USE_SIEVE)
    auto epage = sieve_.evict_and_insert(page->page_id_, page);
#else
    Page* epage = nullptr;
    page->Unpin();
    if (page->pin_count_ == 0) {
      DESTROY_PAGE(page);
    }
#endif
    if (epage != nullptr) {
      auto cnt = epage->pin_count_.fetch_sub(1);
      if (cnt == 1) {
        DESTROY_PAGE(epage);
      }
    }
  }
}

Page* ZoneManager::AllocateSeqPage(u64 length) {
  Page* tmp_page = new Page[length];
  char* page_data = (char*)aligned_alloc(PAGE_SIZE, length * PAGE_SIZE);
  // memset(page_data, 0, length * PAGE_SIZE);
  if (page_data == nullptr || tmp_page == nullptr) {
    return nullptr;
  }
  for (u64 i = 0; i < length; i++) {
    tmp_page[i].pin_count_ = 0;
    tmp_page[i].SetStatus(ACTIVE);
    tmp_page[i].SetData(page_data + i * PAGE_SIZE);
  }
  return tmp_page;
}

Page* ZoneManager::GrabPageFrameImp(u64 length) {
  Page* ret_page = nullptr;
  Item* cur_item = nullptr;
  // DEBUG_PRINT("call of zone grab page frame\n");
  if (!replacer_->IsFull()) {
    // DEBUG_PRINT("!empty\n");
    return AllocateSeqPage(length);
  } else if (replacer_->Victim(&cur_item)) {
    // 2. then find in replacer
    // DEBUG_PRINT("victim frame_id:%u\n", cur_item->frame_id_);
    ret_page = reinterpret_cast<Page*>(cur_item->data_);
    u64 length = cur_item->length;
    AppendPage(ret_page);
    // DESTROY_PAGE(ret_page);
    delete cur_item;
    cur_item = nullptr;
    for (int i = 1; i < BATCH_SIZE; ++i) {
      if (replacer_->Victim(&cur_item)) {
        ret_page = (Page*)cur_item->data_;
        AppendPage(ret_page);
        delete cur_item;
      }
    }
    FlushBatchedPage();
    // AddPage({ret_page, length});
    Page* tmp_page = AllocateSeqPage(length);
    return tmp_page;
  }
  return nullptr;
}

Page* ZoneManager::GetPageImp(page_id_t page_id) {
  Page* ret_page = nullptr;
  if (page_table_.find(page_id) != page_table_.end()) {
    // 1.1 first find in fifo write buffer
    ret_page = page_table_[page_id];
    ret_page->Pin();

    HitHelper();
    page_id_counts_[page_id]++;
  }
  return ret_page;
}

Page* ZoneManager::NewPageImp(page_id_t* page_id, u64 length) {
  Page* ret_page = GrabPageFrameImp(length);
  if (ret_page == nullptr) {
    return nullptr;
  }

  for (u64 i = 0; i < length; i++) {
    if (!AllocatePageId(page_id + i)) {
      return nullptr;
    }
    ret_page[i].page_id_ = page_id[i];
    ret_page[i].read_count_ = 1;
    // ret_page[i].pin_count_ = 1;
    ret_page[i].Pin();
    ret_page[i].is_dirty_ = true;
    page_table_[page_id[i]] = ret_page + i;

    page_id_counts_[page_id[i]] = 1;
    MissHelper();
  }
  replacer_->Add(page_id[0], length, (char*)(ret_page));
  ret_page[0].Pin();
  return ret_page;
}

Page* ZoneManager::NewPage(page_id_t* page_id, u64 length) {
  WriteLockGuard guard(rw_lock_);
  return NewPageImp(page_id, length);
}

Page* ZoneManager::UpdatePage(page_id_t* page_id) {
  WriteLockGuard guard(rw_lock_);
  Page* ret_page = nullptr;
  // 1.1 find in fifo buffer first
  ret_page = GetPageImp(*page_id);
  if (ret_page != nullptr) {
    ret_page->WLatch();
    if (ret_page->IsFlushed()) {
      ret_page->Unpin();
      ret_page->WUnlatch();
      goto new_one_page;
    }
    ret_page->read_count_++;
    ret_page->WUnlatch();
    return ret_page;
  }
new_one_page:
  // 2. allocate a new page_id in zns;
  page_id_t tmp_page_id = INVALID_PAGE_ID;
  // Attenion: MissHelper() is in NewPageImp();
  Page* new_page = NewPageImp(&tmp_page_id);
  Page* evicted = nullptr;
  // 2.1 find page data in read cache
#ifdef USE_LRU_BUFFER
  evicted = lru_buffer_.evict(*page_id);
#elif defined(USE_SIEVE)
  evicted = sieve_.evict(*page_id);
#endif
  if (evicted != nullptr) {
    memcpy(new_page->GetData(), evicted->GetData(), PAGE_SIZE);
    int cnt = evicted->pin_count_.fetch_sub(1);
    evicted->SetStatus(PageStatus::EVICTED);
    if (cnt == 1) DESTROY_PAGE(evicted);
    *page_id = tmp_page_id;
    return new_page;
  }

  // 2.2 find page data in zns ssd
  ReadPageFromZNSImp((bytes_t*)(new_page->GetData()), *page_id);
  *page_id = tmp_page_id;
  return new_page;
}

// 1.find in write buffer (fifo)
// 2.find in read cache (lru or sieve)
// 3.read data from zns ssd
Page* ZoneManager::FetchPage(page_id_t page_id) {
  ReadLockGuard guard(rw_lock_);
  // 1. find in RingBuffer first
  Page* page = nullptr;
  if (page = GetPageImp(page_id)) {
    page->read_count_++;
    return page;
  }
  // 2. find in read cache,
  // page = read_cache_->FetchPage(page_id);
#ifdef USE_LRU_BUFFER
  if (page = lru_buffer_.fetch(page_id)) {
    page->Pin();
    lru_buffer_._hit++;
    page->read_count_++;
    HitHelper();
    return page;
  }
  lru_buffer_._miss++;
#elif defined(USE_SIEVE)
  if (page = sieve_.fetch(page_id)) {
    page->Pin();
    page->read_count_++;
    HitHelper();
    return page;
  }
#endif

  MissHelper();
  page_id_counts_[page_id]++;

  // 3. if not exist in LRU cache, then fetch from disk
  page = AllocateSeqPage(1);
  int ret = 0;
  if (ret = ReadPageFromZNSImp((bytes_t*)page->GetData(), page_id)) {
    FATAL_PRINT("reading page:%ld %s error read zns\n", page_id, strerror(ret));
  } else {
    page->page_id_ = page_id;
    page->Pin();
    page->Pin();
    page->read_count_ = 1;
    page->is_dirty_ = false;
#ifdef USE_LRU_BUFFER
    Page* evicted = lru_buffer_.evict_and_insert(page_id, page);
    if (evicted != nullptr) {
      evicted->SetStatus(PageStatus::EVICTED);
      int cnt = evicted->pin_count_.fetch_sub(1);
      if (cnt == 1) {
        DESTROY_PAGE(evicted);
      }
    }
#elif defined(USE_SIEVE)
    Page* evicted = sieve_.evict_and_insert(page_id, page);
    if (evicted != nullptr) {
      // evicted->SetStatus(EVICTED);
      int cnt = evicted->pin_count_.fetch_sub(1);
      if (cnt == 1) {
        DESTROY_PAGE(evicted);
      }
    }
#else
    // only works for  pure fifo without any read-cache
    // page_table_[page_id] = page;
    page->Unpin();
#endif
  }
  return page;
}

bool ZoneManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  // WriteLockGuard guard(rw_lock_);
  ReadLockGuard guard(rw_lock_);
  // 1. find in RingBuffer first
  // 0 Make sure you can unpin page_id
  Page* cur_page = nullptr;
  if (page_table_.find(page_id) == page_table_.end()) {
#ifdef USE_LRU_BUFFER
    cur_page = lru_buffer_.touch(page_id);
#elif defined(USE_SIEVE)
    cur_page = sieve_.touch(page_id);
#else
#endif
    if (cur_page != nullptr) {
      goto GC;
    }
    return true;
  }
  // 1 get page object
  cur_page = page_table_[page_id];
  if (is_dirty) {
    // if page is dirty but is_dirty indicates non-dirty,
    // it also should be dirty
    cur_page->is_dirty_ |= is_dirty;
  }
GC:
  // cur_page->Unpin();
  int cnt = cur_page->pin_count_.fetch_sub(1);
  if (cur_page->IsEvicted() && cnt == 1) {
    DESTROY_PAGE(cur_page);
  }
  return true;
}

bool ZoneManager::AllocatePageId(page_id_t* page_id) {
restart:
  if (wp_ < end_) {
    *page_id = MAKE_PAGE_ID(zone_id_, wp_);
    wp_++;
    return true;
  } else {
    // DEBUG_PRINT("zone:%lu is full :wp_ %6lu end_ %6lu\n", zone_id_, wp_,
    // end_); evict and flush all pages in replacer
    Page* ret_page = nullptr;
    Item* cur_item = nullptr;
    while (replacer_->Victim(&cur_item)) {
      ret_page = reinterpret_cast<Page*>(cur_item->data_);
      u64 length = cur_item->length;
      // AddPage({ret_page, length});
      AppendPage(ret_page);
      delete cur_item;
    }
    FlushBatchedPage();
  retry:
    Zone* new_zone = zmp_->zns_->GetUsableZone();
    if (new_zone && new_zone->IsFull()) {
      DEBUG_PRINT("allocate new zone %lu failed is also full %lu \n", zone_id_,
                  new_zone->GetZoneNr());
      goto retry;
    }

    // finished raw zone
    zone_->Finish();
    // set new zone
    zns_id_t raw_zone_id = zone_id_;
    zone_id_ = new_zone->GetZoneNr();
    wp_ = new_zone->wp_ / PAGE_SIZE;
    cap_ = new_zone->GetCapacityLeft() / PAGE_SIZE;
    end_ =
        new_zone->start_ / PAGE_SIZE + new_zone->GetMaxCapacity() / PAGE_SIZE;
    zone_ = new_zone;
    // set new zoneid mapping
    zmp_->zone_table_[zone_id_] = zmp_->zone_table_[raw_zone_id];
    //  add wp_
    INFO_PRINT("zone %lu is full, allocate new zone %lu\n", raw_zone_id,
               new_zone->GetZoneNr());
    goto restart;
    // *page_id = INVALID_PAGE_ID;
  }
  return false;
}

bool ZoneManager::ReadPageFromZNSImp(bytes_t* data, zns_id_t zid,
                                     u32 page_nums) {
  // DEBUG_PRINT("read page %u zone %lu\n", page_nums, zid);
  // offset_t zone_offset = zone_->GetMaxCapacity() * GET_ZONE_OFFSET(zid);
  // offset_t zone_offset = zone_->GetMaxCapacity() * GET_ZONE_ID(zid);
  offset_t zone_offset = 0;
  offset_t offset = zone_offset + GET_ZONE_OFFSET(zid) * PAGE_SIZE;
  return zone_->Read((char*)data, page_nums * PAGE_SIZE, offset);
}

/*
 * ===================================================================
 * ===================================================================
 * ========================  ZoneBufferPool ==========================
 * ===================================================================
 * ===================================================================
 */
ZoneManagerPool::ZoneManagerPool(u32 pool_size, u32 num_instances_,
                                 const char* db_file)
    : pool_size_(pool_size), num_instances_(num_instances_), index_(0) {
  zns_ = new ZnsManager(db_file);

  CHECK_OR_EXIT(zns_, "ZnsManager is nullptr\n");

  for (size_t i = 0; i < num_instances_; i++) {
    auto zone = zns_->GetUsableZone();
    auto zbf = new ZoneManager(zone);
    zone_buffers_.push_back(zbf);
    zbf->SetPoolPtr(this);
    zone_table_[zone->GetZoneNr()] = i;
  }

  // INFO_PRINT("ZoneManagerPool is created \n");
  // INFO_PRINT("[Sieve Buffer] instances:%2d  pages:%6d\n", MAX_NUMS_ZONE,
  //  MAX_READ_CACHE_PAGES);
}

ZoneManagerPool::~ZoneManagerPool() {
  for (auto& zbf : zone_buffers_) {
    SAFE_DELETE(zbf);
  }
  SAFE_DELETE(zns_);
  // INFO_PRINT("ZoneManagerPool is deleted\n");
}

Page* ZoneManagerPool::NewPageFrom(page_id_t* page_id, u64 length,
                                   page_id_t from) {
  // 1. robin-round to get zone buffer
  size_t start_index = index_;
  size_t loop_index = index_;
  index_ = (index_ + 1) % num_instances_;
  Page* ret_page = nullptr;
  auto zid = zone_table_[GET_ZONE_ID(from)];
  while (true) {
    if (loop_index == zid) {
      loop_index = (loop_index + 1) % num_instances_;
      assert(num_instances_ > 1);
      continue;
    }
    // 2. get page from zone buffer
    ret_page = zone_buffers_[loop_index]->NewPage(page_id, length);
    if (ret_page != nullptr) {
      // 3. record the zid to zone_buffers_
      zone_table_[GET_ZONE_ID(*page_id)] = loop_index;
      return ret_page;
    }
    loop_index = (loop_index + 1) % num_instances_;
    if (loop_index == start_index) {
      return nullptr;
    }
  }
  return ret_page;
}

// :TODO: :hjl: the condition which zone is full is not implemented
Page* ZoneManagerPool::NewPage(page_id_t* page_id, u64 length) {
  // 1. robin-round to get zone buffer
  size_t start_index = index_;
  size_t loop_index = index_;
  index_ = (index_ + 1) % num_instances_;
  Page* ret_page = nullptr;
  while (true) {
    // 2. get page from zone buffer
    ret_page = zone_buffers_[loop_index]->NewPage(page_id, length);
    if (ret_page != nullptr) {
      // 3. record the zid to zone_buffers_
      zone_table_[GET_ZONE_ID(*page_id)] = loop_index;
      return ret_page;
    }
    loop_index = (loop_index + 1) % num_instances_;
    if (loop_index == start_index) {
      return nullptr;
    }
  }
  return ret_page;
}

// readonly
Page* ZoneManagerPool::FetchPage(page_id_t page_id) {
  // 1. get zone buffer from pageid
  zns_id_t zid = GET_ZONE_ID(page_id);
  // 2. return the page from zone buffer
  return zone_buffers_[zone_table_[zid]]->FetchPage(page_id);
}

// append page
Page* ZoneManagerPool::UpdatePage(page_id_t* page_id) {
  zns_id_t zid = GET_ZONE_ID(*page_id);
  return zone_buffers_[zone_table_[zid]]->UpdatePage(page_id);
}

bool ZoneManagerPool::UnpinPage(page_id_t page_id, bool is_dirty) {
  zns_id_t zid = GET_ZONE_ID(page_id);
  return zone_buffers_[zone_table_[zid]]->UnpinPage(page_id, is_dirty);
}

void ZoneManagerPool::FlushAllPages() {
  for (auto& buffer : zone_buffers_) {
    buffer->FlushAllPages();
  }
}

u64 ZoneManagerPool::GetPoolSize() {
  u64 total = 0;
  for (auto& buffer : zone_buffers_) {
    total += buffer->replacer_->Size();
  }
  return total;
}

u64 ZoneManagerPool::GetFileSize() {
  auto zbd_ = zns_->zbd_;
  return zbd_->GetTotalBytesWritten();
}

u64 ZoneManagerPool::GetReadCount() {
  auto zbd_ = zns_->zbd_;
  return zbd_->GetReadCount();
}

u64 ZoneManagerPool::GetWriteCount() {
  auto zbd_ = zns_->zbd_;
  return zbd_->GetWriteCount();
}

void ZoneManagerPool::PrintReadCache() {
  u64 hit_read_cache = 0;
  u64 miss_read_cache = 0;
  for (auto& instance : zone_buffers_) {
#ifdef USE_SIEVE
    hit_read_cache += instance->sieve_._hit;
    miss_read_cache += instance->sieve_._miss;
#elif defined(USE_LRU_BUFFER)
    hit_read_cache += instance->lru_buffer_._hit;
    miss_read_cache += instance->lru_buffer_._miss;
#endif
  }

  u64 hit = hit_read_cache;
  u64 miss = miss_read_cache;
  double miss_ratio = miss * 100.0 / (miss + hit);
  double hit_ratio = hit * 100.0 / (hit + miss);

#ifdef USE_SIEVE
  INFO_PRINT("[ReadCache->Sieve] ");
#elif defined(USE_LRU_BUFFER)
  INFO_PRINT("[ReadCache->LRU] ");
#endif
  INFO_PRINT(
      "count:%4lu miss: %4lu %2.2lf%% hit: %4lu "
      "hit_ratio: " KCYN "%2.2lf%%" KRESET "\n",
      miss + hit, miss, miss_ratio, hit, hit_ratio);
}

void ZoneManagerPool::Print() {
  u64 page_table_size = 0;
  u64 replacer_size = 0;
  u64 free_list_size = 0;

  u64 count = 0;
  u64 miss = 0;
  u64 hit = 0;

  u64 total = 0;
  for (auto& buffer : zone_buffers_) {
    auto instance = buffer;
    // instance->Print();
    page_table_size += instance->page_table_.size();
    replacer_size += instance->replacer_->Size();

    count += instance->count_;
    miss += instance->miss_;
    hit += instance->hit_;
    total += instance->page_id_counts_.size();
  }

  std::vector<page_id_t> page_ids;
  page_ids.reserve(total);
  for (auto& buffer : zone_buffers_) {
    auto instance = buffer;
    for (auto& count : instance->page_id_counts_) {
      page_ids.push_back(count.second);
    }
  }
  std::sort(page_ids.begin(), page_ids.end());
  size_t avg = 0;
  for (size_t i = 0; i < total; i++) {
    avg += page_ids[i];
  }
  avg /= total;
  auto sz = total;
  // DEBUG_PRINT("%lu %lu sz:%lu\n", page_ids.size(), total, sz);
  INFO_PRINT("[ZoneBufferPool]  Avg Count For a BufferPage: %3lu hits:", avg);
  std::vector<float> percentiles = {0.0, 0.1, 0.2, 0.3, 0.4,   0.5,
                                    0.6, 0.7, 0.8, 0.9, 0.999, 0.9999999};
  INFO_PRINT("  Percentile:")
  for (auto& p : percentiles) {
    INFO_PRINT(" {%2.3lf%%->%4lu}, ", p * 100, page_ids[size_t(p * sz)]);
  }
  INFO_PRINT("\n");

  INFO_PRINT("[ZoneBufferPool] Total size: " KMAG "%4.2f" KRESET
             " MB Instance Nums:%2u Page table "
             "size:%4lu Replacer size: %4lu Free list size: %4lu \n",
             page_table_size * 4.0 / 1024, num_instances_, page_table_size,
             replacer_size, free_list_size);

  double miss_ratio = miss * 100.0 / (hit + miss);
  double hit_ratio = hit * 100.0 / (hit + miss);
  INFO_PRINT(
      "[ZoneBufferPool] count:%4lu miss: %4lu %2.2lf%% hit: %4lu "
      "hit_ratio: " KCYN "%2.2lf%%" KRESET "\n",
      count, miss, miss_ratio, hit, hit_ratio);
}

void ZoneManagerPool::Close() {
  for (auto zbuffer : zone_buffers_) {
    // zone->FlushAllPages();
    delete zbuffer;
  }
  zone_buffers_.clear();
}
