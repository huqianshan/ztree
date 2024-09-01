#include "lru_buffer.h"

lru_buffer::lru_buffer(size_t num_pages)
    : _size(0), _num_pages(num_pages), _hit(0), _miss(0) {
  _head.next = &_head;
  _head.prev = &_head;
  _head.page = nullptr;
}

lru_buffer::~lru_buffer() {
  std::unique_lock<decltype(_lock)> l(_lock);
  while (size() > 0) {
    // todo delete page
    Page* page = delete_last();
    DESTROY_PAGE(page);
  }
  printf("[lru_buffer]hit: %d miss: %d hit ratio: %f\n", _hit.load(),
         _miss.load(),
         _hit.load() == 0
             ? 0.0
             : (_hit.load() / (double)(_hit.load() + _miss.load()) * 100));
}

size_t lru_buffer::size() { return _size; }

bool lru_buffer::insert(page_id_t page_id, Page* page) {
  if (_page_table.find(page_id) != _page_table.end()) {
    return false;
  }
  node* n = new node{.page = page};
  add_front(n);
  _page_table[page_id] = n;
  return true;
}

bool lru_buffer::find(page_id_t page_id) {
  std::unique_lock<decltype(_lock)> l(_lock);
  return _page_table.find(page_id) != _page_table.end();
}

Page* lru_buffer::fetch(page_id_t page_id) {
  std::unique_lock<decltype(_lock)> l(_lock);
  if (_page_table.find(page_id) == _page_table.end()) {
    return nullptr;
  }
  node* n = _page_table[page_id];
  move_front(n);
  return n->page;
}

Page* lru_buffer::touch(page_id_t page_id) {
  std::unique_lock<decltype(_lock)> l(_lock);
  if (_page_table.find(page_id) == _page_table.end()) {
    return nullptr;
  }
  node* n = _page_table[page_id];
  return n->page;
}

Page* lru_buffer::evict(page_id_t page_id) {
  std::unique_lock<decltype(_lock)> l(_lock);
  if (_page_table.find(page_id) == _page_table.end()) {
    return nullptr;
  }
  node* n = _page_table[page_id];
  remove_node(n);
  _page_table.erase(page_id);
  Page* page = n->page;
  delete n;
  return page;
}

Page* lru_buffer::evict_and_insert(page_id_t page_id, Page* page) {
  std::unique_lock<decltype(_lock)> l(_lock);
  if (_page_table.find(page_id) != _page_table.end()) {
    node* n = _page_table[page_id];
    n->page = page;
    return nullptr;
  }
  Page* p = nullptr;
  if (_size >= _num_pages) {
    p = delete_last();
  }
  if (_size >= _num_pages) {
    printf("size: %d\n", _size.load());
    assert(false);
  }
  insert(page_id, page);
  return p;
}

Page* lru_buffer::evict() {
  std::unique_lock<decltype(_lock)> l(_lock);
  if (size() == 0) {
    return nullptr;
  }
  return delete_last();
}

void lru_buffer::add_front(node* n) {
  n->next = _head.next;
  n->prev = &_head;
  _head.next->prev = n;
  _head.next = n;
  _size++;
}

void lru_buffer::remove_node(node* n) {
  n->prev->next = n->next;
  n->next->prev = n->prev;
  _size--;
}

void lru_buffer::move_front(node* n) {
  remove_node(n);
  add_front(n);
}

Page* lru_buffer::delete_last() {
  assert(_size > 0);
  node* last = _head.prev;
  remove_node(last);
  _page_table.erase(last->page->GetPageId());
  Page* page = last->page;
  delete last;
  return page;
}

void sieve_buffer::print_all() {
  std::unique_lock<decltype(_lock)> l(_lock);
  for (sieve_node* n = _head.next; n != &_head; n = n->next) {
    printf("%ld ", n->p->GetPageId());
  }
  printf("\n");
}