#include "test_helpers.h"

namespace {

using namespace test_helpers;

void TestRepairMergedCellsClipsAndMarksCovered() {
    auto root = MakeRoot(3, 3);
    auto grid = root->grid.get();

    grid->C(1, 1)->text.t = "master";
    grid->C(1, 1)->mergexs = 5;
    grid->C(1, 1)->mergeys = 5;
    grid->C(2, 1)->text.t = "covered";
    grid->C(1, 2)->text.t = "covered";
    grid->C(2, 2)->text.t = "covered";

    grid->RepairMergedCells();

    CHECK_EQ(grid->C(1, 1)->mergexs, 2);
    CHECK_EQ(grid->C(1, 1)->mergeys, 2);
    CHECK(grid->C(2, 1)->Covered());
    CHECK(grid->C(1, 2)->Covered());
    CHECK(grid->C(2, 2)->Covered());
    CHECK(!grid->C(2, 1)->HasText());
    CHECK(!grid->C(1, 2)->HasText());
    CHECK(!grid->C(2, 2)->HasText());
    CHECK_EQ(grid->C(0, 0)->mergexs, 1);
    CHECK_EQ(grid->C(0, 0)->mergeys, 1);
}

void TestNormalizeSelectionMovesCoveredCellToMaster() {
    auto root = MakeRoot(3, 3);
    auto grid = root->grid.get();

    grid->C(0, 0)->mergexs = 2;
    grid->C(0, 0)->mergeys = 2;
    grid->RepairMergedCells();

    treesheets::Selection selection(root->grid, 1, 1, 1, 1);
    grid->NormalizeSelection(selection);

    CHECK_EQ(selection.x, 0);
    CHECK_EQ(selection.y, 0);
    CHECK_EQ(selection.xs, 1);
    CHECK_EQ(selection.ys, 1);
}

void TestCloneSelectionPreservesMergedCellsAndBorders() {
    auto root = MakeRoot(3, 3);
    auto grid = root->grid.get();
    treesheets::Selection selection(root->grid, 0, 0, 2, 2);

    grid->C(0, 0)->text.t = "merged";
    grid->C(0, 0)->mergexs = 2;
    grid->C(0, 0)->mergeys = 2;
    grid->RepairMergedCells();
    grid->SetSelectionOuterBorder(selection, 0x112233, 3, true);
    grid->SetSelectionInnerBorder(selection, 0x445566, 2, true);

    auto clone = grid->CloneSel(selection);
    auto cloned_grid = clone->grid.get();

    CHECK_EQ(cloned_grid->xs, 2);
    CHECK_EQ(cloned_grid->ys, 2);
    CHECK_EQ(cloned_grid->C(0, 0)->text.t, wxString("merged"));
    CHECK_EQ(cloned_grid->C(0, 0)->mergexs, 2);
    CHECK_EQ(cloned_grid->C(0, 0)->mergeys, 2);
    CHECK(cloned_grid->C(1, 0)->Covered());
    CHECK(cloned_grid->C(0, 1)->Covered());
    CHECK(cloned_grid->C(1, 1)->Covered());

    CHECK_EQ(cloned_grid->HBorder(0, 0).width, 3);
    CHECK_EQ(cloned_grid->HBorder(1, 0).width, 3);
    CHECK_EQ(cloned_grid->HBorder(0, 2).color, 0x112233u);
    CHECK_EQ(cloned_grid->VBorder(0, 0).width, 3);
    CHECK_EQ(cloned_grid->VBorder(2, 1).color, 0x112233u);
    CHECK_EQ(cloned_grid->VBorder(1, 0).width, 2);
    CHECK_EQ(cloned_grid->HBorder(0, 1).color, 0x445566u);
}

void TestInsertColumnCopiesNeighborStyleAndShiftsContent() {
    auto root = MakeRoot(2, 1);
    auto grid = root->grid.get();

    grid->C(0, 0)->text.t = "left";
    grid->C(0, 0)->text.relsize = 4;
    grid->C(0, 0)->cellcolor = 0xABCDEF;
    grid->C(1, 0)->text.t = "right";

    grid->InsertCells(1, -1, 1, 0);

    CHECK_EQ(grid->xs, 3);
    CHECK_EQ(grid->ys, 1);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("left"));
    CHECK_EQ(grid->C(1, 0)->text.t, wxString(""));
    CHECK_EQ(grid->C(1, 0)->text.relsize, 4);
    CHECK_EQ(grid->C(1, 0)->cellcolor, 0xABCDEFu);
    CHECK_EQ(grid->C(2, 0)->text.t, wxString("right"));
    CHECK_EQ(static_cast<int>(grid->colwidths.size()), 3);
}

void TestDeleteColumnRemovesContentAndShrinksBorderStorage() {
    auto root = MakeRoot(3, 1);
    auto grid = root->grid.get();

    grid->C(0, 0)->text.t = "a";
    grid->C(1, 0)->text.t = "b";
    grid->C(2, 0)->text.t = "c";
    grid->VBorder(3, 0).width = 2;
    grid->HBorder(2, 1).color = 0x654321;

    grid->DeleteCells(1, -1, -1, 0);

    CHECK_EQ(grid->xs, 2);
    CHECK_EQ(grid->ys, 1);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("a"));
    CHECK_EQ(grid->C(1, 0)->text.t, wxString("c"));
    CHECK_EQ(static_cast<int>(grid->colwidths.size()), 2);
    CHECK_EQ(static_cast<int>(grid->vborderstyles.size()), 3);
    CHECK_EQ(static_cast<int>(grid->hborderstyles.size()), 4);
    CHECK_EQ(grid->VBorder(2, 0).width, 2);
    CHECK_EQ(grid->HBorder(1, 1).color, 0x654321u);
}

void TestSetAndClearSelectionBorders() {
    auto root = MakeRoot(3, 3);
    auto grid = root->grid.get();
    treesheets::Selection selection(root->grid, 1, 1, 2, 2);

    CHECK(!grid->HasCustomBorders());
    grid->SetSelectionOuterBorder(selection, 0x102030, 4, true);
    grid->SetSelectionInnerBorder(selection, 0x405060, 2, true);

    CHECK(grid->HasCustomBorders());
    CHECK_EQ(grid->HBorder(1, 1).width, 4);
    CHECK_EQ(grid->VBorder(3, 2).color, 0x102030u);
    CHECK_EQ(grid->VBorder(2, 1).width, 2);
    CHECK_EQ(grid->HBorder(1, 2).color, 0x405060u);

    grid->ClearSelectionBorders(selection);

    CHECK(!grid->HasCustomBorders());
}

void TestTransposeSwapsCellsAndBorders() {
    auto root = MakeRoot(2, 3);
    auto grid = root->grid.get();

    for (int y = 0; y < grid->ys; y++)
        for (int x = 0; x < grid->xs; x++)
            grid->C(x, y)->text.t = wxString::Format("%d,%d", x, y);

    grid->VBorder(2, 1).width = 3;
    grid->VBorder(2, 1).color = 0x112233;
    grid->HBorder(1, 3).width = 4;
    grid->HBorder(1, 3).color = 0x445566;

    grid->Transpose();

    CHECK_EQ(grid->xs, 3);
    CHECK_EQ(grid->ys, 2);
    CHECK_EQ(grid->C(0, 0)->text.t, wxString("0,0"));
    CHECK_EQ(grid->C(1, 0)->text.t, wxString("0,1"));
    CHECK_EQ(grid->C(2, 0)->text.t, wxString("0,2"));
    CHECK_EQ(grid->C(0, 1)->text.t, wxString("1,0"));
    CHECK_EQ(grid->C(1, 1)->text.t, wxString("1,1"));
    CHECK_EQ(grid->C(2, 1)->text.t, wxString("1,2"));
    CHECK_EQ(grid->HBorder(1, 2).width, 3);
    CHECK_EQ(grid->HBorder(1, 2).color, 0x112233u);
    CHECK_EQ(grid->VBorder(3, 1).width, 4);
    CHECK_EQ(grid->VBorder(3, 1).color, 0x445566u);
}

void TestSortRowsUsesSelectionColumnFirst() {
    auto root = MakeRoot(3, 4);
    auto grid = root->grid.get();
    const char *rows[][3] = {
        {"0", "b", "2"},
        {"1", "a", "2"},
        {"2", "b", "1"},
        {"3", "a", "2"},
    };

    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 3; x++)
            grid->C(x, y)->text.t = rows[y][x];

    treesheets::Selection selection(root->grid, 1, 0, 1, 4);
    grid->Sort(selection, false);

    CHECK_EQ(grid->C(0, 0)->text.t, wxString("1"));
    CHECK_EQ(grid->C(0, 1)->text.t, wxString("3"));
    CHECK_EQ(grid->C(0, 2)->text.t, wxString("2"));
    CHECK_EQ(grid->C(0, 3)->text.t, wxString("0"));
}

void TestLargeCloneAndSortStressPreservesDataAndMemoryEstimate() {
    constexpr int xs = 64;
    constexpr int ys = 64;
    auto root = MakeRoot(xs, ys);
    auto grid = root->grid.get();

    for (int y = 0; y < ys; y++) {
        for (int x = 0; x < xs; x++) {
            grid->C(x, y)->text.t = wxString::Format("%03d-%03d", ys - y, x);
            grid->C(x, y)->cellcolor = 0x101010 + static_cast<uint>((x + y) % 32);
        }
    }
    grid->SetSelectionOuterBorder(treesheets::Selection(root->grid, 4, 4, 24, 24), 0xAABBCC, 2,
                                  true);

    auto memory_before = root->EstimatedMemoryUse();
    CHECK(memory_before > 0);

    auto clone = grid->CloneSel(treesheets::Selection(root->grid, 4, 4, 24, 24));
    CHECK_EQ(clone->grid->xs, 24);
    CHECK_EQ(clone->grid->ys, 24);
    CHECK_EQ(clone->grid->C(0, 0)->text.t, wxString("060-004"));
    CHECK_EQ(clone->grid->C(23, 23)->text.t, wxString("037-027"));
    CHECK_EQ(clone->grid->HBorder(0, 0).color, 0xAABBCCu);

    treesheets::Selection sort_selection(root->grid, 0, 0, 1, ys);
    grid->Sort(sort_selection, false);

    CHECK_EQ(grid->C(0, 0)->text.t, wxString("001-000"));
    CHECK_EQ(grid->C(0, ys - 1)->text.t, wxString("064-000"));
    CHECK(root->EstimatedMemoryUse() >= memory_before);
}

}  // namespace

int main() {
    TestSystem test_system;
    if (!test_system.IsOk()) {
        std::cerr << "failed to initialize wxWidgets\n";
        return 1;
    }

    TestRepairMergedCellsClipsAndMarksCovered();
    TestNormalizeSelectionMovesCoveredCellToMaster();
    TestCloneSelectionPreservesMergedCellsAndBorders();
    TestInsertColumnCopiesNeighborStyleAndShiftsContent();
    TestDeleteColumnRemovesContentAndShrinksBorderStorage();
    TestSetAndClearSelectionBorders();
    TestTransposeSwapsCellsAndBorders();
    TestSortRowsUsesSelectionColumnFirst();
    TestLargeCloneAndSortStressPreservesDataAndMemoryEstimate();

    return Finish("grid tests passed");
}
