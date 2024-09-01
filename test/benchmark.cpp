#include "../zbtree/buffer.h"
#include "../zbtree/zbtree.h"
#include "bench_util.h"

// using namespace BTree;
using namespace btreeolc;

/**
 * Test parameters
 */
#define LATENCY
#define DRAM_CONSUMPTION
// #define GDB

/**
 * Tree index
 *
 */
// #define BBTREE_ON_CONV_SSD
// #define BBTREE_ON_F2FS_ZNS_SSD
// #define COWBTREE_ON_ZNS
#define ZBTREE_ON_ZNS

#ifdef BBTREE_ON_CONV_SSD
const std::string TREE_NAME = "BTree-On-ConvSSD";
#elif defined(BBTREE_ON_F2FS_ZNS_SSD)
const std::string TREE_NAME = "BTree-On-F2FS-ZNSSSD";
#elif defined(COWBTREE_ON_ZNS)
const std::string TREE_NAME = "CoWBTree-Buffer-On-ZNSSSD";
#elif defined(ZBTREE_ON_ZNS)
#include "../zbtree/buffer_btree.h"
#include "../zbtree/snapshot.h"
const std::string TREE_NAME = "ZBtree-On-ZNSSSD";
#endif

u64 LOAD_SIZE;
u64 RUN_SIZE;
// u64 MAX_SIZE_LOAD = 200000000ULL;
//  u64 MAX_SIZE_RUN = 200000000ULL;

// for work_queue use
int _num_threads = 0;

void run_test(int num_thread, string load_data, string run_data,
              string workload, u64 max_load_size, u64 max_run_size) {
  _num_threads = num_thread;
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

#ifdef BBTREE_ON_CONV_SSD
  ParallelBufferPoolManager *para = new ParallelBufferPoolManager(
      INSTANCE_SIZE, PAGES_SIZE, FILE_NAME, false);
  btreeolc::BTree *tree = new btreeolc::BTree(para);
#elif defined(BBTREE_ON_F2FS_ZNS_SSD)
  ParallelBufferPoolManager *para = new ParallelBufferPoolManager(
      INSTANCE_SIZE, PAGES_SIZE, FILE_NAME, false);
  btreeolc::BTree *tree = new btreeolc::BTree(para);
#elif defined(COWBTREE_ON_ZNS)
  ZoneManagerPool *para =
      new ZoneManagerPool(MAX_CACHED_PAGES_PER_ZONE, MAX_NUMS_ZONE, ZNS_DEVICE);
  btreeolc::BTree *tree = new btreeolc::BTree(para);
#elif defined(ZBTREE_ON_ZNS)
  ZoneManagerPool *para =
      new ZoneManagerPool(MAX_CACHED_PAGES_PER_ZONE, MAX_NUMS_ZONE, ZNS_DEVICE);
  BTree *device_tree = new BTree(para);
  btreeolc::ZBTree<KeyType, ValueType> *tree =
      new btreeolc::ZBTree<KeyType, ValueType>(device_tree);
#endif
  printf("Tree init:" KWHT " %s" KRESET " %4.2f ms.\n", TREE_NAME.c_str(),
         tr.elapsed<std::chrono::milliseconds>());
  printf("---------------------Load------------------------\n");
#ifdef DRAM_CONSUMPTION
  GetDRAMSpace();
#endif

#ifndef GDB
  SmartLog reg_ssd(REGURLAR_DEVICE);
  SmartLog conv_ssd(ZNS_DEVICE);
  reg_ssd.logBefore();
  conv_ssd.logBefore();
#endif
  int load_thread = (num_thread > 32) ? num_thread : 32;
  auto part = LOAD_SIZE / load_thread;
  auto left = LOAD_SIZE % load_thread;
  {
    // Load
    Timer sw;
    thread ths[load_thread];
    sw.start();
    auto insert = [&](size_t start, size_t len, int tid) {
      auto end = start + len;
      // cout << "start:" << start << "end:" << start + len << endl;
      for (size_t i = start; i < end; i++) {
        tree->Insert(init_keys[i], init_keys[i]);
      }
    };

    for (size_t i = 0; i < load_thread; i++) {
      ths[i] = thread(insert, part * i, part, i);
    }
    for (size_t i = 0; i < left; i++) {
      tree->Insert(init_keys[LOAD_SIZE - left + i],
                   init_keys[LOAD_SIZE - left + i]);
    }
    for (size_t i = 0; i < load_thread; i++) {
      ths[i].join();
    }
    auto t = sw.elapsed<std::chrono::milliseconds>();
    printf("Throughput: load, " KGRN "%3.2f" KRESET " Kops/s\n",
           (LOAD_SIZE * 1.0) / (t));
    printf("Load time: %4.2f sec\n", t / 1000.0);
  }

  para->Print();
#ifdef ZBTREE_ON_ZNS
  // tree->FlushAll();
  tree->Print();
#endif
  // para->FlushAllPages();
  // tree->current->_queue.do_all();
  // for (auto &zone : para->zone_buffers_) {
  //   zone->FlushBatchedPage();
  // }

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
  vector<size_t> read_latency_all;
  vector<size_t> insert_latency_all;
  std::mutex latency_mtx;
#endif
  atomic_uint64_t read_suc = 0, read_fail = 0, read_false = 0;
  std::function<void(size_t start, size_t len, int tid)> fun;
  auto operate = [&](size_t start, size_t len, int tid) {
#ifdef LATENCY
    vector<size_t> insert_latency;
    vector<size_t> read_latency;
#endif
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
        auto r = tree->Get(keys[i], v);
        if (r && v == keys[i] || r && v == keys[i] + 1) {
          read_suc++;
        } else if (!r) {
          read_fail++;
        } else {
          read_false++;
          // cout << keys[i] << " " << v << endl;
        }
      } else if (ops[i] == OP_SCAN) {
        Value v[1000];
        auto r = tree->Scan(keys[i], value_lens[i], v);
        if (r != value_lens[i]) {
          read_fail++;
        } else {
          read_suc++;
        }
        // auto r = tree->Scan(keys[i], 10, v);
        // if (r && v == keys[i] || r && v == keys[i] + 1)
        // read_suc++;
        // else
        // read_fail++;
        // assert(r);
      } else if (ops[i] == OP_DELETE) {
        // tree->Remove(keys[i]);
      }
#ifdef LATENCY
      int current_op = ops[i];
      if (current_op == OP_INSERT || current_op == OP_UPDATE ||
          current_op == OP_DELETE) {
        insert_latency.push_back(l.elapsed<std::chrono::nanoseconds>());
      } else if (current_op == OP_READ || current_op == OP_SCAN) {
        read_latency.push_back(l.elapsed<std::chrono::nanoseconds>());
      }
#endif
    }

#ifdef LATENCY
    lock_guard<std::mutex> lock(latency_mtx);
    read_latency_all.insert(read_latency_all.end(), read_latency.begin(),
                            read_latency.end());
    insert_latency_all.insert(insert_latency_all.end(), insert_latency.begin(),
                              insert_latency.end());
#endif
  };

  fun = operate;
  part = RUN_SIZE / num_thread;
  thread ths[num_thread];
  sw.start();
  for (size_t i = 0; i < num_thread; i++) {
    ths[i] = thread(fun, part * i, part, i);
  }
  for (size_t i = 0; i < num_thread; i++) {
    ths[i].join();
  }
  // tree->FlushAll();
  // para->FlushAllPages();
  auto t = sw.elapsed<std::chrono::milliseconds>();

  printf("Throughput: run, " KRED "%3.2f" KRESET " Kops/s\n",
         (RUN_SIZE * 1.0) / (t));
  printf("Run time: %4.2f sec\n", t / 1000.0);

#ifdef DRAM_CONSUMPTION
  GetDRAMSpace();
#endif

#ifdef LATENCY
  PrintLatency(insert_latency_all, "Insert");
  PrintLatency(read_latency_all, "Read");
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

#ifdef BBTREE_ON_F2FS_ZNS_SSD
  // auto wal_size = tree->wal_->Size();
  // double wal_bytes_avg_write = 1.0 * wal_size / (LOAD_SIZE + RUN_SIZE);
  // printf("[WriteAheadLog]:  Write amp: %6.2f bytes/op\n",
  // wal_bytes_avg_write);
#elif defined(ZBTREE_ON_ZNS) or defined(COWBTREE_ON_ZNS)
  para->Print();
  para->PrintReadCache();
#endif
  delete tree;
  para->Close();

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
  printf("[BTreeIndex]: Read suc: %lu, Read fail: %lu, Read false: %lu\n",
         read_suc.load(), read_fail.load(), read_false.load());
#ifndef GDB
  reg_ssd.logAfter();
  conv_ssd.logAfter();
  reg_ssd.printLog(RUN_SIZE);
  conv_ssd.printLog(RUN_SIZE);
#endif
}

void run_realistic_workload(int num_thread, string workload, u64 max_load_size,
                            u64 max_run_size) {
  ifstream infile(workload);
  u64 tmp_key;
  u64 tmp_value;
  u64 op_count = 0;
  vector<std::pair<u64, u64>> pairs;

  string op;
  while (infile.good() || op_count == max_load_size) {
    infile.read(reinterpret_cast<char *>(&tmp_key), sizeof(tmp_key));
    if (infile.fail()) break;
    infile.read(reinterpret_cast<char *>(&tmp_value), sizeof(tmp_value));
    if (infile.fail()) break;

    op_count++;
    pairs.push_back({tmp_key, tmp_value});
  }
  infile.close();
  op_count = pairs.size();
  printf("Loaded %8lu keys for running for %s.\n", op_count, workload.c_str());

  Timer tr;
  tr.start();
  ZoneManagerPool *para =
      new ZoneManagerPool(MAX_CACHED_PAGES_PER_ZONE, MAX_NUMS_ZONE, ZNS_DEVICE);
  BTree *device_tree = new BTree(para);
  btreeolc::ZBTree<KeyType, ValueType> *tree =
      new btreeolc::ZBTree<KeyType, ValueType>(device_tree);
  printf("Tree init:" KWHT " %s" KRESET " %4.2f ms.\n", TREE_NAME.c_str(),
         tr.elapsed<std::chrono::milliseconds>());
  printf("---------------------Load------------------------\n");

  GetDRAMSpace();

  SmartLog reg_ssd(REGURLAR_DEVICE);
  SmartLog conv_ssd(ZNS_DEVICE);
  reg_ssd.logBefore();
  conv_ssd.logBefore();

  int load_thread = (num_thread > 32) ? num_thread : 32;
  auto part = max_load_size / load_thread;
  auto left = max_load_size % load_thread;
  {
    // Load
    Timer sw;
    thread ths[load_thread];
    sw.start();
    auto insert = [&](size_t start, size_t len, int tid) {
      auto end = start + len;
      // cout << "start:" << start << "end:" << start + len << endl;
      for (size_t i = start; i < end; i++) {
        tree->Insert(pairs[i].first, pairs[i].second);
      }
    };

    for (size_t i = 0; i < load_thread; i++) {
      ths[i] = thread(insert, part * i, part, i);
    }
    for (size_t i = 0; i < left; i++) {
      tree->Insert(pairs[i].first, pairs[i].second);
    }
    for (size_t i = 0; i < load_thread; i++) {
      ths[i].join();
    }
    auto t = sw.elapsed<std::chrono::milliseconds>();
    printf("Throughput: load, " KGRN "%3.2f" KRESET " Kops/s\n",
           (max_load_size * 1.0) / (t));
    printf("Load time: %4.2f sec\n", t / 1000.0);
  }

  std::shuffle(pairs.begin(), pairs.end(), std::default_random_engine());
  para->Print();
  tree->Print();
  reg_ssd.logAfter();
  conv_ssd.logAfter();
  reg_ssd.printLog(max_load_size);
  conv_ssd.printLog(max_load_size);
  reg_ssd.logBefore();
  conv_ssd.logBefore();

  printf("---------------------Run-------------------------\n");
  Timer sw;

  atomic_uint64_t read_suc = 0, read_fail = 0, read_false = 0;
  std::function<void(size_t start, size_t len, int tid)> fun;
  auto operate = [&](size_t start, size_t len, int tid) {
    vector<size_t> latency;
    auto end = start + len;
    Timer l;

    bool rf = false;
    for (size_t i = start; i < end; i++) {
      u64 v;
      u64 key = pairs[i].first;
      u64 value = pairs[i].second;
      auto r = tree->Get(key, v);
      if (r && v == value || r && v == value + 1) {
        read_suc++;
      } else if (!r) {
        read_fail++;
      } else {
        read_false++;
        // cout << keys[i] << " " << v << endl;
      }
    }
  };

  fun = operate;
  part = max_run_size / num_thread;
  thread ths[num_thread];
  sw.start();
  for (size_t i = 0; i < num_thread; i++) {
    ths[i] = thread(fun, part * i, part, i);
  }
  for (size_t i = 0; i < num_thread; i++) {
    ths[i].join();
  }
  tree->FlushAll();

  auto t = sw.elapsed<std::chrono::milliseconds>();

  printf("Throughput: run, " KRED "%3.2f" KRESET " Kops/s\n",
         (max_run_size * 1.0) / (t));
  printf("Run time: %4.2f sec\n", t / 1000.0);

  GetDRAMSpace();
  para->Print();
  para->PrintReadCache();
  delete tree;
  para->Close();

  auto file_size = para->GetFileSize();
  auto read_count = para->GetReadCount();
  auto write_count = para->GetWriteCount();
  delete para;
  printf("--------------------Closed------------------------\n");
  // printf("[zbd] read_count: %lu, write_count: %lu file_size: %luMB\n",
  //  read_count, write_count, file_size / 1024 / 1024);

  double page_read_avg = 1.0 * read_count * PAGE_SIZE / file_size;
  double page_write_avg = 1.0 * write_count * PAGE_SIZE / file_size;
  double bytes_read_avg =
      1.0 * read_count * PAGE_SIZE / (max_load_size + max_run_size);
  double bytes_write_avg =
      1.0 * write_count * PAGE_SIZE / (max_load_size + max_run_size);
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
  printf("[BTreeIndex]: Read suc: %lu, Read fail: %lu, Read false: %lu\n",
         read_suc.load(), read_fail.load(), read_false.load());
  reg_ssd.logAfter();
  conv_ssd.logAfter();
  reg_ssd.printLog(max_run_size);
  conv_ssd.printLog(max_run_size);
}

int main(int argc, char **argv) {
#ifndef GDB
  if (argc != 4) {
    printf("Usage: %s <workload> <threads> <size>\n", argv[0]);
    exit(0);
  };
#endif

  printf("--------------------Test Begin---------------------\n");

#ifndef GDB
  string workload = argv[1];
  printf(KNRM "workload: " KWHT "%s" KRESET ", threads: " KWHT "%s" KRESET "\n",
         argv[1], argv[2]);
  int num_thread = atoi(argv[2]);
  u64 max_load_size = MILLION * atoi(argv[3]);
  u64 max_run_size = MILLION * atoi(argv[3]);
#else
  string workload = "ycsbd";
  int num_thread = 1;
  int GDB_SIZE = 1;
  printf("workload: %s, threads: %2d\n", workload.c_str(), num_thread);
  u64 max_load_size = MILLION * GDB_SIZE;
  u64 max_run_size = MILLION * GDB_SIZE;
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
  } else if (workload.find("real") != string::npos) {
    load_data = AMZN_WORKLOAD;
    run_data = AMZN_WORKLOAD;
    max_load_size = MILLION * 10;
    run_realistic_workload(num_thread, AMZN_WORKLOAD, max_load_size,
                           max_run_size);
    run_realistic_workload(num_thread, FB_WORKLOAD, max_load_size,
                           max_run_size);
    run_realistic_workload(num_thread, WIKI_WORKLOAD, max_load_size,
                           max_run_size);
    goto END;
  } else {
    printf("Wrong workload!\n");
    return 0;
  }

  run_test(num_thread, load_data, run_data, workload, max_load_size,
           max_run_size);
  // remove(FILE_NAME.c_str());
END:
#ifdef DRAM_CONSUMPTION
  GetDRAMSpace();
#endif

  printf("---------------------Test End----------------------\n");

  return 0;
}