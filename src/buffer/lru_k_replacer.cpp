//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015‑2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "buffer/lru_k_replacer.h"
#include "common/exception.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  if (curr_evictable_size_ == 0) {
    return false;
  }
  frame_id_t victim = -1;
  // 保存候选帧的后退k距离、最早时间戳
  size_t max_backward_dist = 0;
  size_t earliest_ts = std::numeric_limits<size_t>::max();
  bool has_inf = false;
  // 遍历所有frame_history_里面的帧
  for (const auto &entry : frame_history_) {
    auto fid = entry.first;
    const auto &history = entry.second;
    // 只考虑可驱逐的帧
    if (!frame_evictable_[fid]) {
      continue;
    }
    size_t backward_dist;
    bool is_inf = false;
    if (history.size() >= k_) {
      // >=k次访问：后退k距离 = 当前时间戳 - 第k次访问(history最头部)
      size_t kth_ts = history.at(0);
      backward_dist = current_timestamp_ - kth_ts;
    } else {
      // <k次访问，后退距离无穷大
      is_inf = true;
      backward_dist = std::numeric_limits<size_t>::max();
    }
    if (is_inf) {
      // 存在无穷大候选，优先选inf集合，比较最早访问时间戳
      if (!has_inf) {
        has_inf = true;
        victim = fid;
        earliest_ts = history[0];
      } else {
        if (history[0] < earliest_ts) {
          victim = fid;
          earliest_ts = history[0];
        }
      }
    } else {
      // 当前没有inf候选，比较后退k距离，取最大
      if (!has_inf) {
        if (victim == -1 || backward_dist > max_backward_dist) {
          victim = fid;
          max_backward_dist = backward_dist;
        }
      }
    }
  }
  if (victim == -1) {
    return false;
  }
  // 成功淘汰victim，清除该帧全部访问历史，修改元数据
  *frame_id = victim;
  frame_history_.erase(victim);
  frame_evictable_.erase(victim);
  curr_evictable_size_--;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  // frame_id合法性检查
  if (static_cast<size_t>(frame_id) >= replacer_size_) {
    throw bustub::Exception("RecordAccess invalid frame_id");
  }
  // 获取该帧的历史记录
  auto &history = frame_history_[frame_id];
  // 压入新的时间戳
  history.push_back(current_timestamp_);
  current_timestamp_++;
  // 如果历史记录超过k个，删除最旧的头部时间戳，只保留最近k次访问
  if (history.size() > k_) {
    history.erase(history.begin());
  }
  // 如果是第一次访问，初始化evictable状态为false，上层调用SetEvictable修改
  if (frame_evictable_.count(frame_id) == 0U) {
    frame_evictable_[frame_id] = false;
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);
  if (static_cast<size_t>(frame_id) >= replacer_size_) {
    throw bustub::Exception("SetEvictable invalid frame_id");
  }
  // 该frame没有访问记录，直接返回
  if (frame_history_.count(frame_id) == 0U) {
    return;
  }
  bool old_val = frame_evictable_[frame_id];
  if (old_val == set_evictable) {
    // 状态没有发生变化，不修改计数
    return;
  }
  frame_evictable_[frame_id] = set_evictable;
  if (set_evictable) {
    curr_evictable_size_++;
  } else {
    curr_evictable_size_--;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  if (static_cast<size_t>(frame_id) >= replacer_size_) {
    throw bustub::Exception("Remove invalid frame_id");
  }
  // 没有该帧记录，直接return
  if (frame_history_.count(frame_id) == 0U) {
    return;
  }
  // Remove只能移除evictable=true的帧，否则抛异常，官方要求
  if (!frame_evictable_[frame_id]) {
    throw bustub::Exception("Remove called on non‑evictable frame");
  }
  frame_history_.erase(frame_id);
  frame_evictable_.erase(frame_id);
  curr_evictable_size_--;
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  return curr_evictable_size_;
}

}  // namespace bustub

