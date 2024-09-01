#ifndef __SNAPSHOT_H
#define __SNAPSHOT_H
#include <filesystem>
#include <unordered_map>

#include "buffer_btree.h"
#include "config.h"
namespace btreeolc {
class snapshot {
 public:
  struct node_info {
    buffer_btree::PageType type;
    uint16_t count;
    int64_t id;
  };
  static void shot(buffer_btree::NodeBase* root) {
    using BTreeInner = buffer_btree::BTreeInner<KeyType>;
    using BTreeLeaf = buffer_btree::BTreeLeaf<KeyType, ValueType>;
    using namespace std::filesystem;
    if (root == nullptr) {
      return;
    }
    static int snapshot_id = get_snapshot_id();
    snapshot_id++;
    path _path = SNAPSHOT_PATH + std::to_string(snapshot_id) + SNAPSHOT_EXT;
    std::queue<decltype(root)> q;
    q.push(root);
    int cur_num = 1;
    int cur_id = 1;
    std::ofstream ofs(_path, std::ios::binary | std::ios::out);
    while (!q.empty()) {
      int sz = q.size();
      for (int i = 0; i < sz; ++i) {
        auto node = q.front();
        q.pop();
        node_info info = {node->type, node->count, cur_id++};
        if (node->type == buffer_btree::PageType::BTreeLeaf) {
          auto leaf = dynamic_cast<BTreeLeaf*>(node);
          ofs.write(reinterpret_cast<char*>(&info), sizeof(node_info));
          if (leaf->keys == nullptr) {
            assert(leaf->count == 0);
          } else {
            ofs.write(reinterpret_cast<char*>(leaf->keys),
                      sizeof(KeyType) * leaf->count);
            ofs.write(reinterpret_cast<char*>(leaf->payloads),
                      sizeof(ValueType) * leaf->count);
          }
        } else {
          auto inner = dynamic_cast<BTreeInner*>(node);
          ofs.write(reinterpret_cast<char*>(&info), sizeof(node_info));
          int64_t buf[BTreeInner::maxEntries];
          for (int j = 0; j <= inner->count; ++j) {
            assert(inner->children[j] != nullptr);
            q.push(inner->children[j]);
            cur_num++;
            buf[j] = cur_num;
          }
          ofs.write(reinterpret_cast<char*>(inner->keys),
                    sizeof(KeyType) * (inner->count));
          ofs.write(reinterpret_cast<char*>(buf),
                    sizeof(buf[0]) * (inner->count + 1));
        }
      }
    }
    ofs.flush();
  }
  static buffer_btree::NodeBase* recover() {
    int snapshot_id = get_snapshot_id();
    assert(snapshot_id != -1);
    using BTreeInner = buffer_btree::BTreeInner<KeyType>;
    using BTreeLeaf = buffer_btree::BTreeLeaf<KeyType, ValueType>;
    using namespace std::filesystem;
    path _path = SNAPSHOT_PATH + std::to_string(snapshot_id) + SNAPSHOT_EXT;
    std::queue<int> q;
    std::ifstream ifs(_path, std::ios::binary | std::ios::in);
    q.push(1);
    std::unordered_map<int64_t, buffer_btree::NodeBase*> id2node;
    while (!q.empty()) {
      int sz = q.size();
      for (int i = 0; i < sz; ++i) {
        node_info info;
        ifs.read(reinterpret_cast<char*>(&info), sizeof(node_info));
        auto id = q.front();
        q.pop();
        assert(id == info.id);
        if (info.type == buffer_btree::PageType::BTreeLeaf) {
          auto leaf = new BTreeLeaf();
          leaf->count = info.count;
          leaf->type = info.type;
          id2node[info.id] = leaf;
          if (leaf->count == 0) {
            leaf->keys = nullptr;
            leaf->payloads = nullptr;
          } else {
            leaf->keys = new KeyType[BTreeLeaf::maxEntries];
            leaf->payloads = new ValueType[BTreeLeaf::maxEntries];
            ifs.read(reinterpret_cast<char*>(leaf->keys),
                     sizeof(KeyType) * leaf->count);
            ifs.read(reinterpret_cast<char*>(leaf->payloads),
                     sizeof(ValueType) * leaf->count);
          }
        } else {
          auto inner = new BTreeInner();
          inner->count = info.count;
          inner->type = info.type;
          id2node[info.id] = inner;
          int64_t buf[BTreeInner::maxEntries];
          ifs.read(reinterpret_cast<char*>(inner->keys),
                   sizeof(KeyType) * (inner->count));
          ifs.read(reinterpret_cast<char*>(buf),
                   sizeof(buf[0]) * (inner->count + 1));
          for (int j = 0; j <= inner->count; ++j) {
            inner->children[j] = (buffer_btree::NodeBase*)buf[j];
            q.push(buf[j]);
          }
        }
      }
    }
    buffer_btree::NodeBase* root = id2node[1];
    std::queue<buffer_btree::NodeBase*> q2;
    q2.push(root);
    while (!q2.empty()) {
      int sz = q2.size();
      for (int i = 0; i < sz; ++i) {
        auto node = q2.front();
        q2.pop();
        if (node->type == buffer_btree::PageType::BTreeInner) {
          auto inner = dynamic_cast<BTreeInner*>(node);
          for (int j = 0; j <= inner->count; ++j) {
            inner->children[j] = id2node[(int64_t)inner->children[j]];
            q2.push(inner->children[j]);
          }
        }
      }
    }
    return root;
  }
  static bool cmp(buffer_btree::NodeBase* rt, buffer_btree::NodeBase* rec) {
    using BTreeInner = buffer_btree::BTreeInner<KeyType>;
    using BTreeLeaf = buffer_btree::BTreeLeaf<KeyType, ValueType>;
    if (rt == nullptr && rec == nullptr) return true;
    if (rt == nullptr || rec == nullptr) {
      std::cout << "rt" << rt << " rec" << rec << endl;
      return false;
    }
    if (rt->type != rec->type || rt->type != rec->type) return false;
    if (rt->type == btreeolc::buffer_btree::PageType::BTreeLeaf) {
      auto l = dynamic_cast<BTreeLeaf*>(rt);
      auto r = dynamic_cast<BTreeLeaf*>(rec);
      for (int i = 0; i < rt->count; ++i) {
        if (l->keys[i] != r->keys[i] || l->payloads[i] != r->payloads[i]) {
          std::cout << "value not same " << i << endl;
          std::cout << "l->keys[i] " << l->keys[i] << " r->keys[i] "
                    << r->keys[i] << endl;
          std::cout << "l->payloads[i] " << l->payloads[i] << " r->payloads[i] "
                    << r->payloads[i] << endl;
          return false;
        }
      }
    } else {
      auto l = dynamic_cast<BTreeInner*>(rt);
      auto r = dynamic_cast<BTreeInner*>(rec);
      for (int i = 0; i < rt->count; ++i) {
        if (l->keys[i] != r->keys[i]) {
          std::cout << "inner key not same" << endl;
          return false;
        }
      }
      for (int i = 0; i <= rt->count; ++i) {
        if (!cmp(l->children[i], r->children[i])) return false;
      }
    }
    return true;
  }

 private:
  static int get_snapshot_id() {
    using namespace std::filesystem;
    int snapshot_id = -1;
    path _path = SNAPSHOT_PATH;
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