#pragma once

#include <libzbd/zbd.h>

#include <cstdint>

#include "../zns/zone_device.h"
#include "config.h"

// namespace BTree {
#define DIRECT_IO

class StorageManager {
 public:
  StorageManager() = default;
  virtual ~StorageManager() = default;

  /**
   * Write the contents of the specified page into disk file
   */
  virtual offset_t write_page(page_id_t page_id, const char *page_data) = 0;

  /**
   * Read the contents of the specified page into the given memory area
   */
  virtual offset_t read_page(page_id_t page_id, char *page_data) = 0;

  virtual offset_t write_n_pages(page_id_t page_id, size_t nr_pages,
                                 const char *page_data) = 0;

  virtual offset_t read_n_pages(page_id_t page_id, size_t nr_pages,
                                char *page_data) = 0;

  /* Get file size */
  virtual u64 get_file_size() = 0;
  virtual u64 get_read_count() = 0;
  virtual u64 get_write_count() = 0;
};

class DiskManager : public StorageManager {
 public:
  DiskManager() = delete;
  DiskManager(const char *db_file, uint32_t num_instances = 1);
  ~DiskManager() override;

  u64 get_file_size() override;
  u64 get_read_count() override;
  u64 get_write_count() override;

  offset_t write_page(page_id_t page_id, const char *page_data) override;
  offset_t read_page(page_id_t page_id, char *page_data) override;
  offset_t write_n_pages(page_id_t page_id, size_t nr_pages,
                         const char *page_data) override;
  offset_t read_n_pages(page_id_t page_id, size_t nr_pages,
                        char *page_data) override;

  //  private:
  int fd_;
  uint32_t num_instances_;

  std::string file_name_;

  std::atomic_uint64_t write_count_;
  std::atomic_uint64_t read_count_;

  std::mutex file_mutex_;
};

// extern DiskManager *db_io;

class ZnsManager : public StorageManager {
 public:
  ZnsManager() = delete;
  ZnsManager(const char *db_file, uint32_t num_instances = 1);
  ~ZnsManager();

  /**
   * @brief Get the Usable Zone object,
   * which is not full and not offline zone
   * @return true
   * @return false
   */
  Zone *GetUsableZone();
  zns_id_t GetEmptyZoneId();
  page_id_t GetNextWritePageId(zns_id_t zone_id);

  offset_t write_page(page_id_t page_id, const char *page_data) override;
  offset_t read_page(page_id_t page_id, char *page_data) override;
  offset_t write_n_pages(page_id_t page_id, size_t nr_pages,
                         const char *page_data) override;
  offset_t read_n_pages(page_id_t page_id, size_t nr_pages,
                        char *page_data) override;

  u64 get_file_size() override;
  u64 get_read_count() override;
  u64 get_write_count() override;

  ZonedBlockDevice *zbd_;
  std::atomic_uint64_t write_count_;
  std::atomic_uint64_t read_count_;
};

// }  // namespace BTree
