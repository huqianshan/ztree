#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>   // for errno
#include <cstring>  // for strerror
#include <string>

// #include "stats.h"
#include "storage.h"

// namespace BTree {

DiskManager *db_io = nullptr;

static void assert_msg(bool expr, const char *msg) {
  if (!expr) {
    perror(msg);
    abort();
  }
  return;
}

DiskManager::DiskManager(const char *db_file, uint32_t num_instances) {
  int flags = O_CREAT | O_RDWR | O_SYNC | O_TRUNC;
#ifdef DIRECT_IO
  flags |= O_DIRECT;
#endif
  fd_ = open(db_file, flags, S_IRUSR | S_IWUSR);
  file_name_ = std::string(db_file);
  assert_msg(fd_ > 0, "open");
  write_count_.store(0, std::memory_order_relaxed);
  read_count_.store(0, std::memory_order_relaxed);
  num_instances_ = num_instances;
}

DiskManager::~DiskManager() {
  u64 size = get_file_size();
  (void)size;
  // INFO_PRINT(
  //     "[Storage] Write_count=%8lu read_count=%8lu PAGES fielsize=%8lu PAGES "
  //     "PAGE= %4d Bytes\n",
  //     write_count_.load(), read_count_.load(), size / PAGE_SIZE, PAGE_SIZE);
  int ret = close(fd_);
  assert_msg(!ret, "close");
}

offset_t DiskManager::write_n_pages(page_id_t page_id, size_t nr_pages,
                                    const char *page_data) {
  // std::lock_guard<std::mutex> lock(file_mutex_);
  // DEBUG_PRINT("write_n_pages: page_id=%u, nr_pages=%u\n", page_id,
  // (unsigned)nr_pages);
  write_count_.fetch_add(1, std::memory_order_relaxed);
  ssize_t len = nr_pages * PAGE_SIZE;
  ssize_t offset = page_id * PAGE_SIZE;
  ssize_t ret = pwrite(fd_, page_data, len, offset);
  // fsync(fd_);
  // assert_msg(ret > 0, "write failed\n");
  assert_msg(ret >= 0, "write failed");
  return offset;
}

offset_t DiskManager::read_n_pages(page_id_t page_id, size_t nr_pages,
                                   char *page_data) {
  // std::lock_guard<std::mutex> lock(file_mutex_);
  read_count_.fetch_add(1, std::memory_order_relaxed);
  ssize_t len = nr_pages * PAGE_SIZE;
  ssize_t offset = page_id * PAGE_SIZE;
  ssize_t ret = pread(fd_, page_data, len, offset);
  assert_msg(ret > 0, strerror(errno));
  return offset;
}

/**
 * Write the contents of the specified page into disk file
 */
offset_t DiskManager::write_page(page_id_t page_id, const char *page_data) {
  page_id_t cur_id = page_id / num_instances_;
  return write_n_pages(cur_id, 1, page_data);
}

/**
 * Read the contents of the specified page into the given memory area
 */
offset_t DiskManager::read_page(page_id_t page_id, char *page_data) {
  page_id_t cur_id = page_id / num_instances_;
  return read_n_pages(cur_id, 1, page_data);
}

u64 DiskManager::get_file_size() {
  struct stat statbuf;
  int ret = fstat(fd_, &statbuf);
  assert_msg(ret != -1, "Failed to get file size");
  return statbuf.st_size;
}

u64 DiskManager::get_read_count() { return read_count_.load(); }
u64 DiskManager::get_write_count() { return write_count_.load(); }

ZnsManager::ZnsManager(const char *db_file, uint32_t num_instances) {
  zbd_ = new ZonedBlockDevice(db_file);
  auto ret = zbd_->Open(false, true);
  if (ret != OK()) {
    return;
  }
  write_count_.store(0, std::memory_order_relaxed);
  read_count_.store(0, std::memory_order_relaxed);
  auto zns_block = zbd_;
  INFO_PRINT(
      "[zns] open succ! recalimable:%s free: %s used: %s open:%2ld "
      "active:%2ld\n",
      CalSize(zns_block->GetReclaimableSpace()).c_str(),
      CalSize(zns_block->GetFreeSpace()).c_str(),
      CalSize(zns_block->GetUsedSpace()).c_str(), zns_block->GetOpenIOZones(),
      zns_block->GetActiveIOZones());
}

ZnsManager::~ZnsManager() {
  // if (zbd_ != nullptr) {
  // delete zbd_;
  // zbd_
  // }
  SAFE_DELETE(zbd_);
}

Zone *ZnsManager::GetUsableZone() {
  Zone *zone = nullptr;
  IOStatus ios = zbd_->AllocateEmptyZone(&zone);
  if (ios != OK()) {
    return nullptr;
  }
  // DEBUG_PRINT("[ZnsManager] new zone_id=%lu, offset=%lu\n",
  // zone->GetZoneNr(), zone->wp_);
  return zone;
}

page_id_t ZnsManager::GetEmptyZoneId() {
  Zone *zone = nullptr;
  IOStatus ios = zbd_->AllocateEmptyZone(&zone);
  if (ios != OK()) {
    return -1;
  }
  DEBUG_PRINT("[ZnsManager] new zone_id=%lu, offset=%lu\n", zone->GetZoneNr(),
              zone->wp_);
  return MAKE_PAGE_ID(zone->GetZoneNr(), zone->wp_);
}

page_id_t ZnsManager::GetNextWritePageId(zns_id_t zone_id) {
  // ZonedBlockDeviceBackend *zbd_be = zbd_->zbd_be_.get();
  offset_t offset = zone_id * zbd_->GetZoneSize();
  Zone *zone = zbd_->GetZoneFromOffset(offset);
  if (zone == nullptr) {
    return -1;
  } else if (zone->IsFull()) {
    page_id_t new_page_id = GetEmptyZoneId();
    zone->Close();
    DEBUG_PRINT("[ZnsManager] new zone_id=%lu, offset=%lu raw zone_id=%lu\n",
                GET_ZONE_ID(new_page_id), GET_ZONE_OFFSET(new_page_id),
                zone_id);
    return new_page_id;
  }
  return MAKE_PAGE_ID(zone_id, zone->wp_);
}

offset_t ZnsManager::write_page(page_id_t page_id, const char *page_data) {
  return write_n_pages(page_id, 1, page_data);
}

offset_t ZnsManager::read_page(page_id_t page_id, char *page_data) {
  return read_n_pages(page_id, 1, page_data);
}

offset_t ZnsManager::write_n_pages(page_id_t page_id, size_t nr_pages,
                                   const char *page_data) {
  page_id_t zone_id = GET_ZONE_ID(page_id);
  Zone *zone = zbd_->GetZone(zone_id);
  assert_msg(zone != nullptr, "zone is nullptr");
  if (zone->IsFull()) {
    return -1;
  }

  offset_t raw_wp = zone->wp_;
  zone->Append((char *)page_data, PAGE_SIZE * nr_pages);
  write_count_.fetch_add(1, std::memory_order_relaxed);
  return MAKE_PAGE_ID(zone_id, raw_wp);
}

offset_t ZnsManager::read_n_pages(page_id_t page_id, size_t nr_pages,
                                  char *page_data) {
  page_id_t zone_id = GET_ZONE_ID(page_id);
  offset_t offset = GET_ZONE_OFFSET(page_id) * ZNS_PAGE_SIZE;
  // DEBUG_PRINT("[ZnsManager] read page_id=%lu, zone_id=%lu, offset=%lu\n",
  // page_id, zone_id, offset);
  Zone *zone = zbd_->GetZone(zone_id);
  assert_msg(zone != nullptr, "zone is nullptr");
  // zbd_->Read(page_data, offset, PAGE_SIZE * nr_pages, true);
  zone->Read(page_data, PAGE_SIZE * nr_pages, offset);
  read_count_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

u64 ZnsManager::get_file_size() { return zbd_->GetTotalBytesWritten(); }

u64 ZnsManager::get_read_count() { return read_count_.load(); }
u64 ZnsManager::get_write_count() { return write_count_.load(); }

// }  // namespace BTree
