#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include "btree.h"
// using namespace BTree;

int main(int argc, char const *argv[]) {
  BTree::DiskManager *disk =
      new BTree::DiskManager("/data/public/hjl/bbtree/hjl.db");
  BTree::ParallelBufferPoolManager *para =
      new BTree::ParallelBufferPoolManager(16, 1024, disk);
  BTree::BTree *btree = new BTree::BTree(para);

  // btree->Insert(key, value);
  std::vector<BTree::KeyType> keys(39);
  std::iota(keys.begin(), keys.end(), 0);
  // std::random_device rd;
  // std::mt19937 g(rd());
  // std::shuffle(keys.begin(), keys.end(), g);

  for (auto key : keys) {
    btree->Insert(key, key * key);
  }

  btree->Remove(4);
  btree->Remove(3);
  btree->Remove(5);

  delete btree;
  return 0;
}
