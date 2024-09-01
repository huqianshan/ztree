#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include "common.h"

namespace BTree {

// const u32 INSTANCE_SIZE = 1;
// const u32 PAGES_SIZE = 26;

class OP {
 public:
  TreeOpType op_type_;
  KeyType key_;
  ValueType value_;

 public:
  OP(TreeOpType op_type, KeyType key, ValueType value)
      : op_type_(op_type), key_(key), value_(value) {}
  ~OP() {}
};

template <typename... Args>
void LaunchParallelTest(uint64_t num_threads, Args &&...args) {
  std::vector<std::thread> thread_group;

  // Launch a group of threads
  for (uint64_t thread_itr = 0; thread_itr < num_threads; ++thread_itr) {
    thread_group.push_back(std::thread(args..., thread_itr));
  }

  // Join the threads with the main thread
  for (uint64_t thread_itr = 0; thread_itr < num_threads; ++thread_itr) {
    thread_group[thread_itr].join();
  }
}

// helper function to insert
void InsertHelper(BTree *tree, const std::vector<KeyType> &keys,
                  __attribute__((unused)) uint64_t thread_itr = 0) {
  // create transaction
  // Transaction *transaction = new Transaction(0);
  for (auto key : keys) {
    int64_t value = key & 0xFFFFFFFF;
    tree->Insert(key, value);
  }
  // delete transaction;
}

// helper function to insert
void InsertHelperSplit(BTree *tree, const std::vector<KeyType> &keys,
                       int total_threads,
                       __attribute__((unused)) uint64_t thread_itr) {
  // create transaction
  // Transaction *transaction = new Transaction(0);
  for (auto key : keys) {
    if (static_cast<uint64_t>(key) % total_threads == thread_itr) {
      int64_t value = key & 0xFFFFFFFF;
      tree->Insert(key, value);
    }
  }
  // delete transaction;
}

// helper function to insert
void DeleteHelper(BTree *tree, const std::vector<KeyType> &keys,
                  __attribute__((unused)) uint64_t thread_itr = 0) {
  // create transaction
  // Transaction *transaction = new Transaction(0);
  for (auto key : keys) {
    tree->Remove(key);
  }
  // delete transaction;
}

// helper function to insert
void DeleteHelperSplit(BTree *tree, const std::vector<KeyType> &keys,
                       int total_threads,
                       __attribute__((unused)) uint64_t thread_itr) {
  // create transaction
  // Transaction *transaction = new Transaction(0);
  for (auto key : keys) {
    if (static_cast<uint64_t>(key) % total_threads == thread_itr) {
      tree->Remove(key);
    }
  }
  // delete transaction;
}

// helper function to mixed test
void MixedHelperSplit(BTree *tree, const std::vector<OP> &ops,
                      int total_threads,
                      __attribute__((unused)) uint64_t thread_itr) {
  // create transaction
  // Transaction *transaction = new Transaction(0);
  for (auto op : ops) {
    auto key = op.key_;
    auto value = op.value_;
    auto type = op.op_type_;
    if (static_cast<uint64_t>(key) % total_threads == thread_itr) {
      if (type == TREE_OP_INSERT) {
        tree->Insert(key, value);
      } else if (type == TREE_OP_REMOVE) {
        tree->Remove(key);
      } else if (type == TREE_OP_UPDATE) {
        tree->Update(key, value);
      } else if (type == TREE_OP_FIND) {
        ValueType ret_value = -1;
        tree->Get(key, &ret_value);
        // EXPECT_TRUE(tree->Get(key, &ret_value));
        // EXPECT_EQ(value, ret_value);
      }
    }
  }
}

TEST(ConcurrentTest, 1_ConcurrentInsertSeq) {
  DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  ParallelBufferPoolManager *para =
      new ParallelBufferPoolManager(INSTANCE_SIZE, PAGES_SIZE, disk);
  BTree *btree = new BTree(para);

  // keys to Insert
  int key_nums = 1024;
  std::vector<KeyType> keys(key_nums);
  std::iota(keys.begin(), keys.end(), 1);

  int thread_nums = 4;
  LaunchParallelTest(thread_nums, InsertHelper, btree, keys);

  // btree->Draw(DOTFILE_NAME_AFTER);

  for (auto key : keys) {
    ValueType value = -1;
    EXPECT_TRUE(btree->Get(key, &value));
    EXPECT_EQ(key, value);
  }

  delete btree;
}

TEST(ConcurrentTest, 2_ConcurrentInsertRandom) {
  DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  ParallelBufferPoolManager *para =
      new ParallelBufferPoolManager(INSTANCE_SIZE, PAGES_SIZE, disk);
  BTree *btree = new BTree(para);

  // keys to Insert
  int key_nums = 1024;
  std::vector<KeyType> keys(key_nums);
  std::iota(keys.begin(), keys.end(), 1);

  // random shuffle
  std::mt19937 g(1024);
  std::shuffle(keys.begin(), keys.end(), g);

  int thread_nums = 4;
  LaunchParallelTest(thread_nums, InsertHelper, btree, keys);

  // btree->Draw(DOTFILE_NAME_AFTER);

  for (auto key : keys) {
    ValueType value = -1;
    EXPECT_TRUE(btree->Get(key, &value));
    EXPECT_EQ(key, value);
  }

  delete btree;
}

TEST(ConcurrentTest, 3_ConcurrentInsertSplitSeq) {
  DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  ParallelBufferPoolManager *para =
      new ParallelBufferPoolManager(INSTANCE_SIZE, PAGES_SIZE, disk);
  BTree *btree = new BTree(para);

  // keys to Insert
  int key_nums = 1024;
  std::vector<KeyType> keys(key_nums);
  std::iota(keys.begin(), keys.end(), 1);

  // random shuffle
  // std::mt19937 g(1024);
  // std::shuffle(keys.begin(), keys.end(), g);

  int thread_nums = 16;
  LaunchParallelTest(thread_nums, InsertHelperSplit, btree, keys, thread_nums);

  // btree->Draw(DOTFILE_NAME_AFTER);

  for (auto key : keys) {
    ValueType value = -1;
    EXPECT_TRUE(btree->Get(key, &value));
    EXPECT_EQ(key, value);
  }

  delete btree;
}

TEST(ConcurrentTest, 4_ConcurrentDeleteSplitSeqReverse) {
  DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  ParallelBufferPoolManager *para =
      new ParallelBufferPoolManager(INSTANCE_SIZE, PAGES_SIZE, disk);
  BTree *btree = new BTree(para);

  // keys to Insert
  int key_nums = 1024;
  std::vector<KeyType> keys(key_nums);
  std::iota(keys.begin(), keys.end(), 1);

  // random shuffle
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(keys.begin(), keys.end(), g);

  int thread_nums = 16;
  LaunchParallelTest(thread_nums, InsertHelperSplit, btree, keys, thread_nums);

  for (auto key : keys) {
    ValueType value = -1;
    EXPECT_TRUE(btree->Get(key, &value));
    EXPECT_EQ(key, value);
  }

  std::reverse(keys.begin(), keys.end());
  std::vector<KeyType> first_half_keys(keys.begin(),
                                       keys.begin() + keys.size() / 2);
  std::vector<KeyType> second_half_keys(keys.begin() + keys.size() / 2,
                                        keys.begin() + keys.size());

  LaunchParallelTest(thread_nums, DeleteHelperSplit, btree, first_half_keys,
                     thread_nums);

  // deleted keys should not exist
  for (const auto &key : first_half_keys) {
    ValueType value = -1;
    EXPECT_FALSE(btree->Get(key, &value));
  }
  // remained keys should be found
  for (const auto &key : second_half_keys) {
    ValueType value = -1;
    EXPECT_TRUE(btree->Get(key, &value));
    EXPECT_EQ(key, value);
  }

  LaunchParallelTest(thread_nums, DeleteHelperSplit, btree, second_half_keys,
                     thread_nums);

  for (const auto &key : second_half_keys) {
    ValueType value = -1;
    EXPECT_FALSE(btree->Get(key, &value)) << "key: " << key;
    if (key == 989) {
      btree->Draw(DOTFILE_NAME_AFTER);
    }
  }

  // now btree is empty
  EXPECT_TRUE(btree->IsEmpty());

  // btree->Draw(DOTFILE_NAME_AFTER);

  delete btree;
}

TEST(ConcurrentTest, 5_MixedSmall) {
  DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  ParallelBufferPoolManager *para =
      new ParallelBufferPoolManager(INSTANCE_SIZE, PAGES_SIZE, disk);
  BTree *btree = new BTree(para);

  // sequential insert
  int64_t len = 1024;
  std::vector<KeyType> keys(len);
  int point = 100;
  for (int i = 1; i < point; i++) {
    keys[i] = (i) * (i - 1);
  }
  InsertHelper(btree, keys);

  // concurrent insert
  keys.clear();
  for (int i = 0; i <= len; i++) {
    keys.push_back(i);
  }
  int thread_nums = 16;
  LaunchParallelTest(thread_nums, InsertHelperSplit, btree, keys, thread_nums);

  // concurrent delete
  std::vector<KeyType> remove_keys = {1, 4, 3, 5, 6};
  LaunchParallelTest(1, DeleteHelper, btree, remove_keys);

  ValueType value = -1;
  KeyType key = 2;
  EXPECT_TRUE(btree->Get(2, &value));
  EXPECT_EQ(key, value);

  key = 7;
  EXPECT_TRUE(btree->Get(key, &value));
  EXPECT_EQ(key, value);

  key = 10;
  EXPECT_TRUE(btree->Get(key, &value));
  EXPECT_EQ(key, value);

  delete para;
  delete disk;
  delete btree;
}

TEST(ConcurrentTest, 6_MixedReal) {
  DiskManager *disk = new DiskManager(FILE_NAME.c_str());
  ParallelBufferPoolManager *para =
      new ParallelBufferPoolManager(INSTANCE_SIZE, PAGES_SIZE, disk);
  BTree *btree = new BTree(para);

  // keys to Insert
  int key_nums = 1024;
  std::vector<KeyType> keys(key_nums);
  std::iota(keys.begin(), keys.end(), 1);
  // random shuffle
  std::mt19937 g(1024);
  std::shuffle(keys.begin(), keys.end(), g);

  int thread_nums = 16;
  LaunchParallelTest(thread_nums, InsertHelperSplit, btree, keys, thread_nums);

  for (auto key : keys) {
    ValueType value = -1;
    EXPECT_TRUE(btree->Get(key, &value));
    EXPECT_EQ(key, value);
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis_end(key_nums - 1, key_nums * 2 - 1);
  std::vector<OP> insert_ops;
  for (int i = 0; i < key_nums; i++) {
    int pos = dis_end(gen);
    insert_ops.push_back(OP(TREE_OP_INSERT, pos, pos));
  }

  std::vector<OP> delete_ops;
  std::uniform_int_distribution<> dis_half(1, key_nums - 1);
  for (int i = 0; i < key_nums / 2; i++) {
    int pos = dis_half(gen);
    delete_ops.push_back(OP(TREE_OP_REMOVE, pos, -1));
  }

  std::vector<OP> update_ops;
  for (int i = key_nums / 2; i < key_nums; i++) {
    update_ops.push_back(OP(TREE_OP_UPDATE, i, i * i));
  }

  std::vector<OP> find_ops;
  for (int i = 1; i < key_nums; i++) {
    find_ops.push_back(OP(TREE_OP_FIND, i, i));
  }

  std::vector<OP> all_ops;
  all_ops.insert(all_ops.end(), insert_ops.begin(), insert_ops.end());
  all_ops.insert(all_ops.end(), delete_ops.begin(), delete_ops.end());
  all_ops.insert(all_ops.end(), update_ops.begin(), update_ops.end());
  all_ops.insert(all_ops.end(), find_ops.begin(), find_ops.end());

  LaunchParallelTest(thread_nums, MixedHelperSplit, btree, all_ops,
                     thread_nums);

  btree->Draw(DOTFILE_NAME_AFTER);

  delete para;
  delete disk;
  delete btree;
}

}  // namespace BTree
