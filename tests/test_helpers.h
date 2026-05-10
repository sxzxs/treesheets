#pragma once

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include <wx/sstream.h>

#include "../src/main.cpp"

#ifndef TREESHEETS_SOURCE_DIR
    #define TREESHEETS_SOURCE_DIR "."
#endif

namespace test_helpers {

inline int failures = 0;

// 最小测试框架：不依赖第三方测试库，方便每个测试目标直接包含主程序代码。
inline void Check(bool condition, const char *expression, const char *file, int line) {
    if (condition) return;
    failures++;
    std::cerr << file << ":" << line << ": check failed: " << expression << '\n';
}

inline wxString Utf8(const char *text) { return wxString::FromUTF8(text); }

inline wxString FixturePath(const char *relative_path) {
    return wxString::FromUTF8(TREESHEETS_SOURCE_DIR) + "/tests/fixtures/" +
           wxString::FromUTF8(relative_path);
}

inline wxString ReadTextFile(const wxString &filename) {
    wxFFileInputStream input(filename);
    Check(input.IsOk(), "input.IsOk()", __FILE__, __LINE__);
    wxStringOutputStream output;
    input.Read(output);
    Check(input.IsOk() || input.Eof(), "input.IsOk() || input.Eof()", __FILE__, __LINE__);
    return output.GetString();
}

inline wxString ReadTextFixture(const char *relative_path) {
    return ReadTextFile(FixturePath(relative_path));
}

inline wxArrayString ReadFixtureLines(const char *relative_path) {
    auto text = ReadTextFixture(relative_path);
    text.Replace("\r\n", "\n");
    text.Replace("\r", "\n");
    auto lines = wxStringTokenize(text, "\n", wxTOKEN_RET_EMPTY_ALL);
    if (!lines.IsEmpty() && text.EndsWith("\n")) lines.RemoveAt(lines.size() - 1);
    return lines;
}

// 代码内部颜色和 HTML/CSS 颜色字节序不同，样式导入导出测试会用到这个转换。
inline uint SwappedColor(uint color) {
    return ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16);
}

// 构造带根 Cell 的网格，保证每个子 Cell 都有正确父指针。
inline std::unique_ptr<treesheets::Cell> MakeRoot(int xs, int ys) {
    auto root = std::make_unique<treesheets::Cell>(nullptr, nullptr, treesheets::CT_DATA,
                                                  std::make_shared<treesheets::Grid>(xs, ys));
    root->grid->InitCells();
    return root;
}

// 无窗口文档初始化：默认选中左上角单元格。
inline void InitDocument(treesheets::Document &doc, std::unique_ptr<treesheets::Cell> root) {
    auto initial = root->grid->C(0, 0).get();
    doc.InitWith(std::move(root), "", initial, 1, 1);
}

inline void RemoveIfExists(const wxString &filename) {
    if (!filename.empty() && ::wxFileExists(filename)) ::wxRemoveFile(filename);
}

inline wxString TempName(const char *prefix, const char *extension) {
    wxString filename = wxFileName::CreateTempFileName(prefix);
    RemoveIfExists(filename);
    filename += extension;
    RemoveIfExists(filename);
    return filename;
}

inline wxString TempCtsName(const char *prefix) { return TempName(prefix, ".cts"); }

inline void WriteBytes(const wxString &filename, const std::vector<char> &bytes) {
    wxFFileOutputStream output(filename);
    Check(output.IsOk(), "output.IsOk()", __FILE__, __LINE__);
    output.Write(bytes.data(), bytes.size());
    Check(output.IsOk(), "output.IsOk()", __FILE__, __LINE__);
}

inline void WriteTextFile(const wxString &filename, const char *text) {
    wxFFileOutputStream output(filename);
    Check(output.IsOk(), "output.IsOk()", __FILE__, __LINE__);
    output.Write(text, std::strlen(text));
    Check(output.IsOk(), "output.IsOk()", __FILE__, __LINE__);
}

class TestSystem {
  public:
    // 每个测试进程只需要一个实例，用 RAII 管理 wx 初始化和全局 sys 指针。
    TestSystem() {
        if (initializer.IsOk()) {
            system = std::make_unique<treesheets::System>(true);
            treesheets::sys = system.get();
        }
    }

    ~TestSystem() {
        if (treesheets::sys == system.get()) treesheets::sys = nullptr;
    }

    bool IsOk() const { return initializer.IsOk(); }

  private:
    wxInitializer initializer;
    std::unique_ptr<treesheets::System> system;
};

inline int Finish(const char *success_message) {
    if (failures) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << success_message << '\n';
    return 0;
}

}  // namespace test_helpers

#define CHECK(expr) test_helpers::Check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) \
    test_helpers::Check(((actual) == (expected)), #actual " == " #expected, __FILE__, __LINE__)
