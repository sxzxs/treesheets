#include <iostream>

#include "../src/main.cpp"

namespace {

int failures = 0;

void Check(bool condition, const char *expression, const char *file, int line) {
    if (condition) return;
    failures++;
    std::cerr << file << ":" << line << ": check failed: " << expression << '\n';
}

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) Check(((actual) == (expected)), #actual " == " #expected, __FILE__, __LINE__)

wxString Utf8(const char *text) { return wxString::FromUTF8(text); }

void TestCombiningMarksStayWithBase() {
    using Text = treesheets::Text;
    auto source = Utf8("a\xCC\x81" "b");

    CHECK_EQ(Text::NextCursorPos(source, 0), 2);
    CHECK_EQ(Text::PreviousCursorPos(source, static_cast<int>(source.Len())), 2);
    CHECK_EQ(Text::NextCursorPos(source, 2), static_cast<int>(source.Len()));
}

void TestZeroWidthJoinerEmojiIsOneCluster() {
    using Text = treesheets::Text;
    auto source = Utf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");

    CHECK_EQ(Text::NextCursorPos(source, 0), static_cast<int>(source.Len()));
    CHECK_EQ(Text::PreviousCursorPos(source, static_cast<int>(source.Len())), 0);
}

void TestRegionalIndicatorsPairByFlag() {
    using Text = treesheets::Text;
    auto source = Utf8("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xA8\xF0\x9F\x87\xA6");
    auto first_flag_end = Text::NextCursorPos(source, 0);

    CHECK(first_flag_end > 0);
    CHECK(first_flag_end < static_cast<int>(source.Len()));
    CHECK_EQ(Text::NextCursorPos(source, first_flag_end), static_cast<int>(source.Len()));
    CHECK_EQ(Text::PreviousCursorPos(source, first_flag_end), 0);
}

void TestDisplayEscapesControlsAndTabs() {
    using Text = treesheets::Text;
    auto display = Text::BuildDisplayLine(wxString("a\t") + wxChar(1) + "b");

    CHECK_EQ(display.text, wxString("a   \\x01b"));
    CHECK_EQ(display.sourceendtodisplayend[1], 1);
    CHECK_EQ(display.sourceendtodisplayend[2], 4);
    CHECK_EQ(display.sourceendtodisplayend[3], 8);
}

void TestHardLineBreaksAreLines() {
    treesheets::Text text;
    text.t = "one\ntwo\n";
    int index = 0;
    treesheets::Text::Line line;

    CHECK(text.GetLine(index, 80, line));
    CHECK_EQ(line.text, wxString("one"));
    CHECK_EQ(line.start, 0);
    CHECK_EQ(line.end, 3);

    CHECK(text.GetLine(index, 80, line));
    CHECK_EQ(line.text, wxString("two"));
    CHECK_EQ(line.start, 4);
    CHECK_EQ(line.end, 7);

    CHECK(text.GetLine(index, 80, line));
    CHECK_EQ(line.text, wxString(""));
    CHECK_EQ(line.start, 8);
    CHECK_EQ(line.end, 8);

    CHECK(!text.GetLine(index, 80, line));
}

void TestSoftWrapKeepsWordsTogether() {
    treesheets::Text text;
    text.t = "alpha beta gamma";
    int index = 0;
    treesheets::Text::Line line;

    CHECK(text.GetLine(index, 10, line));
    CHECK_EQ(line.text, wxString("alpha beta"));
    CHECK_EQ(line.start, 0);
    CHECK_EQ(line.end, 10);

    CHECK(text.GetLine(index, 10, line));
    CHECK_EQ(line.text, wxString("gamma"));
    CHECK_EQ(line.start, 11);
    CHECK_EQ(line.end, 16);
}

void TestBackspaceDeletesOneGraphemeCluster() {
    treesheets::Text text;
    text.t = Utf8("a\xCC\x81" "b");
    treesheets::Selection selection;
    selection.cursor = selection.cursorend = static_cast<int>(text.t.Len());

    text.Backspace(selection);
    CHECK_EQ(text.t, Utf8("a\xCC\x81"));
    CHECK_EQ(selection.cursor, 2);

    text.Backspace(selection);
    CHECK_EQ(text.t, wxString(""));
    CHECK_EQ(selection.cursor, 0);
}

}  // namespace

int main() {
    TestCombiningMarksStayWithBase();
    TestZeroWidthJoinerEmojiIsOneCluster();
    TestRegionalIndicatorsPairByFlag();
    TestDisplayEscapesControlsAndTabs();
    TestHardLineBreaksAreLines();
    TestSoftWrapKeepsWordsTogether();
    TestBackspaceDeletesOneGraphemeCluster();

    if (failures) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "text tests passed\n";
    return 0;
}
