# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [0.1.3] - 2026-04-03

### Fixed
- 修复 Windows 下 `purge-all` 严格确认回归用例，改为基于实际删除结果断言，避免 PowerShell 对原生进程退出码采集产生误判

## [0.1.2] - 2026-04-03

### Fixed
- 修复 Windows 下 `purge-all` 严格确认回归用例，避免测试期间被备份目录提示干扰

## [0.1.1] - 2026-04-03

### Added
- 新增 `clean --purge-all`，用于清理当前机器上所有已知的 Claude Code 痕迹
- 新增 `purge-all` 强制确认机制：即使传入 `-y`，也必须手动输入 `PURGE-ALL`
- 为 `purge-all` 增加 POSIX / Windows 回归测试与对应 `ctest` 入口

### Fixed
- 修复 `restore` 安全校验：阻止 `..` 绕过目标路径白名单
- 修复备份源路径校验：阻止恢复时通过相对路径逃逸 `backup_dir`
- 拒绝超长 `manifest.tsv` 行，避免恢复阶段出现截断解析
- 移除 `popen` / `_popen` 调用，改为更安全的进程执行方式
- 修复 macOS keychain 备份失败路径的小型内存泄漏
- 修复 Linux / Windows 构建兼容性问题
- 修复 Windows 下 `restore` 路径判断与 JetBrains 插件扫描问题

### Changed
- 扩展卸载覆盖范围，新增本地安装目录、native installer、Claude in Chrome Native Host、Linux deep-link desktop entry，以及更多运行时缓存/会话目录的清理与恢复
- 继续扩展卸载覆盖范围，新增 shell 配置注入、Linux mimeapps 关联、VS Code / Cursor / Windsurf 扩展目录、JetBrains 插件目录与全局 npm 安装残留的清理与恢复
- 收敛并稳定 Windows 安全回归脚本，仅保留稳定且关键的跨平台断言
- GitHub Actions 跨平台 CI 已验证通过（Ubuntu / macOS / Windows）

### Docs
- 补充 `CHANGELOG.md`
- 统一 README、英文 README、构建文档与发布说明中的下载与使用入口
- 增加 Release / Changelog 交叉链接，方便源码构建与发布包下载
- 增加发布自动化 workflow、贡献指南与安全策略
- 增加发布说明模板，便于后续版本延续发布流程
- 验证并启用自动发布流程，统一 v0.1.0 现有发布资产命名
- 增补 `--purge-all` 的中英文使用说明

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
