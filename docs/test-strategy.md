# 测试策略

这份文档记录当前自动化测试已经覆盖的范围，以及仍需要真实平台或界面环境验证的范围。

## 自动化测试入口

- 构建并运行聚合测试：`cmake --build _build --config Release --target TreeSheetsTests -j 4`
- 运行 CTest：`ctest --test-dir _build -C Release --output-on-failure`
- 单独运行文档层测试：`_build/TreeSheetsDocumentTests.exe`

四组测试分别覆盖文本、网格、文件序列化和文档编辑行为。测试目标仍包含 `src/main.cpp`，当前先通过 `TreeSheetsTestSupport` 和 `tests/test_helpers.h` 收敛重复配置，完整拆库留作单独重构。

## 测试夹具

固定样本放在 `tests/fixtures/`：

- `csv/` 覆盖引号、跨行字段、制表符、分号和自定义分隔符。
- `html/` 覆盖嵌套表格、颜色、链接、图片替代文本和字体大小。
- `cts/` 保存损坏样本。最小、旧版本和复杂样式 `.cts` 由测试代码生成，避免二进制 fixture 难以审阅。

新增导入导出测试时，优先把复杂输入放进 fixture，再在测试里通过 `ReadTextFixture()` 或 `FixturePath()` 引用。

## 自动保存生命周期

- 自动保存文件与原文件同目录，文件名由 `System::TmpName()` 生成，也就是原始文件名改为 `.tmp` 扩展名。
- 写入时先通过 `System::NewName()` 生成 `.new` 临时写入文件，再原子式替换目标。
- 正常保存成功后，旧 `.tmp` 会被清理。
- 启动读取时如果存在 `.tmp`，界面路径会提示用户选择是否恢复。
- 自动化测试覆盖 `.tmp` 可读性、正常保存后的清理、异常退出后的恢复读取，以及多个文档同时自动保存时互不覆盖。

## 平台与界面回归

这些场景依赖真实窗口系统或操作系统事件，当前不适合放进无窗口单元测试：

- Windows、macOS、Linux 文件监控事件和轮询兜底的重复/漏报检查。
- 输入法组合输入、高分屏、多显示器、暗色模式、菜单工具栏快捷键。
- 打印预览和分页显示。

执行发布前回归时按 `docs/platform-test-checklist.md` 检查。

## 质量工具评估

- MSVC 可优先尝试 `/fsanitize=address` 的专用 Debug/RelWithDebInfo 配置，不建议直接塞进默认 CI，避免 wxWidgets 三方依赖带来噪声。
- Linux/macOS 可增加 Clang ASan/UBSan 夜间或手动 job，先跑四组测试目标。
- 静态分析建议先用 `clang-tidy` 只检查 `src/` 和 `tests/` 的本地改动文件，等噪声可控后再接 CI。
- 当前基线先通过压力测试覆盖撤销栈裁剪、大规模 clone/sort 和文件读取边界，工具链检查作为下一阶段质量增强。
