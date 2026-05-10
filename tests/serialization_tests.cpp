#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

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

std::unique_ptr<treesheets::Cell> MakeRoot(int xs, int ys) {
    auto root = std::make_unique<treesheets::Cell>(nullptr, nullptr, treesheets::CT_DATA,
                                                  std::make_shared<treesheets::Grid>(xs, ys));
    root->grid->InitCells();
    return root;
}

void RemoveIfExists(const wxString &filename) {
    if (!filename.empty() && ::wxFileExists(filename)) ::wxRemoveFile(filename);
}

std::unique_ptr<treesheets::Cell> RoundTripCell(const treesheets::Cell &source,
                                                treesheets::Cell *selected,
                                                treesheets::Cell **loaded_selected,
                                                int *loaded_cells,
                                                int *loaded_text_bytes) {
    wxMemoryOutputStream output;
    wxDataOutputStream data_output(output);
    source.Save(data_output, selected);

    std::vector<char> bytes(output.GetSize());
    output.CopyTo(bytes.data(), bytes.size());

    wxMemoryInputStream input(bytes.data(), bytes.size());
    wxDataInputStream data_input(input);

    treesheets::sys->versionlastloaded = TS_VERSION;
    treesheets::sys->fakelasteditonload = wxDateTime::Now().GetValue();

    treesheets::Cell *initial_selected = nullptr;
    int num_cells = 0;
    int text_bytes = 0;
    std::unique_ptr<treesheets::Cell> loaded(
        treesheets::Cell::LoadWhich(data_input, nullptr, num_cells, text_bytes, initial_selected));

    if (loaded_selected) *loaded_selected = initial_selected;
    if (loaded_cells) *loaded_cells = num_cells;
    if (loaded_text_bytes) *loaded_text_bytes = text_bytes;
    return loaded;
}

void TestCellRoundTripPreservesGridFormattingAndSelectionMarker() {
    auto root = MakeRoot(3, 2);
    auto grid = root->grid.get();

    root->text.t = "root";
    root->text.relsize = 2;
    root->text.stylebits = STYLE_BOLD | STYLE_UNDERLINE;
    root->cellcolor = 0xCCDCE2;
    root->textcolor = 0x123456;
    root->bordercolor = 0x654321;
    root->drawstyle = treesheets::DS_BLOBLINE;
    root->note = "root note";
    root->verticaltextandgrid = false;

    grid->bordercolor = 0x334455;
    grid->user_grid_outer_spacing = 5;
    grid->folded = true;
    grid->colwidths[0] = 31;
    grid->colwidths[1] = 47;
    grid->colwidths[2] = 59;
    grid->HBorder(0, 0).width = 4;
    grid->HBorder(0, 0).color = 0x112233;
    grid->VBorder(3, 1).width = 2;
    grid->VBorder(3, 1).color = 0x445566;

    grid->C(0, 0)->text.t = "master";
    grid->C(0, 0)->mergexs = 2;
    grid->C(0, 0)->mergeys = 1;
    grid->C(1, 0)->text.t = "covered";
    grid->C(2, 1)->text.t = "selected";
    grid->C(2, 1)->celltype = treesheets::CT_CODE;
    grid->RepairMergedCells();

    treesheets::Cell *loaded_selected = nullptr;
    int loaded_cells = 0;
    int loaded_text_bytes = 0;
    auto loaded = RoundTripCell(*root, grid->C(2, 1).get(), &loaded_selected, &loaded_cells,
                                &loaded_text_bytes);

    CHECK(loaded != nullptr);
    CHECK_EQ(loaded_cells, 7);
    CHECK(loaded_text_bytes >= 18);
    CHECK(loaded_selected != nullptr);
    CHECK_EQ(loaded_selected->text.t, wxString("selected"));
    CHECK_EQ(loaded_selected->celltype, treesheets::CT_CODE);

    auto loaded_grid = loaded->grid.get();
    CHECK(loaded_grid != nullptr);
    CHECK_EQ(loaded->text.t, wxString("root"));
    CHECK_EQ(loaded->text.relsize, 2);
    CHECK_EQ(loaded->text.stylebits, STYLE_BOLD | STYLE_UNDERLINE);
    CHECK_EQ(loaded->cellcolor, 0xCCDCE2u);
    CHECK_EQ(loaded->textcolor, 0x123456u);
    CHECK_EQ(loaded->bordercolor, 0x654321u);
    CHECK_EQ(loaded->drawstyle, treesheets::DS_BLOBLINE);
    CHECK_EQ(loaded->note, wxString("root note"));
    CHECK(!loaded->verticaltextandgrid);

    CHECK_EQ(loaded_grid->xs, 3);
    CHECK_EQ(loaded_grid->ys, 2);
    CHECK_EQ(loaded_grid->bordercolor, 0x334455);
    CHECK_EQ(loaded_grid->user_grid_outer_spacing, 5);
    CHECK(loaded_grid->folded);
    CHECK_EQ(loaded_grid->colwidths[0], 31);
    CHECK_EQ(loaded_grid->colwidths[1], 47);
    CHECK_EQ(loaded_grid->colwidths[2], 59);
    CHECK_EQ(loaded_grid->HBorder(0, 0).width, 4);
    CHECK_EQ(loaded_grid->HBorder(0, 0).color, 0x112233u);
    CHECK_EQ(loaded_grid->VBorder(3, 1).width, 2);
    CHECK_EQ(loaded_grid->VBorder(3, 1).color, 0x445566u);

    CHECK_EQ(loaded_grid->C(0, 0)->text.t, wxString("master"));
    CHECK_EQ(loaded_grid->C(0, 0)->mergexs, 2);
    CHECK_EQ(loaded_grid->C(0, 0)->mergeys, 1);
    CHECK(loaded_grid->C(1, 0)->Covered());
    CHECK(!loaded_grid->C(1, 0)->HasText());
}

void TestCellRoundTripAllowsNoSelectionMarker() {
    auto root = MakeRoot(1, 1);
    root->grid->C(0, 0)->text.t = "only cell";

    treesheets::Cell *loaded_selected = nullptr;
    int loaded_cells = 0;
    auto loaded = RoundTripCell(*root, nullptr, &loaded_selected, &loaded_cells, nullptr);

    CHECK(loaded != nullptr);
    CHECK_EQ(loaded_cells, 2);
    CHECK(loaded_selected == nullptr);
    CHECK_EQ(loaded->grid->C(0, 0)->text.t, wxString("only cell"));
}

void TestCSVImportHandlesQuotedFieldsAndMultilineValues() {
    auto root = MakeRoot(1, 4);
    auto grid = root->grid.get();
    wxArrayString lines;
    lines.Add("\"alpha,beta\",gamma");
    lines.Add("\"line one");
    lines.Add("line two\",tail");
    lines.Add("plain,last");

    grid->CSVImport(lines, ",");

    CHECK_EQ(grid->xs, 2);
    CHECK_EQ(grid->ys, 3);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("alpha,beta"));
    CHECK_EQ(grid->C(1, 0)->text.t, wxString("gamma"));
    CHECK_EQ(grid->C(0, 1)->text.t, wxString("line one") + LINE_SEPARATOR + "line two");
    CHECK_EQ(grid->C(1, 1)->text.t, wxString("tail"));
    CHECK_EQ(grid->C(0, 2)->text.t, wxString("plain"));
    CHECK_EQ(grid->C(1, 2)->text.t, wxString("last"));
    CHECK_EQ(static_cast<int>(grid->vborderstyles.size()), 9);
    CHECK_EQ(static_cast<int>(grid->hborderstyles.size()), 8);
}

void TestIndentedTextImportCreatesNestedGrids() {
    auto root = MakeRoot(1, 4);
    auto grid = root->grid.get();
    wxArrayString lines;
    lines.Add("parent");
    lines.Add(" child");
    lines.Add("  grandchild");
    lines.Add("sibling");

    treesheets::sys->FillRows(grid, lines, treesheets::sys->CountCol(lines[0]), 0, 0);

    CHECK_EQ(grid->C(0, 0)->text.t, wxString("parent"));
    CHECK(grid->C(0, 0)->grid != nullptr);
    CHECK_EQ(grid->C(0, 0)->grid->C(0, 0)->text.t, wxString("child"));
    CHECK(grid->C(0, 0)->grid->C(0, 0)->grid != nullptr);
    CHECK_EQ(grid->C(0, 0)->grid->C(0, 0)->grid->C(0, 0)->text.t,
             wxString("grandchild"));
    CHECK_EQ(grid->C(0, 1)->text.t, wxString("sibling"));
}

void TestDocumentSaveDBWritesReadableTreeSheetsFile() {
    auto root = MakeRoot(2, 2);
    auto selected_cell = root->grid->C(1, 1).get();
    root->grid->C(0, 0)->text.t = "saved";
    selected_cell->text.t = "selected";
    selected_cell->cellcolor = 0xBADA55;

    wxString filename = wxFileName::CreateTempFileName("treesheets-save-test");
    RemoveIfExists(filename);
    filename += ".cts";
    RemoveIfExists(filename);
    RemoveIfExists(treesheets::sys->NewName(filename));
    RemoveIfExists(treesheets::sys->BakName(filename));

    treesheets::Document doc;
    doc.InitWith(std::move(root), filename, selected_cell, 1, 1);
    doc.modified = true;

    bool success = false;
    auto message = doc.SaveDB(&success);

    CHECK(success);
    CHECK(message.StartsWith("Saved "));
    CHECK(::wxFileExists(filename));
    CHECK(!doc.modified);
    CHECK_EQ(doc.undolistsizeatfullsave, 0);

    {
        wxFFileInputStream file_input(filename);
        CHECK(file_input.IsOk());

        char magic[4] {};
        file_input.Read(magic, 4);
        CHECK(std::memcmp(magic, "TSFF", 4) == 0);

        char version = 0;
        file_input.Read(&version, 1);
        CHECK_EQ(static_cast<int>(version), TS_VERSION);

        wxDataInputStream header_input(file_input);
        CHECK_EQ(header_input.Read8(), 1);
        CHECK_EQ(header_input.Read8(), 1);
        CHECK_EQ(header_input.Read8(), 0);

        char block = 0;
        file_input.Read(&block, 1);
        CHECK_EQ(block, 'D');

        wxZlibInputStream zlib_input(file_input);
        wxDataInputStream data_input(zlib_input);
        treesheets::sys->versionlastloaded = version;
        treesheets::sys->fakelasteditonload = wxDateTime::Now().GetValue();

        treesheets::Cell *loaded_selected = nullptr;
        int loaded_cells = 0;
        int loaded_text_bytes = 0;
        std::unique_ptr<treesheets::Cell> loaded(treesheets::Cell::LoadWhich(
            data_input, nullptr, loaded_cells, loaded_text_bytes, loaded_selected));

        CHECK(loaded != nullptr);
        CHECK_EQ(loaded_cells, 5);
        CHECK(loaded_selected != nullptr);
        CHECK_EQ(loaded_selected->text.t, wxString("selected"));
        CHECK_EQ(loaded_selected->cellcolor, 0xBADA55u);
        CHECK_EQ(loaded->grid->C(0, 0)->text.t, wxString("saved"));
    }

    RemoveIfExists(filename);
    RemoveIfExists(treesheets::sys->NewName(filename));
    RemoveIfExists(treesheets::sys->BakName(filename));
}

}  // namespace

int main() {
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "failed to initialize wxWidgets\n";
        return 1;
    }

    {
        treesheets::System system(true);
        treesheets::sys = &system;

        TestCellRoundTripPreservesGridFormattingAndSelectionMarker();
        TestCellRoundTripAllowsNoSelectionMarker();
        TestCSVImportHandlesQuotedFieldsAndMultilineValues();
        TestIndentedTextImportCreatesNestedGrids();
        TestDocumentSaveDBWritesReadableTreeSheetsFile();

        treesheets::sys = nullptr;
    }

    if (failures) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "serialization tests passed\n";
    return 0;
}
