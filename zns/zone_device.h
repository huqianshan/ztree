
#pragma once
#include "assert.h"
#include "zone_backend.h"

#define KB (1024)
#define MB (1024 * KB)
#define ZNS_PAGE_SIZE (4 * KB)

#define DIM(x) (sizeof(x) / sizeof(*(x)))
static const char *sizes[] = {"EiB", "PiB", "TiB", "GiB", "MiB", "KiB", "B"};
const uint64_t exbibytes =
    1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;

std::string CalSize(uint64_t size);

/* Minimum of number of zones that makes sense */
#define ZENFS_MIN_ZONES (32)

class ZonedBlockDevice;

class Zone {
  ZonedBlockDevice *zbd_;
  ZonedBlockDeviceBackend *zbd_be_;
  std::atomic_bool busy_;

 public:
  explicit Zone(ZonedBlockDevice *zbd, ZonedBlockDeviceBackend *zbd_be,
                std::unique_ptr<ZoneList> &zones, unsigned int idx);
  ~Zone();

  uint64_t start_;
  uint64_t capacity_; /* remaining capacity */
  uint64_t max_capacity_;
  uint64_t wp_;
  std::atomic<uint64_t> used_capacity_;

  uint64_t read_count_ = 0;
  uint64_t write_count_ = 0;
  uint64_t write_bytes_ = 0;

  IOStatus Reset();
  IOStatus Finish();
  IOStatus Close();
  IOStatus Append(char *data, uint32_t size);
  /** offset is the offset in the zns device ,
   * offset and size are both in bytes
   */
  IOStatus Read(char *data, uint32_t size, uint64_t offset, bool direct = true);

  bool IsUsed();
  bool IsFull();
  bool IsEmpty();
  // bool IsOffline();
  uint64_t GetZoneNr();
  uint64_t GetCapacityLeft();
  uint64_t GetMaxCapacity();
  uint64_t GetNextPageId();

  uint64_t GetReadCount();
  uint64_t GetWriteCount();

  /*
   * Author: chenbo
   * Time: 2024-03-29 11:20:50
   * Description: auxillary function , [start, end), end not included
   */
  // auxillary function , [start, end), end not included
  uint64_t GetReadPageRange() { return wp_ / ZNS_PAGE_SIZE; }
  // auxillary function , [start, end), end not included
  uint64_t GetReadByteRange() { return wp_; }

  void Print();
  void PrintZbd();
  void EncodeJson(std::ostream &json_stream);
  void Counts();

  inline IOStatus CheckRelease();
  bool IsBusy() { return this->busy_.load(std::memory_order_relaxed); }
  bool Acquire() {
    bool expected = false;
    return this->busy_.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel);
  }
  bool Release() {
    bool expected = true;
    return this->busy_.compare_exchange_strong(expected, false,
                                               std::memory_order_acq_rel);
  }
};

class ZonedBlockDevice {
 private:
  std::unique_ptr<ZonedBlockDeviceBackend> zbd_be_;
  std::vector<Zone *> io_zones;
  time_t start_time_;
  //   std::shared_ptr<Logger> logger_;
  uint32_t finish_threshold_ = 0;
  std::atomic<uint64_t> bytes_written_{0};
  std::atomic<uint64_t> gc_bytes_written_{0};
  std::atomic<uint64_t> write_count_{0};
  std::atomic<uint64_t> read_count_{0};

  std::atomic<long> active_io_zones_;
  std::atomic<long> open_io_zones_;
  std::atomic<long> full_io_zones_;

  unsigned int max_nr_active_io_zones_;
  unsigned int max_nr_open_io_zones_;

  //   std::shared_ptr<ZenFSMetrics> metrics_;

  void EncodeJsonZone(std::ostream &json_stream,
                      const std::vector<Zone *> zones);

 public:
  explicit ZonedBlockDevice(std::string path);
  virtual ~ZonedBlockDevice();

  IOStatus Open(bool readonly, bool exclusive);
  int Read(char *buf, uint64_t offset, int n, bool direct);
  IOStatus AllocateEmptyZone(Zone **zone_out);
  IOStatus InvalidateCache(uint64_t pos, uint64_t size);

  void AddWriteCount(uint64_t count) { write_count_.fetch_add(count); };
  void AddReadCount(uint64_t count) { read_count_.fetch_add(count); };
  void AddBytesWritten(uint64_t written) {
    bytes_written_.fetch_add(written, std::memory_order_relaxed);
  };
  void AddGCBytesWritten(uint64_t written) {
    gc_bytes_written_.fetch_add(written, std::memory_order_relaxed);
  };

  /**
   * @attention active zone means: the state of zone is open, and the close.
   */
  uint64_t GetActiveZones();
  // uint64_t GetFullZones();
  uint32_t GetNrZones();
  uint64_t GetFreeSpace();
  uint64_t GetUsedSpace();
  uint64_t GetReclaimableSpace();
  uint64_t GetBlockSize();
  uint64_t GetZoneSize();
  Zone *GetZoneFromOffset(uint64_t offset);
  Zone *GetZone(uint64_t zone_id);

  uint64_t GetReadCount() { return read_count_.load(); };
  uint64_t GetWriteCount() { return write_count_.load(); };
  uint64_t GetUserBytesWritten() {
    return bytes_written_.load() - gc_bytes_written_.load();
  };
  uint64_t GetTotalBytesWritten() { return bytes_written_.load(); };
  uint64_t GetActiveIOZones() { return active_io_zones_.load(); };
  uint64_t GetOpenIOZones() { return open_io_zones_.load(); };
  uint64_t GetFullIOZones() { return full_io_zones_.load(); };
  std::string GetFilename();

  void PrintUsedZones();
  void EncodeJson(std::ostream &json_stream);
};