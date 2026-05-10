struct Grid {
    struct BorderLineStyle {
        uint color {g_bordercolor_default};
        wxUint8 width {0};

        bool Visible() const { return width > 0; }
    };

    // owning cell.
    Cell *cell;
    // subcells
    vector<unique_ptr<Cell>> cells;
    // widths for each column
    vector<int> colwidths;
    vector<BorderLineStyle> vborderstyles;
    vector<BorderLineStyle> hborderstyles;
    // xsize, ysize
    int xs;
    int ys;
    int view_margin;
    int view_grid_outer_spacing;
    int user_grid_outer_spacing {g_usergridouterspacing_default};
    int cell_margin;
    int bordercolor {g_bordercolor_default};
    bool horiz {false};
    bool tinyborder;
    bool folded {false};

    unique_ptr<Cell> &C(int x, int y) {
        ASSERT(x >= 0 && y >= 0 && x < xs && y < ys);
        return cells[x + y * xs];
    }

    Cell *C(int x, int y) const {
        ASSERT(x >= 0 && y >= 0 && x < xs && y < ys);
        return cells[x + y * xs].get();
    }

    #define foreachcell(c)                \
        for (int y = 0; y < ys; y++)      \
            for (int x = 0; x < xs; x++)  \
                for (bool _f = true; _f;) \
                    for (unique_ptr<Cell> &c = C(x, y); _f; _f = false)

    #define foreachcellconst(c)           \
        for (int y = 0; y < ys; y++)      \
            for (int x = 0; x < xs; x++)  \
                for (bool _f = true; _f;) \
                    for (Cell *c = C(x, y); _f; _f = false)

    #define foreachcellrev(c)                 \
        for (int y = ys - 1; y >= 0; y--)     \
            for (int x = xs - 1; x >= 0; x--) \
                for (bool _f = true; _f;)     \
                    for (unique_ptr<Cell> &c = C(x, y); _f; _f = false)
    #define foreachcelly(c)           \
        for (int y = 0; y < ys; y++)  \
            for (bool _f = true; _f;) \
                for (unique_ptr<Cell> &c = C(0, y); _f; _f = false)
    #define foreachcellcolumn(c)          \
        for (int x = 0; x < xs; x++)      \
            for (int y = 0; y < ys; y++)  \
                for (bool _f = true; _f;) \
                    for (unique_ptr<Cell> &c = C(x, y); _f; _f = false)
    #define foreachcellinsel(c, s)                 \
        for (int y = s.y; y < s.y + s.ys; y++)     \
            for (int x = s.x; x < s.x + s.xs; x++) \
                for (bool _f = true; _f;)          \
                    for (unique_ptr<Cell> &c = C(x, y); _f; _f = false)
    #define foreachcellinselrev(c, s)                   \
        for (int y = s.y + s.ys - 1; y >= s.y; y--)     \
            for (int x = s.x + s.xs - 1; x >= s.x; x--) \
                for (bool _f = true; _f;)               \
                    for (unique_ptr<Cell> &c = C(x, y); _f; _f = false)
    #define foreachcellingrid(c, g)         \
        for (int y = 0; y < g->ys; y++)     \
            for (int x = 0; x < g->xs; x++) \
                for (bool _f = true; _f;)   \
                    for (unique_ptr<Cell> &c = g->C(x, y); _f; _f = false)

    Grid(int _xs, int _ys, Cell *_c = nullptr)
        : xs(_xs), ys(_ys), cell(_c), cells(_xs * _ys) {
        InitColWidths();
        InitBorderStyles();
        SetOrient();
    }

    void InitCells(Cell *clonestylefrom = nullptr) {
        foreachcell(c) c = make_unique<Cell>(cell, clonestylefrom);
    }
    void CloneStyleFrom(auto o) {
        bordercolor = o->bordercolor;
        // TODO: what others?
    }

    void InitColWidths() {
        colwidths.clear();
        colwidths.resize(xs, cell ? cell->ColWidth() : sys->defaultmaxcolwidth);
    }

    void InitBorderStyles() {
        vborderstyles.resize((xs + 1) * ys);
        hborderstyles.resize(xs * (ys + 1));
    }

    int VBorderIndex(int x, int y) const {
        ASSERT(x >= 0 && x <= xs && y >= 0 && y < ys);
        return x + y * (xs + 1);
    }

    int HBorderIndex(int x, int y) const {
        ASSERT(x >= 0 && x < xs && y >= 0 && y <= ys);
        return x + y * xs;
    }

    BorderLineStyle &VBorder(int x, int y) { return vborderstyles[VBorderIndex(x, y)]; }
    BorderLineStyle &HBorder(int x, int y) { return hborderstyles[HBorderIndex(x, y)]; }
    const BorderLineStyle &VBorder(int x, int y) const {
        return vborderstyles[VBorderIndex(x, y)];
    }
    const BorderLineStyle &HBorder(int x, int y) const {
        return hborderstyles[HBorderIndex(x, y)];
    }

    int DefaultSelectionBorderWidth() const { return max(1, g_usergridouterspacing_default - 1); }

    bool HasCustomBorders() const {
        for (const auto &style : vborderstyles)
            if (style.Visible()) return true;
        for (const auto &style : hborderstyles)
            if (style.Visible()) return true;
        return false;
    }

    /* Clones g into this grid. This mutates the grid this function is called on. */
    void Clone(shared_ptr<Grid> g) {
        g->bordercolor = bordercolor;
        g->user_grid_outer_spacing = user_grid_outer_spacing;
        g->folded = folded;
        g->vborderstyles = vborderstyles;
        g->hborderstyles = hborderstyles;
        foreachcell(c) g->C(x, y) = c->Clone(g->cell);
        loop(x, xs) g->colwidths[x] = colwidths[x];
    }

    unique_ptr<Cell> CloneSel(const Selection &sel) {
        auto cl = make_unique<Cell>(nullptr, sel.grid->cell, CT_DATA, make_shared<Grid>(sel.xs, sel.ys));
        foreachcellinsel(c, sel) cl->grid->C(x - sel.x, y - sel.y) = c->Clone(cl.get());
        loop(i, sel.xs) cl->grid->colwidths[i] = sel.grid->colwidths[sel.x + i];
        for (int y = 0; y < sel.ys; y++)
            for (int x = 0; x <= sel.xs; x++)
                cl->grid->VBorder(x, y) = sel.grid->VBorder(sel.x + x, sel.y + y);
        for (int y = 0; y <= sel.ys; y++)
            for (int x = 0; x < sel.xs; x++)
                cl->grid->HBorder(x, y) = sel.grid->HBorder(sel.x + x, sel.y + y);
        cl->grid->RepairMergedCells();
        return cl;
    }

    bool IsCovered(int x, int y) const { return C(x, y)->Covered(); }

    int MergeXS(int x, int y) const {
        auto c = C(x, y);
        return c->mergexs > 0 ? min(c->mergexs, xs - x) : 1;
    }

    int MergeYS(int x, int y) const {
        auto c = C(x, y);
        return c->mergeys > 0 ? min(c->mergeys, ys - y) : 1;
    }

    int SpanColWidth(int x, int span) const {
        int width = 0;
        loop(i, min(span, xs - x)) width += colwidths[x + i];
        return width;
    }

    int SpanSize(int span, int *sizes) const {
        int size = 0;
        loop(i, span) size += sizes[i];
        return size + (span - 1) * (g_line_width + cell_margin * 2);
    }

    void GrowSpanToFit(int span, int *sizes, int wanted) {
        int current = SpanSize(span, sizes);
        if (wanted <= current) return;
        int extra = wanted - current;
        int add = (extra + span - 1) / span;
        loop(i, span) sizes[i] += add;
    }

    bool MergeIntersects(int x, int y, int mxs, int mys, const Selection &sel) const {
        return x < sel.x + sel.xs && x + mxs > sel.x && y < sel.y + sel.ys &&
               y + mys > sel.y;
    }

    bool HasMergedIntersecting(const Selection &sel) {
        foreachcell(c) if (!IsCovered(x, y)) {
            int mxs = MergeXS(x, y);
            int mys = MergeYS(x, y);
            if ((mxs > 1 || mys > 1) && MergeIntersects(x, y, mxs, mys, sel)) return true;
        }
        return false;
    }

    bool UnmergeIntersecting(const Selection &sel) {
        bool changed = false;
        foreachcell(c) if (!IsCovered(x, y)) {
            int mxs = MergeXS(x, y);
            int mys = MergeYS(x, y);
            if ((mxs > 1 || mys > 1) && MergeIntersects(x, y, mxs, mys, sel)) {
                loop(yy, mys) loop(xx, mxs) {
                    auto cc = C(x + xx, y + yy).get();
                    cc->mergexs = 1;
                    cc->mergeys = 1;
                }
                changed = true;
            }
        }
        return changed;
    }

    bool UnmergeAll() {
        bool changed = false;
        foreachcell(c) {
            if (c && (c->mergexs != 1 || c->mergeys != 1)) {
                c->mergexs = 1;
                c->mergeys = 1;
                changed = true;
            }
        }
        return changed;
    }

    void RepairMergedCells() {
        foreachcell(c) {
            if (c->mergexs <= 0 || c->mergeys <= 0) {
                c->mergexs = 1;
                c->mergeys = 1;
            }
            c->mergexs = min(c->mergexs, xs - x);
            c->mergeys = min(c->mergeys, ys - y);
        }
        foreachcell(c) if (c->Merged()) {
            loop(yy, MergeYS(x, y)) loop(xx, MergeXS(x, y)) {
                if (!xx && !yy) continue;
                auto cc = C(x + xx, y + yy).get();
                cc->Clear();
                cc->mergexs = 0;
                cc->mergeys = 0;
            }
        }
    }

    void NormalizeSelection(Selection &sel) {
        if (sel.grid.get() != this || sel.xs != 1 || sel.ys != 1 || !IsCovered(sel.x, sel.y))
            return;
        foreachcell(c) if (!IsCovered(x, y)) {
            if (sel.x >= x && sel.x < x + MergeXS(x, y) && sel.y >= y &&
                sel.y < y + MergeYS(x, y)) {
                sel.x = x;
                sel.y = y;
                return;
            }
        }
    }

    size_t EstimatedMemoryUse() {
        size_t sum = 0;
        foreachcell(c) sum += c->EstimatedMemoryUse();
        return sizeof(Grid) + xs * ys * sizeof(Cell *) +
               (vborderstyles.size() + hborderstyles.size()) * sizeof(BorderLineStyle) + sum;
    }

    void SetOrient() {
        if (xs > ys) horiz = true;
        if (ys > xs) horiz = false;
    }

    bool Layout(Document *doc, wxReadOnlyDC &dc, int depth, int &sx, int &sy, int startx, int starty,
                bool forcetiny) {
        auto xa = new int[xs];
        auto ya = new int[ys];
        loop(i, xs) xa[i] = 0;
        loop(i, ys) ya[i] = 0;
        tinyborder = true;
        foreachcell(c) {
            if (IsCovered(x, y)) continue;
            c->LazyLayout(doc, dc, depth + 1, SpanColWidth(x, MergeXS(x, y)), forcetiny);
            tinyborder = c->tiny && tinyborder;
            if (MergeXS(x, y) == 1) xa[x] = max(xa[x], c->sx);
            if (MergeYS(x, y) == 1) ya[y] = max(ya[y], c->sy);
        }
        view_grid_outer_spacing =
            tinyborder || cell->drawstyle != DS_GRID ? 0 : user_grid_outer_spacing;
        view_margin = tinyborder || cell->drawstyle != DS_GRID ? 0 : g_grid_margin;
        cell_margin = tinyborder ? 0 : (cell->drawstyle == DS_GRID ? 0 : g_cell_margin);
        foreachcell(c) {
            if (IsCovered(x, y)) continue;
            GrowSpanToFit(MergeXS(x, y), xa + x, c->sx);
            GrowSpanToFit(MergeYS(x, y), ya + y, c->sy);
        }
        sx = (xs + 1) * g_line_width + xs * cell_margin * 2 +
             2 * (view_grid_outer_spacing + view_margin) + startx;
        sy = (ys + 1) * g_line_width + ys * cell_margin * 2 +
             2 * (view_grid_outer_spacing + view_margin) + starty;
        loop(i, xs) sx += xa[i];
        loop(i, ys) sy += ya[i];
        int cx = view_grid_outer_spacing + view_margin + g_line_width + cell_margin + startx;
        int cy = view_grid_outer_spacing + view_margin + g_line_width + cell_margin + starty;
        if (!cell->tiny) {
            cx += g_margin_extra;
            cy += g_margin_extra;
        }
        foreachcell(c) {
            c->ox = cx;
            c->oy = cy;
            if (!IsCovered(x, y) && MergeYS(x, y) == 1 && c->drawstyle == DS_BLOBLINE && !c->grid) {
                assert(c->sy <= ya[y]);
                c->ycenteroff = (ya[y] - c->sy) / 2;
            }
            if (IsCovered(x, y)) {
                c->sx = xa[x];
                c->sy = ya[y];
            } else {
                c->sx = SpanSize(MergeXS(x, y), xa + x);
                c->sy = SpanSize(MergeYS(x, y), ya + y);
            }
            cx += xa[x] + g_line_width + cell_margin * 2;
            if (x == xs - 1) {
                cy += ya[y] + g_line_width + cell_margin * 2;
                cx = view_grid_outer_spacing + view_margin + g_line_width + cell_margin + startx;
                if (!cell->tiny) cx += g_margin_extra;
            }
        }
        delete[] xa;
        delete[] ya;
        return tinyborder;
    }

    void DrawCustomBorders(Document *doc, wxDC &dc, int bx, int by, int maxx, int maxy) {
        if (!(tinyborder || cell->drawstyle == DS_GRID)) return;
        if (!HasCustomBorders()) return;

        auto linex = [&](int col) {
            return bx + (col == xs ? maxx : C(col, 0)->ox - g_line_width);
        };
        auto liney = [&](int row) {
            return by + (row == ys ? maxy : C(0, row)->oy - g_line_width);
        };
        auto drawsegment = [&](const BorderLineStyle &style, int x1, int y1, int x2, int y2) {
            if (!style.Visible()) return;
            int w = style.width;
            if (max(x1, x2) + w < doc->scrollx || min(x1, x2) - w > doc->maxx ||
                max(y1, y2) + w < doc->scrolly || min(y1, y2) - w > doc->maxy)
                return;
            dc.SetPen(wxPen(LightColor(style.color), style.width));
            dc.DrawLine(x1, y1, x2, y2);
        };

        for (int y = 0; y < ys; y++)
            for (int x = 0; x <= xs; x++)
                if (BorderLineForSelection(Selection(cell->grid, x, y, 0, 1)))
                    drawsegment(VBorder(x, y), linex(x), liney(y), linex(x), liney(y + 1));
        for (int y = 0; y <= ys; y++)
            for (int x = 0; x < xs; x++)
                if (BorderLineForSelection(Selection(cell->grid, x, y, 1, 0)))
                    drawsegment(HBorder(x, y), linex(x), liney(y), linex(x + 1), liney(y));
    }

    void Render(Document *doc, int bx, int by, wxDC &dc, int depth, int sx, int sy, int xoff,
                int yoff) {
        xoff = C(0, 0)->ox - view_margin - view_grid_outer_spacing - 1;
        yoff = C(0, 0)->oy - view_margin - view_grid_outer_spacing - 1;
        int maxx = C(xs - 1, 0)->ox + C(xs - 1, 0)->sx;
        int maxy = C(0, ys - 1)->oy + C(0, ys - 1)->sy;
        if (tinyborder || cell->drawstyle == DS_GRID) {
            int ldelta = view_grid_outer_spacing != 0;
            auto drawlines = [&]() {
                for (int x = ldelta; x <= xs - ldelta; x++) {
                    int xl = (x == xs ? maxx : C(x, 0)->ox - g_line_width) + bx;
                    if (xl >= doc->scrollx && xl <= doc->maxx) loop(line, g_line_width) {
                            dc.DrawLine(
                                xl + line, max(doc->scrolly, by + yoff + view_grid_outer_spacing),
                                xl + line, min(doc->maxy, by + maxy + g_line_width) + view_margin);
                        }
                }
                for (int y = ldelta; y <= ys - ldelta; y++) {
                    int yl = (y == ys ? maxy : C(0, y)->oy - g_line_width) + by;
                    if (yl >= doc->scrolly && yl <= doc->maxy) loop(line, g_line_width) {
                            dc.DrawLine(max(doc->scrollx,
                                            bx + xoff + view_grid_outer_spacing + g_line_width),
                                        yl + line, min(doc->maxx, bx + maxx) + view_margin,
                                        yl + line);
                        }
                }
            };
            if (!sys->fastrender && view_grid_outer_spacing && cell->cellcolor != 0xFFFFFF) {
                dc.SetPen(wxPen(LightColor(0xFFFFFF)));
                drawlines();
            }
            // dotted lines result in very expensive drawline calls
            dc.SetPen(view_grid_outer_spacing && !sys->fastrender ? sys->pen_gridlines
                                                                  : sys->pen_tinygridlines);
            drawlines();
        }

        foreachcell(c) {
            if (IsCovered(x, y)) continue;
            int cx = bx + c->ox;
            int cy = by + c->oy;
            if (cx < doc->maxx && cx + c->sx > doc->scrollx && cy < doc->maxy &&
                cy + c->sy > doc->scrolly) {
                int mxs = MergeXS(x, y);
                int mys = MergeYS(x, y);
                c->Render(doc, cx, cy, dc, depth + 1, (x == 0) * view_margin,
                          (x + mxs == xs) * view_margin, (y == 0) * view_margin,
                          (y + mys == ys) * view_margin, SpanColWidth(x, mxs), cell_margin);
            }
        }
        if (cell->drawstyle == DS_BLOBLINE && !tinyborder && cell->HasHeader() && !cell->tiny) {
            const int arcsize = 8;
            const double pi = std::acos(-1.0);
            int srcy = by + cell->ycenteroff +
                       (cell->verticaltextandgrid ? cell->tys + 2 : cell->tys / 2) + g_margin_extra;
            // fixme: the 8 is chosen to fit the smallest text size, not very portable
            int srcx = bx + (cell->verticaltextandgrid ? 8 : cell->txs + 4) + g_margin_extra;
            int destyfirst = -1, destylast = -1;
            dc.SetPen(wxPen(LightColor(bordercolor)));
            auto drawcorner = [&](int centerx, int centery, double start, double end) {
                constexpr int segments = 8;
                wxPoint prev(static_cast<int>(std::lround(centerx + std::cos(start) * arcsize)),
                             static_cast<int>(std::lround(centery + std::sin(start) * arcsize)));
                for (int i = 1; i <= segments; i++) {
                    double t = start + (end - start) * i / segments;
                    wxPoint next(static_cast<int>(std::lround(centerx + std::cos(t) * arcsize)),
                                 static_cast<int>(std::lround(centery + std::sin(t) * arcsize)));
                    dc.DrawLine(prev, next);
                    prev = next;
                }
            };
            foreachcelly(c) if (!IsCovered(0, y) && c->HasContent() && !c->tiny) {
                int desty = c->ycenteroff + by + c->oy + c->tys / 2 + g_margin_extra;
                int destx = bx + c->ox - 2 + g_margin_extra;
                bool visible = srcx < doc->maxx && destx > doc->scrollx &&
                               desty - arcsize < doc->maxy && desty + arcsize > doc->scrolly;
                if (abs(srcy - desty) < arcsize && !cell->verticaltextandgrid) {
                    if (destyfirst < 0) destyfirst = desty;
                    destylast = desty;
                    if (visible) dc.DrawLine(srcx, desty, destx, desty);
                } else {
                    if (desty < srcy) {
                        if (destyfirst < 0) destyfirst = desty + arcsize;
                        destylast = desty + arcsize;
                        if (visible) drawcorner(srcx + arcsize, desty + arcsize, pi, pi * 1.5);
                    } else {
                        desty--;
                        destylast = desty - arcsize;
                        if (visible) drawcorner(srcx + arcsize, desty - arcsize, pi, pi * 0.5);
                    }
                    if (visible) dc.DrawLine(srcx + arcsize, desty, destx, desty);
                }
            }
            if (cell->verticaltextandgrid) {
                if (destylast > 0) dc.DrawLine(srcx, srcy, srcx, destylast);
            } else {
                if (destyfirst >= 0 && destylast >= 0 && destyfirst < destylast) {
                    destyfirst = min(destyfirst, srcy);
                    destylast = max(destylast, srcy);
                    dc.DrawLine(srcx, destyfirst, srcx, destylast);
                }
            }
        }
        if (view_grid_outer_spacing && cell->drawstyle == DS_GRID) {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(LightColor(bordercolor)));
            loop(i, view_grid_outer_spacing - 1) {
                dc.DrawRoundedRectangle(
                    bx + xoff + view_grid_outer_spacing - i,
                    by + yoff + view_grid_outer_spacing - i,
                    maxx - xoff - view_grid_outer_spacing + 1 + i * 2 + view_margin,
                    maxy - yoff - view_grid_outer_spacing + 1 + i * 2 + view_margin,
                    sys->roundness + i);
            }
        }
        DrawCustomBorders(doc, dc, bx, by, maxx, maxy);
    }

    void FindXY(Document *doc, int px, int py, wxReadOnlyDC &dc) {
        auto column_at = [&](int startx, int starty, int span) {
            for (int xx = 0; xx < span; xx++) {
                auto cc = C(startx + xx, starty).get();
                auto right = xx + 1 < span ? C(startx + xx + 1, starty)->ox : cc->ox + cc->sx;
                if (px >= cc->ox && px < right) return startx + xx;
            }
            return startx + span - 1;
        };
        auto row_at = [&](int startx, int starty, int span) {
            for (int yy = 0; yy < span; yy++) {
                auto cc = C(startx, starty + yy).get();
                auto bottom = yy + 1 < span ? C(startx, starty + yy + 1)->oy : cc->oy + cc->sy;
                if (py >= cc->oy && py < bottom) return starty + yy;
            }
            return starty + span - 1;
        };
        foreachcell(c) {
            if (IsCovered(x, y)) continue;
            int mxs = MergeXS(x, y);
            int mys = MergeYS(x, y);
            int bx = px - c->ox;
            int by = py - c->oy;
            if (bx >= 0 && by >= -g_line_width - g_selmargin && bx < c->sx && by < g_selmargin) {
                doc->hover = Selection(cell->grid, column_at(x, y, mxs), y, 1, 0);
                return;
            }
            if (bx >= 0 && by >= c->sy - g_selmargin && bx < c->sx &&
                by < c->sy + g_line_width + g_selmargin) {
                doc->hover = Selection(cell->grid, column_at(x, y, mxs), y + mys, 1, 0);
                return;
            }
            if (bx >= -g_line_width - g_selmargin && by >= 0 && bx < g_selmargin && by < c->sy) {
                doc->hover = Selection(cell->grid, x, row_at(x, y, mys), 0, 1);
                return;
            }
            if (bx >= c->sx - g_selmargin && by >= 0 && bx < c->sx + g_line_width + g_selmargin &&
                by < c->sy) {
                doc->hover = Selection(cell->grid, x + mxs, row_at(x, y, mys), 0, 1);
                return;
            }
            if (c->IsInside(bx, by)) {
                if (c->GridShown(doc)) c->grid->FindXY(doc, bx, by, dc);
                if (doc->hover.grid) return;
                doc->hover = Selection(cell->grid, x, y, 1, 1);
                if (c->HasText()) {
                    c->text.FindCursor(doc, bx, by - c->ycenteroff, dc, doc->hover,
                                       SpanColWidth(x, mxs));
                }
                return;
            }
        }
    }

    Cell *FindLink(const Selection &sel, Cell *link, Cell *best, bool &lastthis, bool &stylematch,
                   bool forward, bool image) {
        if (forward) {
            foreachcell(c) best =
                c->FindLink(sel, link, best, lastthis, stylematch, forward, image);
        } else {
            foreachcellrev(c) best =
                c->FindLink(sel, link, best, lastthis, stylematch, forward, image);
        }
        return best;
    }

    Cell *FindNextSearchMatch(const wxString &search, Cell *best, Cell *selected,
                              bool &lastwasselected, bool reverse) {
        if (reverse) {
            foreachcellrev(c) best =
                c->FindNextSearchMatch(search, best, selected, lastwasselected, reverse);
        } else {
            foreachcell(c) best =
                c->FindNextSearchMatch(search, best, selected, lastwasselected, reverse);
        }
        return best;
    }

    Cell *FindNextFilterMatch(Cell *best, Cell *selected, bool &lastwasselected) {
        foreachcell(c) best = c->FindNextFilterMatch(best, selected, lastwasselected);
        return best;
    }

    void FindReplaceAll(const wxString &s, const wxString &ls) {
        foreachcell(c) c->FindReplaceAll(s, ls);
    }

    void ReplaceCell(Cell *o, Cell *n) { foreachcell(c) if (c.get() == o) c.reset(n); }
    Selection FindCell(Cell *o) {
        foreachcell(c) if (c.get() == o) return Selection(cell->grid, x, y, 1, 1);
        return Selection();
    }

    Selection SelectAll() { return Selection(cell->grid, 0, 0, xs, ys); }
    void ImageRefCount(bool includefolded) {
        if (includefolded || !folded) foreachcell(c) c->ImageRefCount(includefolded);
    }

    void DrawCursor(Document *doc, wxDC &dc, Selection &sel, bool full, uint color) {
        if (auto c = sel.GetCell(); c && !c->tiny && (c->HasText() || !c->grid))
            c->text.DrawCursor(doc, dc, sel, full, color, SpanColWidth(sel.x, MergeXS(sel.x, sel.y)));
    }

    void DrawInsert(Document *doc, wxDC &dc, Selection &sel, uint colour) {
        dc.SetPen(sys->pen_thinselect);
        if (!sel.xs) {
            auto c = C(sel.x - (sel.x == xs), sel.y).get();
            int x = c->GetX(doc) + (c->sx + g_line_width + cell_margin) * (sel.x == xs) -
                    g_line_width - cell_margin;
            loop(line, g_line_width)
                dc.DrawLine(x + line, max(cell->GetY(doc), doc->scrolly), x + line,
                            min(cell->GetY(doc) + cell->sy, doc->maxy));
            DrawRectangle(dc, colour, x - 1, c->GetY(doc), g_line_width + 2, c->sy);
        } else {
            auto c = C(sel.x, sel.y - (sel.y == ys)).get();
            int y = c->GetY(doc) + (c->sy + g_line_width + cell_margin) * (sel.y == ys) -
                    g_line_width - cell_margin;
            loop(line, g_line_width)
                dc.DrawLine(max(cell->GetX(doc), doc->scrollx), y + line,
                            min(cell->GetX(doc) + cell->sx, doc->maxx), y + line);
            DrawRectangle(dc, colour, c->GetX(doc), y - 1, c->sx, g_line_width + 2);
        }
    }

    wxRect GetRect(Document *doc, const Selection &sel, bool minimal = false) {
        if (sel.Thin()) {
            if (sel.xs) {
                if (sel.y < ys) {
                    auto tl = C(sel.x, sel.y).get();
                    return wxRect(tl->GetX(doc), tl->GetY(doc), tl->sx, 0);
                } else {
                    auto br = C(sel.x, ys - 1).get();
                    return wxRect(br->GetX(doc), br->GetY(doc) + br->sy, br->sx, 0);
                }
            } else {
                if (sel.x < xs) {
                    auto tl = C(sel.x, sel.y).get();
                    return wxRect(tl->GetX(doc), tl->GetY(doc), 0, tl->sy);
                } else {
                    auto br = C(xs - 1, sel.y).get();
                    return wxRect(br->GetX(doc) + br->sx, br->GetY(doc), 0, br->sy);
                }
            }
        } else {
            auto tl = C(sel.x, sel.y).get();
            auto br = C(sel.x + sel.xs - 1, sel.y + sel.ys - 1).get();
            wxRect r(tl->GetX(doc) - cell_margin, tl->GetY(doc) - cell_margin,
                     br->GetX(doc) + br->sx - tl->GetX(doc) + cell_margin * 2,
                     br->GetY(doc) + br->sy - tl->GetY(doc) + cell_margin * 2);
            if (minimal && tl == br) r.width -= tl->sx - tl->minx;
            return r;
        }
    }

    void DrawSelect(Document *doc, wxDC &dc, Selection &sel) {
        if (sel.Thin()) {
            DrawInsert(doc, dc, sel, 0);
        } else {
            dc.SetBrush(wxBrush(LightColor(0x000000)));
            dc.SetPen(wxPen(LightColor(0x000000)));
            wxRect g = GetRect(doc, sel);
            int lw = g_line_width;
            int te = sel.TextEdit();
            dc.DrawRectangle(g.x - 1 - lw, g.y - 1 - lw, g.width + 2 + 2 * lw, 2 + lw - te);
            dc.DrawRectangle(g.x - 1 - lw, g.y - 1 + g.height + te, g.width + 2 + 2 * lw - 5,
                             2 + lw - te);

            dc.DrawRectangle(g.x - 1 - lw, g.y + 1 - te, 2 + lw - te, g.height - 2 + 2 * te);
            dc.DrawRectangle(g.x - 1 + g.width + te, g.y + 1 - te, 2 + lw - te,
                             g.height - 2 + 2 * te - 2 - te);

            dc.DrawRectangle(g.x + g.width, g.y + g.height - 2, lw + 2, lw + 4);
            dc.DrawRectangle(g.x + g.width - lw - 1, g.y + g.height - 2 + 2 * te, lw + 1,
                             lw + 4 - 2 * te);
            if (sel.TextEdit()) {
                DrawCursor(doc, dc, sel, true, sys->cursorcolor);
            } else {
                HintIMELocation(doc, g.x, g.y, g_deftextsize * 1.333, 0);
            }
        }
    }

    static int MapDeletedLine(int v, int deleted) {
        return deleted < 0 ? v : (v <= deleted ? v : v + 1);
    }

    static int MapDeletedSegment(int v, int deleted) {
        return deleted < 0 ? v : (v < deleted ? v : v + 1);
    }

    static int MapInsertedLine(int v, int inserted, int count) {
        if (count <= 0) return v;
        if (v <= inserted) return v;
        if (v >= inserted + count) return v - count;
        return -1;
    }

    static int MapInsertedSegment(int v, int inserted, int count) {
        if (count <= 0) return v;
        if (v < inserted) return v;
        if (v >= inserted + count) return v - count;
        return -1;
    }

    void RestoreBorderStylesAfterDelete(int oldxs, int oldys, int dx, int dy,
                                        const vector<BorderLineStyle> &oldv,
                                        const vector<BorderLineStyle> &oldh) {
        vborderstyles.assign((xs + 1) * ys, BorderLineStyle());
        hborderstyles.assign(xs * (ys + 1), BorderLineStyle());
        for (int y = 0; y < ys; y++) {
            int oy = MapDeletedSegment(y, dy);
            for (int x = 0; x <= xs; x++) {
                int ox = MapDeletedLine(x, dx);
                int oldi = ox + oy * (oldxs + 1);
                if (oldi >= 0 && static_cast<size_t>(oldi) < oldv.size())
                    VBorder(x, y) = oldv[oldi];
            }
        }
        for (int y = 0; y <= ys; y++) {
            int oy = MapDeletedLine(y, dy);
            for (int x = 0; x < xs; x++) {
                int ox = MapDeletedSegment(x, dx);
                int oldi = ox + oy * oldxs;
                if (oldi >= 0 && static_cast<size_t>(oldi) < oldh.size())
                    HBorder(x, y) = oldh[oldi];
            }
        }
    }

    void RestoreBorderStylesAfterInsert(int oldxs, int oldys, int dx, int dy, int nxs, int nys,
                                        const vector<BorderLineStyle> &oldv,
                                        const vector<BorderLineStyle> &oldh) {
        vborderstyles.assign((xs + 1) * ys, BorderLineStyle());
        hborderstyles.assign(xs * (ys + 1), BorderLineStyle());
        for (int y = 0; y < ys; y++) {
            int oy = MapInsertedSegment(y, dy, nys);
            if (oy < 0) continue;
            for (int x = 0; x <= xs; x++) {
                int ox = MapInsertedLine(x, dx, nxs);
                if (ox < 0) continue;
                int oldi = ox + oy * (oldxs + 1);
                if (oldi >= 0 && static_cast<size_t>(oldi) < oldv.size())
                    VBorder(x, y) = oldv[oldi];
            }
        }
        for (int y = 0; y <= ys; y++) {
            int oy = MapInsertedLine(y, dy, nys);
            if (oy < 0) continue;
            for (int x = 0; x < xs; x++) {
                int ox = MapInsertedSegment(x, dx, nxs);
                if (ox < 0) continue;
                int oldi = ox + oy * oldxs;
                if (oldi >= 0 && static_cast<size_t>(oldi) < oldh.size())
                    HBorder(x, y) = oldh[oldi];
            }
        }
    }

    void DeleteCells(int dx, int dy, int nxs, int nys) {
        UnmergeAll();
        int oldxs = xs;
        int oldys = ys;
        auto oldvborderstyles = vborderstyles;
        auto oldhborderstyles = hborderstyles;
        vector<unique_ptr<Cell>> ncells;
        ncells.reserve((xs + nxs) * (ys + nys));
        foreachcell(c) if (!(x == dx || y == dy)) ncells.push_back(std::move(c));
        cells = std::move(ncells);
        xs += nxs;
        ys += nys;
        if (dx >= 0) colwidths.erase(colwidths.begin() + dx);
        RestoreBorderStylesAfterDelete(oldxs, oldys, dx, dy, oldvborderstyles, oldhborderstyles);
        SetOrient();
    }

    void MultiCellDelete(Document *doc, Selection &sel) {
        cell->AddUndo(doc);
        MultiCellDeleteSub(doc, sel);
        if (doc->canvas) doc->canvas->Refresh();
    }

    void MultiCellDeleteSub(Document *doc, Selection &sel) {
        UnmergeIntersecting(sel);
        foreachcellinsel(c, sel) c->Clear();
        bool delhoriz = true, delvert = true;
        foreachcell(c) {
            if (c->HasContent()) {
                if (y >= sel.y && y < sel.y + sel.ys) delhoriz = false;
                if (x >= sel.x && x < sel.x + sel.xs) delvert = false;
            }
        }
        if (delhoriz && (!delvert || sel.xs >= sel.ys)) {
            if (sel.ys == ys) {
                DelSelf(doc, sel);
            } else {
                loop(i, sel.ys) DeleteCells(-1, sel.y, 0, -1);
                sel.ys = 0;
                sel.xs = 1;
            }
        } else if (delvert) {
            if (sel.xs == xs) {
                DelSelf(doc, sel);
            } else {
                loop(i, sel.xs) DeleteCells(sel.x, -1, -1, 0);
                sel.xs = 0;
                sel.ys = 1;
            }
        } else {
            auto c = sel.GetCell();
            if (c) sel.EnterEdit(doc);
        }
    }

    wxString MergeCells(Document *doc, Selection &sel) {
        if (sel.Thin()) return doc->NoThin();
        if (sel.xs * sel.ys <= 1) return _("More than one cell must be selected.");
        cell->AddUndo(doc);
        UnmergeIntersecting(sel);
        auto master = C(sel.x, sel.y).get();
        foreachcellinsel(c, sel) {
            if (c.get() == master) continue;
            c->Clear();
            c->mergexs = 0;
            c->mergeys = 0;
        }
        master->mergexs = sel.xs;
        master->mergeys = sel.ys;
        sel.xs = 1;
        sel.ys = 1;
        cell->ResetChildren();
        if (doc->canvas) doc->canvas->Refresh();
        return wxEmptyString;
    }

    wxString UnmergeCells(Document *doc, Selection &sel) {
        if (sel.Thin()) return doc->NoThin();
        if (!HasMergedIntersecting(sel)) return _("No merged cells in selection.");
        cell->AddUndo(doc);
        UnmergeIntersecting(sel);
        cell->ResetChildren();
        if (doc->canvas) doc->canvas->Refresh();
        return wxEmptyString;
    }

    void DelSelf(Document *doc, Selection &s) {
        if (!doc->drawpath.empty() && doc->drawpath.back().grid == cell->grid) {
            doc->drawpath.pop_back();
            doc->currentdrawroot = doc->WalkPath(doc->drawpath);
        }
        if (!cell->parent) return;  // FIXME: deletion of root cell, what would be better?
        s = cell->parent->grid->FindCell(cell);
        cell->grid = nullptr;
    }

    void InsertCells(int dx, int dy, int nxs, int nys, unique_ptr<Cell> nc = nullptr) {
        UnmergeAll();
        vector<unique_ptr<Cell>> ocells = std::move(cells);
        auto oldvborderstyles = vborderstyles;
        auto oldhborderstyles = hborderstyles;
        int oxs = xs;
        int oys = ys;
        xs += nxs;
        ys += nys;
        cells.resize(xs * ys);
        SetOrient();
        int opos = 0;
        foreachcell(c) {
            if ((nxs > 0 && x >= dx && x < dx + nxs) || (nys > 0 && y >= dy && y < dy + nys)) {
                if (nc) {
                    c = std::move(nc);
                } else {
                    int sx = nxs ? max(0, min(dx - 1, xs - 1)) : x;
                    int sy = nys ? max(0, min(dy - 1, ys - 1)) : y;
                    Cell *colcell = C(sx, sy).get();
                    c = make_unique<Cell>(cell, colcell);
                    if (colcell) c->text.relsize = colcell->text.relsize;
                }
            } else {
                c = std::move(ocells[opos++]);
            }
        }
        if (dx >= 0 && nxs > 0) colwidths.insert(colwidths.begin() + dx, nxs, cell->ColWidth());
        RestoreBorderStylesAfterInsert(oxs, oys, dx, dy, nxs, nys, oldvborderstyles,
                                       oldhborderstyles);
    }

    void SaveBorderStyles(wxDataOutputStream &dos) const {
        for (const auto &style : vborderstyles) {
            dos.Write8(style.width);
            dos.Write32(style.color);
        }
        for (const auto &style : hborderstyles) {
            dos.Write8(style.width);
            dos.Write32(style.color);
        }
    }

    void LoadBorderStyles(wxDataInputStream &dis) {
        InitBorderStyles();
        for (auto &style : vborderstyles) {
            style.width = dis.Read8();
            style.color = dis.Read32() & 0xFFFFFF;
        }
        for (auto &style : hborderstyles) {
            style.width = dis.Read8();
            style.color = dis.Read32() & 0xFFFFFF;
        }
    }

    void Save(wxDataOutputStream &dos, Cell *ocs) const {
        dos.Write32(xs);
        dos.Write32(ys);
        dos.Write32(bordercolor);
        dos.Write32(user_grid_outer_spacing);
        dos.Write8(cell->verticaltextandgrid);
        dos.Write8(folded);
        loop(x, xs) dos.Write32(colwidths[x]);
        SaveBorderStyles(dos);
        foreachcellconst(c) c->Save(dos, ocs);
    }

    bool LoadContents(wxDataInputStream &dis, int &numcells, int &textbytes, Cell *&ics) {
        if (sys->versionlastloaded >= 10) {
            bordercolor = dis.Read32() & 0xFFFFFF;
            user_grid_outer_spacing = dis.Read32();
            if (sys->versionlastloaded >= 11) {
                cell->verticaltextandgrid = dis.Read8() != 0;
                if (sys->versionlastloaded >= 13) {
                    if (sys->versionlastloaded >= 16) {
                        folded = dis.Read8() != 0;
                        if (folded && sys->versionlastloaded <= 17) {
                            // Before v18, folding would use the image slot. So if this cell
                            // contains an image, clear it.
                            cell->text.image = nullptr;
                        }
                    }
                    loop(x, xs) colwidths[x] = dis.Read32();
                    if (sys->versionlastloaded >= 28) LoadBorderStyles(dis);
                }
            }
        }
        if (sys->versionlastloaded < 28) InitBorderStyles();
        foreachcell(c) {
            Cell *rc = Cell::LoadWhich(dis, cell, numcells, textbytes, ics);
            if (!rc) return false;
            c.reset(rc);
        }
        if (sys->versionlastloaded >= 26) RepairMergedCells();
        return true;
    }

    void Formatter(wxString &r, int format, int indent, const wxString &xml, const wxString &html,
                   const wxString &htmlb) {
        if (format == A_EXPXML) {
            r.Append(' ', indent);
            r.Append(xml);
        } else if (format == A_EXPHTMLT || format == A_EXPHTMLTI || format == A_EXPHTMLTE) {
            r.Append(' ', indent);
            r.Append(html);
        } else if (format == A_EXPHTMLB && !htmlb.IsEmpty()) {
            r.Append(' ', indent);
            r.Append(htmlb);
        }
    }

    wxString ToText(int indent, const Selection &sel, int format, Document *doc, bool inheritstyle,
                    Cell *root) {
        return ConvertToText(SelectAll(), indent + 2, format, doc, inheritstyle, root);
    };

    wxString ConvertToText(const Selection &sel, int indent, int format, Document *doc,
                           bool inheritstyle, Cell *root) {
        if (format == A_EXPJSON) {
            wxString json = JSONIndent(indent) + "{\n";
            bool first = true;

            JSONAppendField(json, indent + 2, "columns", wxString::Format("%d", sel.xs),
                            first);
            JSONAppendField(json, indent + 2, "rows", wxString::Format("%d", sel.ys), first);
            JSONAppendField(json, indent + 2, "folded", JSONBool(folded), first);
            JSONAppendField(json, indent + 2, "border_color", JSONColor(bordercolor), first);
            JSONAppendField(json, indent + 2, "outer_spacing",
                            wxString::Format("%d", user_grid_outer_spacing), first);

            wxString rows = JSONIndent(indent + 2) + "[\n";
            for (int yy = sel.y; yy < sel.y + sel.ys; yy++) {
                if (yy != sel.y) rows += ",\n";
                rows += JSONIndent(indent + 4) + "[\n";
                for (int xx = sel.x; xx < sel.x + sel.xs; xx++) {
                    if (xx != sel.x) rows += ",\n";
                    rows += C(xx, yy)->ToText(indent + 6, sel, format, doc, inheritstyle, root);
                }
                rows += "\n" + JSONIndent(indent + 4) + "]";
            }
            rows += "\n" + JSONIndent(indent + 2) + "]";
            JSONAppendField(json, indent + 2, "cells", rows, first);

            json += "\n" + JSONIndent(indent) + "}";
            return json;
        }

        wxString r;
        const int root_grid_spacing = 2;  // Can't be adjusted in editor, so use a default.
        const int font_size = 14 - indent / 2;
        const int grid_border_width =
            cell == doc->root.get() ? root_grid_spacing : user_grid_outer_spacing - 1;

        wxString xmlstr("<grid");
        if (folded) xmlstr.Append(wxString::Format(" folded=\"%d\"", folded));
        if (bordercolor != g_bordercolor_default) {
            xmlstr.Append(wxString::Format(" bordercolor=\"0x%06X\"", bordercolor));
        }
        if (user_grid_outer_spacing != g_usergridouterspacing_default) {
            xmlstr.Append(wxString::Format(" outerspacing=\"%d\"", user_grid_outer_spacing));
        }
        xmlstr.Append(">\n");

        Formatter(r, format, indent, xmlstr,
                  wxString::Format("<table style=\"border-width: %dpt; font-size: %dpt;\">\n",
                                   grid_border_width, font_size),
                  wxString::Format("<ul style=\"font-size: %dpt;\">\n", font_size));
        foreachcellinsel(c, sel) {
            if (x == sel.x) Formatter(r, format, indent, "<row>\n", "<tr>\n", "");
            r.Append(c->ToText(indent, sel, format, doc, inheritstyle, root));
            if (format == A_EXPCSV) r.Append(x == sel.x + sel.xs - 1 ? '\n' : ',');
            if (x == sel.x + sel.xs - 1) Formatter(r, format, indent, "</row>\n", "</tr>\n", "");
        }
        Formatter(r, format, indent, "</grid>\n", "</table>\n", "</ul>\n");
        return r;
    }

    void RelSize(int dir, int zoomdepth) { foreachcell(c) c->RelSize(dir, zoomdepth); }
    void RelSize(int dir, const Selection &sel, int zoomdepth) {
        foreachcellinsel(c, sel) c->RelSize(dir, zoomdepth);
    }
    void SetBorderWidth(int visible_width) {
        user_grid_outer_spacing = max(0, visible_width) + 1;
    }

    void SetBorderStyle(BorderLineStyle &style, uint color, int visible_width, bool set_width) {
        style.color = color;
        if (set_width) {
            style.width = max(0, visible_width);
        } else if (!style.Visible()) {
            style.width = DefaultSelectionBorderWidth();
        }
    }

    struct BorderLineRange {
        bool horizontal {false};
        int x {0};
        int y {0};
        int length {0};
    };

    bool BorderLineRangeForSelection(const Selection &sel, BorderLineRange &range) const {
        if (!sel.Thin()) return false;
        if (sel.xs == 1 && sel.ys == 0 && sel.x >= 0 && sel.x < xs && sel.y >= 0 &&
            sel.y <= ys) {
            range = {true, sel.x, sel.y, 1};
            foreachcellconst(c) if (!IsCovered(x, y)) {
                int mxs = MergeXS(x, y);
                int mys = MergeYS(x, y);
                if (sel.x < x || sel.x >= x + mxs) continue;
                if (sel.y > y && sel.y < y + mys) return false;
            }
            return true;
        }
        if (sel.xs == 0 && sel.ys == 1 && sel.x >= 0 && sel.x <= xs && sel.y >= 0 &&
            sel.y < ys) {
            range = {false, sel.x, sel.y, 1};
            foreachcellconst(c) if (!IsCovered(x, y)) {
                int mxs = MergeXS(x, y);
                int mys = MergeYS(x, y);
                if (sel.y < y || sel.y >= y + mys) continue;
                if (sel.x > x && sel.x < x + mxs) return false;
            }
            return true;
        }
        return false;
    }

    bool BorderStyleNeedsPaint(const BorderLineStyle &style, uint color) const {
        auto width = style.Visible() ? style.width : DefaultSelectionBorderWidth();
        return style.color != color || style.width != width;
    }

    Selection ExpandSelectionToMergedCells(const Selection &sel) const {
        if (sel.Thin()) return sel;
        Selection expanded = sel;
        bool changed;
        do {
            changed = false;
            foreachcellconst(c) if (!IsCovered(x, y)) {
                int mxs = MergeXS(x, y);
                int mys = MergeYS(x, y);
                if (!MergeIntersects(x, y, mxs, mys, expanded)) continue;
                int nx = min(expanded.x, x);
                int ny = min(expanded.y, y);
                int nx2 = max(expanded.x + expanded.xs, x + mxs);
                int ny2 = max(expanded.y + expanded.ys, y + mys);
                if (nx != expanded.x || ny != expanded.y || nx2 != expanded.x + expanded.xs ||
                    ny2 != expanded.y + expanded.ys) {
                    expanded.x = nx;
                    expanded.y = ny;
                    expanded.xs = nx2 - nx;
                    expanded.ys = ny2 - ny;
                    changed = true;
                }
            }
        } while (changed);
        return expanded;
    }

    void SetBorderLine(const Selection &sel, uint color, int visible_width, bool set_width) {
        BorderLineRange range;
        if (!BorderLineRangeForSelection(sel, range)) return;
        for (int i = 0; i < range.length; i++) {
            auto &style =
                range.horizontal ? HBorder(range.x + i, range.y) : VBorder(range.x, range.y + i);
            SetBorderStyle(style, color, visible_width, set_width);
        }
    }

    BorderLineStyle *BorderLineForSelection(const Selection &sel) {
        BorderLineRange range;
        if (!BorderLineRangeForSelection(sel, range)) return nullptr;
        return range.horizontal ? &HBorder(range.x, range.y) : &VBorder(range.x, range.y);
    }

    const BorderLineStyle *BorderLineForSelection(const Selection &sel) const {
        BorderLineRange range;
        if (!BorderLineRangeForSelection(sel, range)) return nullptr;
        return range.horizontal ? &HBorder(range.x, range.y) : &VBorder(range.x, range.y);
    }

    bool BorderLineNeedsPaint(const Selection &sel, uint color) const {
        BorderLineRange range;
        if (!BorderLineRangeForSelection(sel, range)) return false;
        for (int i = 0; i < range.length; i++) {
            const auto &style =
                range.horizontal ? HBorder(range.x + i, range.y) : VBorder(range.x, range.y + i);
            if (BorderStyleNeedsPaint(style, color)) return true;
        }
        return false;
    }

    bool PaintBorderLine(const Selection &sel, uint color) {
        BorderLineRange range;
        if (!BorderLineRangeForSelection(sel, range)) return false;
        if (!BorderLineNeedsPaint(sel, color)) return false;
        SetBorderLine(sel, color, 0, false);
        return true;
    }

    void SetSelectionOuterBorder(const Selection &sel, uint color, int visible_width,
                                 bool set_width) {
        auto expanded = ExpandSelectionToMergedCells(sel);
        for (int x = expanded.x; x < expanded.x + expanded.xs; x++) {
            SetBorderLine(Selection(cell->grid, x, expanded.y, 1, 0), color, visible_width,
                          set_width);
            SetBorderLine(Selection(cell->grid, x, expanded.y + expanded.ys, 1, 0), color,
                          visible_width, set_width);
        }
        for (int y = expanded.y; y < expanded.y + expanded.ys; y++) {
            SetBorderLine(Selection(cell->grid, expanded.x, y, 0, 1), color, visible_width,
                          set_width);
            SetBorderLine(Selection(cell->grid, expanded.x + expanded.xs, y, 0, 1), color,
                          visible_width, set_width);
        }
    }

    void SetSelectionInnerBorder(const Selection &sel, uint color, int visible_width,
                                 bool set_width) {
        auto expanded = ExpandSelectionToMergedCells(sel);
        for (int x = expanded.x + 1; x < expanded.x + expanded.xs; x++)
            for (int y = expanded.y; y < expanded.y + expanded.ys; y++)
                SetBorderLine(Selection(cell->grid, x, y, 0, 1), color, visible_width, set_width);
        for (int y = expanded.y + 1; y < expanded.y + expanded.ys; y++)
            for (int x = expanded.x; x < expanded.x + expanded.xs; x++)
                SetBorderLine(Selection(cell->grid, x, y, 1, 0), color, visible_width, set_width);
    }

    void ClearSelectionBorders(const Selection &sel) {
        auto expanded = ExpandSelectionToMergedCells(sel);
        for (int y = expanded.y; y <= expanded.y + expanded.ys; y++)
            for (int x = expanded.x; x < expanded.x + expanded.xs; x++)
                SetBorderStyle(HBorder(x, y), g_bordercolor_default, 0, true);
        for (int y = expanded.y; y < expanded.y + expanded.ys; y++)
            for (int x = expanded.x; x <= expanded.x + expanded.xs; x++)
                SetBorderStyle(VBorder(x, y), g_bordercolor_default, 0, true);
    }

    void ClearAllCustomBorders() {
        ranges::fill(vborderstyles, BorderLineStyle());
        ranges::fill(hborderstyles, BorderLineStyle());
    }

    int MinRelsize(int rs) {
        foreachcell(c) {
            int crs = c->MinRelsize();
            rs = min(rs, crs);
        }
        return rs;
    }

    void ResetChildren() {
        cell->Reset();
        foreachcell(c) c->ResetChildren();
    }

    void Move(int dx, int dy, const Selection &sel) {
        if (dx < 0 || dy < 0)
            foreachcellinsel(c, sel) std::swap(c, C((x + dx + xs) % xs, (y + dy + ys) % ys));
        else
            foreachcellinselrev(c, sel) std::swap(c, C((x + dx + xs) % xs, (y + dy + ys) % ys));
    }

    void Add(unique_ptr<Cell> c) {
        c->parent = cell;
        if (horiz)
            InsertCells(xs, -1, 1, 0, std::move(c));
        else
            InsertCells(-1, ys, 0, 1, std::move(c));
    }

    void MergeWithParent(shared_ptr<Grid> p, Selection &sel, Document *doc) {
        shared_ptr<Grid> keepalive = cell->grid;
        int nxs = sel.x + xs - p->xs;
        int nys = sel.y + ys - p->ys;
        if (nxs > 0 || nys > 0)
            p->InsertCells(nxs > 0 ? p->xs : -1, nys > 0 ? p->ys : -1, nxs > 0 ? nxs : 0,
                           nys > 0 ? nys : 0);
        foreachcell(c) {
            int tx = x + sel.x;
            int ty = y + sel.y;
            p->C(tx, ty) = std::move(c);
            if (p->C(tx, ty)) {
                p->C(tx, ty)->parent = p->cell;
            }
        }
        sel.grid = p;
        sel.xs += xs - 1;
        sel.ys += ys - 1;
        sel.ExitEdit(doc);
    }

    void SetStyle(Document *doc, const Selection &sel, int sb) {
        cell->AddUndo(doc);
        cell->ResetChildren();
        if (sel.TextEdit()) {
            if (auto c = sel.GetCell(); c && c->text.HasSelectionRange(sel)) {
                c->text.ToggleRichStyle(sel, sb, c->textcolor, c->text.stylebits);
            } else if (auto c = sel.GetCell()) {
                c->text.ToggleBaseStyleBit(sb);
            }
        } else {
            foreachcellinsel(c, sel) {
                c->text.ToggleBaseStyleBit(sb);
            }
        }
        if (doc->canvas) doc->canvas->Refresh();
    }

    void ColorChange(Document *doc, int which, uint color, const Selection &sel) {
        cell->AddUndo(doc);
        cell->ResetChildren();
        if (which == A_BORDCOLOR) {
            bordercolor = color;
            if (doc->canvas) doc->canvas->Refresh();
            return;
        }
        if (which == A_TEXTCOLOR && sel.TextEdit()) {
            if (auto c = sel.GetCell(); c && c->text.HasSelectionRange(sel)) {
                c->text.SetRichColor(sel, color, c->textcolor, c->text.stylebits);
                if (doc->canvas) doc->canvas->Refresh();
                return;
            }
        }
        foreachcellinsel(c, sel) c->ColorChange(doc, which, color);
        if (doc->canvas) doc->canvas->Refresh();
    }

    void ReplaceStr(Document *doc, const wxString &s, const wxString &ls, const Selection &sel) {
        cell->AddUndo(doc);
        cell->ResetChildren();
        foreachcellinsel(c, sel) c->text.ReplaceStr(s, ls);
        doc->canvas->Refresh();
    }

    void CSVImport(const wxArrayString &as, const wxString &sep) {
        int cy = 0;
        loop(y, as.size()) {
            auto s = as[y];
            wxString word;
            if (cy >= ys) InsertCells(-1, cy, 0, 1);
            for (int x = 0;; x++) {
                bool hasmore = false;
                if (!s.empty() && s[0] == '\"') {
                    word = "";
                    for (int i = 1;; i++) {
                        if (!s[i]) {
                            if (y < static_cast<int>(as.size()) - 1) {
                                word += LINE_SEPARATOR;
                                s = as[++y];
                                i = -1;
                            } else {
                                s = L"";
                                break;
                            }
                        } else if (s[i] == '\"') {
                            if (s[i + 1] == '\"')
                                word += s[++i];
                            else {
                                auto afterquote = i + 1;
                                if (s.Mid(afterquote, sep.Len()) == sep) {
                                    s = s.Mid(afterquote + sep.Len());
                                    hasmore = true;
                                } else {
                                    s = wxString(L"");
                                }
                                break;
                            }
                        } else
                            word += s[i];
                    }
                } else {
                    int pos = s.Find(sep);
                    if (pos < 0) {
                        word = s;
                        s = "";
                    } else {
                        word = s.Left(pos);
                        s = s.Mid(pos + sep.Len());
                        hasmore = true;
                    }
                }
                if (x >= xs) InsertCells(x, -1, 1, 0);
                Cell *c = C(x, cy).get();
                c->text.t = word;
                if (!hasmore) break;
            }
            cy++;
        }

        ys = cy;  // throws memory away, but doesn't matter
        InitBorderStyles();
    }

    unique_ptr<Cell> EvalGridCell(auto &ev, unique_ptr<Cell> &c, unique_ptr<Cell> acc, int &x,
                                  int &y, bool &alldata, bool vert) {
        int ct = c->celltype;  // Type of subcell being evaluated
        // Update alldata condition (variable reads act like data)
        alldata = alldata && (ct == CT_DATA || ct == CT_VARU);
        ev.vert = vert;  // Inform evaluatour of vert status. (?)
        switch (ct) {
            // Var assign
            case CT_VARD: {
                if (vert) return acc;  // (Reject vertical assignments)
                // If we have no data, lets see if we can generate something useful from the
                // subgrid.
                if (!acc->grid && acc->text.t.IsEmpty()) {
                    acc = c->Eval(ev);
                    if (!acc) { return nullptr; }
                }
                // Assign the current data temporary to the text
                ev.Assign(c.get(), acc.get());
                // Pass the original data onwards
                return acc;
            }
            // View
            case CT_VIEWV:
            case CT_VIEWH:
                if (vert ? ct == CT_VIEWH : ct == CT_VIEWV) { return c->Clone(nullptr); }
                c = acc ? acc->Clone(cell) : make_unique<Cell>(cell);
                c->celltype = ct;
                return acc;
            // Operation
            case CT_CODE: {
                auto op = ev.FindOp(c->text.t);
                switch (op ? strlen(op->args) : -1) {
                    default: return nullptr;
                    case 0: return ev.Execute(op);
                    case 1: return acc ? ev.Execute(op, std::move(acc)) : nullptr;
                    case 2:
                        if (vert) {
                            if (acc && y + 1 < ys) {
                                return ev.Execute(op, std::move(acc), C(x, ++y).get());
                            } else {
                                return nullptr;
                            }
                        } else {
                            if (acc && x + 1 < xs) {
                                return ev.Execute(op, std::move(acc), C(++x, y).get());
                            } else {
                                return nullptr;
                            }
                        }
                    case 3:
                        if (vert) {
                            if (acc && y + 2 < ys) {
                                y += 2;
                                return ev.Execute(op, std::move(acc), C(x, y - 1).get(),
                                                  C(x, y).get());
                            } else {
                                return nullptr;
                            }
                        } else {
                            if (acc && x + 2 < xs) {
                                x += 2;
                                return ev.Execute(op, std::move(acc), C(x - 1, y).get(),
                                                  C(x, y).get());
                            } else {
                                return nullptr;
                            }
                        }
                }
            }
            // Var read, Data
            default: return c->Eval(ev);
        }
    }

    unique_ptr<Cell> Eval(auto &ev) {
        unique_ptr<Cell> acc;  // Actual/Accumulating data temporary
        bool alldata = true;   // Is the grid all data?
        // Do left to right processing
        if (xs > 1 || ys == 1) foreachcell(c) {
                if (x == 0) acc.reset();
                acc = EvalGridCell(ev, c, std::move(acc), x, y, alldata, false);
            }
        // Do top to bottom processing
        if (ys > 1) foreachcellcolumn(c) {
                if (y == 0) acc.reset();
                acc = EvalGridCell(ev, c, std::move(acc), x, y, alldata, true);
            }
        // If all data is true then we can exit now.
        if (alldata) {
            auto result = cell->Clone(nullptr);  // Potential result if all data.
            foreachcellingrid(rc, result->grid) { rc = rc->Eval(ev); }
            return result;
        }
        return acc;
    }

    void Split(vector<shared_ptr<Grid>> &gs, bool vert) {
        loop(i, vert ? xs : ys) gs.push_back(make_unique<Grid>(vert ? 1 : xs, vert ? ys : 1));
        foreachcell(c) {
            auto g = gs[vert ? x : y].get();
            c->SetParent(g->cell);
            g->cells[vert ? y : x] = std::move(c);
        }
    }

    unique_ptr<Cell> Sum() {
        double total = 0;
        foreachcell(c) {
            if (c->HasText()) total += c->text.GetNum();
        }
        auto c = make_unique<Cell>();
        c->text.SetNum(total);
        return c;
    }

    void Transpose() {
        int oldxs = xs;
        int oldys = ys;
        auto oldvborderstyles = vborderstyles;
        auto oldhborderstyles = hborderstyles;
        vector<unique_ptr<Cell>> tr(xs * ys);
        foreachcell(c) tr[y + x * ys] = std::move(c);
        cells = std::move(tr);
        swap_(xs, ys);
        SetOrient();
        InitColWidths();
        InitBorderStyles();
        for (int y = 0; y < oldys; y++)
            for (int x = 0; x <= oldxs; x++)
                HBorder(y, x) = oldvborderstyles[x + y * (oldxs + 1)];
        for (int y = 0; y <= oldys; y++)
            for (int x = 0; x < oldxs; x++)
                VBorder(y, x) = oldhborderstyles[x + y * oldxs];
    }

    void Sort(Selection &sel, bool descending) {
        vector<int> row_indices(sel.ys);
        loop(i, sel.ys) row_indices[i] = i;

        std::stable_sort(row_indices.begin(), row_indices.end(), [&](int i, int j) {
            loop(k, xs) {
                int col = (k + sel.x) % xs;
                int cmp = C(col, sel.y + i)->text.t.CmpNoCase(C(col, sel.y + j)->text.t);
                if (cmp) return descending ? cmp > 0 : cmp < 0;
            }
            return false;
        });

        vector<unique_ptr<Cell>> new_cells;
        new_cells.reserve(sel.ys * xs);
        for (int i : row_indices) {
            loop(c, xs) {
                new_cells.push_back(std::move(C(c, sel.y + i)));
            }
        }

        loop(i, sel.ys * xs) {
            cells[sel.y * xs + i] = std::move(new_cells[i]);
        }
    }

    Cell *FindExact(const wxString &s) {
        foreachcell(c) {
            auto f = c->FindExact(s);
            if (f) return f;
        }
        return nullptr;
    }

    Selection HierarchySwap(wxString tag) {
        Cell *selcell = nullptr;
        bool done = false;
    lookformore:
        foreachcell(c) if (c->grid && !done) {
            auto f = c->grid->FindExact(tag);
            if (f) {
                // add all parent tags as extra hierarchy inside the cell
                for (auto p = f->parent; p != cell; p = p->parent) {
                    // Special case check: if parents have same name, this would cause infinite
                    // swapping.
                    if (p->text.t == tag) done = true;
                    auto t = make_unique<Cell>(f, p);
                    t->text = p->text;
                    t->text.cell = t.get();
                    t->text.ResetImageCache();
                    t->note = p->note;
                    t->grid = f->grid;
                    if (t->grid) t->grid->ReParent(t.get());
                    f->grid = make_shared<Grid>(1, 1);
                    f->grid->cell = f;
                    f->grid->cells[0] = std::move(t);
                }
                // remove cell from parent, recursively if parent becomes empty
                for (auto r = f; r && r != cell; r = r->parent->grid->DeleteTagParent(r, cell, f));
                // merge newly constructed hierarchy at this level
                if (!cells[0]) {
                    cells[0].reset(f);
                    f->parent = cell;
                    selcell = f;
                } else {
                    MergeTagCell(unique_ptr<Cell>(f), selcell);
                }
                goto lookformore;
            }
        }
        ASSERT(selcell);
        return FindCell(selcell);
    }

    void ReParent(auto p) {
        cell = p;
        foreachcell(c) c->parent = p;
    }

    Cell *DeleteTagParent(Cell *tag, Cell *basecell, Cell *found) {
        shared_ptr<Grid> keepalive = cell->grid;
        Cell *next = tag->parent;
        unique_ptr<Cell> detached_tag;
        int found_x = -1, found_y = -1;
        foreachcell(c) if (c.get() == tag) {
            detached_tag = std::move(c);
            found_x = x;
            found_y = y;
            break;
        }
        ASSERT(detached_tag);
        if (xs * ys == 1) {
            if (cell != basecell) {
                cell->grid = nullptr;
            }
            if (tag == found) detached_tag.release();
            return next;
        } else {
            if (ys > 1)
                DeleteCells(-1, found_y, 0, -1);
            else
                DeleteCells(found_x, -1, -1, 0);
            if (tag == found) detached_tag.release();
            return nullptr;
        }
    }

    void MergeTagCell(unique_ptr<Cell> f, Cell *&selcell) {
        foreachcell(c) if (c->text.t == f->text.t) {
            if (!selcell) selcell = c.get();

            if (f->grid) {
                if (c->grid) {
                    f->grid->MergeTagAll(c.get());
                } else {
                    c->grid = f->grid;
                    c->grid->ReParent(c.get());
                    f->grid = nullptr;
                }
            }
            return;
        }
        if (!selcell) selcell = f.get();
        Add(std::move(f));
    }

    void MergeTagAll(Cell *into) {
        foreachcell(c) {
            into->grid->MergeTagCell(std::move(c), into /*dummy*/);
        }
    }

    void SetGridTextLayout(int ds, bool vert, bool noset, const Selection &sel, int depth = -1) {
        foreachcellinsel(c, sel) c->SetGridTextLayout(ds, vert, noset, depth);
    }

    bool IsTable() {
        foreachcell(c) if (c->grid) return false;
        return true;
    }

    void Hierarchify(Document *doc) {
        loop(y, ys) {
            unique_ptr<Cell> rest;
            if (xs > 1) {
                Selection s(cell->grid, 1, y, xs - 1, 1);
                rest = CloneSel(s);
            }
            Cell *c = C(0, y).get();
            loop(prevy, y) {
                if (Cell *prev = C(0, prevy).get(); prev->text.t == c->text.t) {
                    if (rest) {
                        ASSERT(prev->grid);
                        prev->grid->MergeRow(rest->grid.get());
                        rest = nullptr;
                    }

                    Selection s(cell->grid, 0, y, xs, 1);
                    MultiCellDeleteSub(doc, s);
                    y--;

                    goto done;
                }
            }
            if (rest) {
                std::swap(c->grid, rest->grid);
                c->grid->ReParent(c);
            }
        done:;
        }
        Selection s(cell->grid, 1, 0, xs - 1, ys);
        MultiCellDeleteSub(doc, s);
        foreachcell(c) if (c->grid && c->grid->xs > 1) c->grid->Hierarchify(doc);
    }

    void MergeRow(Grid *tm) {
        ASSERT(xs == tm->xs && tm->ys == 1);
        InsertCells(-1, ys, 0, 1);
        loop(x, xs) {
            std::swap(C(x, ys - 1), tm->C(x, 0));
            C(x, ys - 1)->parent = cell;
        }
    }

    void MaxDepthLeaves(int curdepth, int &maxdepth, int &leaves) {
        foreachcell(c) c->MaxDepthLeaves(curdepth, maxdepth, leaves);
    }

    int Flatten(int curdepth, int cury, Grid *g) {
        foreachcell(c) if (c->grid) { cury = c->grid->Flatten(curdepth + 1, cury, g); }
        else {
            Cell *ic = c.get();
            for (int i = curdepth; i >= 0; i--) {
                Cell *dest = g->C(i, cury).get();
                dest->text = ic->text;
                dest->text.cell = dest;
                dest->text.ResetImageCache();
                ic = ic->parent;
            }
            cury++;
        }
        return cury;
    }

    void ResizeColWidths(int dir, const Selection &sel, bool hierarchical) {
        for (int x = sel.x; x < sel.x + sel.xs; x++) {
            colwidths[x] += dir * 5;
            if (colwidths[x] < 5) colwidths[x] = 5;
            loop(y, ys) {
                if (C(x, y)->grid && hierarchical)
                    C(x, y)->grid->ResizeColWidths(dir, C(x, y)->grid->SelectAll(), hierarchical);
            }
        }
    }

    int GetColWidth(Cell *ct) {
        foreachcell(c) {
            if (c.get() == ct) return colwidths[x];
        }
        return 0;
    }

    void SetColWidth(Cell *ct, int w) {
        foreachcell(c) {
            if (c.get() == ct) {
                colwidths[x] = w;
                return;
            }
        }
    }

    void CollectCells(auto &itercells) { foreachcell(c) c->CollectCells(itercells); }
    void CollectCellsSel(auto &itercells, const Selection &sel, bool recurse) {
        foreachcellinsel(c, sel) c->CollectCells(itercells, recurse);
    }

    void SetStyles(const Selection &sel, Cell *o) {
        foreachcellinsel(c, sel) {
            c->cellcolor = o->cellcolor;
            c->textcolor = o->textcolor;
            c->bordercolor = o->bordercolor;
            c->text.stylebits = o->text.stylebits;
            c->text.image = o->text.image;
            c->note = o->note;
        }
    }

    void ClearImages(const Selection &sel) {
        foreachcellinsel(c, sel) {
            c->text.image = nullptr;
            c->text.ResetImageScale();
            c->text.WasEdited();
        }
    }
};
