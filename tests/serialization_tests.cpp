#include "test_helpers.h"

namespace {

using namespace test_helpers;

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

void RemoveSaveArtifacts(const wxString &filename) {
    RemoveIfExists(filename);
    RemoveIfExists(treesheets::sys->NewName(filename));
    RemoveIfExists(treesheets::sys->BakName(filename));
    RemoveIfExists(treesheets::sys->TmpName(filename));
    RemoveIfExists(treesheets::sys->NewName(treesheets::sys->TmpName(filename)));
}

void RemoveDirectoryIfExists(const wxString &dirname) {
    if (!dirname.empty() && ::wxDirExists(dirname)) ::wxRmdir(dirname);
}

std::vector<char> ReadFileBytes(const wxString &filename) {
    wxFFileInputStream input(filename);
    CHECK(input.IsOk());
    auto length = input.GetLength();
    CHECK(length >= 0);
    std::vector<char> bytes(static_cast<size_t>(length));
    if (!bytes.empty()) input.Read(bytes.data(), bytes.size());
    CHECK(input.IsOk());
    return bytes;
}

void ExpectReadDBRejects(const wxString &filename) {
    treesheets::System::LoadedDB loaded;
    auto message = treesheets::sys->ReadDB(filename, loaded);
    CHECK(!message.IsEmpty());
    CHECK(loaded.root == nullptr);
    RemoveIfExists(filename);
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
    auto lines = ReadFixtureLines("csv/quoted_multiline.csv");

    grid->CSVImport(lines, ",");

    CHECK_EQ(grid->xs, 2);
    CHECK_EQ(grid->ys, 5);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("alpha,beta"));
    CHECK_EQ(grid->C(1, 0)->text.t, wxString("gamma"));
    CHECK_EQ(grid->C(0, 1)->text.t, wxString("line one") + LINE_SEPARATOR + "line two");
    CHECK_EQ(grid->C(1, 1)->text.t, wxString("tail"));
    CHECK_EQ(grid->C(0, 2)->text.t, wxString("plain"));
    CHECK_EQ(grid->C(1, 2)->text.t, wxString("last"));
    CHECK_EQ(grid->C(0, 3)->text.t, wxString("trailing"));
    CHECK_EQ(grid->C(1, 3)->text.t, wxString(""));
    CHECK_EQ(grid->C(0, 4)->text.t, wxString(""));
    CHECK_EQ(grid->C(1, 4)->text.t, wxString("leading"));
    CHECK_EQ(static_cast<int>(grid->vborderstyles.size()), 15);
    CHECK_EQ(static_cast<int>(grid->hborderstyles.size()), 12);
}

void TestCSVImportSupportsAlternateSeparatorsAndEscapedQuotes() {
    {
        auto root = MakeRoot(1, 1);
        auto grid = root->grid.get();
        auto lines = ReadFixtureLines("csv/alternate_separators.tsv");

        grid->CSVImport(lines, "\t");

        CHECK_EQ(grid->xs, 3);
        CHECK_EQ(grid->ys, 1);
        CHECK_EQ(grid->C(0, 0)->text.t, wxString("left"));
        CHECK_EQ(grid->C(1, 0)->text.t, wxString("middle"));
        CHECK_EQ(grid->C(2, 0)->text.t, wxString("right"));
    }

    {
        auto root = MakeRoot(1, 1);
        auto grid = root->grid.get();
        wxArrayString lines;
        lines.Add("alpha;\"beta;semi\";gamma");

        grid->CSVImport(lines, ";");

        CHECK_EQ(grid->xs, 3);
        CHECK_EQ(grid->ys, 1);
        CHECK_EQ(grid->C(0, 0)->text.t, wxString("alpha"));
        CHECK_EQ(grid->C(1, 0)->text.t, wxString("beta;semi"));
        CHECK_EQ(grid->C(2, 0)->text.t, wxString("gamma"));
    }

    {
        auto root = MakeRoot(1, 1);
        auto grid = root->grid.get();
        auto lines = ReadFixtureLines("csv/custom_separator.txt");

        grid->CSVImport(lines, "||");

        CHECK_EQ(grid->xs, 3);
        CHECK_EQ(grid->ys, 1);
        CHECK_EQ(grid->C(0, 0)->text.t, wxString("say \"hi\""));
        CHECK_EQ(grid->C(1, 0)->text.t, wxString("  padded  "));
        CHECK_EQ(grid->C(2, 0)->text.t, wxString("two||parts"));
    }
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

void TestFillXMLImportsGridSettingsAndCellStyles() {
    auto filename = TempName("treesheets-xml-import-test", ".xml");
    WriteTextFile(
        filename,
        "<grid folded=\"1\" bordercolor=\"0x112233\" outerspacing=\"6\">"
        "<row>"
        "<cell colorbg=\"0x010203\" colorfg=\"0x040506\" colorborder=\"0x070809\" "
        "relsize=\"2\" stylebits=\"3\" type=\"2\">Alpha</cell>"
        "<cell>Beta</cell>"
        "</row>"
        "<row><cell>Gamma</cell><cell>Delta</cell></row>"
        "</grid>");

    wxXmlDocument xml;
    CHECK(xml.Load(filename));

    auto root = MakeRoot(1, 1);
    auto cell = root->grid->C(0, 0).get();
    treesheets::sys->FillXML(cell, xml.GetRoot(), false);

    CHECK(cell->grid != nullptr);
    CHECK_EQ(cell->grid->xs, 2);
    CHECK_EQ(cell->grid->ys, 2);
    CHECK(cell->grid->folded);
    CHECK_EQ(cell->grid->bordercolor, 0x112233);
    CHECK_EQ(cell->grid->user_grid_outer_spacing, 6);

    auto first = cell->grid->C(0, 0).get();
    CHECK_EQ(first->text.t, wxString("Alpha"));
    CHECK_EQ(first->cellcolor, 0x010203u);
    CHECK_EQ(first->textcolor, 0x040506u);
    CHECK_EQ(first->bordercolor, 0x070809u);
    CHECK_EQ(first->text.relsize, -2);
    CHECK_EQ(first->text.stylebits, 3);
    CHECK_EQ(first->celltype, treesheets::CT_VARD);
    CHECK_EQ(cell->grid->C(1, 0)->text.t, wxString("Beta"));
    CHECK_EQ(cell->grid->C(0, 1)->text.t, wxString("Gamma"));
    CHECK_EQ(cell->grid->C(1, 1)->text.t, wxString("Delta"));

    RemoveIfExists(filename);
}

void TestDocumentSaveDBWritesReadableTreeSheetsFile() {
    auto root = MakeRoot(2, 2);
    auto selected_cell = root->grid->C(1, 1).get();
    root->grid->C(0, 0)->text.t = "saved";
    selected_cell->text.t = "selected";
    selected_cell->cellcolor = 0xBADA55;

    wxString filename = TempCtsName("treesheets-save-test");
    RemoveIfExists(treesheets::sys->NewName(filename));
    RemoveIfExists(treesheets::sys->BakName(filename));

    treesheets::Document doc;
    doc.InitWith(std::move(root), filename, selected_cell, 1, 1);
    doc.tags["important"] = 0x123456;
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

    treesheets::Document loaded_doc;
    int loaded_doc_cells = 0;
    int loaded_doc_text_bytes = 0;
    int loaded_doc_zoom = -1;
    auto load_message = treesheets::sys->LoadDBIntoDocument(
        loaded_doc, filename, &loaded_doc_cells, &loaded_doc_text_bytes, &loaded_doc_zoom);

    CHECK(load_message.IsEmpty());
    CHECK_EQ(loaded_doc.filename, filename);
    CHECK_EQ(loaded_doc_cells, 5);
    CHECK(loaded_doc_text_bytes >= 13);
    CHECK_EQ(loaded_doc_zoom, 0);
    CHECK_EQ(loaded_doc.root->grid->C(0, 0)->text.t, wxString("saved"));
    CHECK_EQ(loaded_doc.selected.GetCell()->text.t, wxString("selected"));
    CHECK_EQ(loaded_doc.selected.GetCell()->cellcolor, 0xBADA55u);
    CHECK_EQ(loaded_doc.tags["important"], 0x123456u);

    RemoveIfExists(filename);
    RemoveIfExists(treesheets::sys->NewName(filename));
    RemoveIfExists(treesheets::sys->BakName(filename));
}

void TestDocumentSaveDBClearsSuccessOnFailure() {
    auto root = MakeRoot(1, 1);
    auto initial = root->grid->C(0, 0).get();
    treesheets::Document doc;
    doc.InitWith(std::move(root), "", initial, 1, 1);

    bool success = true;
    auto message = doc.SaveDB(&success);

    CHECK(!success);
    CHECK(!message.IsEmpty());
}

void TestDocumentSaveDBReportsFilesystemFailures() {
    {
        auto filename = TempCtsName("treesheets-save-write-failure-test");
        RemoveSaveArtifacts(filename);
        auto blocker = treesheets::sys->NewName(filename);
        RemoveDirectoryIfExists(blocker);
        CHECK(::wxMkdir(blocker));

        auto root = MakeRoot(1, 1);
        auto initial = root->grid->C(0, 0).get();
        treesheets::Document doc;
        doc.InitWith(std::move(root), filename, initial, 1, 1);

        bool success = true;
        auto message = doc.SaveDB(&success);

        CHECK(!success);
        CHECK_EQ(message, wxString("Error writing to file."));
        CHECK(!::wxFileExists(filename));

        RemoveDirectoryIfExists(blocker);
        RemoveSaveArtifacts(filename);
    }

    {
        auto filename = TempCtsName("treesheets-temp-save-write-failure-test");
        RemoveSaveArtifacts(filename);
        auto blocker = treesheets::sys->NewName(treesheets::sys->TmpName(filename));
        RemoveDirectoryIfExists(blocker);
        CHECK(::wxMkdir(blocker));

        auto root = MakeRoot(1, 1);
        auto initial = root->grid->C(0, 0).get();
        treesheets::Document doc;
        doc.InitWith(std::move(root), filename, initial, 1, 1);

        bool success = true;
        auto message = doc.SaveDB(&success, true);

        CHECK(!success);
        CHECK_EQ(message, wxString("Error writing to file."));
        CHECK(!::wxFileExists(treesheets::sys->TmpName(filename)));

        RemoveDirectoryIfExists(blocker);
        RemoveSaveArtifacts(filename);
    }

    {
        auto filename = TempCtsName("treesheets-save-rename-failure-test");
        RemoveSaveArtifacts(filename);
        RemoveDirectoryIfExists(filename);
        CHECK(::wxMkdir(filename));

        auto root = MakeRoot(1, 1);
        auto initial = root->grid->C(0, 0).get();
        treesheets::Document doc;
        doc.InitWith(std::move(root), filename, initial, 1, 1);

        bool success = true;
        auto message = doc.SaveDB(&success);

        CHECK(!success);
        CHECK_EQ(message, wxString("Error renaming temporary file."));
        CHECK(::wxFileExists(treesheets::sys->NewName(filename)));

        RemoveIfExists(treesheets::sys->NewName(filename));
        RemoveDirectoryIfExists(filename);
        RemoveSaveArtifacts(filename);
    }
}

void TestAutoSaveWritesReadableTemporaryFile() {
    auto filename = TempCtsName("treesheets-autosave-test");
    RemoveSaveArtifacts(filename);

    auto root = MakeRoot(1, 1);
    auto initial = root->grid->C(0, 0).get();
    initial->text.t = "autosaved text";

    treesheets::Document doc;
    doc.InitWith(std::move(root), filename, initial, 1, 1);
    doc.modified = true;
    doc.lastmodsinceautosave = wxGetLocalTime() - 61;
    doc.tmpsavesuccess = true;

    doc.AutoSave(false, -1);

    auto tmpname = treesheets::sys->TmpName(filename);
    CHECK(::wxFileExists(tmpname));
    CHECK(doc.modified);
    CHECK(doc.tmpsavesuccess);
    CHECK_EQ(doc.lastmodsinceautosave, 0);

    treesheets::Document recovered;
    auto message = treesheets::sys->LoadDBIntoDocument(recovered, tmpname);

    CHECK(message.IsEmpty());
    CHECK_EQ(recovered.root->grid->C(0, 0)->text.t, wxString("autosaved text"));

    RemoveSaveArtifacts(filename);
}

void TestNormalSaveRemovesAutosaveTemporaryFile() {
    auto filename = TempCtsName("treesheets-autosave-cleanup-test");
    RemoveSaveArtifacts(filename);

    auto root = MakeRoot(1, 1);
    auto initial = root->grid->C(0, 0).get();
    initial->text.t = "final text";

    treesheets::Document doc;
    doc.InitWith(std::move(root), filename, initial, 1, 1);
    doc.modified = true;
    WriteBytes(treesheets::sys->TmpName(filename), {'s', 't', 'a', 'l', 'e'});

    bool success = false;
    auto message = doc.SaveDB(&success);

    CHECK(success);
    CHECK(message.StartsWith("Saved "));
    CHECK(::wxFileExists(filename));
    CHECK(!::wxFileExists(treesheets::sys->TmpName(filename)));
    CHECK(!doc.modified);

    RemoveSaveArtifacts(filename);
}

void TestExternalModificationPollingState() {
    auto filename = TempName("treesheets-external-modification-test", ".txt");
    RemoveIfExists(filename);
    WriteTextFile(filename, "first");

    treesheets::Document doc;
    InitDocument(doc, MakeRoot(1, 1));
    doc.ChangeFileName(filename, false);

    CHECK(!doc.lastmodificationtime.IsValid());
    CHECK(!treesheets::sys->DocumentChangedOnDisk(&doc));
    CHECK(doc.lastmodificationtime.IsValid());
    CHECK(!treesheets::sys->DocumentChangedOnDisk(&doc));

    doc.lastmodificationtime = wxDateTime(static_cast<time_t>(0));
    wxDateTime detected;
    CHECK(treesheets::sys->DocumentChangedOnDisk(&doc, &detected));
    CHECK(detected.IsValid());

    RemoveIfExists(filename);
    CHECK(!treesheets::sys->DocumentChangedOnDisk(&doc));
}

void TestAutoSaveKeepsMultipleDocumentsIsolated() {
    auto filename_a = TempCtsName("treesheets-autosave-a-test");
    auto filename_b = TempCtsName("treesheets-autosave-b-test");
    RemoveSaveArtifacts(filename_a);
    RemoveSaveArtifacts(filename_b);

    treesheets::Document doc_a;
    auto root_a = MakeRoot(1, 1);
    auto initial_a = root_a->grid->C(0, 0).get();
    initial_a->text.t = "document a";
    doc_a.InitWith(std::move(root_a), filename_a, initial_a, 1, 1);
    doc_a.modified = true;
    doc_a.lastmodsinceautosave = wxGetLocalTime() - 61;

    treesheets::Document doc_b;
    auto root_b = MakeRoot(1, 1);
    auto initial_b = root_b->grid->C(0, 0).get();
    initial_b->text.t = "document b";
    doc_b.InitWith(std::move(root_b), filename_b, initial_b, 1, 1);
    doc_b.modified = true;
    doc_b.lastmodsinceautosave = wxGetLocalTime() - 61;

    doc_a.AutoSave(false, -1);
    doc_b.AutoSave(false, -1);

    treesheets::Document recovered_a;
    treesheets::Document recovered_b;
    CHECK(treesheets::sys->LoadDBIntoDocument(recovered_a, treesheets::sys->TmpName(filename_a))
              .IsEmpty());
    CHECK(treesheets::sys->LoadDBIntoDocument(recovered_b, treesheets::sys->TmpName(filename_b))
              .IsEmpty());
    CHECK_EQ(recovered_a.root->grid->C(0, 0)->text.t, wxString("document a"));
    CHECK_EQ(recovered_b.root->grid->C(0, 0)->text.t, wxString("document b"));

    RemoveSaveArtifacts(filename_a);
    RemoveSaveArtifacts(filename_b);
}

std::vector<uint8_t> SampleImageBytes() {
    return {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
}

void TestSaveDBPersistsExpectedLoadStats() {
    auto filename = TempCtsName("treesheets-save-stats-test");
    RemoveSaveArtifacts(filename);
    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();

    auto root = MakeRoot(2, 2);
    root->text.t = "root";
    root->grid->C(0, 0)->text.t = "alpha";
    root->grid->C(1, 0)->text.t = "beta";
    root->grid->C(0, 1)->text.t = "gamma";
    auto image_cell = root->grid->C(1, 1).get();

    treesheets::Document doc;
    doc.InitWith(std::move(root), filename, image_cell, 1, 1);
    auto image_data = SampleImageBytes();
    doc.SetImageBM(image_cell, std::vector<uint8_t>(image_data), 'I', 1.25);

    bool success = false;
    CHECK(doc.SaveDB(&success).StartsWith("Saved "));
    CHECK(success);

    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();
    treesheets::Document loaded_doc;
    int numcells = -1;
    int textbytes = -1;

    CHECK(treesheets::sys->LoadDBIntoDocument(loaded_doc, filename, &numcells, &textbytes)
              .IsEmpty());
    CHECK_EQ(numcells, 5);
    CHECK_EQ(textbytes, 18);
    CHECK_EQ(treesheets::sys->imagelist.size(), static_cast<size_t>(1));
    CHECK(loaded_doc.root->grid->C(1, 1)->text.image != nullptr);
    CHECK(loaded_doc.root->grid->C(1, 1)->text.image->data == image_data);

    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();
    RemoveSaveArtifacts(filename);
}

void WriteLegacyText(wxDataOutputStream &output, const wxString &text, int stylebits = 0) {
    output.WriteString(text);
    output.Write32(0);
    output.Write32(-1);
    output.Write32(stylebits);
    wxLongLong lastedit = wxDateTime::Now().GetValue();
    output.Write64(&lastedit, 1);
}

void WriteLegacyCellV20(wxDataOutputStream &output, int flags, const wxString &text = wxEmptyString,
                        bool selected = false) {
    output.Write8(treesheets::CT_DATA);
    output.Write32(g_cellcolor_default);
    output.Write32(g_textcolor_default);
    output.Write8(treesheets::DS_GRID);
    output.Write8(flags | (selected ? TS_SELECTION_MASK : 0));
    if (flags == TS_TEXT || flags == TS_BOTH) WriteLegacyText(output, text, STYLE_BOLD);
}

void WriteLegacyCellV27(wxDataOutputStream &output, int flags, const wxString &text,
                        int mergexs, int mergeys, bool selected = false) {
    output.Write8(treesheets::CT_DATA);
    output.Write32(g_cellcolor_default);
    output.Write32(g_textcolor_default);
    output.Write32(0x112233);
    output.Write8(treesheets::DS_GRID);
    output.WriteString("legacy note");
    output.Write32(mergexs);
    output.Write32(mergeys);
    output.Write8(flags | (selected ? TS_SELECTION_MASK : 0));
    if (flags == TS_TEXT || flags == TS_BOTH) WriteLegacyText(output, text, STYLE_ITALIC);
}

void WriteLegacyGridHeader(wxDataOutputStream &output, int xs, int ys) {
    output.Write32(xs);
    output.Write32(ys);
    output.Write32(0x445566);
    output.Write32(7);
    output.Write8(1);
    output.Write8(0);
    for (int x = 0; x < xs; x++) output.Write32(40 + x);
}

void WriteLegacyVersion20File(const wxString &filename) {
    wxFFileOutputStream file(filename);
    CHECK(file.IsOk());
    file.Write("TSFF", 4);
    char version = 20;
    file.Write(&version, 1);
    file.Write("D", 1);

    {
        wxZlibOutputStream zlib(file, 9);
        wxDataOutputStream output(zlib);
        WriteLegacyCellV20(output, TS_GRID);
        WriteLegacyGridHeader(output, 1, 1);
        WriteLegacyCellV20(output, TS_TEXT, "legacy v20", true);
        output.WriteString(wxEmptyString);
    }

    CHECK(file.IsOk());
}

void WriteLegacyVersion27File(const wxString &filename) {
    wxFFileOutputStream file(filename);
    CHECK(file.IsOk());
    file.Write("TSFF", 4);
    char version = 27;
    file.Write(&version, 1);
    file.PutC(2);
    file.PutC(2);
    file.PutC(0);
    file.Write("D", 1);

    {
        wxZlibOutputStream zlib(file, 9);
        wxDataOutputStream output(zlib);
        WriteLegacyCellV27(output, TS_GRID, wxEmptyString, 1, 1);
        WriteLegacyGridHeader(output, 2, 2);
        WriteLegacyCellV27(output, TS_TEXT, "legacy merged", 2, 2, true);
        WriteLegacyCellV27(output, TS_NEITHER, wxEmptyString, 0, 0);
        WriteLegacyCellV27(output, TS_NEITHER, wxEmptyString, 0, 0);
        WriteLegacyCellV27(output, TS_NEITHER, wxEmptyString, 0, 0);
        output.WriteString(wxEmptyString);
    }

    CHECK(file.IsOk());
}

void TestImageRoundTripPreservesDataScaleAndSharedReference() {
    auto filename = TempCtsName("treesheets-image-roundtrip-test");
    RemoveSaveArtifacts(filename);
    treesheets::sys->imagelist.clear();

    auto image_data = SampleImageBytes();
    {
        auto root = MakeRoot(2, 1);
        auto first = root->grid->C(0, 0).get();
        auto second = root->grid->C(1, 0).get();
        treesheets::Document doc;
        doc.InitWith(std::move(root), filename, first, 1, 1);
        doc.SetImageBM(first, std::vector<uint8_t>(image_data), 'I', 2.5);
        doc.SetImageBM(second, std::vector<uint8_t>(image_data), 'I', 2.5);
        CHECK_EQ(treesheets::sys->imagelist.size(), static_cast<size_t>(1));
        CHECK(first->text.image == second->text.image);

        bool success = false;
        CHECK(doc.SaveDB(&success).StartsWith("Saved "));
        CHECK(success);
    }

    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();

    treesheets::Document loaded_doc;
    auto message = treesheets::sys->LoadDBIntoDocument(loaded_doc, filename);

    CHECK(message.IsEmpty());
    CHECK_EQ(treesheets::sys->imagelist.size(), static_cast<size_t>(1));
    auto first_image = loaded_doc.root->grid->C(0, 0)->text.image;
    auto second_image = loaded_doc.root->grid->C(1, 0)->text.image;
    CHECK(first_image != nullptr);
    CHECK(first_image == second_image);
    CHECK(first_image->data == image_data);
    CHECK_EQ(first_image->type, 'I');
    CHECK_EQ(first_image->display_scale, 2.5);

    RemoveSaveArtifacts(filename);
}

void TestSaveDBSkipsUnreferencedImages() {
    auto filename = TempCtsName("treesheets-unused-image-test");
    RemoveSaveArtifacts(filename);
    treesheets::sys->imagelist.clear();

    auto used_data = SampleImageBytes();
    auto unused_data = std::vector<uint8_t> {'u', 'n', 'u', 's', 'e', 'd'};
    {
        auto root = MakeRoot(1, 1);
        auto cell = root->grid->C(0, 0).get();
        treesheets::Document doc;
        doc.InitWith(std::move(root), filename, cell, 1, 1);
        doc.SetImageBM(cell, std::vector<uint8_t>(used_data), 'I', 1.0);
        treesheets::sys->AddImageToList(1.0, std::move(unused_data), 'I');
        CHECK_EQ(treesheets::sys->imagelist.size(), static_cast<size_t>(2));

        bool success = false;
        CHECK(doc.SaveDB(&success).StartsWith("Saved "));
        CHECK(success);
    }

    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();

    treesheets::Document loaded_doc;
    auto message = treesheets::sys->LoadDBIntoDocument(loaded_doc, filename);

    CHECK(message.IsEmpty());
    CHECK_EQ(treesheets::sys->imagelist.size(), static_cast<size_t>(1));
    CHECK(loaded_doc.root->grid->C(0, 0)->text.image != nullptr);
    CHECK(loaded_doc.root->grid->C(0, 0)->text.image->data == used_data);

    RemoveSaveArtifacts(filename);
}

void TestLoadImageIntoCellRejectsMissingAndCorruptFiles() {
    treesheets::sys->imagelist.clear();
    auto root = MakeRoot(1, 1);
    auto cell = root->grid->C(0, 0).get();
    treesheets::Document doc;
    InitDocument(doc, std::move(root));

    CHECK(!doc.LoadImageIntoCell(TempName("treesheets-missing-image-test", ".png"), cell, 1.0));
    CHECK(cell->text.image == nullptr);

    auto corrupt = TempName("treesheets-corrupt-image-test", ".png");
    WriteBytes(corrupt, {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'});

    CHECK(!doc.LoadImageIntoCell(corrupt, cell, 1.0));
    CHECK(cell->text.image == nullptr);
    CHECK(treesheets::sys->imagelist.empty());

    RemoveIfExists(corrupt);
}

void TestReadDBLoadsLegacyVersion20File() {
    auto filename = TempCtsName("treesheets-legacy-v20-test");
    RemoveSaveArtifacts(filename);
    WriteLegacyVersion20File(filename);

    treesheets::Document doc;
    int numcells = 0;
    int textbytes = 0;
    int zoomlevel = -1;
    auto message = treesheets::sys->LoadDBIntoDocument(doc, filename, &numcells, &textbytes,
                                                       &zoomlevel);

    CHECK(message.IsEmpty());
    CHECK_EQ(numcells, 2);
    CHECK(textbytes >= 10);
    CHECK_EQ(zoomlevel, 0);
    CHECK_EQ(doc.selected.GetCell()->text.t, wxString("legacy v20"));
    CHECK_EQ(doc.root->grid->xs, 1);
    CHECK_EQ(doc.root->grid->ys, 1);
    CHECK_EQ(doc.root->grid->bordercolor, 0x445566);
    CHECK_EQ(doc.root->grid->user_grid_outer_spacing, 7);
    CHECK_EQ(doc.root->grid->colwidths[0], 40);
    CHECK(doc.root->grid->C(0, 0)->text.stylebits & STYLE_BOLD);

    RemoveSaveArtifacts(filename);
}

void TestReadDBLoadsLegacyVersion27FileAndInitializesBorders() {
    auto filename = TempCtsName("treesheets-legacy-v27-test");
    RemoveSaveArtifacts(filename);
    WriteLegacyVersion27File(filename);

    treesheets::Document doc;
    int zoomlevel = -1;
    auto message = treesheets::sys->LoadDBIntoDocument(doc, filename, nullptr, nullptr, &zoomlevel);

    CHECK(message.IsEmpty());
    CHECK_EQ(zoomlevel, 0);
    CHECK_EQ(doc.selected.xs, 2);
    CHECK_EQ(doc.selected.ys, 2);
    auto grid = doc.root->grid.get();
    auto master = grid->C(0, 0).get();
    CHECK_EQ(grid->xs, 2);
    CHECK_EQ(grid->ys, 2);
    CHECK_EQ(static_cast<int>(grid->vborderstyles.size()), 6);
    CHECK_EQ(static_cast<int>(grid->hborderstyles.size()), 6);
    CHECK_EQ(master->text.t, wxString("legacy merged"));
    CHECK_EQ(master->bordercolor, 0x112233u);
    CHECK_EQ(master->note, wxString("legacy note"));
    CHECK(master->text.stylebits & STYLE_ITALIC);
    CHECK_EQ(master->mergexs, 2);
    CHECK_EQ(master->mergeys, 2);
    CHECK(grid->C(1, 0)->Covered());
    CHECK(grid->C(0, 1)->Covered());
    CHECK(grid->C(1, 1)->Covered());

    RemoveSaveArtifacts(filename);
}

void TestLargeDocumentSaveLoadStress() {
    constexpr int xs = 96;
    constexpr int ys = 64;
    auto filename = TempCtsName("treesheets-large-document-test");
    RemoveSaveArtifacts(filename);

    auto root = MakeRoot(xs, ys);
    auto grid = root->grid.get();
    for (int y = 0; y < ys; y++) {
        for (int x = 0; x < xs; x++) {
            auto cell = grid->C(x, y).get();
            cell->text.t = wxString::Format("cell %03d,%03d alpha beta gamma", x, y);
            if ((x + y) % 17 == 0) cell->text.stylebits = STYLE_BOLD | STYLE_ITALIC;
            if ((x * 3 + y) % 29 == 0) cell->cellcolor = 0xDDEEFF;
        }
    }
    for (int x = 0; x < xs; x += 8) grid->colwidths[x] = 40 + x;
    for (int y = 0; y < ys; y += 11) {
        grid->HBorder(0, y).width = 2;
        grid->HBorder(0, y).color = 0x445566;
    }

    auto selected = grid->C(xs - 1, ys - 1).get();
    treesheets::Document doc;
    doc.InitWith(std::move(root), filename, selected, 1, 1);
    doc.modified = true;

    bool success = false;
    auto save_message = doc.SaveDB(&success);

    CHECK(success);
    CHECK(save_message.StartsWith("Saved "));
    CHECK(::wxFileExists(filename));

    treesheets::Document loaded_doc;
    int numcells = 0;
    int textbytes = 0;
    int zoomlevel = -1;
    auto load_message = treesheets::sys->LoadDBIntoDocument(loaded_doc, filename, &numcells,
                                                            &textbytes, &zoomlevel);

    CHECK(load_message.IsEmpty());
    CHECK_EQ(numcells, xs * ys + 1);
    CHECK(textbytes > xs * ys * 20);
    CHECK_EQ(zoomlevel, 0);
    CHECK_EQ(loaded_doc.root->grid->xs, xs);
    CHECK_EQ(loaded_doc.root->grid->ys, ys);
    CHECK_EQ(loaded_doc.selected.GetCell()->text.t,
             wxString::Format("cell %03d,%03d alpha beta gamma", xs - 1, ys - 1));
    CHECK_EQ(loaded_doc.root->grid->C(0, 0)->text.t,
             wxString("cell 000,000 alpha beta gamma"));
    CHECK(loaded_doc.root->grid->C(0, 0)->text.stylebits & STYLE_BOLD);
    CHECK_EQ(loaded_doc.root->grid->C(29, 0)->cellcolor, 0xDDEEFFu);
    CHECK_EQ(loaded_doc.root->grid->colwidths[16], 56);
    CHECK_EQ(loaded_doc.root->grid->HBorder(0, 11).width, 2);
    CHECK_EQ(loaded_doc.root->grid->HBorder(0, 11).color, 0x445566u);

    RemoveSaveArtifacts(filename);
}

void TestReadDBRejectsInvalidAndTruncatedFiles() {
    {
        auto filename = TempCtsName("treesheets-empty-file-test");
        WriteBytes(filename, {});

        treesheets::System::LoadedDB loaded;
        auto message = treesheets::sys->ReadDB(filename, loaded);

        CHECK(!message.IsEmpty());
        CHECK(loaded.root == nullptr);
        RemoveIfExists(filename);
    }

    {
        auto filename = TempCtsName("treesheets-random-bytes-test");
        WriteBytes(filename,
                   {static_cast<char>(0x9A), static_cast<char>(0x04), static_cast<char>(0xFF),
                    static_cast<char>(0x31), static_cast<char>(0x80), static_cast<char>(0x12)});

        treesheets::System::LoadedDB loaded;
        auto message = treesheets::sys->ReadDB(filename, loaded);

        CHECK(!message.IsEmpty());
        CHECK(loaded.root == nullptr);
        RemoveIfExists(filename);
    }

    {
        auto filename = TempCtsName("treesheets-bad-magic-test");
        WriteBytes(filename, {'N', 'O', 'P', 'E'});

        treesheets::System::LoadedDB loaded;
        auto message = treesheets::sys->ReadDB(filename, loaded);

        CHECK(!message.IsEmpty());
        CHECK(loaded.root == nullptr);
        RemoveIfExists(filename);
    }

    {
        treesheets::System::LoadedDB loaded;
        auto message = treesheets::sys->ReadDB(FixturePath("cts/corrupt.cts"), loaded);

        CHECK(!message.IsEmpty());
        CHECK(loaded.root == nullptr);
    }

    {
        auto filename = TempCtsName("treesheets-truncated-version-test");
        WriteBytes(filename, {'T', 'S', 'F', 'F'});

        treesheets::System::LoadedDB loaded;
        auto message = treesheets::sys->ReadDB(filename, loaded);

        CHECK(!message.IsEmpty());
        CHECK(loaded.root == nullptr);
        RemoveIfExists(filename);
    }

    {
        auto filename = TempCtsName("treesheets-header-only-test");
        WriteBytes(filename, {'T', 'S', 'F', 'F', static_cast<char>(TS_VERSION), 1, 1, 0});

        treesheets::System::LoadedDB loaded;
        auto message = treesheets::sys->ReadDB(filename, loaded);

        CHECK(!message.IsEmpty());
        CHECK(loaded.root == nullptr);
        RemoveIfExists(filename);
    }
}

void TestReadDBRejectsInvalidSizesZoomAndImageLengths() {
    auto valid_filename = TempCtsName("treesheets-invalid-header-source-test");
    RemoveSaveArtifacts(valid_filename);

    {
        auto root = MakeRoot(1, 1);
        auto selected = root->grid->C(0, 0).get();
        selected->text.t = "valid";

        treesheets::Document doc;
        doc.InitWith(std::move(root), valid_filename, selected, 1, 1);

        bool success = false;
        CHECK(doc.SaveDB(&success).StartsWith("Saved "));
        CHECK(success);
    }

    auto valid_bytes = ReadFileBytes(valid_filename);
    CHECK(valid_bytes.size() > 8);

    {
        auto filename = TempCtsName("treesheets-zero-selection-size-test");
        auto bytes = valid_bytes;
        bytes[5] = 0;
        bytes[6] = 0;
        WriteBytes(filename, bytes);

        treesheets::Document loaded_doc;
        auto message = treesheets::sys->LoadDBIntoDocument(loaded_doc, filename);

        CHECK(message.IsEmpty());
        CHECK(loaded_doc.root != nullptr);
        CHECK_EQ(loaded_doc.selected.xs, 0);
        CHECK_EQ(loaded_doc.selected.ys, 0);
        RemoveIfExists(filename);
    }

    {
        auto filename = TempCtsName("treesheets-invalid-xsize-test");
        auto bytes = valid_bytes;
        bytes[5] = 2;
        WriteBytes(filename, bytes);
        ExpectReadDBRejects(filename);
    }

    {
        auto filename = TempCtsName("treesheets-invalid-ysize-test");
        auto bytes = valid_bytes;
        bytes[6] = 2;
        WriteBytes(filename, bytes);
        ExpectReadDBRejects(filename);
    }

    {
        auto filename = TempCtsName("treesheets-invalid-zoom-test");
        auto bytes = valid_bytes;
        bytes[7] = static_cast<char>(200);
        WriteBytes(filename, bytes);
        ExpectReadDBRejects(filename);
    }

    RemoveSaveArtifacts(valid_filename);

    auto image_filename = TempCtsName("treesheets-invalid-image-source-test");
    RemoveSaveArtifacts(image_filename);
    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();

    {
        auto root = MakeRoot(1, 1);
        auto selected = root->grid->C(0, 0).get();

        treesheets::Document doc;
        doc.InitWith(std::move(root), image_filename, selected, 1, 1);
        doc.SetImageBM(selected, SampleImageBytes(), 'I', 1.0);

        bool success = false;
        CHECK(doc.SaveDB(&success).StartsWith("Saved "));
        CHECK(success);
    }

    auto image_bytes = ReadFileBytes(image_filename);
    CHECK(image_bytes.size() > 25);
    CHECK(image_bytes[8] == 'I');
    for (size_t i = 17; i < 25; ++i) image_bytes[i] = static_cast<char>(0x7F);

    auto invalid_image_filename = TempCtsName("treesheets-invalid-image-length-test");
    WriteBytes(invalid_image_filename, image_bytes);
    ExpectReadDBRejects(invalid_image_filename);

    treesheets::sys->imagelist.clear();
    treesheets::sys->loadimageids.clear();
    RemoveSaveArtifacts(image_filename);
}

void TestExternalTreeSheetsDirectoryIfRequested() {
    wxString directory;
    if (!wxGetEnv("TREESHEETS_EXTERNAL_FIXTURE_DIR", &directory) || directory.IsEmpty()) return;

    wxArrayString files;
    wxDir::GetAllFiles(directory, &files, "*.cts", wxDIR_FILES);
    wxDir::GetAllFiles(directory, &files, "*.bak", wxDIR_FILES);
    CHECK(!files.IsEmpty());

    for (const auto &filename : files) {
        treesheets::Document doc;
        auto message = treesheets::sys->LoadDBIntoDocument(doc, filename);
        if (!message.IsEmpty()) {
            failures++;
            std::cerr << "failed to load external fixture "
                      << filename.ToStdString(wxConvUTF8) << ": "
                      << message.ToStdString(wxConvUTF8) << '\n';
        }
    }
}

}  // namespace

int main() {
    TestSystem test_system;
    if (!test_system.IsOk()) {
        std::cerr << "failed to initialize wxWidgets\n";
        return 1;
    }

    TestCellRoundTripPreservesGridFormattingAndSelectionMarker();
    TestCellRoundTripAllowsNoSelectionMarker();
    TestCSVImportHandlesQuotedFieldsAndMultilineValues();
    TestCSVImportSupportsAlternateSeparatorsAndEscapedQuotes();
    TestIndentedTextImportCreatesNestedGrids();
    TestFillXMLImportsGridSettingsAndCellStyles();
    TestDocumentSaveDBWritesReadableTreeSheetsFile();
    TestDocumentSaveDBClearsSuccessOnFailure();
    TestDocumentSaveDBReportsFilesystemFailures();
    TestAutoSaveWritesReadableTemporaryFile();
    TestNormalSaveRemovesAutosaveTemporaryFile();
    TestExternalModificationPollingState();
    TestAutoSaveKeepsMultipleDocumentsIsolated();
    TestImageRoundTripPreservesDataScaleAndSharedReference();
    TestSaveDBSkipsUnreferencedImages();
    TestSaveDBPersistsExpectedLoadStats();
    TestLoadImageIntoCellRejectsMissingAndCorruptFiles();
    TestReadDBLoadsLegacyVersion20File();
    TestReadDBLoadsLegacyVersion27FileAndInitializesBorders();
    TestLargeDocumentSaveLoadStress();
    TestReadDBRejectsInvalidAndTruncatedFiles();
    TestReadDBRejectsInvalidSizesZoomAndImageLengths();
    TestExternalTreeSheetsDirectoryIfRequested();

    return Finish("serialization tests passed");
}
