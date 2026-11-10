# Project 1 测试结果截图

本目录保存 BusTub Project 1 三个模块的测试通过截图，均在本仓库当前代码（commit `582533d`，已完成 clang-format）上实际运行得到。

## 截图清单

| 截图 | 模块 | 测试命令 | 结果 |
| --- | --- | --- | --- |
| `01-extendible-hash-table-test.png` | Extendible Hash Table | `./test/extendible_hash_table_test` | 12 tests passed |
| `02-lru-k-replacer-test.png` | LRU-K Replacer | `./test/lru_k_replacer_test` | 4 tests passed |
| `03-buffer-pool-manager-test.png` | Buffer Pool Manager | `./test/buffer_pool_manager_instance_test` | 13 tests passed |

## 复现方式

```bash
cd build
./test/extendible_hash_table_test
./test/lru_k_replacer_test
./test/buffer_pool_manager_instance_test
```

三个可执行文件均可由 `make extendible_hash_table_test`、`make lru_k_replacer_test`、`make buffer_pool_manager_instance_test` 单独编译生成。

## 运行环境

- 系统：Ubuntu 22.04（WSL2）
- 编译器：GNU g++ 11.4.0
- 构建工具：CMake + Make
- 测试框架：GoogleTest / GoogleMock

## 说明

- `01` 对应可扩展哈希表（`src/include/container/hash/extendible_hash_table.h`、`src/container/hash/extendible_hash_table.cpp`）。
- `02` 对应 LRU-K 替换器（`src/include/buffer/lru_k_replacer.h`、`src/buffer/lru_k_replacer.cpp`）。
- `03` 对应缓冲池管理器（`src/include/buffer/buffer_pool_manager_instance.h`、`src/buffer/buffer_pool_manager_instance.cpp`）。
- 三张截图均在执行 `make format` 之后重新运行取得，用于确认格式化未影响功能。
