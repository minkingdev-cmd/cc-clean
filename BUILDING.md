# cc-clean 构建与测试

## 1. 本机构建

```bash
cmake -S . -B build-cc-clean
cmake --build build-cc-clean
```

生成：

```bash
./build-cc-clean/cc-clean
```

## 2. 本地测试

### macOS / Linux

```bash
bash scripts/test_cc_clean_posix.sh
```

### Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_cc_clean_windows.ps1
```

## 3. Docker 端到端测试

```bash
bash scripts/test_cc_clean_container_e2e.sh
```

覆盖：

- 默认清理
- `--include-related`
- `--include-installation`
- `--purge-all`

## 4. CTest

```bash
cmake -S . -B build-cc-clean
cmake --build build-cc-clean
cd build-cc-clean
ctest --output-on-failure
```

如需把 Docker E2E 也注册进 `ctest`：

```bash
FG_ENABLE_DOCKER_E2E=1 cmake -S . -B build-cc-clean
cmake --build build-cc-clean
cd build-cc-clean
ctest -L docker --output-on-failure
```

## 5. GitHub Actions 发布

仓库内置：`.github/workflows/release.yml`

触发方式：

```bash
git tag v0.1.1
git push origin v0.1.1
```

workflow 会自动：

- 在 GitHub runner 上构建 `cc-clean`
- 执行测试
- 打包下载文件
- 生成 `.sha256` 校验文件
- 创建或更新对应 GitHub Release

默认产物平台：

- `linux-x64`
- `macos-x64`
- `macos-arm64`
- `windows-x64`

## 6. 常用清理命令

默认清理：

```bash
./build-cc-clean/cc-clean clean -y
```

扩展清理：

```bash
./build-cc-clean/cc-clean clean --include-related -y
./build-cc-clean/cc-clean clean --include-installation -y
```

最彻底清理：

```bash
./build-cc-clean/cc-clean clean --purge-all -y
```

如果配置目录不是默认 `~/.claude`：

```bash
./build-cc-clean/cc-clean clean --config-dir <dir> --purge-all --allow-unsafe-purge -y
```
