#pragma once

// #if !defined(ROCKSDB_LITE) && defined(OS_LINUX)
#include <errno.h>
#include <libzbd/zbd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// https://stackoverflow.com/questions/71274207/how-to-bold-text-in-c-program
#ifndef COLOR_MACRO
#define COLOR_MACRO
#define STR(X) #X
#define KNRM "\x1B[0m"
#define KBOLD "\e[1m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"
#define KYEL_ITA_BOLD "\e[1;3;33m"
#define KBLK_GRN_ITA \
  "\033[3;30;42m"  // #"black green italic" = black text with green background,
                   // italic text
#define KBLK_RED_ITA \
  "\033[9;30;41m"  // #"black red strike" = black text with red background,
                   // strikethrough line through the text
#define KRESET "\033[0m"

// 1 = bold; 5 = slow blink; 31 = foreground color red
// 34 = foreground color blue
// See:
// https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_(Select_Graphic_Rendition)_parameters
#define COLOR_BOLD_SLOW_BLINKING "\e[1;5m"
#define COLOR_BOLD_SLOW_BLINKING_RED "\e[1;5;31m"
#define COLOR_BOLD_BLUE "\e[1;34m"
#endif

using IOStatus = int;
enum Code : unsigned char {
  kOk = 0,
  kNotFound = 1,
  kCorruption = 2,
  kNotSupported = 3,
  kInvalidArgument = 4,
  kIOError = 5,
  kMergeInProgress = 6,
  kIncomplete = 7,
  kShutdownInProgress = 8,
  kTimedOut = 9,
  kAborted = 10,
  kBusy = 11,
  kExpired = 12,
  kTryAgain = 13,
  kCompactionTooLarge = 14,
  kColumnFamilyDropped = 15,
  kMaxCode
};
IOStatus OK();
IOStatus Corruption(const std::string str);
IOStatus NotSupported(const std::string str);
IOStatus InvalidArgument(const std::string str);
IOStatus IOError(const std::string str);
IOStatus NoSpace(const std::string str);

class ZoneList {
 private:
  void *data_;
  unsigned int zone_count_;

 public:
  ZoneList(void *data, unsigned int zone_count)
      : data_(data), zone_count_(zone_count){};
  void *GetData() { return data_; };
  unsigned int ZoneCount() { return zone_count_; };
  ~ZoneList() { free(data_); };
};

class ZonedBlockDeviceBackend {
 public:
  uint32_t block_sz_ = 0;
  uint64_t zone_sz_ = 0;
  uint32_t nr_zones_ = 0;

 public:
  virtual IOStatus Open(bool readonly, bool exclusive,
                        unsigned int *max_active_zones,
                        unsigned int *max_open_zones) = 0;

  virtual std::unique_ptr<ZoneList> ListZones() = 0;
  virtual IOStatus Reset(uint64_t start, bool *offline,
                         uint64_t *max_capacity) = 0;
  virtual IOStatus Finish(uint64_t start) = 0;
  virtual IOStatus Close(uint64_t start) = 0;
  virtual int Read(char *buf, int size, uint64_t pos, bool direct) = 0;
  virtual int Write(char *data, uint32_t size, uint64_t pos) = 0;
  virtual int InvalidateCache(uint64_t pos, uint64_t size) = 0;
  virtual bool ZoneIsSwr(std::unique_ptr<ZoneList> &zones,
                         unsigned int idx) = 0;
  virtual bool ZoneIsOffline(std::unique_ptr<ZoneList> &zones,
                             unsigned int idx) = 0;
  virtual bool ZoneIsWritable(std::unique_ptr<ZoneList> &zones,
                              unsigned int idx) = 0;
  virtual bool ZoneIsActive(std::unique_ptr<ZoneList> &zones,
                            unsigned int idx) = 0;
  virtual bool ZoneIsOpen(std::unique_ptr<ZoneList> &zones,
                          unsigned int idx) = 0;
  virtual uint64_t ZoneStart(std::unique_ptr<ZoneList> &zones,
                             unsigned int idx) = 0;
  virtual uint64_t ZoneMaxCapacity(std::unique_ptr<ZoneList> &zones,
                                   unsigned int idx) = 0;
  virtual uint64_t ZoneWp(std::unique_ptr<ZoneList> &zones,
                          unsigned int idx) = 0;
  virtual std::string GetFilename() = 0;
  uint32_t GetBlockSize() { return block_sz_; };
  uint64_t GetZoneSize() { return zone_sz_; };
  uint32_t GetNrZones() { return nr_zones_; };
  virtual ~ZonedBlockDeviceBackend(){};
  // virtual void Print() = 0;
};

enum class ZbdBackendType {
  kBlockDev,
  kZoneFS,
};

class ZbdlibBackend : public ZonedBlockDeviceBackend {
 private:
  std::string filename_;
  int read_f_;
  int read_direct_f_;
  int write_f_;

 public:
  explicit ZbdlibBackend(std::string bdevname);
  ~ZbdlibBackend() {
    zbd_close(read_f_);
    zbd_close(read_direct_f_);
    zbd_close(write_f_);
  }

  IOStatus Open(bool readonly, bool exclusive, unsigned int *max_active_zones,
                unsigned int *max_open_zones);
  std::unique_ptr<ZoneList> ListZones();
  IOStatus Reset(uint64_t start, bool *offline, uint64_t *max_capacity);
  IOStatus Finish(uint64_t start);
  IOStatus Close(uint64_t start);
  int Read(char *buf, int size, uint64_t pos, bool direct);
  int Write(char *data, uint32_t size, uint64_t pos);
  int InvalidateCache(uint64_t pos, uint64_t size);

  bool ZoneIsSwr(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_type(z) == ZBD_ZONE_TYPE_SWR;
  };

  bool ZoneIsOffline(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_offline(z);
  };

  bool ZoneIsWritable(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return !(zbd_zone_full(z) || zbd_zone_offline(z) || zbd_zone_rdonly(z));
  };

  bool ZoneIsActive(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_imp_open(z) || zbd_zone_exp_open(z) || zbd_zone_closed(z);
  };

  bool ZoneIsOpen(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_imp_open(z) || zbd_zone_exp_open(z);
  };

  uint64_t ZoneStart(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_start(z);
  };

  uint64_t ZoneMaxCapacity(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_capacity(z);
  };

  uint64_t ZoneWp(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_wp(z);
  };

  void ZonePrint(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return print_zone_info(z);
  }
  static void print_zone_info(struct zbd_zone *zone) {
    if (zone == NULL) {
      return;
    }
    uint64_t offset = zone->wp - zone->start;
    printf(KBOLD "start:" KRESET COLOR_BOLD_BLUE "%14llu " KRESET KBOLD
                 "wp:" COLOR_BOLD_SLOW_BLINKING_RED "%14lu " KRESET KBOLD
                 "cond:" KYEL "%2u(%s) " KRESET KBOLD "cap:" KMAG
                 "%10llu " KRESET KBOLD "len:" KCYN "%10llu " KRESET
                 "type:%2u(%s) flags:%2u \n",
           zone->start, offset, zone->cond, zbd_zone_cond_str(zone, false),
           zone->len, zone->capacity, zone->type, zbd_zone_type_str(zone, true),
           zone->flags);
  }

  std::string GetFilename() { return filename_; }

 private:
  IOStatus CheckScheduler();
  std::string ErrorToString(int err);
};

// #endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)