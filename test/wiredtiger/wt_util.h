#ifndef _WIREDTIGER_UTIL_H
#define _WIREDTIGER_UTIL_H

#include <sys/stat.h>
#include <wiredtiger.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

#define error_check(call)                                           \
  do {                                                              \
    int __ret;                                                      \
    if ((__ret = (call)) != 0) {                                    \
      fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, #call, \
              wiredtiger_strerror(__ret));                          \
      exit(EXIT_FAILURE);                                           \
    }                                                               \
  } while (0)

namespace {
#define WT_PREFIX "wiredtiger"
const std::string PROP_HOME = WT_PREFIX ".home";
const std::string PROP_HOME_DEFAULT = "";

const std::string PROP_FORMAT = WT_PREFIX ".format";
const std::string PROP_FORMAT_DEFAULT = "single";

const std::string PROP_CACHE_SIZE = WT_PREFIX ".cache_size";
const std::string PROP_CACHE_SIZE_DEFAULT = "100MB";

const std::string PROP_DIRECT_IO = WT_PREFIX ".direct_io";
const std::string PROP_DIRECT_IO_DEFAULT = "[]";

const std::string PROP_IN_MEMORY = WT_PREFIX ".in_memory";
const std::string PROP_IN_MEMORY_DEFAULT = "false";

const std::string PROP_LSM_MGR_MERGE = WT_PREFIX ".lsm_mgr.merge";
const std::string PROP_LSM_MGR_MERGE_DEFAULT = "true";

const std::string PROP_LSM_MGR_MAX_WORKERS = WT_PREFIX ".lsm_mgr.max_workers";
const std::string PROP_LSM_MGR_MAX_WORKERS_DEFAULT = "4";

const std::string PROP_BLK_MGR_ALLOCATION_SIZE =
    WT_PREFIX ".blk_mgr.allocation_size";
const std::string PROP_BLK_MGR_ALLOCATION_SIZE_DEFAULT = "4KB";

const std::string PROP_BLK_MGR_BLOOM_BIT_COUNT =
    WT_PREFIX ".blk_mgr.bloom_bit_count";
const std::string PROP_BLK_MGR_BLOOM_BIT_COUNT_DEFAULT = "16";

const std::string PROP_BLK_MGR_BLOOM_HASH_COUNT =
    WT_PREFIX ".blk_mgr.bloom_hash_count";
const std::string PROP_BLK_MGR_BLOOM_HASH_COUNT_DEFAULT = "8";

const std::string PROP_BLK_MGR_CHUNK_MAX = WT_PREFIX ".blk_mgr.chunk_max";
const std::string PROP_BLK_MGR_CHUNK_MAX_DEFAULT = "5GB";

const std::string PROP_BLK_MGR_CHUNK_SIZE = WT_PREFIX ".blk_mgr.chunk_size";
const std::string PROP_BLK_MGR_CHUNK_SIZE_DEFAULT = "10MB";

const std::string PROP_BLK_MGR_COMPRESSOR = WT_PREFIX ".blk_mgr.compressor";
const std::string PROP_BLK_MGR_COMPRESSOR_DEFAULT = "snappy";

const std::string PROP_BLK_MGR_BTREE_INTERNAL_PAGE_MAX =
    WT_PREFIX ".blk_mgr.btree.internal_page_max";
const std::string PROP_BLK_MGR_BTREE_INTERNAL_PAGE_MAX_DEFAULT = "4KB";

const std::string PROP_BLK_MGR_BTREE_LEAF_KEY_MAX =
    WT_PREFIX ".blk_mgr.btree.leaf_key_max";
const std::string PROP_BLK_MGR_BTREE_LEAF_KEY_MAX_DEFAULT = "0";

const std::string PROP_BLK_MGR_BTREE_LEAF_VALUE_MAX =
    WT_PREFIX ".blk_mgr.btree.leaf_value_max";
const std::string PROP_BLK_MGR_BTREE_LEAF_VALUE_MAX_DEFAULT = "0";

const std::string PROP_BLK_MGR_BTREE_LEAF_PAGE_MAX =
    WT_PREFIX ".blk_mgr.btree.leaf_page_max";
const std::string PROP_BLK_MGR_BTREE_LEAF_PAGE_MAX_DEFAULT = "32KB";
}  // namespace

class Exception : public std::exception {
 public:
  Exception(const std::string &message) : message_(message) {}
  const char *what() const noexcept { return message_.c_str(); }

 private:
  std::string message_;
};

inline std::string Trim(const std::string &str) {
  auto front = std::find_if_not(str.begin(), str.end(),
                                [](int c) { return std::isspace(c); });
  return std::string(
      front,
      std::find_if_not(str.rbegin(), std::string::const_reverse_iterator(front),
                       [](int c) { return std::isspace(c); })
          .base());
}

class Properties {
 public:
  std::string GetProperty(
      const std::string &key,
      const std::string &default_value = std::string()) const;
  const std::string &operator[](const std::string &key) const;
  void SetProperty(const std::string &key, const std::string &value);
  bool ContainsKey(const std::string &key) const;
  void Load(std::ifstream &input);

 private:
  std::map<std::string, std::string> properties_;
};

inline std::string Properties::GetProperty(
    const std::string &key, const std::string &default_value) const {
  std::map<std::string, std::string>::const_iterator it = properties_.find(key);
  if (properties_.end() == it) {
    return default_value;
  } else {
    return it->second;
  }
}

inline const std::string &Properties::operator[](const std::string &key) const {
  return properties_.at(key);
}

inline void Properties::SetProperty(const std::string &key,
                                    const std::string &value) {
  properties_[key] = value;
}

inline bool Properties::ContainsKey(const std::string &key) const {
  return properties_.find(key) != properties_.end();
}

inline void Properties::Load(std::ifstream &input) {
  if (!input.is_open()) {
    throw Exception("File not open!");
  }

  while (!input.eof() && !input.bad()) {
    std::string line;
    std::getline(input, line);
    if (line[0] == '#') continue;
    size_t pos = line.find_first_of('=');
    if (pos == std::string::npos) continue;
    SetProperty(Trim(line.substr(0, pos)), Trim(line.substr(pos + 1)));
  }
}

WT_CURSOR *wtInit(WT_CONNECTION **conn_, WT_SESSION **session_) {
  WT_CONNECTION *conn = nullptr;
  WT_CURSOR *cursor = nullptr;
  WT_SESSION *session = nullptr;
  const char *key, *value;
  int ret;

  // if (conn_) {
  //   error_check(conn->open_session(conn, NULL, NULL, &session));
  //   error_check(session->open_cursor(session, "table:wired-zns", NULL,
  //                                    "overwrite=true", &cursor));
  //   if (session_ == nullptr) {
  //     *session_ = session;
  //   }
  //   return cursor;
  // }

  Properties *props_ = new Properties();
  std::ifstream input("./wiredtiger/wiredtiger.properties");
  props_->Load(input);

  const Properties &props = *props_;
  {
    // 1. Setup wiredtiger home directory
    const std::string &home = props.GetProperty(PROP_HOME, PROP_HOME_DEFAULT);
    if (home.empty()) {
      throw Exception(WT_PREFIX " home is missing");
    }
    int ret = mkdir(home.c_str(), 0775);
    if (ret && errno != EEXIST) {
      throw Exception(std::string("Init mkdir: ") + strerror(errno));
    }

    // 2. Setup db config
    std::string db_config("create,");
    {  // 2.1 General
      const std::string &cache_size =
          props.GetProperty(PROP_CACHE_SIZE, PROP_CACHE_SIZE_DEFAULT);
      const std::string &direct_io =
          props.GetProperty(PROP_DIRECT_IO, PROP_DIRECT_IO_DEFAULT);
      const std::string &in_memory =
          props.GetProperty(PROP_IN_MEMORY, PROP_IN_MEMORY_DEFAULT);
      if (!cache_size.empty()) db_config += "cache_size=" + cache_size + ",";
      if (!direct_io.empty()) db_config += "direct_io=" + direct_io + ",";
      if (!in_memory.empty()) db_config += "in_memory=" + in_memory + ",";
    }
    {  // 2.2 LSM Manager
      std::string lsm_config;
      const std::string &lsm_merge =
          props.GetProperty(PROP_LSM_MGR_MERGE, PROP_LSM_MGR_MERGE_DEFAULT);
      const std::string &lsm_max_workers = props.GetProperty(
          PROP_LSM_MGR_MAX_WORKERS, PROP_LSM_MGR_MAX_WORKERS_DEFAULT);
      if (!lsm_merge.empty()) lsm_config += "merge=" + lsm_merge + ",";
      if (!lsm_max_workers.empty())
        lsm_config += "worker_thread_max=" + lsm_max_workers;

      if (!lsm_config.empty()) db_config += "lsm_manager=(" + lsm_config + ")";
    }
    // db_config +=
    // ",block_cache=(enabled=true,hashsize=10K,size=300MB,system_ram=300MB,type=DRAM)";
    // may degrade performance;
    db_config += ",statistics=(fast)";
    std::cout << "db config: " << db_config << std::endl;
    /* Open a connection to the database, creating it if necessary. */
    error_check(wiredtiger_open(home.c_str(), NULL, db_config.c_str(), &conn));
  }

  /* Open a session handle for the database. */
  error_check(conn->open_session(conn, NULL, NULL, &session));

  // Create table (once)
  {  // 1. Setup block manager
    std::string table_config("key_format=Q,value_format=Q,");
    {  // 1.1 General
      const std::string &alloc_size = props.GetProperty(
          PROP_BLK_MGR_ALLOCATION_SIZE, PROP_BLK_MGR_ALLOCATION_SIZE_DEFAULT);
      const std::string &compressor = props.GetProperty(
          PROP_BLK_MGR_COMPRESSOR, PROP_BLK_MGR_COMPRESSOR_DEFAULT);
      if (!alloc_size.empty())
        table_config += "allocation_size=" + alloc_size + ",";
      // if (!compressor.empty() &&
      // !std::set<std::string>{"snappy", "lz4", "zlib", "zstd", "empty"}
      //  .count(compressor)) {
      // throw Exception("unknown compressor name");
      // } else
      // table_config += "block_compressor=" + compressor + ",";
    }
    {  // 1.2 LSM relevant
      std::string lsm_config;
      const std::string &bloom_bit_count = props.GetProperty(
          PROP_BLK_MGR_BLOOM_BIT_COUNT, PROP_BLK_MGR_BLOOM_BIT_COUNT_DEFAULT);
      const std::string &bloom_hash_count = props.GetProperty(
          PROP_BLK_MGR_BLOOM_HASH_COUNT, PROP_BLK_MGR_BLOOM_HASH_COUNT_DEFAULT);
      const std::string &chunk_max = props.GetProperty(
          PROP_BLK_MGR_CHUNK_MAX, PROP_BLK_MGR_CHUNK_MAX_DEFAULT);
      const std::string &chunk_size = props.GetProperty(
          PROP_BLK_MGR_CHUNK_SIZE, PROP_BLK_MGR_CHUNK_SIZE_DEFAULT);
      if (!bloom_bit_count.empty())
        lsm_config += "bloom_bit_count=" + bloom_bit_count + ",";
      if (!bloom_hash_count.empty())
        lsm_config += "bloom_hash_count=" + bloom_hash_count + ",";
      if (!chunk_max.empty()) lsm_config += "chunk_max=" + chunk_max + ",";
      if (!chunk_size.empty()) lsm_config += "chunk_size=" + chunk_size;

      if (!lsm_config.empty()) table_config += "lsm=(" + lsm_config + "),";
    }
    {  // 1.3 BTree nodes
      const std::string &internal_page_max =
          props.GetProperty(PROP_BLK_MGR_BTREE_INTERNAL_PAGE_MAX,
                            PROP_BLK_MGR_BTREE_INTERNAL_PAGE_MAX_DEFAULT);
      const std::string &leaf_key_max =
          props.GetProperty(PROP_BLK_MGR_BTREE_LEAF_KEY_MAX,
                            PROP_BLK_MGR_BTREE_LEAF_KEY_MAX_DEFAULT);
      const std::string &leaf_value_max =
          props.GetProperty(PROP_BLK_MGR_BTREE_LEAF_VALUE_MAX,
                            PROP_BLK_MGR_BTREE_LEAF_VALUE_MAX_DEFAULT);
      const std::string &leaf_page_max =
          props.GetProperty(PROP_BLK_MGR_BTREE_LEAF_PAGE_MAX,
                            PROP_BLK_MGR_BTREE_LEAF_PAGE_MAX_DEFAULT);
      if (!internal_page_max.empty())
        table_config += "internal_page_max=" + internal_page_max + ",";
      if (!leaf_key_max.empty())
        table_config += "leaf_key_max=" + leaf_key_max + ",";
      if (!leaf_value_max.empty())
        table_config += "leaf_value_max=" + leaf_value_max + ",";
      if (!leaf_page_max.empty())
        table_config += "leaf_page_max=" + leaf_page_max;
    }
    std::cout << "table config: " << table_config << std::endl;
    error_check(
        session->create(session, "table:wired-zns", table_config.c_str()));
  }

  // Open cursor (per thread)
  error_check(session->open_cursor(session, "table:wired-zns", NULL,
                                   "overwrite=true", &cursor));
  if (conn_) {
    *conn_ = conn;
  }
  if (session_) {
    *session_ = session;
  }
  return cursor;
}

void get_stat(WT_CURSOR *cursor, int stat_field, uint64_t *valuep) {
  const char *desc, *pvalue;
  cursor->set_key(cursor, stat_field);
  error_check(cursor->search(cursor));
  error_check(cursor->get_value(cursor, &desc, &pvalue, valuep));
}

class WiredTigerZns {
 public:
  WT_CONNECTION *conn;
  WT_CURSOR *cursor{nullptr};
  WT_SESSION *session{nullptr};
  WiredTigerZns() = default;
  void Init(WT_CONNECTION *c_) {
    if (c_ != nullptr) {
      error_check(c_->open_session(c_, NULL, NULL, &session));
      error_check(session->open_cursor(session, "table:wired-zns", NULL,
                                       "overwrite=true", &cursor));
      return;
    }
    // Open connection (once, per process)
    cursor = wtInit(&conn, &session);
  }
  void Close() {
    cursor->close(cursor);
    error_check(session->close(session, NULL));
  }
};
#endif  // _WIREDTIGER_UTIL_H