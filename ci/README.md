# CI

自动验证 `code/rewrite/` 的算法重写——每次 push/PR 编译并跑数值测试。

## workflow

GitHub Actions workflow 位于 `.github/workflows/test.yml`：
编译 `production_system.cpp + demo.cpp` 并运行 45 项断言。

## 本地等价命令

```
g++ -std=c++17 -Wall -Wextra code/rewrite/production_system.cpp code/rewrite/demo.cpp -o demo && ./demo
```

## 设计说明

- 用 `ubuntu-latest` + g++：算法是纯 C++17、无平台依赖，Linux 验证已足够
- 单 job、单命令：保持快速（<1 min），CI 只是回归护栏
- 后续加机制模块时，在 `code/rewrite/` 下新增文件并追加测试用例即可，
  workflow 无需改动
