#pragma once
#include <algorithm>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "../zns/zone_device.h"
#include "config.h"
#include "page.h"
#include "replacer.h"
#include "storage.h"

// lru read buffer only
class lru_buffer {
 public:
  explicit lru_buffer(size_t num_pages);
  ~lru_buffer();

  size_t size();
  std::atomic_int _hit;
  std::atomic_int _miss;
  bool find(page_id_t page_id);
  Page* fetch(page_id_t page_id);
  Page* touch(page_id_t page_id);
  Page* evict(page_id_t page_id);
  Page* evict();
  Page* evict_and_insert(page_id_t page_id, Page* page);
  void Print();

 private:
  bool insert(page_id_t page_id, Page* page);
  struct node {
    node* prev;
    node* next;
    Page* page;
  };
  std::unordered_map<page_id_t, node*> _page_table;
  node _head;
  std::atomic_int _size;
  size_t _num_pages;
  // std::mutex _lock;
  std::shared_mutex _lock;

  void add_front(node* n);
  void remove_node(node* n);
  void move_front(node* n);
  Page* delete_last();
};

#ifndef SIEVE
#define SIEVE
struct sieve_node {
  sieve_node* prev;
  sieve_node* next;
  Page* p;
  int visited;
  Page* unlink() {
    next->prev = prev;
    prev->next = next;
    Page* t = p;
    p = nullptr;
    return t;
  }
  void add_after(sieve_node* n) {
    n->next->prev = this;
    this->next = n->next;
    n->next = this;
    this->prev = n;
  }
  bool empty() { return next == this; }
};
struct hash_map {
  std::vector<std::list<std::pair<page_id_t, sieve_node*>>> _table;
  hash_map() : _table(HASH_MAP_SIZE) {}

  void insert(page_id_t page_id, sieve_node* page) {
    size_t idx = std::hash<int64_t>()(page_id) % HASH_MAP_SIZE;
    auto it =
        std::find_if(_table[idx].begin(), _table[idx].end(),
                     [page_id](const std::pair<page_id_t, sieve_node*>& p) {
                       return p.first == page_id;
                     });
    if (it != _table[idx].end()) {
      it->second = page;
    } else {
      _table[idx].push_front(std::make_pair(page_id, page));
    }
  }
  sieve_node* erase(page_id_t page_id) {
    size_t idx = std::hash<int64_t>()(page_id) % HASH_MAP_SIZE;
    auto it =
        std::find_if(_table[idx].begin(), _table[idx].end(),
                     [page_id](const std::pair<page_id_t, sieve_node*>& p) {
                       return p.first == page_id;
                     });
    if (it != _table[idx].end()) {
      sieve_node* page = it->second;
      _table[idx].erase(it);
      return page;
    }
    return nullptr;
  }
  bool find(page_id_t page_id) {
    size_t idx = std::hash<int64_t>()(page_id) % HASH_MAP_SIZE;
    auto it =
        std::find_if(_table[idx].begin(), _table[idx].end(),
                     [page_id](const std::pair<page_id_t, sieve_node*>& p) {
                       return p.first == page_id;
                     });
    return it != _table[idx].end();
  }
  sieve_node* fetch(page_id_t page_id) {
    size_t idx = std::hash<int64_t>()(page_id) % HASH_MAP_SIZE;
    auto it =
        std::find_if(_table[idx].begin(), _table[idx].end(),
                     [page_id](const std::pair<page_id_t, sieve_node*>& p) {
                       return p.first == page_id;
                     });
    if (it != _table[idx].end()) {
      return it->second;
    }
    return nullptr;
  }
};

class sieve_buffer {
 public:
  hash_map _table;
  sieve_node _head;
  sieve_node _free;
  sieve_node* _pstart;
  sieve_node* _hand = nullptr;
  const size_t _num_pages;
  size_t _sz;
  std::atomic_int64_t _hit;
  std::atomic_int64_t _miss;
  explicit sieve_buffer(size_t num_pages) : _num_pages(num_pages) {
    _head.next = &_head;
    _head.prev = &_head;
    _free.next = &_free;
    _free.prev = &_free;
    _pstart = new sieve_node[num_pages];
    _sz = 0;
    // printf("[sieve_buffer] %ld\n", _num_pages);
    for (int i = 0; i < num_pages; ++i) {
      _pstart[i].add_after(&_free);
    }
  }
  ~sieve_buffer() {
    std::unique_lock<decltype(_lock)> l(_lock);
    // Print();
    for (sieve_node* n = _head.next; n != &_head; n = n->next) {
      DESTROY_PAGE(n->p);
    }
    delete[] _pstart;
  }
  std::shared_mutex _lock;

  size_t size() {
    std::shared_lock<decltype(_lock)> l(_lock);
    return _sz;
  }
  bool find(page_id_t page_id) {
    std::shared_lock<decltype(_lock)> l(_lock);
    return _table.find(page_id);
  }
  Page* fetch(page_id_t page_id) {
    std::shared_lock<decltype(_lock)> l(_lock);
    sieve_node* p = _table.fetch(page_id);
    if (p != nullptr) {
      p->visited = 1;
      _hit++;
      return p->p;
    }
    _miss++;
    return nullptr;
  }
  Page* touch(page_id_t page_id) {
    std::shared_lock<decltype(_lock)> l(_lock);
    sieve_node* p = _table.fetch(page_id);
    if (p != nullptr) {
      return p->p;
    }
    return nullptr;
  }
  Page* evict(page_id_t page_id) {
    std::unique_lock<decltype(_lock)> l(_lock);
    sieve_node* p = _table.erase(page_id);
    if (p != nullptr) {
      if (_hand == p) {
        _hand = nullptr;
      }
      p->visited = 0;
      Page* page = p->unlink();
      p->add_after(&_free);
      --_sz;
      return page;
    }
    return nullptr;
  }
  Page* evict() {
    assert(false);
    return nullptr;
  }
  Page* evict_and_insert(page_id_t page_id, Page* page) {
    std::unique_lock<decltype(_lock)> l(_lock);
    assert(page_id == page->GetPageId());
    if (_table.find(page_id)) {
      sieve_node* n = _table.fetch(page_id);
      n->p = page;
      return nullptr;
    }
    Page* victim = nullptr;
    if (_sz >= _num_pages) {
      sieve_node* o = _hand;
      if (o == nullptr || o == &_head) {
        o = _head.prev;
      }
      while (o->visited == 1) {
        o->visited = 0;
        o = o->prev;
        if (o == &_head) {
          o = _head.prev;
        }
      }
      _hand = o->prev;
      victim = o->unlink();
      _table.erase(victim->GetPageId());
      o->add_after(&_free);
      _sz--;
    }
    _sz++;
    sieve_node* p = _free.next;
    p->unlink();
    p->add_after(&_head);
    _table.insert(page_id, p);
    p->p = page;
    p->visited = 0;
    return victim;
  }
  void Print() {
    double hit_ratio = (_hit == 0 ? 0.0 : (double)_hit / (_hit + _miss)) * 100;
    printf("[sieve_buffer]hit: %ld miss: %ld hit_ratio: %.5f%%\n", _hit.load(),
           _miss.load(), hit_ratio);
  }
  void print_all();
};
#endif