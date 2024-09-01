#pragma once

#include "config.h"

typedef u32 frame_id_t;  // frame id type

/**
 * LRUReplacer implements the Least Recently Used replacement policy.
 */
class LRUReplacer {
 public:
  /**
   * Create a new LRUReplacer.
   * @param num_pages the maximum number of pages the LRUReplacer will be
   * required to store
   */
  explicit LRUReplacer(size_t num_pages);

  /**
   * Destroys the LRUReplacer.
   */
  ~LRUReplacer();

  bool Victim(frame_id_t *frame_id);

  void Pin(frame_id_t frame_id);

  void Unpin(frame_id_t frame_id);

  size_t Size();

 private:
  // LRU list node
  struct Node {
    frame_id_t prev_id_;
    frame_id_t next_id_;
  };
  const frame_id_t dummy_;
  size_t size_;
  // LRU list has a dummy head node
  Node *lru_list_;
  // page frame exists in the replacer
  bool IsValid(frame_id_t frame_id) const;
  // remove node
  void Invalidate(frame_id_t frame_id);
  // insert node at MRU end
  void Add(frame_id_t frame_id);
};
