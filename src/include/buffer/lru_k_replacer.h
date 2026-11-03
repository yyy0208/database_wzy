//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.h
//
// Identification: src/include/buffer/lru_k_replacer.h
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once
#include <limits>
#include <mutex>  // NOLINT
#include <unordered_map>
#include <vector>
#include "common/config.h"
#include "common/macros.h"

namespace bustub {

/**
 * LRUKReplacer implements the LRU‑k replacement policy.
 *
 * The LRU‑k algorithm evicts a frame whose backward k‑distance is maximum
 * of all frames. Backward k‑distance is computed as the difference in time between
 * current timestamp and the timestamp of kth previous access.
 *
 * A frame with less than k historical references is given
 * +inf as its backward k‑distance. When multiple frames have +inf backward k‑distance,
 * classical LRU algorithm is used to choose victim.
 */
class LRUKReplacer {
 public:
  explicit LRUKReplacer(size_t num_frames, size_t k);
  DISALLOW_COPY_AND_MOVE(LRUKReplacer);
  ~LRUKReplacer() = default;

  auto Evict(frame_id_t *frame_id) -> bool;
  void RecordAccess(frame_id_t frame_id);
  void SetEvictable(frame_id_t frame_id, bool set_evictable);
  void Remove(frame_id_t frame_id);
  auto Size() -> size_t;

 private:
  // 全局递增时间戳，每次RecordAccess自增，**不能使用系统时钟**
  size_t current_timestamp_{0};
  // replacer能够管理的最大frame数目，构造传入
  size_t replacer_size_;
  // LRU‑K的参数K
  size_t k_;
  // 当前可驱逐帧数量，Size()返回该值
  size_t curr_evictable_size_{0};
  std::mutex latch_;
  // 每一个frame保存访问时间戳，最多保存k个，旧的在vector头部，新的在尾部
  std::unordered_map<frame_id_t, std::vector<size_t>> frame_history_;
  // 标记该frame是否允许被驱逐
  std::unordered_map<frame_id_t, bool> frame_evictable_;
};

}  // namespace bustub

