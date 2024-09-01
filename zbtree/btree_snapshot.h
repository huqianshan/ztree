#ifndef __BTREE_SNAPSHOT_H
#define __BTREE_SNAPSHOT_H
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <filesystem>
#include <queue>
#include <unordered_map>
#include <vector>

#include "wal.h"
#include "zbtree.h"
namespace btreeolc {

struct btree_snapshot {
  struct node_info {
    PageType type;
    page_id_t page_id;
    uint16_t count;
    int64_t id;
  };
  static void shot(NodeBase* root) {
    // using BTreeInner = BTreeInner;
    // using BTreeLeaf = BTreeLeaf;
    using namespace std::filesystem;
    if (root == nullptr) {
      return;
    }
    static int snapshot_id = get_snapshot_id();
    snapshot_id++;
    path _path =
        SNAPSHOT_PATH_BTREE + std::to_string(snapshot_id) + SNAPSHOT_EXT;
    std::queue<decltype(root)> q;
    q.push(root);
    int cur_num = 1;
    int cur_id = 1;
    std::ofstream ofs(_path, std::ios::binary | std::ios::out);
    time_t timestamp = time(nullptr);
    ofs.write((char*)&timestamp, sizeof(time_t));
    while (!q.empty()) {
      int sz = q.size();
      for (int i = 0; i < sz; ++i) {
        auto node = q.front();
        q.pop();
        node_info info = {.type = node->type,
                          .page_id = INVALID_PAGE_ID,
                          .count = node->count,
                          .id = cur_id++};
        if (node->type == PageType::BTreeLeaf) {
          auto leaf = dynamic_cast<BTreeLeaf*>(node);
          info.page_id = leaf->page_id;
          ofs.write(reinterpret_cast<char*>(&info), sizeof(node_info));
        } else {
          auto inner = dynamic_cast<BTreeInner*>(node);
          ofs.write(reinterpret_cast<char*>(&info), sizeof(node_info));
          int64_t buf[InnerNodeMaxEntries + 2];
          for (int j = 0; j <= inner->count; ++j) {
            assert(inner->children[j] != nullptr);
            q.push(inner->children[j]);
            cur_num++;
            buf[j] = cur_num;
          }
          ofs.write(reinterpret_cast<char*>(inner->keys),
                    sizeof(Key) * (inner->count));
          ofs.write(reinterpret_cast<char*>(buf),
                    sizeof(buf[0]) * (inner->count + 1));
        }
      }
    }
    ofs.flush();
  }
  static std::pair<NodeBase*, time_t> recover() {
    int snapshot_id = get_snapshot_id();
    assert(snapshot_id != -1);
    using namespace std::filesystem;
    path _path =
        SNAPSHOT_PATH_BTREE + std::to_string(snapshot_id) + SNAPSHOT_EXT;
    std::queue<int> q;
    std::ifstream ifs(_path, std::ios::binary | std::ios::in);
    q.push(1);
    time_t timestamp = 0;
    ifs.read((char*)&timestamp, sizeof(time_t));
    std::unordered_map<int64_t, NodeBase*> id2node;
    while (!q.empty()) {
      int sz = q.size();
      for (int i = 0; i < sz; ++i) {
        node_info info;
        ifs.read(reinterpret_cast<char*>(&info), sizeof(node_info));
        auto id = q.front();
        q.pop();
        assert(id == info.id);
        if (info.type == PageType::BTreeLeaf) {
          auto leaf = new BTreeLeaf();
          leaf->count = info.count;
          leaf->type = info.type;
          leaf->page_id = info.page_id;
          leaf->data = nullptr;
          id2node[info.id] = leaf;
        } else {
          auto inner = new BTreeInner();
          inner->count = info.count;
          inner->type = info.type;
          id2node[info.id] = inner;
          int64_t buf[InnerNodeMaxEntries + 2];
          ifs.read(reinterpret_cast<char*>(inner->keys),
                   sizeof(KeyType) * (inner->count));
          ifs.read(reinterpret_cast<char*>(buf),
                   sizeof(buf[0]) * (inner->count + 1));
          for (int j = 0; j <= inner->count; ++j) {
            inner->children[j] = (NodeBase*)buf[j];
            q.push(buf[j]);
          }
        }
      }
    }
    NodeBase* root = id2node[1];
    std::queue<NodeBase*> q2;
    q2.push(root);
    while (!q2.empty()) {
      int sz = q2.size();
      for (int i = 0; i < sz; ++i) {
        auto node = q2.front();
        q2.pop();
        if (node->type == PageType::BTreeInner) {
          auto inner = dynamic_cast<BTreeInner*>(node);
          for (int j = 0; j <= inner->count; ++j) {
            inner->children[j] = id2node[(int64_t)inner->children[j]];
            q2.push(inner->children[j]);
          }
        }
      }
    }
    return {root, timestamp};
  }

  static std::vector<KeyValueType> recover_from_wal(time_t timestamp) {
    // we will read the wal file and replay the log that timetamp is larger than
    // the snapshot
    u32 instance = INSTANCE_SIZE;
    u32 length = PAGE_SIZE;
    std::vector<KeyValueType> res;
    for (int i = 0; i < instance; i++) {
      std::string name_with_suffix = WAL_NAME + "_" + std::to_string(i);
      char buffer[PAGE_SIZE];
      memset(buffer, 0, PAGE_SIZE);
      int fd = open(name_with_suffix.c_str(), O_RDONLY);
      assert(fd > 0);
      ssize_t ret = 0;
      while (1) {
        ret = read(fd, buffer, length);
        if (ret <= 0) {
          break;
        }
        assert(ret == length);
        time_t wal_timestamp = 0;
        int16_t wal_length = 0;
        memcpy(&wal_timestamp, buffer, WAL_TIMESTAMP_SIZE);
        memcpy(&wal_length, buffer + WAL_TIMESTAMP_SIZE, WAL_LENGTH_SIZE);
        if (wal_timestamp <= timestamp) {
          continue;
        }
        ssize_t offset = WAL_TIMESTAMP_SIZE + WAL_LENGTH_SIZE;
        for (int i = 0; offset < wal_length;
             i++, offset += sizeof(KeyValueType)) {
          KeyValueType kv;
          memcpy(&kv, buffer + offset, sizeof(KeyValueType));
          res.push_back(kv);
        }
        offset += ret;
      };
    }
    return res;
  }

 private:
  static int get_snapshot_id() {
    using namespace std::filesystem;
    int snapshot_id = -1;
    path _path = SNAPSHOT_PATH_BTREE;
    if (!exists(_path)) {
      create_directory(_path);
    }
    directory_entry entry(_path);
    if (!entry.exists()) {
      assert(false);
    }
    directory_iterator iter(_path);
    for (auto& it : iter) {
      if (it.is_regular_file()) {
        std::string filename = it.path().filename().string();
        if (filename.find(SNAPSHOT_EXT) != std::string::npos) {
          int id = std::stoi(
              filename.substr(0, filename.length() - SNAPSHOT_EXT.length()));
          snapshot_id = std::max(snapshot_id, id);
        }
      }
    }
    return snapshot_id;
  }
};
}  // namespace btreeolc

#endif