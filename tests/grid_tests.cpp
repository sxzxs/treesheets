#include <iostream>
#include <memory>

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

        TestRepairMergedCellsClipsAndMarksCovered();
        TestNormalizeSelectionMovesCoveredCellToMaster();
        TestCloneSelectionPreservesMergedCellsAndBorders();
        TestInsertColumnCopiesNeighborStyleAndShiftsContent();
        TestDeleteColumnRemovesContentAndShrinksBorderStorage();
        TestSetAndClearSelectionBorders();

        treesheets::sys = nullptr;
    }

    if (failures) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "grid tests passed\n";
    return 0;
}
