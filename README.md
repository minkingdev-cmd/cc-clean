# cc-clean

[![CI](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml/badge.svg)](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml)

`cc-clean` 是一个用于**彻底卸载 Claude Code** 的跨平台清理工具。

它负责扫描、备份、清理并恢复 Claude Code 在本机上的相关数据与系统集成项，帮助用户完成一次完整、可控、可回滚的卸载流程。

## 主要能力

- 扫描 Claude Code 相关本地数据
- 清理常见残留内容，包括：
  - 配置目录
  - 缓存目录
  - 日志目录
  - 会话历史
  - 插件目录
  - 平台相关凭据与系统项
- 在清理前自动备份目标内容
- 使用 `restore` 从备份恢复
- 提供 JSON 输出，便于自动化脚本集成
- 提供跨平台回归测试与 CI

## 支持平台

- macOS
- Linux
- Windows
- x86_64 / aarch64

## 使用场景

- 卸载 Claude Code 前检查本机残留
- 执行 Claude Code 全量清理
- 在清理后恢复误删内容
- 在自动化环境中批量执行卸载与清理任务

## 快速开始

### 构建

```bash
cmake -S . -B build
cmake --build build
```

可执行文件通常位于：

```bash
./build/cc-clean
```

### 检查残留

```bash
./build/cc-clean check
```

### 清理并备份

```bash
./build/cc-clean clean --backup-dir ./backup -y
```

### 从备份恢复

```bash
./build/cc-clean restore --backup-dir ./backup -y
```

### 输出 JSON

```bash
./build/cc-clean check --json
```

## 测试

```bash
cd build
cmake ..
ctest --output-on-failure
```

按名称运行：

```bash
ctest -R cfg_restore_json --output-on-failure
```

按 label 运行：

```bash
ctest -L json -V
ctest -L restore -V
ctest -L safety -V
ctest -L dangerous -V
```

## 仓库结构

- `csrc/cc_clean_main.c / cc_clean_cli.c / cc_clean_core.c / cc_clean_report.c`：C 主程序
- `CMakeLists.txt`：跨平台构建配置
- `BUILDING.md`：构建、测试与 CTest 说明
- `LICENSE`：许可证
- `scripts/test_cc_clean_posix.sh`：macOS / Linux 回归测试
- `scripts/test_cc_clean_windows.ps1`：Windows 回归测试
- `scripts/cc_clean.py`：早期 Python 参考实现
- `docs/BUILDING.original.md`：历史构建说明
- `docs/releases/`：发布说明

## 许可证

本项目基于 MIT License 发布。

## 更多说明

详见：

- `BUILDING.md`
