# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

- 补充 `CHANGELOG.md`
- 统一 README、英文 README、构建文档与发布说明中的下载与使用入口
- 增加 Release / Changelog 交叉链接，方便源码构建与发布包下载

## [0.1.0] - 2026-04-02

### Added
- 发布 `cc-clean` 首个公开版本
- 提供用于彻底卸载 Claude Code 的跨平台清理工具
- 支持 `check`、`clean`、`restore` 主流程
- 支持备份目录、`index.md` 索引与 `manifest` 恢复
- 支持 JSON 输出，便于自动化脚本集成
- 提供 macOS / Linux / Windows 跨平台回归测试与 GitHub Actions CI

### Changed
- 稳定 Windows CI 测试脚本
- 升级 GitHub Actions `actions/checkout` 至 `v5`
- 统一文档、Release 与下载说明
