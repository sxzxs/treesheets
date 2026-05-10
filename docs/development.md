# 开发与测试

这份说明记录当前本地迭代最常用的构建和测试命令。

## 配置构建目录

```sh
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release
```

Windows + Visual Studio 生成器通常需要在后续构建命令里加 `--config Release`。

## 构建主程序

```sh
cmake --build _build --config Release --target TreeSheets -j 4
```

## 构建并运行测试

```sh
cmake --build _build --config Release --target TreeSheetsTests -j 4
ctest --test-dir _build -C Release --output-on-failure
```

`TreeSheetsTests` 会构建当前的文本、网格、序列化和文档测试。MSVC 下测试目标会使用 `/utf-8`，避免中文测试注释在默认代码页下编译失败。
