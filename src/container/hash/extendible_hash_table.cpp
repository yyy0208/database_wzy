//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <functional>
#include <list>
#include <utility>

#include "container/hash/extendible_hash_table.h"
#include "storage/page/page.h"

namespace bustub {

template <typename K, typename V>
ExtendibleHashTable<K, V>::ExtendibleHashTable(size_t bucket_size) : bucket_size_(bucket_size) {
  // 初始状态：全局深度为 0，目录中只有一个深度为 0 的桶
  dir_.push_back(std::make_shared<Bucket>(bucket_size_, 0));
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::IndexOf(const K &key) -> size_t {
  // 框架自带的目录索引计算：取哈希值的低 global_depth_ 位；掩码用无符号类型，避免有符号移位溢出
  size_t mask = (1UL << global_depth_) - 1;
  return std::hash<K>()(key) & mask;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepth() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetGlobalDepthInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepthInternal() const -> int {
  return global_depth_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepth(int dir_index) const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetLocalDepthInternal(dir_index);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepthInternal(int dir_index) const -> int {
  return dir_[dir_index]->GetDepth();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBuckets() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetNumBucketsInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBucketsInternal() const -> int {
  return num_buckets_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Find(const K &key, V &value) -> bool {
  // 公共接口入口加锁，内部调用均在持锁状态下完成
  std::scoped_lock<std::mutex> lock(latch_);
  return dir_[IndexOf(key)]->Find(key, value);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Remove(const K &key) -> bool {
  // 仅删除桶内条目，不做任何桶合并/目录收缩
  std::scoped_lock<std::mutex> lock(latch_);
  return dir_[IndexOf(key)]->Remove(key);
}

template <typename K, typename V>
void ExtendibleHashTable<K, V>::Insert(const K &key, const V &value) {
  // 全局大锁：拆分与重分布全程持锁，保证多线程并发安全
  std::scoped_lock<std::mutex> lock(latch_);

  // 步骤 1：先尝试直接插入——key 已存在时 Bucket::Insert 会覆盖旧值，新 key 桶未满时直接写入，
  // 两种情况均返回 true；仅当桶满且为新 key 时返回 false，再进入下面的拆分流程
  auto current_bucket = dir_[IndexOf(key)];
  if (current_bucket->Insert(key, value)) {
    return;
  }

  // 步骤 2：插入前检查目标桶是否已满，满则循环拆分直到存在空位
  // （重分布后所有 KV 可能再次落入同一个桶，因此需要 while 循环）
  while (dir_[IndexOf(key)]->IsFull()) {
    size_t index = IndexOf(key);
    auto old_bucket = dir_[index];
    int old_local_depth = old_bucket->GetDepth();

    // 分支 A：局部深度 == 全局深度，说明只有一个目录项指向该桶，需要先扩容目录
    if (old_local_depth == global_depth_) {
      global_depth_++;
      size_t old_capacity = dir_.size();
      dir_.resize(old_capacity << 1);
      // 目录翻倍：后半段逐项复制前半段中原旧桶的指针，此时新桶尚未创建
      for (size_t i = 0; i < old_capacity; i++) {
        dir_[i + old_capacity] = dir_[i];
      }
    }

    // 步骤 3：旧桶局部深度自增，并新建一个兄弟桶（均在重分发 KV 之前完成）
    old_bucket->IncrementDepth();
    auto new_bucket = std::make_shared<Bucket>(bucket_size_, old_bucket->GetDepth());
    num_buckets_++;

    // 步骤 4：重写指向旧桶的兄弟指针——新提升出来的那一位为 1 的目录项改指新桶
    // 掩码位编号取自增前的局部深度，即本次拆分新引入的哈希位
    size_t split_bit = 1UL << old_local_depth;
    for (size_t i = 0; i < dir_.size(); i++) {
      if (dir_[i] == old_bucket && (i & split_bit) != 0) {
        dir_[i] = new_bucket;
      }
    }

    // 步骤 5：快照旧桶全部 KV 后清空旧桶，避免边遍历边删除导致的迭代器失效；
    // 再用更新后的 global_depth_ 调用 IndexOf 重新计算每个 key 的目录索引并分发
    auto snapshot = old_bucket->GetItems();
    old_bucket->GetItems().clear();
    for (const auto &item : snapshot) {
      size_t new_index = IndexOf(item.first);
      dir_[new_index]->Insert(item.first, item.second);
    }
  }

  // 步骤 6：目标桶已存在空位，插入新的键值对
  dir_[IndexOf(key)]->Insert(key, value);
}

//===--------------------------------------------------------------------===//
// Bucket
//===--------------------------------------------------------------------===//
template <typename K, typename V>
ExtendibleHashTable<K, V>::Bucket::Bucket(size_t array_size, int depth) : size_(array_size), depth_(depth) {}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Find(const K &key, V &value) -> bool {
  // 遍历桶内 list，命中则回填 value 并返回 true
  for (const auto &item : list_) {
    if (item.first == key) {
      value = item.second;
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Remove(const K &key) -> bool {
  // 定位到对应 key 后 erase 删除；未找到返回 false
  for (auto it = list_.begin(); it != list_.end(); ++it) {
    if (it->first == key) {
      list_.erase(it);
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Insert(const K &key, const V &value) -> bool {
  // 重复 key 先覆盖旧值
  for (auto &item : list_) {
    if (item.first == key) {
      item.second = value;
      return true;
    }
  }
  // 桶已满且为新 key，无法插入
  if (IsFull()) {
    return false;
  }
  list_.emplace_back(key, value);
  return true;
}

template class ExtendibleHashTable<page_id_t, Page *>;
template class ExtendibleHashTable<Page *, std::list<Page *>::iterator>;
template class ExtendibleHashTable<int, int>;
// test purpose
template class ExtendibleHashTable<int, std::string>;
template class ExtendibleHashTable<int, std::list<int>::iterator>;

}  // namespace bustub
