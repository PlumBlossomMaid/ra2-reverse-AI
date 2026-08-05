# CI

自动验证 `code/rewrite/` 的算法重写——每次 push/PR 用 CMake 构建 + Google Test 跑测试。

## workflow

GitHub Actions workflow 位于 `.github/workflows/test.yml`：
checkout（含 submodule）→ CMake 配置 → 构建 → ctest（9 组 Google Test 用例）。

## 本地等价命令

```
git submodule update --init --recursive   # 拉取 third_party/gtest
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 设计说明

- `ubuntu-latest` + g++：算法是纯 C++17、无平台依赖，Linux 验证已足够
- 测试框架：Google Test（`third_party/gtest`，git submodule，架构参考 PaddlePaddle）
- 本地 MSVC 构建同样支持（顶层 CMakeLists 自动加 `/utf-8`）
- 后续加机制模块时，在 `code/rewrite/` 下新增源码并加测试用例，
  workflow 无需改动
