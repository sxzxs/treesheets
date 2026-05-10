# 测试架构记录

当前测试目标仍然包含 `src/main.cpp`，这是为了沿用 TreeSheets 现有的单翻译单元组织方式，避免在本轮测试收敛里把主程序链接结构和业务逻辑同时重排。

已经完成的收敛：

- `TreeSheetsTestSupport` 统一测试编译定义、源码根目录路径和 wxWidgets 链接依赖。
- `add_treesheets_test()` 统一四个测试目标的创建、CTest 注册、工作目录和 MSVC `/utf-8` 选项。
- `tests/test_helpers.h` 统一 wx 初始化、无窗口 `System`、临时文件、fixture 读取和断言。

后续如果要彻底拆库，建议先把 `src/main.cpp` 里的常量、枚举和全局实现移动到独立实现目标，再让 GUI 程序和测试目标共同链接该库。这个拆分会影响主程序链接边界，适合作为单独重构处理。
