#include "./bench_util.h"

/**
 * Test parameters
 */
#define LATENCY
#define DRAM_CONSUMPTION
// #define GDB

#define ROCKSDB_ZFS

#include <rocksdb/db.h>
#include <rocksdb/env.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/plugin/zenfs/fs/fs_zenfs.h>
#include <rocksdb/statistics.h>
const std::string TREE_NAME = "RocksDBZenFS";
rocksdb::FileSystem *fs_ptr = nullptr;
rocksdb::DB *CreateRocksDBZenFs() {
  rocksdb::DB *db;
  rocksdb::Options options;

  // 创建ZenFS文件系统对象
  std::string zbd = ZNS_DEVICE;
  std::string prefix = "/dev/";
  size_t pos = zbd.find(prefix);
  if (pos != std::string::npos) {
    zbd = zbd.substr(pos + prefix.size());
  }
  std::cout << zbd << std::endl;
  rocksdb::FileSystem *zenfs;
  rocksdb::Status s =
      rocksdb::NewZenFS(&zenfs, rocksdb::ZbdBackendType::kBlockDev, zbd);
  if (!s.ok()) {
    std::cout << "NewZenFSFileSystem failed: " << s.ToString() << std::endl;
    return nullptr;
  }
  fs_ptr = zenfs;
  // 设置RocksDB的文件系统
  options.env =
      rocksdb::NewCompositeEnv(std::shared_ptr<rocksdb::FileSystem>(zenfs))
          .release();

  options.statistics = rocksdb::CreateDBStatistics();

  // 其他的选项设置
  options.create_if_missing = true;
  options.write_buffer_size = BUFFER_POOL_SIZE / 4;
  // options.write_buffer_size = BUFFER_POOL_SIZE / 4;
  // options.bytes_per_sync = 32 * 1024;
  options.max_write_buffer_number = 1;
  options.compression = rocksdb::kNoCompression;
  options.use_direct_io_for_flush_and_compaction = true;
  options.use_direct_reads = true;
  // options.max_background_flushes = 0;
  // options.max_background_jobs = 14;

  options.compaction_style = rocksdb::kCompactionStyleLevel;
  // options.compaction_style = rocksdb::CompactionStyle::kCompactionStyleNone;
  // options.compaction_style = rocksdb::CompactionStyle::kCompactionStyleFIFO;
  options.max_bytes_for_level_base = 1 * 1024 * 1024;
  options.num_levels = 4;
  options.max_bytes_for_level_multiplier = 4;
  options.level0_slowdown_writes_trigger = 12;
  options.level0_stop_writes_trigger = 16;
  // 使options生效

  rocksdb::BlockBasedTableOptions table_options;

  // auto cache = rocksdb::NewLRUCache(BUFFER_POOL_SIZE / 2);
  auto cache = rocksdb::NewLRUCache(BUFFER_POOL_SIZE * 3 / 4);
  table_options.block_cache = cache;
  // table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(2, false));
  table_options.filter_policy.reset();
  table_options.block_size = 4 * 1024;
  table_options.no_block_cache = true;
  table_options.block_cache.reset();
  options.table_factory.reset(
      rocksdb::NewBlockBasedTableFactory(table_options));
  // 打开数据库时删除原有数据库
  // 删除旧数据库
  rocksdb::Status status = rocksdb::DestroyDB("", options);
  if (!status.ok()) {
    std::cerr << "删除旧数据库失败: " << status.ToString() << std::endl;
    // 处理错误
    exit(1);
  }

  // open时覆盖原数据库
  s = rocksdb::DB::Open(options, "", &db);

  assert(s.ok());
  return db;
}

u64 LOAD_SIZE;
u64 RUN_SIZE;

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
#endif

  printf("Tree init:" KWHT " %s" KRESET " %4.2f ms.\n", TREE_NAME.c_str(),
         tr.elapsed<std::chrono::milliseconds>());

  auto part = LOAD_SIZE / num_thread;

  auto store_keys = new string[LOAD_SIZE];
  for (size_t i = 0; i < LOAD_SIZE; i++) {
    store_keys[i] = to_string(init_keys[i]);
  }
  auto find_keys = new string[RUN_SIZE];
  for (size_t i = 0; i < RUN_SIZE; i++) {
    find_keys[i] = to_string(keys[i]);
  }

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
#else
        u64 v;
        auto r = tree->Get(keys[i], v);
#endif
      } else if (ops[i] == OP_DELETE) {
        // tree->Remove(keys[i]);
      } else if (ops[i] == OP_SCAN) {
#ifdef ROCKSDB_ZFS
        rocksdb::ReadOptions read_options;
        std::unique_ptr<rocksdb::Iterator> it(tree->NewIterator(read_options));
        rocksdb::Slice key(find_keys[i]);
        int count = 0;
        for (it->Seek(key); it->Valid() && count < value_lens[i]; it->Next()) {
        }

#endif
        // tree->Scan(keys[i],keys[i]+100);
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

#ifndef ROCKSDB_ZFS
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

#ifndef BBTREE_ON_EXT4_SSDD
  std::string compactionStats;
  if (tree->GetProperty("rocksdb.stats", &compactionStats)) {
    std::cout << "Stats:\n" << compactionStats << std::endl;
  } else {
    std::cerr << "获取Stats失败" << std::endl;
  }
  // tree->GetProperty("rocksdb.compaction.stats", &compactionStats);
  //   std::cout << "Compaction Stats:\n" << compactionStats << std::endl;
  delete tree;
#endif

#ifndef ROCKSDB_ZFS
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
  rocksdb::DB *tree = CreateRocksDBZenFs();

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
        u64 tmp_key = pairs[i].first;
        u64 tmp_value = pairs[i].second;
        rocksdb::Slice key(reinterpret_cast<char *>(&tmp_key));
        rocksdb::Slice value(reinterpret_cast<char *>(&tmp_value));
        tree->Put(rocksdb::WriteOptions(), key, value);
      }
    };

    for (size_t i = 0; i < load_thread; i++) {
      ths[i] = thread(insert, part * i, part, i);
    }
    for (size_t i = 0; i < left; i++) {
      u64 tmp_key = pairs[i].first;
      u64 tmp_value = pairs[i].second;
      rocksdb::Slice key(reinterpret_cast<char *>(&tmp_key));
      rocksdb::Slice value(reinterpret_cast<char *>(&tmp_value));
      tree->Put(rocksdb::WriteOptions(), key, value);
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
      std::string get_value;
      u64 tmp_key = pairs[i].first;
      rocksdb::Slice key(reinterpret_cast<char *>(&tmp_key));
      rocksdb::Slice value(reinterpret_cast<char *>(&tmp_value));
      tree->Get(rocksdb::ReadOptions(), key, &get_value);
      if (get_value != value.ToString()) {
        // printf("%s %s\n", get_value.c_str(), value.ToString().c_str());
        // cout << "Read error!" << endl;
        // cout << key.ToString() << " " << get_value << endl;
        read_fail++;
        // exit(1);
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
  delete tree;
  delete fs_ptr;
  fs_ptr = nullptr;
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