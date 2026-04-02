# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

- 修复 restore 安全校验：阻止 `..` 绕过目标路径白名单、阻止备份源路径逃逸 `backup_dir`、拒绝超长 `manifest.tsv` 行，并移除 `popen` / `_popen` 调用

- 补充 `CHANGELOG.md`
- 统一 README、英文 README、构建文档与发布说明中的下载与使用入口
- 增加 Release / Changelog 交叉链接，方便源码构建与发布包下载
- 增加发布自动化 workflow、贡献指南与安全策略
- 增加发布说明模板，便于后续版本延续发布流程
- 验证并启用自动发布流程，统一 v0.1.0 现有发布资产命名
- 扩展卸载覆盖范围，新增本地安装目录、native installer、Claude in Chrome Native Host、Linux deep-link desktop entry，以及更多运行时缓存/会话目录的清理与恢复
- 继续扩展卸载覆盖范围，新增 shell 配置注入、Linux mimeapps 关联、VS Code/Cursor/Windsurf 扩展目录、JetBrains 插件目录与全局 npm 安装残留的清理与恢复

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
- 扩展卸载覆盖范围，新增本地安装目录、native installer、Claude in Chrome Native Host、Linux deep-link desktop entry，以及更多运行时缓存/会话目录的清理与恢复
