#include <libzbd/zbd.h>

#include <cstdint>
#include <iostream>

#include "../zns/zone_device.h"

std::string DEVICE_NAME = "/dev/nvme";
const uint64_t BUF_SIZE = 4096;
const uint64_t PAGE_SIZE = 4096;

void test_zbdlib_backend() {
  auto zns = new ZbdlibBackend(DEVICE_NAME);
  unsigned int max_active_zones = 0;
  unsigned int max_open_zones = 0;

  auto status = zns->Open(true, false, &max_active_zones, &max_open_zones);
  (void)status;
  printf("%u %u\n", max_active_zones, max_open_zones);
  auto zone_list = zns->ListZones();
  auto nr_zones = zone_list->ZoneCount();
  auto zones_array = zone_list->GetData();
  for (int i = 0; i < nr_zones; i += 1000) {
    // struct zbd_zone c_zone = zones[i];
    ZbdlibBackend::print_zone_info(((struct zbd_zone *)zones_array + i));
  }
  // std::cout << sizeof(struct zbd_zone) * nr_zones << std::endl; 226kB
  delete zns;
}

void test_zoned_blocked_device() {
  auto zns_block = new ZonedBlockDevice(DEVICE_NAME);

  bool readonly = false;
  bool exclusive = true;

  auto status = zns_block->Open(readonly, exclusive);
  (void)status;
  printf("recalimable:%s free: %s used: %s open:%2ld active:%2ld full:%2ld\n",
         CalSize(zns_block->GetReclaimableSpace()).c_str(),
         CalSize(zns_block->GetFreeSpace()).c_str(),
         CalSize(zns_block->GetUsedSpace()).c_str(),
         zns_block->GetOpenIOZones(), zns_block->GetActiveIOZones(),
         zns_block->GetFullIOZones());

  uint64_t offset = 0;
  Zone *zone = zns_block->GetZone(offset);
  if (zone != nullptr) {
    zone->Print();
    // auto ret = zbd_open_zones(zone, 0, info.zone_size * OPEN_ZONE_NUMS);
    uint64_t size =
        std::max(BUF_SIZE * sizeof(char) + 2 * PAGE_SIZE, PAGE_SIZE * 3);
    auto data = new char[size];
    auto raw_data = data;
    memset((void *)data, '0' + (rand() % 10), BUF_SIZE);
    zone->Append(data, BUF_SIZE);
    printf("zone wp %lu\n", zone->wp_ / PAGE_SIZE);
    printf("zone number %lu", zone->GetReadPageRange());
    zone->Print();
    delete[] raw_data;
  }

  // std::cout << sizeof(struct zbd_zone) * nr_zones << std::endl; 226kB
  // delete zone;
  printf("\n");
  zns_block->PrintUsedZones();
  delete zns_block;
  return;
}

void reset_zone(uint64_t z_begin, uint64_t z_end) {
  auto zns_block = new ZonedBlockDevice(DEVICE_NAME);
  bool readonly = false;
  bool exclusive = true;
  auto status = zns_block->Open(readonly, exclusive);
  (void)status;

  printf("recalimable:%s free: %s used: %s open:%2ld active:%2ld full:%2ld\n",
         CalSize(zns_block->GetReclaimableSpace()).c_str(),
         CalSize(zns_block->GetFreeSpace()).c_str(),
         CalSize(zns_block->GetUsedSpace()).c_str(),
         zns_block->GetOpenIOZones(), zns_block->GetActiveIOZones(),
         zns_block->GetFullIOZones());
  // zns_block->PrintUsedZones();

  for (uint64_t i = z_begin; i < z_end; i++) {
    Zone *zone = zns_block->GetZone(i);
    if (zone->IsEmpty()) {
      continue;
    }
    printf("reset before:%4lu  ", i);
    zone->Print();
    zone->Reset();
    printf("reset after: %4lu  ", i);
    zone->Print();
  }

  delete zns_block;
}

void print_target_zone(uint64_t zid) {
  auto zns_block = new ZonedBlockDevice(DEVICE_NAME);
  bool readonly = false;
  bool exclusive = true;
  auto status = zns_block->Open(readonly, exclusive);
  (void)status;
  Zone *zone = zns_block->GetZone(zid);
  zone->Print();
  delete zns_block;
}

void print_used_zones() {
  auto zns_block = new ZonedBlockDevice(DEVICE_NAME);
  bool readonly = false;
  bool exclusive = true;
  auto status = zns_block->Open(readonly, exclusive);
  (void)status;

  printf("recalimable:%s free: %s used: %s open:%2ld active:%2ld full:%2ld\n",
         CalSize(zns_block->GetReclaimableSpace()).c_str(),
         CalSize(zns_block->GetFreeSpace()).c_str(),
         CalSize(zns_block->GetUsedSpace()).c_str(),
         zns_block->GetOpenIOZones(), zns_block->GetActiveIOZones(),
         zns_block->GetFullIOZones());
  zns_block->PrintUsedZones();
  delete zns_block;
}

int main(int argc, char const *argv[]) {
  // test_zoned_blocked_device();
  // test_zbdlib_backend();
  std::string partion_name = argv[1];
  DEVICE_NAME += partion_name;
  if (argc < 3) {
    goto EXIT;
  }

  if (strcmp(argv[2], "print") == 0) {
    print_target_zone(atoll(argv[3]));
  } else if (strcmp(argv[2], "used") == 0) {
    print_used_zones();
  } else if (strcmp(argv[2], "reset") == 0) {
    uint64_t z_begin = atoll(argv[3]);
    uint64_t z_end = atoll(argv[4]);
    reset_zone(z_begin, z_end);
  } else {
  EXIT:
    printf("zns_test 2n2 print zid => print the target zone in zns nvme2n2\n");
    printf("zns_test 0n2 used => print the used zones in zns nvme0n2\n");
    printf(
        "zns_test 1n2 reset zid_begin zid_end => reset the zones in "
        "[begin,end)\n");
  }

  return 0;
}
