#include "zone_device.h"

std::string CalSize(uint64_t size) {
  char *result = (char *)malloc(sizeof(char) * 20);

  uint64_t multiplier = exbibytes;
  int i;

  for (i = 0; i < DIM(sizes); i++, multiplier /= 1024) {
    if (size < multiplier) continue;
    if (size % multiplier == 0)
      sprintf(result, "%lu %s", size / multiplier, sizes[i]);
    else
      sprintf(result, "%.1f %s", (float)size / multiplier, sizes[i]);
    std::string r = result;
    free(result);
    return r;
  }
  strcpy(result, "0");
  std::string ret;
  ret.copy(result, 20, 0);
  free(result);
  return ret;
}

Zone::Zone(ZonedBlockDevice *zbd, ZonedBlockDeviceBackend *zbd_be,
           std::unique_ptr<ZoneList> &zones, unsigned int idx)
    : zbd_(zbd),
      zbd_be_(zbd_be),
      busy_(false),
      start_(zbd_be->ZoneStart(zones, idx)),
      max_capacity_(zbd_be->ZoneMaxCapacity(zones, idx)),
      wp_(zbd_be->ZoneWp(zones, idx)) {
  used_capacity_ = wp_ - start_;
  capacity_ = 0;
  if (zbd_be->ZoneIsWritable(zones, idx)) {
    capacity_ = max_capacity_ - used_capacity_;
  }
}
void Zone::Counts() {
  zbd_->AddWriteCount(write_count_);
  zbd_->AddReadCount(read_count_);
  zbd_->AddBytesWritten(write_bytes_);
}

Zone::~Zone() {
  // if (Close()) {
  // 1. add write count read count to zbd_
  // printf("[zone] write count:%lu read count:%lu write bytes:%6.2lf MB \n",
  //        write_count_, read_count_, write_bytes_ * 1.0 / 1024 / 1024);
  // printf("zone start:%lu zone wp:%lu\n", start_, wp_);
  Counts();
  // this->Print();
  // }
}

bool Zone::IsUsed() { return (used_capacity_ > 0); }
bool Zone::IsFull() { return (capacity_ == 0); }
bool Zone::IsEmpty() { return (wp_ == start_); }
uint64_t Zone::GetCapacityLeft() { return capacity_; }
uint64_t Zone::GetMaxCapacity() { return max_capacity_; }
uint64_t Zone::GetNextPageId() { return wp_ / ZNS_PAGE_SIZE; }
uint64_t Zone::GetZoneNr() { return start_ / zbd_->GetZoneSize(); }

void Zone::EncodeJson(std::ostream &json_stream) {
  json_stream << "{";
  json_stream << "\"start\":" << start_ << ",";
  json_stream << "\"capacity\":" << capacity_ << ",";
  json_stream << "\"max_capacity\":" << max_capacity_ << ",";
  json_stream << "\"wp\":" << wp_ << ",";
  // json_stream << "\"lifetime\":" << lifetime_ << ",";
  json_stream << "\"used_capacity\":" << used_capacity_;
  json_stream << "}";
}

IOStatus Zone::Reset() {
  bool offline;
  uint64_t max_capacity;

  assert(!IsUsed());
  assert(IsBusy());

  IOStatus ios = zbd_be_->Reset(start_, &offline, &max_capacity);
  if (ios != OK()) return ios;

  if (offline)
    capacity_ = 0;
  else
    max_capacity_ = capacity_ = max_capacity;

  wp_ = start_;

  return OK();
}

IOStatus Zone::Finish() {
  assert(IsBusy());

  IOStatus ios = zbd_be_->Finish(start_);
  if (ios != OK()) return ios;

  capacity_ = 0;
  wp_ = start_ + zbd_->GetZoneSize();

  Counts();
  return OK();
}

IOStatus Zone::Close() {
  assert(IsBusy());

  if (!(IsEmpty() || IsFull())) {
    IOStatus ios = zbd_be_->Close(start_);
    if (ios != OK()) return ios;
  }

  return OK();
}

IOStatus Zone::Read(char *data, uint32_t size, uint64_t offset, bool direct) {
  int ret = 0;
  int left = size;
  int r = -1;

  while (left) {
    r = zbd_be_->Read(data, left, offset, direct);
    if (r < 0) {
      return IOError(strerror(errno));
    } else if (r != left) {
      printf("not read all data\n");
      printf("r:%d left:%d offset:%lu\n", r, left, offset);
      exit(1);
      return IOError("Read failed");
    }
    left -= r;
    ret += r;
    offset += r;
    data += r;
  }

  read_count_++;
  return OK();
}

IOStatus Zone::Append(char *data, uint32_t size) {
  char *ptr = data;
  uint32_t left = size;
  int ret;

  if (capacity_ < size) return NoSpace("Not enough capacity for append");
  if ((uint64_t)data & 0xfff) return IOError("Addr must align to 4KB");
  assert((size % zbd_->GetBlockSize()) == 0);

  while (left) {
    ret = zbd_be_->Write(ptr, left, wp_);
    if (ret < 0) {
      return IOError(strerror(errno));
    } else if (ret != size) {
      printf("not write all data\n");
      exit(1);
      return IOError("Write failed");
    }

    ptr += ret;
    wp_ += ret;
    capacity_ -= ret;
    left -= ret;
    write_bytes_ += ret;
  }

  write_count_++;
  return OK();
}

uint64_t Zone::GetReadCount() { return read_count_; };
uint64_t Zone::GetWriteCount() { return write_count_; };

inline IOStatus Zone::CheckRelease() {
  if (!Release()) {
    assert(false);
    return Corruption("Failed to unset busy flag of zone " +
                      std::to_string(GetZoneNr()));
  }

  return OK();
}

void Zone::Print() {
  auto zone_list = zbd_be_->ListZones();
  struct zbd_zone *zones_array = (struct zbd_zone *)zone_list->GetData();
  auto zone = zones_array + GetZoneNr();
  if (zone == NULL) {
    return;
  }
  printf(KBOLD "[ZoneID:%5lu]start:" KRESET COLOR_BOLD_BLUE
               "%12lu " KRESET KBOLD "wp:" COLOR_BOLD_SLOW_BLINKING_RED
               "%12lu " KRESET KBOLD "remain capa:" KYEL "%10lu " KRESET KBOLD
               "max cap:" KMAG "%10lu " KRESET KBOLD "used capa:" KCYN
               "%10lu" KRESET KBOLD " cond: " KYEL "%2u(%s)\n" KRESET,
         GetZoneNr(), start_ / ZNS_PAGE_SIZE, wp_ / ZNS_PAGE_SIZE,
         capacity_ / ZNS_PAGE_SIZE, max_capacity_ / ZNS_PAGE_SIZE,
         (max_capacity_ - capacity_) / ZNS_PAGE_SIZE, zone->cond,
         zbd_zone_cond_str(zone, false));
}

void Zone::PrintZbd() {
  auto zone_list = zbd_be_->ListZones();
  struct zbd_zone *zones_array = (struct zbd_zone *)zone_list->GetData();
  auto zone = zones_array + GetZoneNr();
  if (zone == NULL) {
    return;
  }
  uint64_t offset = zone->wp - zone->start;
  printf(KBOLD "[ZBD_ZONE:%5lu] start:" KRESET COLOR_BOLD_BLUE
               "%14llu " KRESET KBOLD "wp:" COLOR_BOLD_SLOW_BLINKING_RED
               "%14lu " KRESET KBOLD "cond:" KYEL "%2u(%s) " KRESET KBOLD
               "cap:" KMAG "%10llu " KRESET KBOLD "len:" KCYN "%10llu " KRESET
               "type:%2u(%s) flags:%2u \n" KRESET,
         GetZoneNr(), zone->start, offset, zone->cond,
         zbd_zone_cond_str(zone, false), zone->len, zone->capacity, zone->type,
         zbd_zone_type_str(zone, true), zone->flags);
}

ZonedBlockDevice::ZonedBlockDevice(std::string path) {
  zbd_be_ = std::unique_ptr<ZbdlibBackend>(new ZbdlibBackend(path));
}

IOStatus ZonedBlockDevice::Open(bool readonly, bool exclusive) {
  std::unique_ptr<ZoneList> zone_rep;
  unsigned int max_nr_active_zones;
  unsigned int max_nr_open_zones;
  // Status s;
  uint64_t i = 0;
  uint64_t m = 0;
  // Reserve one zone for metadata and another one for extent migration
  int reserved_zones = 0;

  if (!readonly && !exclusive)
    return InvalidArgument("Write opens must be exclusive");

  IOStatus ios = zbd_be_->Open(readonly, exclusive, &max_nr_active_zones,
                               &max_nr_open_zones);
  if (ios != OK()) return ios;

  if (zbd_be_->GetNrZones() < ZENFS_MIN_ZONES) {
    return NotSupported("To few zones on zoned backend (" +
                        std::to_string(ZENFS_MIN_ZONES) + " required)");
  }

  if (max_nr_active_zones == 0)
    max_nr_active_io_zones_ = zbd_be_->GetNrZones();
  else
    max_nr_active_io_zones_ = max_nr_active_zones - reserved_zones;

  if (max_nr_open_zones == 0)
    max_nr_open_io_zones_ = zbd_be_->GetNrZones();
  else
    max_nr_open_io_zones_ = max_nr_open_zones - reserved_zones;

  zone_rep = zbd_be_->ListZones();
  if (zone_rep == nullptr || zone_rep->ZoneCount() != zbd_be_->GetNrZones()) {
    return IOError("Failed to list zones");
  }

  active_io_zones_ = 0;
  open_io_zones_ = 0;
  full_io_zones_ = 0;

  uint64_t n_swr = 0, n_offline = 0;
  for (int i = 0; i < zone_rep->ZoneCount(); i++) {
    /* Only use sequential write required zones */
    if (zbd_be_->ZoneIsSwr(zone_rep, i)) {
      if (!zbd_be_->ZoneIsOffline(zone_rep, i)) {
        Zone *newZone = new Zone(this, zbd_be_.get(), zone_rep, i);
        if (!newZone->Acquire()) {
          assert(false);
          return Corruption("Failed to set busy flag of zone " +
                            std::to_string(newZone->GetZoneNr()));
        }
        if (newZone->IsFull()) {
          full_io_zones_++;
        }
        io_zones.push_back(newZone);
        if (zbd_be_->ZoneIsActive(zone_rep, i)) {
          active_io_zones_++;
          if (zbd_be_->ZoneIsOpen(zone_rep, i)) {
            if (!readonly) {
              newZone->Close();
            }
          }
        }
        IOStatus status = newZone->CheckRelease();
        if (status != OK()) {
          return status;
        }
      } else {
        n_offline++;
      }
    } else {
      n_swr++;
    }
  }
  // debug
  assert(n_offline == 0 && n_offline == 0);
  // printf("[debug] not swr zone:%2lu offline zone:%2lu\n", n_swr, n_offline);
  start_time_ = time(NULL);
  return OK();
}

/**
 * @brief return not full zone which may not be the minium zone id
 */
IOStatus ZonedBlockDevice::AllocateEmptyZone(Zone **zone_out) {
  IOStatus s;
  Zone *allocated_zone = nullptr;
  for (const auto z : io_zones) {
    if (z->Acquire()) {
      // if (z->IsEmpty()) {
      if (!z->IsFull()) {
        allocated_zone = z;
        break;
      } else {
        s = z->CheckRelease();
        if (s != OK()) return s;
      }
    }
  }
  *zone_out = allocated_zone;
  return OK();
}

Zone *ZonedBlockDevice::GetZoneFromOffset(uint64_t offset) {
  return io_zones[offset / zbd_be_->GetZoneSize()];
}

Zone *ZonedBlockDevice::GetZone(uint64_t zone_id) { return io_zones[zone_id]; }

uint64_t ZonedBlockDevice::GetFreeSpace() {
  uint64_t free = 0;
  for (const auto z : io_zones) {
    free += z->capacity_;
  }
  return free;
}

uint64_t ZonedBlockDevice::GetUsedSpace() {
  uint64_t used = 0;
  for (const auto z : io_zones) {
    used += z->used_capacity_;
  }
  return used;
}

// reclaimable is always zero??
uint64_t ZonedBlockDevice::GetReclaimableSpace() {
  uint64_t reclaimable = 0;
  for (const auto z : io_zones) {
    if (z->IsFull()) reclaimable += (z->used_capacity_);
  }
  return reclaimable;
}

uint64_t ZonedBlockDevice::GetActiveZones() {
  uint64_t active = 0;
  for (const auto z : io_zones) {
    // if ((!z->Is)) active++;
  }
  return active;
}

ZonedBlockDevice::~ZonedBlockDevice() {
  for (const auto z : io_zones) {
    delete z;
  }
}

IOStatus ZonedBlockDevice::InvalidateCache(uint64_t pos, uint64_t size) {
  int ret = zbd_be_->InvalidateCache(pos, size);

  if (ret) {
    return IOError("Failed to invalidate cache");
  }
  return OK();
}

int ZonedBlockDevice::Read(char *buf, uint64_t offset, int n, bool direct) {
  int ret = 0;
  int left = n;
  int r = -1;

  while (left) {
    r = zbd_be_->Read(buf, left, offset, direct);
    if (r <= 0) {
      if (r == -1 && errno == EINTR) {
        continue;
      }
      break;
    }
    ret += r;
    buf += r;
    left -= r;
    offset += r;
  }

  if (r < 0) return r;
  return ret;
}

std::string ZonedBlockDevice::GetFilename() { return zbd_be_->GetFilename(); }

uint64_t ZonedBlockDevice::GetBlockSize() { return zbd_be_->GetBlockSize(); }

uint64_t ZonedBlockDevice::GetZoneSize() { return zbd_be_->GetZoneSize(); }

uint32_t ZonedBlockDevice::GetNrZones() { return zbd_be_->GetNrZones(); }

void ZonedBlockDevice::EncodeJsonZone(std::ostream &json_stream,
                                      const std::vector<Zone *> zones) {
  bool first_element = true;
  json_stream << "[";
  for (Zone *zone : zones) {
    if (first_element) {
      first_element = false;
    } else {
      json_stream << ",";
    }
    zone->EncodeJson(json_stream);
  }

  json_stream << "]";
}

void ZonedBlockDevice::EncodeJson(std::ostream &json_stream) {
  json_stream << "{";
  json_stream << "\"io\":";
  EncodeJsonZone(json_stream, io_zones);
  json_stream << "}";
}

void ZonedBlockDevice::PrintUsedZones() {
  printf(
      "\n------------------------Used Zones--------------------------------\n");

  int used = 0;
  int full = 0;
  int total = 0;
  for (const auto z : io_zones) {
    bool flag = !z->IsEmpty();
    if (flag) {
      z->Print();
      if (z->IsFull()) {
        full++;
      }
      used++;
    }
    total++;
  }
  printf("-------------Total:%4d Used zones:%4d Full Zones:%4d-----------\n",
         total, used, full);
}
