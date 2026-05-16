#include "test_helpers.h"

namespace {

using namespace test_helpers;

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

void TestTinyDisplayLineUsesOneLongestWrappedLine() {
    treesheets::Text text;
    text.t = "short\nlonger line\nmid";

    auto display = text.TinyDisplayLine(80);

    CHECK_EQ(display.text, wxString("longer line"));

    text.t = "alpha beta gamma";
    display = text.TinyDisplayLine(10);

    CHECK_EQ(display.text, wxString("alpha beta"));
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

void TestRichStylesApplyToSelectionAndTrackDeletes() {
    treesheets::Text text;
    text.t = "abcdef";
    treesheets::Selection selection;
    selection.cursor = 1;
    selection.cursorend = 4;

    text.ToggleRichStyle(selection, STYLE_BOLD, g_textcolor_default, 0);
    text.SetRichColor(selection, 0x112233, g_textcolor_default, 0);

    CHECK_EQ(text.richstyles.size(), static_cast<size_t>(1));
    CHECK_EQ(text.richstyles[0].start, 1);
    CHECK_EQ(text.richstyles[0].end, 4);
    CHECK(text.StyleBitsAt(0, 0) == 0);
    CHECK(text.StyleBitsAt(2, 0) & STYLE_BOLD);
    CHECK_EQ(text.ColorAt(2, g_textcolor_default), 0x112233u);

    selection.cursor = 2;
    selection.cursorend = 3;
    text.Delete(selection);

    CHECK_EQ(text.t, wxString("abdef"));
    CHECK_EQ(text.richstyles.size(), static_cast<size_t>(1));
    CHECK_EQ(text.richstyles[0].start, 1);
    CHECK_EQ(text.richstyles[0].end, 3);
    CHECK(text.StyleBitsAt(2, 0) & STYLE_BOLD);
    CHECK_EQ(text.ColorAt(2, g_textcolor_default), 0x112233u);
}

}  // namespace

int main() {
    TestCombiningMarksStayWithBase();
    TestZeroWidthJoinerEmojiIsOneCluster();
    TestRegionalIndicatorsPairByFlag();
    TestDisplayEscapesControlsAndTabs();
    TestHardLineBreaksAreLines();
    TestSoftWrapKeepsWordsTogether();
    TestTinyDisplayLineUsesOneLongestWrappedLine();
    TestBackspaceDeletesOneGraphemeCluster();
    TestRichStylesApplyToSelectionAndTrackDeletes();

    return Finish("text tests passed");
}
