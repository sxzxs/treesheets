#include "test_helpers.h"

#include <cstdlib>

namespace {

using namespace test_helpers;

std::vector<uint8_t> DocumentSampleImageBytes() {
    return {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
}

#define RUN_DOCUMENT_TEST(test_name)                         \
    do {                                                     \
        if (std::getenv("TREESHEETS_TEST_TRACE"))            \
            std::cerr << #test_name << '\n';                 \
        test_name();                                         \
    } while (false)

void TestUndoRedoRestoresCellTextAndModifiedState() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 1));

    CHECK(!doc.modified);
    doc.selected.GetCell()->AddUndo(&doc);
    doc.selected.GetCell()->text.t = "after";

    CHECK(doc.modified);
    CHECK_EQ(doc.undolist.size(), static_cast<size_t>(1));
    CHECK_EQ(doc.redolist.size(), static_cast<size_t>(0));

    doc.Undo(doc.undolist, doc.redolist);

    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString(""));
    CHECK(!doc.modified);
    CHECK_EQ(doc.undolist.size(), static_cast<size_t>(0));
    CHECK_EQ(doc.redolist.size(), static_cast<size_t>(1));

    doc.Undo(doc.redolist, doc.undolist, true);

    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("after"));
    CHECK(doc.modified);
    CHECK_EQ(doc.undolist.size(), static_cast<size_t>(1));
    CHECK_EQ(doc.redolist.size(), static_cast<size_t>(0));
}

void TestUndoRedoRestoresGridShapeChanges() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(2, 1));
    doc.root->grid->C(0, 0)->text.t = "left";
    doc.root->grid->C(1, 0)->text.t = "right";

    doc.root->AddUndo(&doc);
    doc.root->grid->InsertCells(1, -1, 1, 0);
    doc.root->grid->C(1, 0)->text.t = "middle";

    CHECK_EQ(doc.root->grid->xs, 3);
    CHECK_EQ(doc.root->grid->C(2, 0)->text.t, wxString("right"));

    doc.Undo(doc.undolist, doc.redolist);

    CHECK_EQ(doc.root->grid->xs, 2);
    CHECK_EQ(doc.root->grid->ys, 1);
    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("left"));
    CHECK_EQ(doc.root->grid->C(1, 0)->text.t, wxString("right"));
    CHECK(!doc.modified);

    doc.Undo(doc.redolist, doc.undolist, true);

    CHECK_EQ(doc.root->grid->xs, 3);
    CHECK_EQ(doc.root->grid->C(1, 0)->text.t, wxString("middle"));
    CHECK_EQ(doc.root->grid->C(2, 0)->text.t, wxString("right"));
    CHECK(doc.modified);
}

void TestUndoGroupsSameGenerationChanges() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(2, 1));

    auto first = doc.root->grid->C(0, 0).get();
    auto second = doc.root->grid->C(1, 0).get();
    doc.AddUndo(first);
    first->text.t = "first";
    doc.AddUndo(second, false);
    second->text.t = "second";

    CHECK_EQ(doc.undolist.size(), static_cast<size_t>(2));

    doc.Undo(doc.undolist, doc.redolist);

    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString(""));
    CHECK_EQ(doc.root->grid->C(1, 0)->text.t, wxString(""));
    CHECK_EQ(doc.undolist.size(), static_cast<size_t>(0));
    CHECK_EQ(doc.redolist.size(), static_cast<size_t>(2));
    CHECK(!doc.modified);

    doc.Undo(doc.redolist, doc.undolist, true);

    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("first"));
    CHECK_EQ(doc.root->grid->C(1, 0)->text.t, wxString("second"));
    CHECK_EQ(doc.undolist.size(), static_cast<size_t>(2));
    CHECK_EQ(doc.redolist.size(), static_cast<size_t>(0));
}

void TestUndoStackStressCullsOldEntriesButKeepsRecentUndo() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(50, 25));

    for (int i = 0; i < 1100; i++) {
        auto cell = doc.root->grid->C(i % 50, i / 50).get();
        doc.SetSelect(cell->parent->grid->FindCell(cell));
        doc.AddUndo(cell);
        cell->text.t = wxString::Format("edit %d", i);
    }

    CHECK(doc.modified);
    CHECK(doc.undolist.size() <= static_cast<size_t>(1000));
    CHECK(!doc.undolist.empty());

    auto latest = doc.root->grid->C(49, 21).get();
    CHECK_EQ(latest->text.t, wxString("edit 1099"));
    doc.Undo(doc.undolist, doc.redolist);
    CHECK_EQ(doc.root->grid->C(49, 21)->text.t, wxString(""));
    doc.Undo(doc.redolist, doc.undolist, true);
    CHECK_EQ(doc.root->grid->C(49, 21)->text.t, wxString("edit 1099"));
}

void TestPasteTabDelimitedTextMergesIntoParentGrid() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 1));

    wxTextDataObject text(wxString("A\tB") + LINE_SEPARATOR + "C\tD");
    doc.PasteOrDrop(text);

    CHECK_EQ(doc.root->grid->xs, 2);
    CHECK_EQ(doc.root->grid->ys, 2);
    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("A"));
    CHECK_EQ(doc.root->grid->C(1, 0)->text.t, wxString("B"));
    CHECK_EQ(doc.root->grid->C(0, 1)->text.t, wxString("C"));
    CHECK_EQ(doc.root->grid->C(1, 1)->text.t, wxString("D"));
    CHECK_EQ(doc.selected.x, 0);
    CHECK_EQ(doc.selected.y, 0);
    CHECK_EQ(doc.selected.xs, 2);
    CHECK_EQ(doc.selected.ys, 2);
    CHECK(doc.modified);
}

void TestLargeTabDelimitedPasteStress() {
    constexpr int rows = 80;
    constexpr int cols = 40;
    wxString content;
    for (int y = 0; y < rows; y++) {
        if (y) content += LINE_SEPARATOR;
        for (int x = 0; x < cols; x++) {
            if (x) content += '\t';
            content += wxString::Format("r%d-c%d", y, x);
        }
    }

    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 1));
    wxTextDataObject text(content);
    doc.PasteOrDrop(text);

    auto grid = doc.root->grid.get();
    CHECK_EQ(grid->xs, cols);
    CHECK_EQ(grid->ys, rows);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("r0-c0"));
    CHECK_EQ(grid->C(cols - 1, rows - 1)->text.t,
             wxString::Format("r%d-c%d", rows - 1, cols - 1));
    CHECK(doc.modified);
}

void TestCrossDocumentCopyCutPasteUsesStoredCellClone() {
    treesheets::Document source;
    InitDocument(source, MakeRoot(1, 1));
    auto source_cell = source.root->grid->C(0, 0).get();
    source_cell->text.t = "copied";
    source_cell->cellcolor = 0x123456;

    treesheets::Document target;
    InitDocument(target, MakeRoot(1, 1));

    source.Copy(wxID_COPY);

    wxTextDataObject text(treesheets::sys->clipboardcopy);
    target.PasteOrDrop(text);

    auto pasted = target.root->grid->C(0, 0).get();
    CHECK_EQ(pasted->text.t, wxString("copied"));
    CHECK_EQ(pasted->cellcolor, 0x123456u);
    CHECK(target.modified);

    treesheets::Document cut_source;
    InitDocument(cut_source, MakeRoot(2, 2));
    cut_source.root->grid->C(0, 0)->text.t = "cut";
    cut_source.root->grid->C(0, 0)->cellcolor = 0x654321;
    cut_source.root->grid->C(1, 0)->text.t = "row anchor";
    cut_source.root->grid->C(0, 1)->text.t = "column anchor";

    CHECK_EQ(cut_source.Action(wxID_CUT), wxString(""));
    CHECK_EQ(cut_source.root->grid->C(0, 0)->text.t, wxString(""));
    CHECK(cut_source.modified);

    treesheets::Document cut_target;
    InitDocument(cut_target, MakeRoot(1, 1));
    wxTextDataObject cut_text(treesheets::sys->clipboardcopy);
    cut_target.PasteOrDrop(cut_text);

    CHECK_EQ(cut_target.root->grid->C(0, 0)->text.t, wxString("cut"));
    CHECK_EQ(cut_target.root->grid->C(0, 0)->cellcolor, 0x654321u);
    CHECK(cut_target.modified);

    treesheets::sys->clipboardcopy.clear();
    treesheets::sys->cellclipboard = nullptr;
}

void TestDragMoveAndCopyCellsWithoutCanvas() {
    {
        treesheets::Document doc;
        InitDocument(doc, MakeRoot(2, 2));
        auto grid = doc.root->grid.get();
        grid->C(0, 0)->text.t = "copy";
        grid->C(0, 0)->cellcolor = 0x112233;

        doc.SetSelect(treesheets::Selection(doc.root->grid, 0, 0, 1, 1));
        doc.begindrag = treesheets::Selection(doc.root->grid, 1, 1, 1, 1);
        doc.isctrlshiftdrag = 2;
        doc.SelectUp();

        CHECK_EQ(grid->C(0, 0)->text.t, wxString("copy"));
        CHECK_EQ(grid->C(1, 1)->text.t, wxString("copy"));
        CHECK_EQ(grid->C(1, 1)->cellcolor, 0x112233u);
        CHECK(doc.modified);
    }

    {
        treesheets::Document doc;
        InitDocument(doc, MakeRoot(2, 2));
        auto grid = doc.root->grid.get();
        grid->C(0, 0)->text.t = "move";
        grid->C(1, 0)->text.t = "row anchor";
        grid->C(0, 1)->text.t = "column anchor";

        doc.SetSelect(treesheets::Selection(doc.root->grid, 0, 0, 1, 1));
        doc.begindrag = treesheets::Selection(doc.root->grid, 1, 1, 1, 1);
        doc.isctrlshiftdrag = 1;
        doc.SelectUp();

        CHECK_EQ(grid->C(0, 0)->text.t, wxString(""));
        CHECK_EQ(grid->C(1, 1)->text.t, wxString("move"));
        CHECK(doc.modified);
    }
}

void TestPasteSingleTextReplacesCellAndEntersTextEdit() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 1));
    doc.selected.GetCell()->text.t = "old";

    wxTextDataObject text("new text");
    doc.PasteOrDrop(text);

    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("new text"));
    CHECK(doc.selected.TextEdit());
    CHECK_EQ(doc.selected.cursor, 8);
    CHECK_EQ(doc.selected.cursorend, 8);
    CHECK(doc.modified);
}

void TestBackspaceDeleteAndWordEditingActions() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(2, 2));
    auto first = doc.root->grid->C(0, 0).get();

    first->text.t = "abcdef";
    doc.selected.EnterEdit(&doc, 0, 0);
    CHECK_EQ(doc.Action(A_BACKSPACE), wxString(""));
    CHECK_EQ(first->text.t, wxString("abcdef"));
    CHECK_EQ(doc.undolist.size(), static_cast<size_t>(0));

    doc.selected.EnterEdit(&doc, 2, 5);
    CHECK_EQ(doc.Action(A_BACKSPACE), wxString(""));
    CHECK_EQ(first->text.t, wxString("abf"));
    CHECK_EQ(doc.selected.cursor, 2);
    CHECK_EQ(doc.selected.cursorend, 2);

    first->text.t = Utf8("alpha βeta gamma");
    doc.selected.EnterEdit(&doc, 10, 10);
    CHECK_EQ(doc.Action(A_BACKSPACE_WORD), wxString(""));
    CHECK_EQ(first->text.t, Utf8("alpha  gamma"));

    first->text.t = Utf8("alpha βeta gamma");
    doc.selected.EnterEdit(&doc, 6, 6);
    CHECK_EQ(doc.Action(A_DELETE_WORD), wxString(""));
    CHECK_EQ(first->text.t, Utf8("alpha  gamma"));

    doc.SetSelect(treesheets::Selection(doc.root->grid, 1, 0, 1, 1));
    doc.selected.GetCell()->text.t = "cell";
    doc.root->grid->C(1, 1)->text.t = "keeps column";
    CHECK_EQ(doc.Action(A_BACKSPACE), wxString(""));
    CHECK_EQ(doc.root->grid->C(1, 0)->text.t, wxString(""));
}

void TestKeyboardNavigationSelectionAndTextRanges() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(3, 2));

    CHECK_EQ(doc.Action(A_RIGHT), wxString(""));
    CHECK_EQ(doc.selected.x, 1);
    CHECK_EQ(doc.selected.y, 0);
    CHECK(doc.selected.Thin());

    CHECK_EQ(doc.Action(A_SRIGHT), wxString(""));
    CHECK_EQ(doc.selected.x, 1);
    CHECK_EQ(doc.selected.xs, 1);

    CHECK_EQ(doc.Action(A_HOME), wxString(""));
    CHECK_EQ(doc.selected.x, 0);
    CHECK_EQ(doc.selected.y, 0);

    CHECK_EQ(doc.Action(A_END), wxString(""));
    CHECK_EQ(doc.selected.x, 2);
    CHECK_EQ(doc.selected.y, 1);

    bool unprocessed = false;
    CHECK_EQ(doc.Key(WXK_NONE, WXK_PAGEUP, false, false, false, unprocessed), wxString(""));
    CHECK_EQ(doc.Key(WXK_NONE, WXK_PAGEDOWN, false, false, false, unprocessed), wxString(""));

    auto cell = doc.selected.GetCell();
    cell->text.t = "one\ntwo\nthree";
    doc.selected.EnterEdit(&doc, 5, 5);
    CHECK_EQ(doc.Action(A_SHOME), wxString(""));
    CHECK_EQ(doc.selected.cursor, 0);
    CHECK_EQ(doc.selected.cursorend, 5);

    doc.selected.EnterEdit(&doc, 4, 4);
    CHECK_EQ(doc.Action(A_SEND), wxString(""));
    CHECK_EQ(doc.selected.cursor, 4);
    CHECK_EQ(doc.selected.cursorend, 13);

    treesheets::Selection from(doc.root->grid, doc.selected.x, doc.selected.y, 1, 1);
    from.EnterEdit(&doc, 1, 1);
    treesheets::Selection to(doc.root->grid, doc.selected.x, doc.selected.y, 1, 1);
    to.EnterEdit(&doc, 8, 8);
    treesheets::Selection merged;
    merged.Merge(from, to);
    CHECK(merged.TextEdit());
    CHECK_EQ(merged.cursor, 1);
    CHECK_EQ(merged.cursorend, 8);
}

void TestPasteTextEditNormalizesLineEndings() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 1));
    doc.selected.GetCell()->text.t = "prefix";
    doc.selected.EnterEdit(&doc, 6, 6);

    wxTextDataObject text("A\r\nB\rC");
    doc.PasteOrDrop(text);

    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("prefixA\nB\nC"));
    CHECK(doc.selected.TextEdit());
    CHECK_EQ(doc.selected.cursor, 11);
    CHECK_EQ(doc.selected.cursorend, 11);
    CHECK(doc.modified);
}

void TestPasteHTMLTablePreservesSpansTextAndStyles() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 1));

    auto html =
        "<table>"
        "<tr><td colspan=\"2\" style=\"background-color:#112233;color:#445566;"
        "font-weight:bold\">A &amp; B</td></tr>"
        "<tr><td rowspan=\"2\">C<br>D</td><td><i>E</i></td></tr>"
        "<tr><td>F</td></tr>"
        "</table>";

    CHECK(doc.PasteHTMLTable(html));

    auto grid = doc.root->grid.get();
    CHECK_EQ(grid->xs, 2);
    CHECK_EQ(grid->ys, 3);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("A & B"));
    CHECK_EQ(grid->C(0, 0)->mergexs, 2);
    CHECK_EQ(grid->C(0, 0)->mergeys, 1);
    CHECK_EQ(grid->C(0, 0)->cellcolor, SwappedColor(0x112233));
    CHECK_EQ(grid->C(0, 0)->textcolor, SwappedColor(0x445566));
    CHECK(grid->C(0, 0)->text.stylebits & STYLE_BOLD);
    CHECK(grid->C(1, 0)->Covered());

    CHECK_EQ(grid->C(0, 1)->text.t, wxString("C") + LINE_SEPARATOR + "D");
    CHECK_EQ(grid->C(0, 1)->mergexs, 1);
    CHECK_EQ(grid->C(0, 1)->mergeys, 2);
    CHECK(grid->C(0, 2)->Covered());

    CHECK_EQ(grid->C(1, 1)->text.t, wxString("E"));
    CHECK(grid->C(1, 1)->text.stylebits & STYLE_ITALIC);
    CHECK_EQ(grid->C(1, 2)->text.t, wxString("F"));
    CHECK(doc.modified);
}

void TestPasteHTMLFixturesHandleNestedTablesRichStylesLinksAndImages() {
    {
        treesheets::Document doc;
        InitDocument(doc, MakeRoot(1, 1));

        CHECK(doc.PasteHTMLTable(ReadTextFixture("html/nested_table.html")));

        auto grid = doc.root->grid.get();
        CHECK_EQ(grid->xs, 2);
        CHECK_EQ(grid->ys, 1);
        CHECK_EQ(grid->C(0, 0)->text.t, wxString("Outer Inner"));
        CHECK_EQ(grid->C(1, 0)->text.t, wxString("Right"));
    }

    {
        treesheets::Document doc;
        InitDocument(doc, MakeRoot(1, 1));

        CHECK(doc.PasteHTMLTable(ReadTextFixture("html/rich_table.html")));

        auto grid = doc.root->grid.get();
        CHECK_EQ(grid->xs, 2);
        CHECK_EQ(grid->ys, 1);
        CHECK_EQ(grid->C(0, 0)->text.t, wxString("Example"));
        CHECK_EQ(grid->C(0, 0)->cellcolor, SwappedColor(0x112233));
        CHECK_EQ(grid->C(0, 0)->textcolor, SwappedColor(0x445566));
        CHECK_EQ(grid->C(0, 0)->text.relsize, -6);
        CHECK_EQ(grid->C(1, 0)->text.t, wxString("Logo"));
    }
}

void TestExportFormatsEscapeTextAndPreserveStyles() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(2, 1));
    auto grid = doc.root->grid.get();
    auto left = grid->C(0, 0).get();
    auto right = grid->C(1, 0).get();

    left->text.t = "A&B<1>";
    left->cellcolor = SwappedColor(0x112233);
    left->textcolor = SwappedColor(0x445566);
    left->bordercolor = SwappedColor(0x778899);
    left->text.relsize = -3;
    left->celltype = treesheets::CT_CODE;
    right->text.t = "quote \"x\"";
    right->text.stylebits = STYLE_BOLD | STYLE_ITALIC;

    auto selection = grid->SelectAll();
    auto csv = grid->ConvertToText(selection, 0, A_EXPCSV, &doc, true, doc.root.get());
    CHECK_EQ(csv, wxString("\"A&B<1>\",\"quote \"\"x\"\"\"\n"));

    auto xml = grid->ConvertToText(selection, 0, A_EXPXML, &doc, true, doc.root.get());
    CHECK(xml.Find("<grid") != wxNOT_FOUND);
    CHECK(xml.Find("A&amp;B&lt;1&gt;") != wxNOT_FOUND);
    CHECK(xml.Find("relsize=\"3\"") != wxNOT_FOUND);
    CHECK(xml.Find("stylebits=\"3\"") != wxNOT_FOUND);
    CHECK(xml.Find("colorborder=\"0x998877\"") != wxNOT_FOUND);
    CHECK(xml.Find(wxString::Format("type=\"%d\"", treesheets::CT_CODE)) != wxNOT_FOUND);
    CHECK(xml.Find("quote \"x\"") != wxNOT_FOUND);

    auto html = grid->ConvertToText(selection, 0, A_EXPHTMLT, &doc, true, doc.root.get());
    CHECK(html.Find("<table") != wxNOT_FOUND);
    CHECK(html.Find("A&amp;B&lt;1&gt;") != wxNOT_FOUND);
    CHECK(html.Find("background-color: #112233;") != wxNOT_FOUND);
    CHECK(html.Find("color: #445566;") != wxNOT_FOUND);
    CHECK(html.Find("font-weight: bold;") != wxNOT_FOUND);
    CHECK(html.Find("font-style: italic;") != wxNOT_FOUND);
}

void TestHTMLExportEmbedsImagesAndNavigatesTextLinks() {
    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();

    treesheets::Document doc;
    InitDocument(doc, MakeRoot(2, 1));
    auto grid = doc.root->grid.get();
    auto left = grid->C(0, 0).get();
    auto right = grid->C(1, 0).get();

    left->text.t = "https://example.com";
    right->text.t = "https://example.com";
    doc.SetImageBM(right, DocumentSampleImageBytes(), 'I', 1.0);

    auto html = grid->ConvertToText(grid->SelectAll(), 0, A_EXPHTMLTI, &doc, true, doc.root.get());
    CHECK(html.Find("<img src=\"data:image/png;base64,") != wxNOT_FOUND);
    CHECK(html.Find("https://example.com") != wxNOT_FOUND);

    doc.SetSelect(grid->FindCell(left));
    CHECK_EQ(doc.Action(A_LINK), wxString(""));
    CHECK_EQ(doc.selected.GetCell(), right);

    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();
}

void TestHierarchifyAndFlattenRoundTripTable() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(3, 3));
    const char *rows[][3] = {
        {"A", "x1", "y1"},
        {"A", "x2", "y2"},
        {"B", "x3", "y3"},
    };

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            doc.root->grid->C(x, y)->text.t = rows[y][x];

    doc.root->grid->Hierarchify(&doc);

    CHECK_EQ(doc.root->grid->xs, 1);
    CHECK_EQ(doc.root->grid->ys, 2);
    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("A"));
    CHECK_EQ(doc.root->grid->C(0, 1)->text.t, wxString("B"));
    CHECK(doc.root->grid->C(0, 0)->grid != nullptr);
    CHECK(doc.root->grid->C(0, 0)->grid->C(0, 0)->grid != nullptr);
    CHECK_EQ(doc.root->grid->C(0, 0)->grid->C(0, 0)->text.t, wxString("x1"));
    CHECK_EQ(doc.root->grid->C(0, 0)->grid->C(0, 0)->grid->C(0, 0)->text.t,
             wxString("y1"));

    int maxdepth = 0;
    int leaves = 0;
    doc.root->MaxDepthLeaves(0, maxdepth, leaves);
    auto flat = std::make_shared<treesheets::Grid>(maxdepth, leaves);
    flat->InitCells();
    doc.root->grid->Flatten(0, 0, flat.get());
    doc.root->grid = flat;
    flat->ReParent(doc.root.get());

    CHECK_EQ(doc.root->grid->xs, 3);
    CHECK_EQ(doc.root->grid->ys, 3);
    CHECK_EQ(doc.root->grid->C(0, 0)->text.t, wxString("A"));
    CHECK_EQ(doc.root->grid->C(1, 0)->text.t, wxString("x1"));
    CHECK_EQ(doc.root->grid->C(2, 0)->text.t, wxString("y1"));
    CHECK_EQ(doc.root->grid->C(0, 1)->text.t, wxString("A"));
    CHECK_EQ(doc.root->grid->C(1, 1)->text.t, wxString("x2"));
    CHECK_EQ(doc.root->grid->C(2, 1)->text.t, wxString("y2"));
    CHECK_EQ(doc.root->grid->C(0, 2)->text.t, wxString("B"));
    CHECK_EQ(doc.root->grid->C(1, 2)->text.t, wxString("x3"));
    CHECK_EQ(doc.root->grid->C(2, 2)->text.t, wxString("y3"));
}

void TestWrapSelectionCreatesNestedGridAndUndoRestoresParent() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(2, 2));
    auto grid = doc.root->grid.get();
    grid->C(0, 0)->text.t = "a";
    grid->C(1, 0)->text.t = "b";
    grid->C(0, 1)->text.t = "c";
    grid->C(1, 1)->text.t = "d";

    doc.SetSelect(grid->SelectAll());
    CHECK_EQ(doc.Action(A_WRAP), wxString(""));

    CHECK_EQ(doc.root->grid->xs, 1);
    CHECK_EQ(doc.root->grid->ys, 1);
    auto wrapped = doc.root->grid->C(0, 0).get();
    CHECK(wrapped->grid != nullptr);
    CHECK_EQ(wrapped->grid->xs, 2);
    CHECK_EQ(wrapped->grid->ys, 2);
    CHECK_EQ(wrapped->grid->C(1, 1)->text.t, wxString("d"));

    doc.Undo(doc.undolist, doc.redolist);
    CHECK_EQ(doc.root->grid->xs, 2);
    CHECK_EQ(doc.root->grid->ys, 2);
    CHECK_EQ(doc.root->grid->C(1, 1)->text.t, wxString("d"));
}

void TestMergeAndUnmergeCellsThroughDocumentAction() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(3, 2));
    auto grid = doc.root->grid.get();

    grid->C(0, 0)->text.t = "master";
    grid->C(1, 0)->text.t = "right";
    grid->C(0, 1)->text.t = "down";
    doc.SetSelect(treesheets::Selection(doc.root->grid, 0, 0, 2, 2));

    CHECK_EQ(doc.Action(A_MERGECELLS), wxString(""));
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("master"));
    CHECK_EQ(grid->C(0, 0)->mergexs, 2);
    CHECK_EQ(grid->C(0, 0)->mergeys, 2);
    CHECK(grid->C(1, 0)->Covered());
    CHECK(grid->C(0, 1)->Covered());
    CHECK(grid->C(1, 1)->Covered());
    CHECK(!grid->C(1, 0)->HasText());
    CHECK_EQ(doc.selected.x, 0);
    CHECK_EQ(doc.selected.y, 0);
    CHECK_EQ(doc.selected.xs, 1);
    CHECK_EQ(doc.selected.ys, 1);
    CHECK(doc.modified);

    CHECK_EQ(doc.Action(A_UNMERGECELLS), wxString(""));
    CHECK_EQ(grid->C(0, 0)->mergexs, 1);
    CHECK_EQ(grid->C(0, 0)->mergeys, 1);
    CHECK(!grid->C(1, 0)->Covered());
    CHECK(!grid->C(0, 1)->Covered());
    CHECK(!grid->C(1, 1)->Covered());
}

void TestDeleteCellSelectionClearsContentAndUndoRestoresIt() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(3, 3));
    auto grid = doc.root->grid.get();

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) grid->C(x, y)->text.t = wxString::Format("%d,%d", x, y);

    doc.SetSelect(treesheets::Selection(doc.root->grid, 1, 1, 1, 1));
    CHECK_EQ(doc.Action(A_DELETE), wxString(""));

    CHECK_EQ(grid->xs, 3);
    CHECK_EQ(grid->ys, 3);
    CHECK_EQ(grid->C(1, 1)->text.t, wxString(""));
    CHECK(doc.selected.TextEdit());
    CHECK(doc.modified);

    doc.Undo(doc.undolist, doc.redolist);

    CHECK_EQ(doc.root->grid->C(1, 1)->text.t, wxString("1,1"));
    CHECK(!doc.modified);
}

void TestDeleteThinRowSelectionRemovesRow() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(3, 3));
    auto grid = doc.root->grid.get();

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) grid->C(x, y)->text.t = wxString::Format("r%d c%d", y, x);

    doc.SetSelect(treesheets::Selection(doc.root->grid, 0, 1, grid->xs, 0));
    CHECK_EQ(doc.Action(A_DELETE), wxString(""));

    CHECK_EQ(grid->xs, 3);
    CHECK_EQ(grid->ys, 2);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("r0 c0"));
    CHECK_EQ(grid->C(0, 1)->text.t, wxString("r2 c0"));
    CHECK(doc.modified);
}

void TestDeleteThinColumnSelectionRemovesColumn() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(3, 2));
    auto grid = doc.root->grid.get();

    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++) grid->C(x, y)->text.t = wxString::Format("r%d c%d", y, x);

    doc.SetSelect(treesheets::Selection(doc.root->grid, 1, 0, 0, grid->ys));
    CHECK_EQ(doc.Action(A_DELETE), wxString(""));

    CHECK_EQ(grid->xs, 2);
    CHECK_EQ(grid->ys, 2);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("r0 c0"));
    CHECK_EQ(grid->C(1, 0)->text.t, wxString("r0 c2"));
    CHECK(doc.modified);
}

void TestFoldActionsToggleNestedGrids() {
    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 2));
    auto grid = doc.root->grid.get();
    auto first = grid->C(0, 0).get();
    auto second = grid->C(0, 1).get();

    first->grid = std::make_shared<treesheets::Grid>(1, 1, first);
    first->grid->InitCells();
    second->grid = std::make_shared<treesheets::Grid>(1, 1, second);
    second->grid->InitCells();

    doc.SetSelect(treesheets::Selection(doc.root->grid, 0, 0, 1, 1));
    CHECK_EQ(doc.Action(A_FOLD), wxString(""));
    CHECK(first->grid->folded);
    CHECK(!second->grid->folded);

    CHECK_EQ(doc.Action(A_FOLD), wxString(""));
    CHECK(!first->grid->folded);

    doc.SetSelect(grid->SelectAll());
    CHECK_EQ(doc.Action(A_FOLDALL), wxString(""));
    CHECK(first->grid->folded);
    CHECK(second->grid->folded);

    CHECK_EQ(doc.Action(A_UNFOLDALL), wxString(""));
    CHECK(!first->grid->folded);
    CHECK(!second->grid->folded);
}

}  // namespace

int main() {
    TestSystem test_system;
    if (!test_system.IsOk()) {
        std::cerr << "failed to initialize wxWidgets\n";
        return 1;
    }

    RUN_DOCUMENT_TEST(TestUndoRedoRestoresCellTextAndModifiedState);
    RUN_DOCUMENT_TEST(TestUndoRedoRestoresGridShapeChanges);
    RUN_DOCUMENT_TEST(TestUndoGroupsSameGenerationChanges);
    RUN_DOCUMENT_TEST(TestUndoStackStressCullsOldEntriesButKeepsRecentUndo);
    RUN_DOCUMENT_TEST(TestPasteTabDelimitedTextMergesIntoParentGrid);
    RUN_DOCUMENT_TEST(TestLargeTabDelimitedPasteStress);
    RUN_DOCUMENT_TEST(TestCrossDocumentCopyCutPasteUsesStoredCellClone);
    RUN_DOCUMENT_TEST(TestDragMoveAndCopyCellsWithoutCanvas);
    RUN_DOCUMENT_TEST(TestPasteSingleTextReplacesCellAndEntersTextEdit);
    RUN_DOCUMENT_TEST(TestBackspaceDeleteAndWordEditingActions);
    RUN_DOCUMENT_TEST(TestKeyboardNavigationSelectionAndTextRanges);
    RUN_DOCUMENT_TEST(TestPasteTextEditNormalizesLineEndings);
    RUN_DOCUMENT_TEST(TestPasteHTMLTablePreservesSpansTextAndStyles);
    RUN_DOCUMENT_TEST(TestPasteHTMLFixturesHandleNestedTablesRichStylesLinksAndImages);
    RUN_DOCUMENT_TEST(TestExportFormatsEscapeTextAndPreserveStyles);
    RUN_DOCUMENT_TEST(TestHTMLExportEmbedsImagesAndNavigatesTextLinks);
    RUN_DOCUMENT_TEST(TestHierarchifyAndFlattenRoundTripTable);
    RUN_DOCUMENT_TEST(TestWrapSelectionCreatesNestedGridAndUndoRestoresParent);
    RUN_DOCUMENT_TEST(TestMergeAndUnmergeCellsThroughDocumentAction);
    RUN_DOCUMENT_TEST(TestDeleteCellSelectionClearsContentAndUndoRestoresIt);
    RUN_DOCUMENT_TEST(TestDeleteThinRowSelectionRemovesRow);
    RUN_DOCUMENT_TEST(TestDeleteThinColumnSelectionRemovesColumn);
    RUN_DOCUMENT_TEST(TestFoldActionsToggleNestedGrids);

    return Finish("document tests passed");
}
