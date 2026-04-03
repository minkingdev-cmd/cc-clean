# cc-clean

[![CI](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml/badge.svg)](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/minkingdev-cmd/cc-clean)](https://github.com/minkingdev-cmd/cc-clean/releases)

中文 | [English](./README-EN.md)

`cc-clean` 是一个用于**彻底卸载 Claude Code** 的跨平台清理工具。

它负责扫描、备份、清理并恢复 Claude Code 在本机上的相关数据与系统集成项，帮助用户完成一次完整、可控、可回滚的卸载流程。

## 主要能力

- 扫描 Claude Code 相关本地数据
- 清理常见残留内容，包括：
  - 配置目录
  - 缓存目录
  - 日志目录
  - 会话历史
  - 本地安装目录与 native installer
  - shell 配置注入与 completions
  - Linux mimeapps / deep-link 关联
  - 浏览器集成（Claude in Chrome / deep-link）
  - VS Code / Cursor / Windsurf 扩展目录
  - JetBrains 插件目录
  - 全局 npm 安装残留
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

## 推荐流程

1. 先执行 `check` 查看本机残留
2. 执行 `clean --backup-dir <目录>` 进行备份和清理
3. 如需回滚，执行 `restore --backup-dir <目录>`

## 获取程序

### 从源码构建

```bash
cmake -S . -B build
cmake --build build
```

可执行文件通常位于：

```bash
./build/cc-clean
```

### 从 Release 下载

- Release 页面：<https://github.com/minkingdev-cmd/cc-clean/releases>
- 当前版本：<https://github.com/minkingdev-cmd/cc-clean/releases/tag/v0.1.0>
- 校验文件：`SHA256SUMS.txt`

未来的 tag 发布会由 GitHub Actions 自动构建并上传对应平台资产。

当前源码已覆盖更多卸载残留，包括本地安装目录、native installer、shell 配置注入、Linux mimeapps 关联、Claude in Chrome Native Host、IDE 插件目录、全局 npm 安装残留与 Linux deep-link desktop entry。

当前 `v0.1.0` 已提供：

- `cc-clean-v0.1.0-linux-x86_64.tar.gz`
- `cc-clean-v0.1.0-linux-x86_64.zip`
- `cc-clean-v0.1.0-macos-aarch64.tar.gz`
- `cc-clean-v0.1.0-macos-aarch64.zip`
- `cc-clean-v0.1.0-windows-x86_64.zip`
- `SHA256SUMS.txt`

## 使用示例

### 检查残留

```bash
./build/cc-clean check
```

### 清理并备份

```bash
./build/cc-clean clean --backup-dir ./backup -y
```

### 全量清理所有已知 Claude Code 痕迹

```bash
./build/cc-clean clean --purge-all
```

说明：

- `--purge-all` 会清理所有已知的 Claude Code 痕迹，包括配置、缓存、日志、历史、安装残留、shell / 浏览器 / IDE 集成及系统级项目
- `--purge-all` 不需要额外传 `--allow-unsafe-purge`
- 即使传入 `-y`，也仍然必须先查看删除计划并手动输入 `PURGE-ALL` 才会真正执行
- 可先使用 `--dry-run` 仅查看计划

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
- `CHANGELOG.md`：版本变更记录
- `LICENSE`：许可证
- `scripts/test_cc_clean_posix.sh`：macOS / Linux 回归测试
- `scripts/test_cc_clean_windows.ps1`：Windows 回归测试
- `scripts/cc_clean.py`：早期 Python 参考实现
- `docs/BUILDING.original.md`：历史构建说明
- `docs/releases/`：发布说明
- `docs/releases/TEMPLATE.md`：新版本发布说明模板

## 文档

- [构建与测试说明](./BUILDING.md)
- [变更记录](./CHANGELOG.md)
- [贡献指南](./CONTRIBUTING.md)
- [安全策略](./SECURITY.md)
- [发布说明](./docs/releases/v0.1.0.md)

## 许可证

本项目基于 MIT License 发布。
