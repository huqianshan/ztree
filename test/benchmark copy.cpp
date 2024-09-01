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

#include "../zbtree/buffer.h"
#include "../zbtree/buffer_btree.h"
#include "../zbtree/snapshot.h"
#include "../zbtree/zbtree.h"
#include "timer.h"
// using namespace BTree;
using namespace btreeolc;
using namespace std;

const u64 MILLION = 1000 * 1000;

/**
 * Test parameters
 */
// #define LATENCY
#define DRAM_CONSUMPTION
// #define GDB

/**
 * Tree index
 *
 */
// #define RAW_BTREE_ON_FS
// #define BBTREE_ON_EXT4_SSDD
#define COWBTREE_ON_ZNS
// #define ZBTREE_ON_ZNS

#define CONV_BTREE "convBtree"
#define F2FS_BTREE "f2fsBtree"
#define COW_BTREE "cowBtree"
#define Z_BTREE "zBtree"

string TREE = "";

#ifdef RAW_BTREE_ON_FS
const std::string TREE_NAME = "BTreeBufferOnExt4SSD";
#elif defined(BBTREE_ON_EXT4_SSDD)
const std::string TREE_NAME = "B+Tree-Buffer-On-Ext4-ConvSSD";
#elif defined(COWBTREE_ON_ZNS)
const std::string TREE_NAME = "CoWBTree-Buffer-On-ZNSSSD";
#elif defined(ZBTREE_ON_ZNS)
#include "../zbtree/buffer_btree.h"
#include "../zbtree/snapshot.h"
const std::string TREE_NAME = "ZBtree-On-ZNSSSD";
#endif

class BufferPoolManager {
 public:
  BufferPoolManager() = default;
  virtual ~BufferPoolManager() = default;
  virtual void Print();
  virtual void PrintReadCache();
  virtual void GetFileSize();
  virtual void GetReadCount();
  virtual void GetWriteCount();
};

class VBTree {
 public:
  VBTree() = default;
  virtual ~VBTree() = default;
  virtual bool Get(Key k, Value, v);
  virtual bool Scan(Key k, Value, v);
  virtual bool Delete(Key k, Value, v);
  virtual bool Remove(Key k, Value, v);

}

/**
 * Workloads
 */
const std::string YCSB_LOAD_FILE_NAME =
    "/home/hjl/Academical/BBTree/test/YCSB/workloads/ycsb_load_workload";
// "/data/public/hjl/YCSB/ycsb_load_workload";
const std::string YCSB_RUN_FILE_NAME =
    "/home/hjl/Academical/BBTree/test/YCSB/workloads/ycsb_run_workload";
// "/data/public/hjl/YCSB/ycsb_run_workload";
const std::string ZIPFIAN_LOAD_FILE_NAME =
    "/home/hjl/Academical/BBTree/test/zipfian/workloads/zipfian_load_workload";
const std::string ZIPFIAN_RUN_FILE_NAME =
    "/home/hjl/Academical/BBTree/test/zipfian/workloads/zipfian_run_workload";

enum OP { OP_INSERT, OP_READ, OP_DELETE, OP_UPDATE, OP_SCAN };

enum TreeIndex {
  BufferBTreeF2Fs = 0,
};

u64 LOAD_SIZE;
u64 RUN_SIZE;
// u64 MAX_SIZE_LOAD = 200000000ULL;
//  u64 MAX_SIZE_RUN = 200000000ULL;

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

void run_test(int num_thread, string load_data, string run_data,
              string workload, u64 max_load_size, u64 max_run_size) {
  u64 MAX_SIZE_LOAD = max_load_size;
  u64 MAX_SIZE_RUN = max_run_size;
  string insert("INSERT");
  string remove("REMOVE");
  string read("READ");
  string update("UPDATE");
  string scan("SCAN");

  ifstream infile_load(load_data);
  string op;
  u64 key;
  u64 value_len;
  u64 op_read_count = 0;
  u64 op_write_count = 0;

  vector<u64> init_keys(MAX_SIZE_LOAD);
  vector<u64> keys(MAX_SIZE_RUN);
  vector<u64> init_value_lens(MAX_SIZE_LOAD);
  vector<u64> value_lens(MAX_SIZE_RUN);
  vector<int> ops(MAX_SIZE_RUN);

  int count = 0;
  while ((count < MAX_SIZE_LOAD) && infile_load.good()) {
    infile_load >> op >> key >> value_len;
    if (!op.size()) continue;
    if (op.size() && op.compare(insert) != 0) {
      cout << "READING LOAD FILE FAIL!\n";
      cout << op << endl;
      return;
    }
    init_keys[count] = key;
    init_value_lens[count] = value_len;
    count++;
  }
  LOAD_SIZE = count;
  infile_load.close();
  printf("Loaded %8lu keys for initialing.\n", LOAD_SIZE);

  ifstream infile_run(run_data);
  count = 0;
  while ((count < MAX_SIZE_RUN) && infile_run.good()) {
    infile_run >> op >> key;
    if (op.compare(insert) == 0) {
      infile_run >> value_len;
      ops[count] = OP_INSERT;
      keys[count] = key;
      value_lens[count] = value_len;
      op_write_count++;
    } else if (op.compare(update) == 0) {
      infile_run >> value_len;
      ops[count] = OP_UPDATE;
      keys[count] = key;
      value_lens[count] = value_len;
      op_write_count++;
    } else if (op.compare(read) == 0) {
      ops[count] = OP_READ;
      keys[count] = key;
      op_read_count++;
    } else if (op.compare(remove) == 0) {
      ops[count] = OP_DELETE;
      keys[count] = key;
      op_write_count++;
    } else if (op.compare(scan) == 0) {
      infile_run >> value_len;
      ops[count] = OP_SCAN;
      keys[count] = key;
      value_lens[count] = value_len;
      op_read_count++;
    } else {
      continue;
    }
    count++;
  }
  RUN_SIZE = count;

  printf("Loaded %8lu keys for running.\n", RUN_SIZE);

  Timer tr;
  tr.start();

  void *para = nullptr;
  void *tree = nullptr;
  // btreeolc::ZBTree *
  if (TREE.find(CONV_BTREE) != string::npos ||
      TREE.find(F2FS_BTREE) != string::npos) {
    para = new ParallelBufferPoolManager(INSTANCE_SIZE, PAGES_SIZE, FILE_NAME,
                                         false);
    tree = new btreeolc::BTree(para);
  } else if (TREE.find(COW_BTREE) != string::npos) {
    para = new ZoneManagerPool(MAX_CACHED_PAGES_PER_ZONE, MAX_NUMS_ZONE,
                               ZNS_DEVICE);
    tree = new btreeolc::BTree(para);
  } else if (TREE.find(Z_BTREE) != string::npos) {
    ZoneManagerPool *para = new ZoneManagerPool(MAX_CACHED_PAGES_PER_ZONE,
                                                MAX_NUMS_ZONE, ZNS_DEVICE);
    BTree *device_tree = new BTree(para);
    tree = new btreeolc::ZBTree<KeyType, ValueType>(device_tree);
  }

  // #ifdef RAW_BTREE_ON_FS
  //   // DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  //   ParallelBufferPoolManager *para = new ParallelBufferPoolManager(
  //       INSTANCE_SIZE, PAGES_SIZE, FILE_NAME, false);
  //   btreeolc::BTree *tree = new btreeolc::BTree(para);
  // #elif defined(BBTREE_ON_EXT4_SSDD)
  //   ParallelBufferPoolManager *para = new ParallelBufferPoolManager(
  //       INSTANCE_SIZE, PAGES_SIZE, FILE_NAME, false);
  //   btreeolc::BTree *tree = new btreeolc::BTree(para);
  // #elif defined(COWBTREE_ON_ZNS)
  //   ZoneManagerPool *para =
  //       new ZoneManagerPool(MAX_CACHED_PAGES_PER_ZONE, MAX_NUMS_ZONE,
  //       ZNS_DEVICE);
  //   btreeolc::BTree *tree = new btreeolc::BTree(para);
  // #elif defined(ZBTREE_ON_ZNS)
  //   ZoneManagerPool *para =
  //       new ZoneManagerPool(MAX_CACHED_PAGES_PER_ZONE, MAX_NUMS_ZONE,
  //       ZNS_DEVICE);
  //   BTree *device_tree = new BTree(para);
  //   btreeolc::ZBTree<KeyType, ValueType> *tree =
  //       new btreeolc::ZBTree<KeyType, ValueType>(device_tree);
  //   // btreeolc::BTree *tree = new btreeolc::BTree(para);
  // #endif
  printf("Tree init:" KWHT " %s" KRESET " %4.2f ms.\n", TREE.c_str(),
         tr.elapsed<std::chrono::milliseconds>());
  printf("---------------------Load------------------------\n");
#ifdef DRAM_CONSUMPTION
  GetDRAMSpace();
#endif

  auto part = LOAD_SIZE / num_thread;

#ifndef GDB
  SmartLog reg_ssd(REGURLAR_DEVICE);
  SmartLog conv_ssd(ZNS_DEVICE);
  reg_ssd.logBefore();
  conv_ssd.logBefore();
#endif

  {
    // Load
    Timer sw;
    thread ths[num_thread];
    sw.start();
    auto insert = [&](size_t start, size_t len, int tid) {
      auto end = start + len;
      // cout << "start:" << start << "end:" << start + len << endl;
      for (size_t i = start; i < end; i++) {
        tree->Insert(init_keys[i], init_keys[i]);
      }
    };

    for (size_t i = 0; i < num_thread; i++) {
      ths[i] = thread(insert, part * i, part, i);
    }
    for (size_t i = 0; i < num_thread; i++) {
      ths[i].join();
    }
    auto t = sw.elapsed<std::chrono::milliseconds>();
    printf("Throughput: load, " KGRN "%3.2f" KRESET " Kops/s\n",
           (LOAD_SIZE * 1.0) / (t));
    printf("Load time: %4.2f sec\n", t / 1000.0);
  }
  part = RUN_SIZE / num_thread;

  // para->FlushAllPages();
  para->Print();
#ifdef ZBTREE_ON_ZNS
  // tree->FlushAll();
  tree->Print();
#endif

#ifndef GDB
  reg_ssd.logAfter();
  conv_ssd.logAfter();
  reg_ssd.printLog(LOAD_SIZE);
  conv_ssd.printLog(LOAD_SIZE);
  reg_ssd.logBefore();
  conv_ssd.logBefore();
#endif

  /*
  *************************************************
  -----------------------Run-----------------------
  *************************************************
   */
  printf("---------------------Run-------------------------\n");
  Timer sw;
#ifdef LATENCY
  vector<size_t> latency_all;
  Mutex latency_mtx;
#endif
  atomic_uint64_t read_suc = 0, read_fail = 0;
  std::function<void(size_t start, size_t len, int tid)> fun;
  auto operate = [&](size_t start, size_t len, int tid) {
    vector<size_t> latency;
    auto end = start + len;
    Timer l;

    bool rf = false;
    for (size_t i = start; i < end; i++) {
#ifdef LATENCY
      l.start();
#endif
      if (ops[i] == OP_INSERT) {
        tree->Insert(keys[i], keys[i]);
      } else if (ops[i] == OP_UPDATE) {
        tree->Insert(keys[i], keys[i] + 1);
      } else if (ops[i] == OP_READ) {
        u64 v;
        auto r = tree->Get(init_keys[i], v);
        if (r && v == init_keys[i] || r && v == init_keys[i] + 1)
          read_suc++;
        else
          read_fail++;
      } else if (ops[i] == OP_SCAN) {
        Value v[1000];
        auto r = tree->Scan(keys[i], value_lens[i], v);
        // if (r && v == keys[i] || r && v == keys[i] + 1)
        // read_suc++;
        // else
        // read_fail++;
        // assert(r);
      } else if (ops[i] == OP_DELETE) {
        // tree->Remove(keys[i]);
      }
#ifdef LATENCY
      latency.push_back(l.elapsed<std::chrono::nanoseconds>());
#endif
    }

#ifdef LATENCY
    lock_guard<Mutex> lock(latency_mtx);
    latency_all.insert(latency_all.end(), latency.begin(), latency.end());
#endif
  };

  fun = operate;
  thread ths[num_thread];
  sw.start();
  for (size_t i = 0; i < num_thread; i++) {
    ths[i] = thread(fun, part * i, part, i);
  }
  for (size_t i = 0; i < num_thread; i++) {
    ths[i].join();
  }
#ifdef BBTREE_ON_EXT4_SSDD
  // tree->FlushAll();
#endif
  auto t = sw.elapsed<std::chrono::milliseconds>();

  printf("Throughput: run, " KRED "%3.2f" KRESET " Kops/s\n",
         (RUN_SIZE * 1.0) / (t));
  printf("Run time: %4.2f sec\n", t / 1000.0);

#ifdef DRAM_CONSUMPTION
  GetDRAMSpace();
#endif

#ifdef LATENCY
  sort(latency_all.begin(), latency_all.end());
  auto sz = latency_all.size();
  size_t avg = 0;
  for (size_t i = 0; i < sz; i++) {
    avg += latency_all[i];
  }
  avg /= sz;

  cout << "Latency: " << avg << " ns\n";
  cout << "\t0 " << latency_all[0] << "\n"
       << "\t50% " << latency_all[size_t(0.5 * sz)] << "\n"
       << "\t90% " << latency_all[size_t(0.9 * sz)] << "\n"
       << "\t99% " << latency_all[size_t(0.99 * sz)] << "\n"
       << "\t99.9% " << latency_all[size_t(0.999 * sz)] << "\n"
       << "\t99.99% " << latency_all[size_t(0.9999 * sz)] << "\n"
       << "\t99.999% " << latency_all[size_t(0.99999 * sz)] << "\n"
       << "\t100% " << latency_all[sz - 1] << endl;
#endif
  // test snapshot
  // auto rt = tree->current->root.load();
  // btreeolc::snapshot::shot(rt);

  // auto rec = btreeolc::snapshot::recover();
  // assert(btreeolc::snapshot::cmp(rt, rec));

#ifdef ZBTREE_ON_ZNS
  // tree->FlushAll();
  // tree->Print();
#endif
  // para->FlushAllPages();

#ifdef BBTREE_ON_EXT4_SSDD
  // auto wal_size = tree->wal_->Size();
  // double wal_bytes_avg_write = 1.0 * wal_size / (LOAD_SIZE + RUN_SIZE);
  // printf("[WriteAheadLog]:  Write amp: %6.2f bytes/op\n",
  // wal_bytes_avg_write);
#else
  para->Print();
  para->PrintReadCache();
#endif
  delete tree;
#ifdef ZBTREE_ON_ZNS
  para->Close();
#endif
  auto file_size = para->GetFileSize();
  auto read_count = para->GetReadCount();
  auto write_count = para->GetWriteCount();
  delete para;
  printf("--------------------Closed------------------------\n");
  // printf("[zbd] read_count: %lu, write_count: %lu file_size: %luMB\n",
  //  read_count, write_count, file_size / 1024 / 1024);

  double page_read_avg = 1.0 * read_count * PAGE_SIZE / file_size;
  double page_write_avg = 1.0 * write_count * PAGE_SIZE / file_size;
  double bytes_read_avg = 1.0 * read_count * PAGE_SIZE / (LOAD_SIZE + RUN_SIZE);
  double bytes_write_avg =
      1.0 * write_count * PAGE_SIZE / (LOAD_SIZE + RUN_SIZE);
  printf(
      "[Storage] Write_count=%8lu read_count=%8lu PAGES fielsize=%8lu PAGES "
      "PAGE= %4d Bytes\n",
      write_count, read_count, file_size / PAGE_SIZE, PAGE_SIZE);
  printf(
      "[BTreeIndex]: In-place read in a page: %6.2f, In-place write in a page: "
      "%6.2f\n",
      page_read_avg, page_write_avg);
  printf("[BTreeIndex]: Read amp: " KBLU "%6.2f" KRESET
         " bytes/op, Write amp: " KYEL "%6.2f" KRESET " bytes/op\n",
         bytes_read_avg, bytes_write_avg);
  printf("[BTreeIndex]: Read suc: %lu, Read fail: %lu\n", read_suc.load(),
         read_fail.load());
#ifndef GDB
  reg_ssd.logAfter();
  conv_ssd.logAfter();
  reg_ssd.printLog(RUN_SIZE);
  conv_ssd.printLog(RUN_SIZE);
#endif
}

int main(int argc, char **argv) {
#ifndef GDB
  if (argc != 5) {
    printf("Usage: %s <tree> <workload> <threads> <size>\n", argv[0]);
    exit(0);
  };
#endif

  printf("--------------------Test Begin---------------------\n");

#ifndef GDB
  TREE = argv[1];
  string workload = argv[2];
  printf(KNRM "workload: " KWHT "%s" KRESET ", threads: " KWHT "%s" KRESET "\n",
         argv[2], argv[3]);
  int num_thread = atoi(argv[4]);
  u64 max_load_size = MILLION * atoi(argv[5]);
  u64 max_run_size = MILLION * atoi(argv[5]);

#else
  string workload = "ycsbd";
  string tree = "zbtree";
  int num_thread = 1;
  int GDB_SIZE = 1;
  u64 max_load_size = MILLION * GDB_SIZE;
  u64 max_run_size = MILLION * GDB_SIZE;
  printf("workload: %s, threads: %2d\n", workload.c_str(), num_thread);
#endif

  string load_data = "";
  string run_data = "";
  if (workload.find("ycsb") != string::npos) {
    load_data = YCSB_LOAD_FILE_NAME;
    load_data += workload[workload.size() - 1];
    run_data = YCSB_RUN_FILE_NAME;
    run_data += workload[workload.size() - 1];
  } else if (workload.find("zip") != string::npos) {
    load_data = ZIPFIAN_LOAD_FILE_NAME;
    load_data += workload[workload.size() - 1];
    run_data = ZIPFIAN_RUN_FILE_NAME;
    run_data += workload[workload.size() - 1];
  } else {
    printf("Wrong workload!\n");
    return 0;
  }

  run_test(num_thread, load_data, run_data, workload, max_load_size,
           max_run_size);
  // remove(FILE_NAME.c_str());

#ifdef DRAM_CONSUMPTION
  GetDRAMSpace();
#endif

  printf("---------------------Test End----------------------\n");

  return 0;
}