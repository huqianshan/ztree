#include "zone_backend.h"

#include <fcntl.h>

#include <fstream>
#include <iostream>
#include <string>

IOStatus OK() { return kOk; }
IOStatus Corruption(const std::string str) {
  printf("Error: %s\n", str.c_str());
  printf("%s\n", __func__);
  return kCorruption;
}
IOStatus NotSupported(const std::string str) {
  printf("Error: %s\n", str.c_str());
  printf("%s\n", __func__);
  return kNotSupported;
}
IOStatus InvalidArgument(const std::string str) {
  printf("Error: %s\n", str.c_str());
  // exit(-1);
  printf("%s\n", __func__);
  return kInvalidArgument;
}
IOStatus IOError(const std::string str) {
  printf("Error: %s\n", str.c_str());
  printf("%s\n", __func__);
  return kIOError;
}
IOStatus NoSpace(const std::string str) {
  printf("Error: no space%s\n", str.c_str());
  printf("%s\n", __func__);
  return kIOError;
}

ZbdlibBackend::ZbdlibBackend(std::string bdevname)
    : filename_(bdevname), read_f_(-1), read_direct_f_(-1), write_f_(-1) {}

std::string ZbdlibBackend::ErrorToString(int err) {
  char *err_str = strerror(err);
  if (err_str != nullptr) return std::string(err_str);
  return "";
}

IOStatus ZbdlibBackend::CheckScheduler() {
  std::ostringstream path;
  std::string s = filename_;
  std::fstream f;

  s.erase(0, 5);  // Remove "/dev/" from /dev/nvmeXnY
  path << "/sys/block/" << s << "/queue/scheduler";
  f.open(path.str(), std::fstream::in);
  if (!f.is_open()) {
    return InvalidArgument("Failed to open " + path.str());
  }

  std::string buf;
  getline(f, buf);
  if (buf.find("[mq-deadline]") == std::string::npos) {
    f.close();
    return InvalidArgument(
        "Current ZBD scheduler is not mq-deadline, set it to mq-deadline.");
  }

  f.close();
  return OK();
}

IOStatus ZbdlibBackend::Open(bool readonly, bool exclusive,
                             unsigned int *max_active_zones,
                             unsigned int *max_open_zones) {
  zbd_info info;

  /* The non-direct file descriptor acts as an exclusive-use semaphore */
  if (exclusive) {
    read_f_ = zbd_open(filename_.c_str(), O_RDONLY | O_EXCL, &info);
  } else {
    read_f_ = zbd_open(filename_.c_str(), O_RDONLY, &info);
  }

  if (read_f_ < 0) {
    return InvalidArgument("Failed to open zoned block device for read: " +
                           ErrorToString(errno));
  }

  read_direct_f_ = zbd_open(filename_.c_str(), O_RDONLY | O_DIRECT, &info);
  if (read_direct_f_ < 0) {
    return InvalidArgument(
        "Failed to open zoned block device for direct read: " +
        ErrorToString(errno));
  }

  if (readonly) {
    write_f_ = -1;
  } else {
    write_f_ = zbd_open(filename_.c_str(), O_WRONLY | O_DIRECT, &info);
    if (write_f_ < 0) {
      return InvalidArgument("Failed to open zoned block device for write: " +
                             ErrorToString(errno));
    }
  }

  if (info.model != ZBD_DM_HOST_MANAGED) {
    return NotSupported("Not a host managed block device");
  }

  IOStatus ios = CheckScheduler();
  if (ios != OK()) return ios;

  block_sz_ = info.pblock_size;
  zone_sz_ = info.zone_size;
  nr_zones_ = info.nr_zones;
  *max_active_zones = info.max_nr_active_zones;
  *max_open_zones = info.max_nr_open_zones;
  return OK();
}

std::unique_ptr<ZoneList> ZbdlibBackend::ListZones() {
  int ret;
  void *zones;
  unsigned int nr_zones;

  ret = zbd_list_zones(read_f_, 0, zone_sz_ * nr_zones_, ZBD_RO_ALL,
                       (struct zbd_zone **)&zones, &nr_zones);
  if (ret) {
    return nullptr;
  }

  std::unique_ptr<ZoneList> zl(new ZoneList(zones, nr_zones));

  return zl;
}

IOStatus ZbdlibBackend::Reset(uint64_t start, bool *offline,
                              uint64_t *max_capacity) {
  unsigned int report = 1;
  struct zbd_zone z;
  int ret;

  ret = zbd_reset_zones(write_f_, start, zone_sz_);
  if (ret) return IOError("Zone reset failed\n");

  ret = zbd_report_zones(read_f_, start, zone_sz_, ZBD_RO_ALL, &z, &report);

  if (ret || (report != 1)) return IOError("Zone report failed\n");

  if (zbd_zone_offline(&z)) {
    *offline = true;
    *max_capacity = 0;
  } else {
    *offline = false;
    *max_capacity = zbd_zone_capacity(&z);
  }

  return OK();
}

IOStatus ZbdlibBackend::Finish(uint64_t start) {
  int ret;

  ret = zbd_finish_zones(write_f_, start, zone_sz_);
  if (ret) return IOError("Zone finish failed\n");

  return OK();
}

IOStatus ZbdlibBackend::Close(uint64_t start) {
  int ret;

  ret = zbd_close_zones(write_f_, start, zone_sz_);
  if (ret) return IOError("Zone close failed\n");

  return OK();
}

int ZbdlibBackend::InvalidateCache(uint64_t pos, uint64_t size) {
  /* posix_fadvise是linux上对文件进行预取的系统调用，其中第四个参数int
   * advice为预取的方式，主要有以下几种：
      POSIX_FADV_NORMAL 无特别建议 重置预读大小为默认值
      POSIX_FADV_SEQUENTIAL                将要进行顺序操作
   * 设预读大小为默认值的2 倍 POSIX_FADV_RANDOM 将要进行随机操作
   * 将预读大小清零（禁止预读） POSIX_FADV_NOREUSE 指定的数据将只访问一次
   * （暂无动作） POSIX_FADV_WILLNEED                    指定的数据即将被访问
   * 立即预读数据到page cache POSIX_FADV_DONTNEED 指定的数据近期不会被访问
   * 立即从page cache 中丢弃数据
   * 其中，POSIX_FADV_NORMAL、POSIX_FADV_SEQUENTIAL、POSIX_FADV_RANDOM
   * 是用来调整预取窗口的大小，POSIX_FADV_WILLNEED则可以将指定范围的磁盘文件读入到page
   * cache中，POSIX_FADV_DONTNEED则将指定的磁盘文件中数据从page cache中换出。
   * 对于POSIX_FADV_WILLNEED的情况linux有自己特有的一个接口，定义如下，就不具体给例子了：
   * ssize_t readahead(int fd, off64_t offset, size_t count);
   *  */
  return posix_fadvise(read_f_, pos, size, POSIX_FADV_DONTNEED);
}

int ZbdlibBackend::Read(char *buf, int size, uint64_t pos, bool direct) {
  return pread(direct ? read_direct_f_ : read_f_, buf, size, pos);
}

int ZbdlibBackend::Write(char *data, uint32_t size, uint64_t pos) {
  return pwrite(write_f_, data, size, pos);
}
