

#include "replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages)
    : dummy_(static_cast<frame_id_t>(num_pages)) {
  lru_list_ = new Node[num_pages + 1];
  for (frame_id_t i = 0; i <= dummy_; ++i) {
    lru_list_[i].next_id_ = i;
    lru_list_[i].prev_id_ = i;
  }
  size_ = 0;
}

LRUReplacer::~LRUReplacer() { delete[] lru_list_; }

bool LRUReplacer::Victim(frame_id_t *frame_id) {
  if (size_ == 0) {
    return false;
  }
  *frame_id = lru_list_[dummy_].prev_id_;
  Invalidate(*frame_id);
  size_--;
  return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
  if (!IsValid(frame_id)) {
    return;
  }
  Invalidate(frame_id);
  size_--;
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
  if (IsValid(frame_id)) {
    return;
  }
  // insert node
  Add(frame_id);
  size_++;
}

size_t LRUReplacer::Size() { return size_; }
/**
 * @brief
 *
 * @param frame_id
 * @return true: frame_id is in already unpined and in replacer
 * @return false: frame_id is pinned and not in replacer
 */
bool LRUReplacer::IsValid(frame_id_t frame_id) const {
  return lru_list_[frame_id].next_id_ != frame_id;
}

void LRUReplacer::Add(frame_id_t frame_id) {
  lru_list_[frame_id].next_id_ = lru_list_[dummy_].next_id_;
  lru_list_[frame_id].prev_id_ = dummy_;
  lru_list_[lru_list_[dummy_].next_id_].prev_id_ = frame_id;
  lru_list_[dummy_].next_id_ = frame_id;
}

void LRUReplacer::Invalidate(frame_id_t frame_id) {
  lru_list_[lru_list_[frame_id].prev_id_].next_id_ =
      lru_list_[frame_id].next_id_;
  lru_list_[lru_list_[frame_id].next_id_].prev_id_ =
      lru_list_[frame_id].prev_id_;
  lru_list_[frame_id].prev_id_ = frame_id;
  lru_list_[frame_id].next_id_ = frame_id;
}
