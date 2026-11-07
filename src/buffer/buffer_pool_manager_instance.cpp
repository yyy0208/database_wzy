//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

// 分配一个新的 page_id，每次调用自增
auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

// 创建一个新页面：取一个可用帧，分配新的 page_id，并把它挂到 page table 上
auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::lock_guard<std::mutex> guard(latch_);

  frame_id_t frame_id = -1;

  // 优先使用空闲帧。空闲帧里没有页面数据，不需要考虑写回的问题
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    // 没有空闲帧时，从替换器中淘汰一个最久未使用的帧
    if (!replacer_->Evict(&frame_id)) {
      // 所有帧都被 pin 住，无法腾出空间
      return nullptr;
    }
  }

  Page *page = &pages_[frame_id];

  // 帧里如果还残留着旧页面，需要先处理掉。旧页是脏页时必须写回磁盘，
  // 否则这部分修改就丢失了
  if (page->GetPageId() != INVALID_PAGE_ID) {
    if (page->IsDirty()) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
    }
    page_table_->Remove(page->GetPageId());
  }

  page_id_t new_page_id = AllocatePage();

  // 复用这个帧存放新页面，重新设置页面的元数据
  page->ResetMemory();
  page->page_id_ = new_page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;

  // 建立 page_id 到 frame_id 的映射
  page_table_->Insert(new_page_id, frame_id);

  // 新页面交给调用者使用，pin 住防止被淘汰，同时记一次访问历史
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  *page_id = new_page_id;
  return page;
}

// 获取页面：命中缓冲池就直接返回，否则先把页面从磁盘读进来
auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  std::lock_guard<std::mutex> guard(latch_);

  frame_id_t frame_id = -1;

  // 先查 page table，看这个页面是否已经在缓冲池中
  if (page_table_->Find(page_id, frame_id)) {
    Page *page = &pages_[frame_id];
    page->pin_count_++;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return page;
  }

  // 不在缓冲池中，需要读磁盘，先找到一个可用的帧
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  }

  Page *page = &pages_[frame_id];

  // 帧里原有的页面如果是脏的，先写回磁盘再覆盖
  if (page->GetPageId() != INVALID_PAGE_ID) {
    if (page->IsDirty()) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
    }
    page_table_->Remove(page->GetPageId());
  }

  // 从磁盘读取目标页面到帧的内存中
  page->ResetMemory();
  disk_manager_->ReadPage(page_id, page->GetData());

  page->page_id_ = page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;

  page_table_->Insert(page_id, frame_id);
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return page;
}

// 取消固定页面：pin 计数减一，减到 0 表示这一帧可以被淘汰
auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  frame_id_t frame_id;

  // 页面不在缓冲池中，或者已经被删除
  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  Page *page = &pages_[frame_id];

  // pin 计数已经为 0，说明这个页面没有被固定，不能再 unpin
  if (page->GetPinCount() <= 0) {
    return false;
  }

  page->pin_count_--;

  // 调用者在写页面时传 is_dirty = true，这里把脏标记置上，
  // 后续淘汰或 flush 时才知道需要写回
  if (is_dirty) {
    page->is_dirty_ = true;
  }

  // 没有线程再持有这个页面，交给替换器处理
  if (page->GetPinCount() == 0) {
    replacer_->SetEvictable(frame_id, true);
  }

  return true;
}

// 把指定页面写回磁盘。不论页面是否为脏，都执行写入
auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  frame_id_t frame_id;

  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  Page *page = &pages_[frame_id];
  disk_manager_->WritePage(page_id, page->GetData());

  // 写回之后内存和磁盘内容一致，脏标记可以清掉
  page->is_dirty_ = false;

  return true;
}

// 把缓冲池中所有有效页面写回磁盘
void BufferPoolManagerInstance::FlushAllPgsImp() {
  std::lock_guard<std::mutex> guard(latch_);

  for (size_t i = 0; i < pool_size_; i++) {
    Page *page = &pages_[i];

    // page_id 为 INVALID_PAGE_ID 说明这一帧是空的，跳过
    if (page->GetPageId() != INVALID_PAGE_ID) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
      page->is_dirty_ = false;
    }
  }
}

// 删除页面：从 page table 摘掉，帧归还到空闲列表
auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  frame_id_t frame_id;

  // 页面本来就不在缓冲池中，视为删除成功
  if (!page_table_->Find(page_id, frame_id)) {
    return true;
  }

  Page *page = &pages_[frame_id];

  // 页面还被 pin 着，其他线程可能正在使用，不能删
  if (page->GetPinCount() > 0) {
    return false;
  }

  page_table_->Remove(page_id);
  replacer_->Remove(frame_id);
  free_list_.push_back(frame_id);

  // 帧变为空帧，元数据全部复位
  page->ResetMemory();
  page->page_id_ = INVALID_PAGE_ID;
  page->pin_count_ = 0;
  page->is_dirty_ = false;

  // 模拟释放磁盘上的页面
  DeallocatePage(page_id);

  return true;
}

}  // namespace bustub
