
#include "./bench_util.h"

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

#include <wiredtiger.h>

#include "./wiredtiger/wt_util.h"

#define WiredTigerZNS
const std::string TREE_NAME = "WiredTigerZNS";

u64 LOAD_SIZE;
u64 RUN_SIZE;

std::string exec(const char *cmd) {
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

std::pair<u64, u64> getDataUnits(const std::string &device) {
  std::string cmd_base =
      "sudo nvme smart-log " + device + " | grep 'Data Units ";
  std::string output_read =
      exec((cmd_base + "Read' | awk '{print $5}'").c_str());
  std::string output_written =
      exec((cmd_base + "Written' | awk '{print $5}'").c_str());
  return {stoull(output_read), stoull(output_written)};
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
    } else if (op.compare(update) == 0) {
      infile_run >> value_len;
      ops[count] = OP_UPDATE;
      keys[count] = key;
      value_lens[count] = value_len;
    } else if (op.compare(read) == 0) {
      ops[count] = OP_READ;
      keys[count] = key;
    } else if (op.compare(remove) == 0) {
      ops[count] = OP_DELETE;
      keys[count] = key;
    } else if (op.compare(scan) == 0) {
      infile_run >> value_len;
      ops[count] = OP_SCAN;
      keys[count] = key;
      value_lens[count] = value_len;
    } else {
      continue;
    }
    count++;
  }
  RUN_SIZE = count;

  printf("Loaded %8lu keys for running.\n", RUN_SIZE);
#ifdef DRAM_CONSUMPTION
  //   test();
  //   int ret = system((dram_shell + process_name).c_str());
  GetDRAMSpace();
#endif
  Timer tr;
  tr.start();

#ifdef RAW_BTREE_ON_FS
  // DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  ParallelBufferPoolManager *para = new ParallelBufferPoolManager(
      INSTANCE_SIZE, PAGES_SIZE, FILE_NAME, false);
  btreeolc::BTree *tree = new btreeolc::BTree(para);
#elif defined(BBTREE_ON_EXT4_SSDD)
  ParallelBufferPoolManager *para = new ParallelBufferPoolManager(
      INSTANCE_SIZE, PAGES_SIZE, FILE_NAME, false);
  std::shared_ptr<btreeolc::BTree> device_tree =
      std::make_shared<btreeolc::BTree>(para);
  std::shared_ptr<btreeolc::BufferBTree<KeyType, ValueType>> tree(
      new btreeolc::BufferBTree<KeyType, ValueType>(device_tree));
#elif defined(BTREE_ON_ZNS)
  DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  FIFOBatchBufferPool *para = new FIFOBatchBufferPool(PAGES_SIZE, disk);
  btreeolc::BTree *tree = new btreeolc::BTree(para);
#elif defined(ROCKSDB_ZFS)
  rocksdb::DB *tree = CreateRocksDBZenFs();
#elif defined(WiredTigerZNS)
  WT_CONNECTION *conn_ = nullptr;
  WT_CURSOR *tmp_cursor = nullptr;
  WT_SESSION *session = nullptr;
  tmp_cursor = wtInit(&conn_, &session);

  vector<std::pair<WT_SESSION *, WT_CURSOR *>> tree_workers;
  for (int i = 0; i < num_thread; i++) {
    WT_SESSION *session = nullptr;
    WT_CURSOR *cursor = nullptr;
    error_check(conn_->open_session(conn_, NULL, NULL, &session));
    error_check(session->open_cursor(session, "table:wired-zns", NULL,
                                     "overwrite=true", &cursor));
    tree_workers.push_back(std::make_pair(session, cursor));
  }
#endif

  printf("Tree init:" KWHT " %s" KRESET " %4.2f ms.\n", TREE_NAME.c_str(),
         tr.elapsed<std::chrono::milliseconds>());

  auto part = LOAD_SIZE / num_thread;

#ifdef ROCKSDB_ZFS
  auto store_keys = new string[LOAD_SIZE];
  for (size_t i = 0; i < LOAD_SIZE; i++) {
    store_keys[i] = to_string(init_keys[i]);
  }
  auto find_keys = new string[RUN_SIZE];
  for (size_t i = 0; i < RUN_SIZE; i++) {
    find_keys[i] = to_string(keys[i]);
  }
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
#ifdef ROCKSDB_ZFS
        rocksdb::Slice key(store_keys[i]);
        tree->Put(rocksdb::WriteOptions(), key, key);
#elif defined(WiredTigerZNS)
        WT_CURSOR *cursor = tree_workers[tid % num_thread].second;
        cursor->set_key(cursor, init_keys[i]);
        cursor->set_value(cursor, init_keys[i]);
        error_check(cursor->insert(cursor));
#else
        tree->Insert(init_keys[i], init_keys[i]);
#endif
      }
    };

    for (size_t i = 0; i < num_thread; i++) {
      ths[i] = thread(insert, part * i, part, i);
    }
    for (size_t i = 0; i < num_thread; i++) {
      ths[i].join();
    }
    auto t = sw.elapsed<std::chrono::milliseconds>();
    printf("Throughput: load, " KGRN "%3.3f" KRESET " Kops/s\n",
           (LOAD_SIZE * 1.0) / (t));
    printf("Load time: %4.4f sec\n", t / 1000.0);
  }
  part = RUN_SIZE / num_thread;
  // Run
  Timer sw;
#ifdef LATENCY
  vector<size_t> read_latency_all;
  vector<size_t> insert_latency_all;
  std::mutex latency_mtx;
#endif
  atomic_uint64_t read_suc = 0, read_fail = 0;
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
#ifdef ROCKSDB_ZFS
        rocksdb::Slice key(find_keys[i]);
        tree->Put(rocksdb::WriteOptions(), key, key);
#elif defined(WiredTigerZNS)
        WT_CURSOR *cursor = tree_workers[tid % num_thread].second;
        cursor->set_key(cursor, keys[i]);
        cursor->set_value(cursor, keys[i]);
        error_check(cursor->insert(cursor));
#else
        tree->Insert(keys[i], keys[i]);
#endif
      } else if (ops[i] == OP_UPDATE) {
        // tree->Update(keys[i], keys[i] + 1);
      } else if (ops[i] == OP_READ) {
#ifdef ROCKSDB_ZFS
        std::string get_value;
        rocksdb::Slice key(find_keys[i]);
        tree->Get(rocksdb::ReadOptions(), key, &get_value);
        if (key.ToString() != get_value) {
          // cout << "Read error!" << endl;
          // cout << key.ToString() << " " << get_value << endl;
          read_fail++;
          // exit(1);
        } else {
          read_suc++;
        }
#elif defined(WiredTigerZNS)
        u64 value = 0;
        WT_CURSOR *cursor = tree_workers[tid % num_thread].second;
        cursor->set_key(cursor, init_keys[i]);
        cursor->search(cursor);
        cursor->get_value(cursor, &value);
        if (init_keys[i] != value) {
          read_fail++;
        } else {
          read_suc++;
        }
        // printf("%lu %lu\n", init_keys[i], value);
#else
        u64 v;
        auto r = tree->Get(keys[i], v);
#endif
      } else if (ops[i] == OP_DELETE) {
        // tree->Remove(keys[i]);
      } else if (ops[i] == OP_SCAN) {
        // tree->Scan(keys[i],keys[i]+100);
#ifdef WiredTigerZNS
        u64 value = 0;
        int ret = 0, exact;
        WT_CURSOR *cursor = tree_workers[tid % num_thread].second;
        cursor->set_key(cursor, init_keys[i]);
        cursor->search_near(cursor, &exact);
        if (exact < 0) {
          ret = cursor->next(cursor);
        }
        for (int j = 0; !ret && j < value_lens[i]; ++j) {
          cursor->get_value(cursor, &value);
        }
#endif
      }
      // printf("%lu %lu\n", init_keys[i], value);
#ifdef LATENCY
      int current_op = ops[i];
      if (current_op == OP_INSERT || current_op == OP_UPDATE ||
          current_op == OP_DELETE) {
        insert_latency.push_back(l.elapsed<std::chrono::nanoseconds>());
      } else if (current_op == OP_READ || current_op == OP_SCAN) {
        read_latency.push_back(l.elapsed<std::chrono::nanoseconds>());
      };
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
  thread ths[num_thread];

  sw.start();
  for (size_t i = 0; i < num_thread; i++) {
    ths[i] = thread(fun, part * i, part, i);
  }
  for (size_t i = 0; i < num_thread; i++) {
    ths[i].join();
  }
#ifdef BBTREE_ON_EXT4_SSDD
  tree->flush_all();
#endif
  auto t = sw.elapsed<std::chrono::milliseconds>();

  printf("Throughput: run, " KRED "%f" KRESET " Kops/s\n",
         (RUN_SIZE * 1.0) / (t));
  printf("Run time: %4.4f sec\n", t / 1000.0);

#ifdef LATENCY
  PrintLatency(insert_latency_all, "Insert");
  PrintLatency(read_latency_all, "Read");
#endif
#ifdef DRAM_CONSUMPTION
  //   ret = system((dram_shell + process_name).c_str());
  GetDRAMSpace();
#endif

#if !defined(ROCKSDB_ZFS) && !defined(WiredTigerZNS)
  para->FlushAllPages();
  auto file_size = para->GetFileSize();
  auto read_count = para->GetReadCount();
  auto write_count = para->GetWriteCount();
#endif

#ifdef BBTREE_ON_EXT4_SSDD
  auto wal_size = tree->wal_->Size();
  double wal_bytes_avg_write = 1.0 * wal_size / (LOAD_SIZE + RUN_SIZE);
  printf("[WriteAheadLog]:  Write amp: %6.2f bytes/op\n", wal_bytes_avg_write);
#endif

#ifdef WiredTigerZNS

  WT_CURSOR *stat_cursor = nullptr;
  error_check(session->open_cursor(session, "statistics:table:wired-zns", NULL,
                                   NULL, &stat_cursor));
  u64 app_insert = 0, app_remove = 0, app_update = 0, fs_writes = 0,
      fs_reads = 0;
  get_stat(stat_cursor, WT_STAT_DSRC_CURSOR_INSERT_BYTES, &app_insert);
  get_stat(stat_cursor, WT_STAT_DSRC_CURSOR_REMOVE_BYTES, &app_remove);
  get_stat(stat_cursor, WT_STAT_DSRC_CURSOR_UPDATE_BYTES, &app_update);
  get_stat(stat_cursor, WT_STAT_DSRC_CACHE_BYTES_WRITE, &fs_writes);
  get_stat(stat_cursor, WT_STAT_DSRC_CACHE_BYTES_READ, &fs_reads);
  if (app_insert + app_remove + app_update != 0) {
    printf(
        "insert: %lu, remove: %lu, update: %lu, fs_writes: %lu fs_reads: %lu\n",
        app_insert, app_remove, app_update, fs_writes, fs_reads);
    printf("Write amplification is "KYEL"%.2lf\n"KRESET,
           (double)fs_writes / (app_insert + app_remove + app_update));
  }
  error_check(stat_cursor->close(stat_cursor));

  // can not run https://source.wiredtiger.com/11.1.0/tune_statistics.html
  for (int i = 0; i < num_thread; i++) {
    error_check(tree_workers[i].second->close(tree_workers[i].second));
    error_check(tree_workers[i].first->close(tree_workers[i].first, NULL));
  }

  error_check(tmp_cursor->close(tmp_cursor));
  error_check(session->close(session, NULL));
  error_check(conn_->close(conn_, NULL));
#else
  delete tree;
#endif

#if !defined(ROCKSDB_ZFS) && !defined(WiredTigerZNS)
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
      "[BufferPool]: In-place read in a page: %6.2f, In-place write in a page: "
      "%6.2f\n",
      page_read_avg, page_write_avg);
  printf("[BTreeIndex]: Read amp: " KBLU "%6.2f" KRESET
         " bytes/op, Write amp: " KYEL "%6.2f" KRESET " bytes/op\n",
         bytes_read_avg, bytes_write_avg);
#endif
  printf("[BTreeIndex]: Read suc: %lu, Read fail: %lu\n", read_suc.load(),
         read_fail.load());
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
  WT_CONNECTION *conn_ = nullptr;
  WT_CURSOR *tmp_cursor = nullptr;
  WT_SESSION *session = nullptr;
  tmp_cursor = wtInit(&conn_, &session);

  vector<std::pair<WT_SESSION *, WT_CURSOR *>> tree_workers;
  for (int i = 0; i < num_thread; i++) {
    WT_SESSION *session = nullptr;
    WT_CURSOR *cursor = nullptr;
    error_check(conn_->open_session(conn_, NULL, NULL, &session));
    error_check(session->open_cursor(session, "table:wired-zns", NULL,
                                     "overwrite=true", &cursor));
    tree_workers.push_back(std::make_pair(session, cursor));
  }

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
        WT_CURSOR *cursor = tree_workers[tid % num_thread].second;
        u64 tmp_key = pairs[i].first;
        u64 tmp_value = pairs[i].second;
        cursor->set_key(cursor, tmp_key);
        cursor->set_value(cursor, tmp_value);
        error_check(cursor->insert(cursor));
      }
    };

    for (size_t i = 0; i < load_thread; i++) {
      ths[i] = thread(insert, part * i, part, i);
    }
    for (size_t i = 0; i < left; i++) {
      WT_CURSOR *cursor = tree_workers[0].second;
      u64 tmp_key = pairs[i].first;
      u64 tmp_value = pairs[i].second;
      cursor->set_key(cursor, tmp_key);
      cursor->set_value(cursor, tmp_value);
      error_check(cursor->insert(cursor));
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
      u64 value = 0;
      u64 tmp_key = pairs[i].first;
      u64 tmp_value = pairs[i].first;
      WT_CURSOR *cursor = tree_workers[tid % num_thread].second;

      cursor->set_key(cursor, tmp_key);
      cursor->search(cursor);
      cursor->get_value(cursor, &value);
      if (tmp_value != value) {
        read_fail++;
      } else {
        read_suc++;
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

  auto t = sw.elapsed<std::chrono::milliseconds>();

  printf("Throughput: run, " KRED "%3.2f" KRESET " Kops/s\n",
         (max_run_size * 1.0) / (t));
  printf("Run time: %4.2f sec\n", t / 1000.0);

  GetDRAMSpace();
  for (int i = 0; i < num_thread; i++) {
    error_check(tree_workers[i].second->close(tree_workers[i].second));
    error_check(tree_workers[i].first->close(tree_workers[i].first, NULL));
  }
  error_check(tmp_cursor->close(tmp_cursor));
  error_check(session->close(session, NULL));
  error_check(conn_->close(conn_, NULL));

  printf("[BTreeIndex]: Read suc: %lu, Read fail: %lu\n", read_suc.load(),
         read_fail.load());
  printf("--------------------Closed------------------------\n");

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
  LOAD_SIZE = max_load_size;
  RUN_SIZE = max_run_size;
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
    run_realistic_workload(num_thread, WIKI_WORKLOAD, max_load_size,
                           max_run_size);
    run_realistic_workload(num_thread, FB_WORKLOAD, max_load_size,
                           max_run_size);
    run_realistic_workload(num_thread, AMZN_WORKLOAD, max_load_size,
                           max_run_size);
    printf("---------------------Test End----------------------\n");
    return 0;
  } else {
    printf("Wrong workload!\n");
    return 0;
  }

#ifndef GDB
  SmartLog reg_ssd(REGURLAR_DEVICE);
  SmartLog conv_ssd(ZNS_DEVICE);
  reg_ssd.logBefore();
  conv_ssd.logBefore();
#endif
  run_test(num_thread, load_data, run_data, workload, max_load_size,
           max_run_size);
#ifndef GDB
  reg_ssd.logAfter();
  conv_ssd.logAfter();
  reg_ssd.printLog(LOAD_SIZE + RUN_SIZE);
  conv_ssd.printLog(LOAD_SIZE + RUN_SIZE);
#endif

  printf("---------------------Test End----------------------\n");
  return 0;
}