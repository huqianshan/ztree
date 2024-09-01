#ifndef BENCH_UTIL_H
#define BENCH_UTIL_H

#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../zbtree/config.h"
#include "timer.h"
using namespace std;
const u64 MILLION = 1000 * 1000;

/**
 * Workloads
 */
const std::string YCSB_LOAD_FILE_NAME =
    // "/data/public/hjl/YCSB/ycsb_load_workload";
"/home/hjl/Academical/BBTree/test/YCSB/workloads/ycsb_load_workload";
const std::string YCSB_RUN_FILE_NAME =
    // "/data/public/hjl/YCSB/ycsb_run_workload";
"/home/hjl/Academical/BBTree/test/YCSB/workloads/ycsb_run_workload";
const std::string ZIPFIAN_LOAD_FILE_NAME =
    "/home/hjl/Academical/BBTree/test/zipfian/workloads/zipfian_load_workload";
const std::string ZIPFIAN_RUN_FILE_NAME =
    "/home/hjl/Academical/BBTree/test/zipfian/workloads/zipfian_run_workload";

#define REALASTIC_WORKLOAD_DIR "/home/hjl/Tem/SOSD/data/"
#define AMZN_WORKLOAD \
  REALASTIC_WORKLOAD_DIR "books_200M_uint32_equality_lookups_10M"

#define FB_WORKLOAD REALASTIC_WORKLOAD_DIR "fb_200M_uint64_equality_lookups_10M"

#define WIKI_WORKLOAD \
  REALASTIC_WORKLOAD_DIR "wiki_ts_200M_uint64_equality_lookups_10M"

enum OP { OP_INSERT, OP_READ, OP_DELETE, OP_UPDATE, OP_SCAN };

// unit size in nvme smart-log
const u64 UNITS_SIZE = 1000 * 512;
// unit size in nvme smart-log-add
const u64 NANDS_SIZE = 32ull * 1024 * 1024;

std::string execCmd(const char *cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

class SmartLog {
 public:
  SmartLog() = default;
  ~SmartLog() = default;
  SmartLog(const string &device_name) : device_(device_name) {
    read_before = 0;
    written_before = 0;
    read_after = 0;
    written_after = 0;
  }

  void logBefore() {
    std::tie(read_before, written_before) = getDataUnits(device_);
  }

  void logAfter() {
    std::tie(read_after, written_after) = getDataUnits(device_);
  }

  // return in bytes
  std::pair<u64, u64> getDataUnits(const std::string &device,
                                   bool is_zns = true) {
    std::string cmd_base =
        "sudo nvme smart-log " + device + " | grep 'Data Units ";
    std::string output_read =
        execCmd((cmd_base + "Read' | awk '{print $5}'").c_str());
    u64 read_bytes = stoull(output_read) * UNITS_SIZE;
    u64 written_bytes = 0;
    if (is_zns) {
      // since current zns device WDC ZNS 540 can't get the nands_bytes_written
      // from  `sudo nvme wdc vs-smart-add-log`
      // simply taking hosts-bytes-written as nand_bytes_written, meaning that
      // wa_in_zns ~= 1 and this is confirmed by the vendor
      std::string output_written =
          execCmd((cmd_base + "Written' | awk '{print $5}'").c_str());
      written_bytes = stoull(output_written) * UNITS_SIZE;
    } else {
      // assuming the regular device is intel vendor (P4510)
      // but the nand_bytes_written only increased when every 32GiB datas are
      // written
      cmd_base = "sudo nvme intel smart-log-add " + device +
                 " | grep 'nand_bytes_written' | awk '{print $4}'";
      std::string output_written = execCmd(cmd_base.c_str());
      written_bytes = stoull(output_written) * NANDS_SIZE;
    }
    return {read_bytes, written_bytes};
  };

  void printLog(u64 op_count) {
    u64 read_total = read_after - read_before;
    u64 written_total = written_after - written_before;
    double bytes_per_write = written_total * 1.0 / (op_count);
    double bytes_per_read = read_total * 1.0 / (op_count);

    printf("[%s] Read amp on SSD:" KBLU " %7.2f" KRESET
           " bytes/op, Write amp on SSD: " KYEL "%6.2f " KRESET "bytes/op \n",
           device_.c_str(), bytes_per_read, bytes_per_write);
  }

  u64 read_before;
  u64 read_after;
  u64 written_before;
  u64 written_after;
  string device_;
};

void GetDRAMSpace() {
  auto pid = getpid();
  std::array<char, 128> buffer;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(
      popen(("cat /proc/" + to_string(pid) + "/status").c_str(), "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    string result = buffer.data();
    if (result.find("VmRSS") != string::npos) {
      printf("DRAM consumption: %s", result.c_str());
      // std::string mem_ocp = std::regex_replace(
      //     result, std::regex("[^0-9]*([0-9]+).*"), std::string("$1"));
      // printf("DRAM consumption: %4.2f MB.\n", stof(mem_ocp) / 1024);
      return;
      // break;
    }
  }
}

void PrintLatency(vector<size_t> &latency, string name) {
  auto sz = latency.size();
  if (sz == 0) {
    return;
  }
  size_t avg = 0;
  for (size_t i = 0; i < sz; i++) {
    avg += latency[i];
  }
  avg /= sz;

  sort(latency.begin(), latency.end());
  printf("%-6s LatencyHeader:", name.c_str());
  vector<string> percent = {"avg", "0",    "50",    "90",
                            "99",  "99.9", "99.99", "100"};
  vector<double> percent_val = {-1, 0, 0.5, 0.9, 0.99, 0.999, 0.9999, 1};
  for (size_t i = 0; i < percent.size(); i++) {
    printf("%8s,", percent[i].c_str());
  }

  printf("\n%-6s LatencyValue:" KYEL_ITA_BOLD, name.c_str());
  for (size_t i = 0; i < percent.size(); i++) {
    if (i == 0) {
      printf("%8lu,", avg);
    } else if (i == percent.size() - 1) {
      printf("%8lu", latency[sz - 1]);
    } else {
      printf("%8lu,", latency[size_t(percent_val[i] * sz)]);
    }
  }
  printf("\n" KRESET);
}

#endif  // BENCH_UTIL_H