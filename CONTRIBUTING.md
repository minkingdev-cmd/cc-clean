# Contributing

感谢你为 `cc-clean` 做贡献。

## 开发环境

推荐准备以下工具：

- CMake 3.16+
- C11 编译器
- Git
- macOS / Linux / Windows 任一平台

## 本地构建

```bash
cmake -S . -B build
cmake --build build
```

## 本地测试

```bash
cd build
cmake ..
ctest --output-on-failure
```

也可以按 label 运行：

```bash
ctest -L safety -V
ctest -L restore -V
ctest -L json -V
```

## 提交建议

- 优先保持跨平台兼容
- 新功能优先补测试
- 变更命令行行为时同步更新 README / BUILDING / CHANGELOG
- 变更发布行为时同步更新 `docs/releases/`

## 发布相关

- `CI` workflow 负责常规构建与测试
- `Release` workflow 负责在 tag 上构建并上传发布资产
- 发布前建议先确认 `main` 上三平台 CI 全部通过
