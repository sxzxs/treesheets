struct UndoItem {
    vector<Selection> path;
    vector<Selection> selpath;
    Selection sel;
    unique_ptr<Cell> clone;
    size_t estimated_size {0};
    uintptr_t cloned_from;  // May be dead.
    int generation {0};
};

struct Document {
    TSCanvas *canvas {nullptr};
    unique_ptr<Cell> root {nullptr};
    Selection prev;
    Selection hover;
    Selection selected;
    Selection image_selection;
    Selection begindrag;
    int isctrlshiftdrag;
    int scrollx;
    int scrolly;
    int maxx;
    int maxy;
    int centerx {0};
    int centery {0};
    int layoutxs;
    int layoutys;
    int hierarchysize;
    int fgutter {6};
    int lasttextsize;
    int laststylebits;
    Cell *currentdrawroot {nullptr};  // for use during Render() calls
    vector<unique_ptr<UndoItem>> undolist;
    vector<unique_ptr<UndoItem>> redolist;
    vector<Selection> drawpath;
    int pathscalebias {0};
    wxString filename {""};
    long lastmodsinceautosave {0};
    long undolistsizeatfullsave {0};
    long lastsave {wxGetLocalTime()};
    bool modified {false};
    bool tmpsavesuccess {true};
    wxDataObjectComposite *dndobjc {new wxDataObjectComposite()};
    wxTextDataObject *dndobjt {new wxTextDataObject()};
    wxBitmapDataObject *dndobji {new wxBitmapDataObject()};
    wxFileDataObject *dndobjf {new wxFileDataObject()};

    struct Printout : wxPrintout {
        Document *doc;
        Printout(Document *d) : wxPrintout("printout"), doc(d) {}

        bool OnPrintPage(int page) {
            auto dc = GetDC();
            if (!dc) return false;
            doc->Print(*dc, *this);
            return true;
        }

        bool OnBeginDocument(int startPage, int endPage) {
            return wxPrintout::OnBeginDocument(startPage, endPage);
        }

        void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo) {
            *minPage = 1;
            *maxPage = 1;
            *selPageFrom = 1;
            *selPageTo = 1;
        }

        bool HasPage(int pageNum) { return pageNum == 1; }
    };

    bool while_printing {false};
    wxPrintData printData;
    wxPageSetupDialogData pageSetupData;
    uint printscale {0};
    bool scaledviewingmode {false};
    bool paintscrolltoselection {true};
    double currentviewscale {1.0};
    bool image_resize_dragging {false};
    bool image_selected {false};
    Cell *image_resize_cell {nullptr};
    double image_resize_start_scale {1.0};
    int image_resize_start_width {0};
    int image_resize_start_height {0};
    wxPoint image_resize_anchor;
    Image *image_viewer_image {nullptr};
    double image_viewer_scale {1.0};
    wxPoint image_viewer_offset;
    wxPoint image_viewer_last_mouse;
    bool image_viewer_dragging {false};
    bool borderpaint_undo_added {false};
    Selection borderpaint_last;
    bool searchfilter {false};
    int editfilter {0};
    wxDateTime lastmodificationtime;
    map<wxString, uint> tags;
    vector<Cell *> itercells;

    #define loopcellsin(par, c) \
        CollectCells(par);      \
        loopv(_i, itercells) for (auto c = itercells[_i]; c; c = nullptr)
    #define loopallcells(c)     \
        CollectCells(root.get()); \
        for (auto c : itercells)
    #define loopallcellssel(c, rec) \
        CollectCellsSel(rec);     \
        for (auto c : itercells)

    Document() {
        ResetFont();
        pageSetupData = printData;
        pageSetupData.SetMarginTopLeft(wxPoint(15, 15));
        pageSetupData.SetMarginBottomRight(wxPoint(15, 15));
        dndobjc->Add(dndobjt);
        dndobjc->Add(dndobji);
        dndobjc->Add(dndobjf);
    }

    uint Background() { return root ? root->cellcolor : 0xFFFFFF; }

    void BeginBorderPaint() {
        borderpaint_undo_added = false;
        borderpaint_last = Selection();
    }

    void FinishBorderPaint() {
        borderpaint_undo_added = false;
        borderpaint_last = Selection();
    }

    bool SameBorderPaintTarget(const Selection &a, const Selection &b) const {
        return a.grid == b.grid && a.x == b.x && a.y == b.y && a.xs == b.xs && a.ys == b.ys;
    }

    uint CurrentBorderPaintColor() const {
        auto color = sys->lastbordcolor;
        if (sys->frame && sys->frame->bordercolordropdown) {
            auto idx = sys->frame->bordercolordropdown->GetSelection();
            if (idx >= 0 && idx < static_cast<int>(celltextcolors.size()))
                color = idx == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[idx];
        }
        sys->lastbordcolor = color;
        return color;
    }

    bool PaintHoveredBorderLine() {
        if (!hover.grid || !hover.Thin()) return false;
        if (!hover.grid->BorderLineForSelection(hover)) return false;
        if (borderpaint_last.grid && SameBorderPaintTarget(borderpaint_last, hover)) return true;
        auto color = CurrentBorderPaintColor();

        if (!hover.grid->BorderLineNeedsPaint(hover, color)) {
            borderpaint_last = hover;
            return true;
        }

        if (!borderpaint_undo_added) {
            hover.grid->cell->AddUndo(this);
            borderpaint_undo_added = true;
        }
        auto painted = hover.grid->PaintBorderLine(hover, color);
        if (painted) borderpaint_last = hover;
        return painted;
    }

    void InitCellSelect(Cell *initialselected, int xsize, int ysize) {
        if (!initialselected) {
            SetSelect(Selection(root->grid, 0, 0, 1, 1));
            return;
        }
        SetSelect(initialselected->parent->grid->FindCell(initialselected));
        selected.xs = xsize;
        selected.ys = ysize;
        if (sys->frame) sys->frame->UpdateStatus(selected, true);
    }

    void InitWith(unique_ptr<Cell> root, const wxString &filename, Cell *initialselected, int xsize, int ysize) {
        this->root = std::move(root);
        currentdrawroot = this->root.get();
        InitCellSelect(initialselected, xsize, ysize);
        ChangeFileName(filename, false);
    }

    void UpdateFileName(int page = -1) {
        if (!sys->frame) return;
        sys->frame->SetPageTitle(filename, modified ? (lastmodsinceautosave ? "*" : "+") : "",
                                 page);
    }

    void ChangeFileName(const wxString &newfilename, bool checkext) {
        filename = newfilename;
        if (checkext) {
            wxFileName wxfn(filename);
            if (!wxfn.HasExt()) filename.Append(".cts");
        }
        UpdateFileName();
    }

    wxString SaveDB(bool *success, bool istempfile = false, int page = -1) {
        if (success) *success = false;
        if (filename.empty()) return _("Save cancelled.");
        Cell *ocs = nullptr;
        if (selected.xs != 0 || selected.ys != 0)
            ocs = selected.grid->C(
                selected.x != selected.grid->xs ? selected.x : selected.x - 1,
                selected.y != selected.grid->ys ? selected.y : selected.y - 1)
            .get();
        auto start_saving_time = wxGetLocalTimeMillis();

        auto targetfilename = istempfile ? sys->TmpName(filename) : filename;
        auto savefilename = sys->NewName(targetfilename);

        {  // limit destructors
            wxBusyCursor wait;
            wxFFileOutputStream fos(savefilename);
            if (!fos.IsOk()) {
                if (!istempfile && sys->frame)
                    wxMessageBox(
                        _("Error writing TreeSheets file! (try saving under new filename)."),
                        savefilename.wx_str(), wxOK, sys->frame);
                return _("Error writing to file.");
            }

            wxDataOutputStream sos(fos);
            fos.Write("TSFF", 4);
            char vers = TS_VERSION;
            fos.Write(&vers, 1);
            sos.Write8(selected.xs);
            sos.Write8(selected.ys);
            sos.Write8(ocs ? drawpath.size() : 0);  // zoom level
            RefreshImageRefCount(true);
            int realindex = 0;
            loopv(i, sys->imagelist) {
                if (auto &image = *sys->imagelist[i]; image.trefc) {
                    fos.PutC(image.type);
                    sos.WriteDouble(image.display_scale);
                    wxInt64 imagelen(image.data.size());
                    sos.Write64(imagelen);
                    fos.Write(image.data.data(), imagelen);
                    image.savedindex = realindex++;
                }
            }

            fos.Write("D", 1);
            wxZlibOutputStream zos(fos, 9);
            if (!zos.IsOk()) return _("Zlib error while writing file.");
            wxDataOutputStream dos(zos);
            root->Save(dos, ocs);
            for (auto &[tag, color] : tags) {
                dos.WriteString(tag);
                dos.Write32(color);
            }
            dos.WriteString(wxEmptyString);
        }

        if (!istempfile && sys->makebaks && ::wxFileExists(filename)) {
            ::wxRemoveFile(sys->BakName(filename));
            ::wxRenameFile(filename, sys->BakName(filename));
        }

        if (!::wxRenameFile(savefilename, targetfilename, true)) {
            return _("Error renaming temporary file.");
        }

        lastmodsinceautosave = 0;
        lastsave = wxGetLocalTime();
        auto end_saving_time = wxGetLocalTimeMillis();

        if (!istempfile) {
            undolistsizeatfullsave = undolist.size();
            modified = false;
            tmpsavesuccess = true;
            sys->FileUsed(filename, this);
            if (::wxFileExists(sys->TmpName(filename))) ::wxRemoveFile(sys->TmpName(filename));
        }
        if (sys->autohtmlexport) {
            ExportFile(sys->ExtName(filename, ".html"),
                       sys->autohtmlexport == A_AUTOEXPORT_HTML_WITH_IMAGES - A_AUTOEXPORT_HTML_NONE
                           ? A_EXPHTMLTE
                           : A_EXPHTMLT,
                       false);
        }
        UpdateFileName(page);
        if (success) *success = true;
        return wxString::Format(_("Saved %s successfully (in %lld milliseconds)."), filename,
                                end_saving_time - start_saving_time);
    }

    void DrawSelect(wxDC &dc, Selection &s) {
        if (!s.grid) return;
        ResetFont();
        s.grid->DrawSelect(this, dc, s);
    }

    wxPoint DeviceToDocumentPoint(int mx, int my) {
        int x, y;
        canvas->CalcUnscrolledPosition(mx, my, &x, &y);
        return wxPoint(static_cast<int>(x / currentviewscale - centerx / currentviewscale),
                       static_cast<int>(y / currentviewscale - centery / currentviewscale));
    }

    Cell *ImageCellAtDevicePoint(int mx, int my) {
        if (!canvas) return nullptr;
        auto cell = hover.GetCell();
        if (!cell || !cell->text.image) return nullptr;
        wxRect image_rect;
        if (!cell->ImageDisplayRect(this, image_rect)) return nullptr;
        return image_rect.Contains(DeviceToDocumentPoint(mx, my)) ? cell : nullptr;
    }

    static wxRect ImageResizeHandleRect(const wxRect &image_rect) {
        const int handle = 8;
        return wxRect(image_rect.x + max(0, image_rect.width - handle),
                      image_rect.y + max(0, image_rect.height - handle), handle, handle);
    }

    bool SelectedImageRect(wxRect &rect, Cell **image_cell = nullptr) {
        if (!selected.grid || selected.TextEdit()) return false;
        auto cell = selected.GetCell();
        if (!cell || !cell->text.image) return false;
        if (!cell->ImageDisplayRect(this, rect)) return false;
        if (image_cell) *image_cell = cell;
        return true;
    }

    void DrawSelectedImageOutline(wxDC &dc) {
        if (!SelectedImageCell()) return;
        wxRect image_rect;
        if (!SelectedImageRect(image_rect)) return;
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(LightColor(0x000000), 2));
        dc.DrawRectangle(image_rect);
    }

    bool ImageResizeHitTest(int mx, int my) {
        if (!canvas) return false;
        wxRect image_rect;
        if (!SelectedImageRect(image_rect)) return false;
        return ImageResizeHandleRect(image_rect).Contains(DeviceToDocumentPoint(mx, my));
    }

    void DrawImageResizeHandle(wxDC &dc) {
        wxRect image_rect;
        if (!SelectedImageRect(image_rect)) return;
        auto handle = ImageResizeHandleRect(image_rect);
        dc.SetBrush(wxBrush(LightColor(0x000000)));
        dc.SetPen(wxPen(LightColor(0xFFFFFF)));
        dc.DrawRectangle(handle);
        dc.DrawLine(handle.x + 2, handle.y + handle.height - 2, handle.x + handle.width - 2,
                    handle.y + 2);
    }

    bool StartImageResize(int mx, int my) {
        if (!canvas) return false;
        wxRect image_rect;
        Cell *cell = nullptr;
        if (!SelectedImageRect(image_rect, &cell)) return false;
        if (!ImageResizeHandleRect(image_rect).Contains(DeviceToDocumentPoint(mx, my))) return false;

        cell->AddUndo(this);
        image_resize_dragging = true;
        image_resize_cell = cell;
        image_resize_start_scale = cell->text.image_scale;
        image_resize_start_width = max(1, image_rect.width);
        image_resize_start_height = max(1, image_rect.height);
        image_resize_anchor = image_rect.GetTopLeft();
        return true;
    }

    bool UpdateImageResize(int mx, int my) {
        if (!image_resize_dragging || !image_resize_cell || !image_resize_cell->text.image)
            return false;
        auto p = DeviceToDocumentPoint(mx, my);
        auto new_width = max(8, p.x - image_resize_anchor.x);
        auto new_height = max(8, p.y - image_resize_anchor.y);
        auto width_scale = static_cast<double>(new_width) / image_resize_start_width;
        auto height_scale = static_cast<double>(new_height) / image_resize_start_height;
        image_resize_cell->text.SetImageScale(image_resize_start_scale *
                                              max(width_scale, height_scale));
        currentdrawroot->ResetChildren();
        currentdrawroot->ResetLayout();
        paintscrolltoselection = false;
        if (canvas) canvas->Refresh();
        return true;
    }

    bool FinishImageResize() {
        if (!image_resize_dragging) return false;
        image_resize_dragging = false;
        image_resize_cell = nullptr;
        return true;
    }

    bool ImageViewerActive() const { return image_viewer_image != nullptr; }

    void ResetImageViewer() {
        image_viewer_image = nullptr;
        image_viewer_scale = 1.0;
        image_viewer_offset = wxPoint(0, 0);
        image_viewer_last_mouse = wxPoint(0, 0);
        image_viewer_dragging = false;
    }

    bool CloseImageViewer() {
        if (!ImageViewerActive()) return false;
        ResetImageViewer();
        if (canvas) canvas->Refresh();
        return true;
    }

    wxString OpenSelectedImageViewer() {
        auto cell = SelectedImageCell();
        if (!cell && selected.grid && !selected.TextEdit()) {
            auto selected_cell = selected.GetCell();
            if (selected_cell && selected_cell->text.image) cell = selected_cell;
        }
        if (!cell || !cell->text.image) return _("No selected image.");

        image_viewer_image = cell->text.image;
        image_viewer_scale = 1.0;
        image_viewer_offset = wxPoint(0, 0);
        image_viewer_dragging = false;
        if (canvas) {
            canvas->SetFocus();
            canvas->Refresh();
        }
        return _("Image viewer opened. Mouse wheel zooms, drag pans, Esc closes.");
    }

    wxBitmap *ImageViewerBitmap() {
        if (!image_viewer_image) return nullptr;
        auto &bitmap = image_viewer_image->Display();
        return bitmap.IsOk() ? &bitmap : nullptr;
    }

    wxRect ImageViewerRect() {
        auto bitmap = ImageViewerBitmap();
        if (!canvas || !bitmap) return wxRect();
        image_viewer_scale = Text::ClampImageScale(image_viewer_scale);
        auto width = max(1, static_cast<int>(std::lround(bitmap->GetWidth() * image_viewer_scale)));
        auto height =
            max(1, static_cast<int>(std::lround(bitmap->GetHeight() * image_viewer_scale)));
        auto client = canvas->GetClientSize();
        return wxRect((client.GetWidth() - width) / 2 + image_viewer_offset.x,
                      (client.GetHeight() - height) / 2 + image_viewer_offset.y, width, height);
    }

    void DrawImageViewer(wxDC &dc) {
        auto bitmap = ImageViewerBitmap();
        if (!canvas || !bitmap) return;

        dc.SetUserScale(1, 1);
        dc.SetDeviceOrigin(0, 0);
        dc.SetLogicalOrigin(0, 0);

        #if wxUSE_GRAPHICS_CONTEXT
            {
                auto client = canvas->GetClientSize();
                unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
                if (gc) {
                    gc->SetBrush(wxBrush(wxColour(0, 0, 0, 72)));
                    gc->SetPen(*wxTRANSPARENT_PEN);
                    gc->DrawRectangle(0, 0, client.GetWidth(), client.GetHeight());
                }
            }
        #endif

        auto rect = ImageViewerRect();
        sys->ImageDraw(bitmap, dc, rect.x, rect.y, rect.width, rect.height);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(LightColor(0x000000), 2));
        dc.DrawRectangle(rect.x - 1, rect.y - 1, rect.width + 2, rect.height + 2);
    }

    bool ZoomImageViewer(int steps, const wxPoint &anchor) {
        if (!ImageViewerActive() || !canvas || !steps) return false;
        auto old_scale = Text::ClampImageScale(image_viewer_scale);
        auto new_scale = Text::ClampImageScale(old_scale * std::pow(1.12, steps));
        if (std::abs(new_scale - old_scale) < 0.000001) return false;

        auto client = canvas->GetClientSize();
        auto centerx = client.GetWidth() / 2.0;
        auto centery = client.GetHeight() / 2.0;
        auto ratio = new_scale / old_scale;
        image_viewer_offset.x = static_cast<int>(std::lround(
            image_viewer_offset.x +
            (anchor.x - centerx - image_viewer_offset.x) * (1.0 - ratio)));
        image_viewer_offset.y = static_cast<int>(std::lround(
            image_viewer_offset.y +
            (anchor.y - centery - image_viewer_offset.y) * (1.0 - ratio)));
        image_viewer_scale = new_scale;
        canvas->Refresh();
        return true;
    }

    bool BeginImageViewerDrag(const wxPoint &pos) {
        if (!ImageViewerActive()) return false;
        image_viewer_dragging = true;
        image_viewer_last_mouse = pos;
        return true;
    }

    bool UpdateImageViewerDrag(const wxPoint &pos) {
        if (!ImageViewerActive() || !image_viewer_dragging) return false;
        auto delta = pos - image_viewer_last_mouse;
        image_viewer_offset.x += delta.x;
        image_viewer_offset.y += delta.y;
        image_viewer_last_mouse = pos;
        if (canvas) canvas->Refresh();
        return true;
    }

    bool FinishImageViewerDrag() {
        if (!image_viewer_dragging) return false;
        image_viewer_dragging = false;
        return true;
    }

    void UpdateHover(wxReadOnlyDC &dc, int mx, int my) {
        ResetFont();
        int x, y;
        canvas->CalcUnscrolledPosition(mx, my, &x, &y);
        prev = hover;
        hover = Selection();
        auto drawroot = WalkPath(drawpath);
        if (drawroot->grid)
            drawroot->grid->FindXY(
                this, x / currentviewscale - centerx / currentviewscale - hierarchysize,
                y / currentviewscale - centery / currentviewscale - hierarchysize, dc);
    }

    void ScrollIfSelectionOutOfView(const Selection &sel, int sx, int sy, int mx, int my) {
        if (!scaledviewingmode) {
            // required, since sizes of things may have been reset by the last editing operation
            int canvasw, canvash;
            canvas->GetClientSize(&canvasw, &canvash);
            if ((layoutys > canvash || layoutxs > canvasw) && sel.grid) {
                wxRect r = sel.grid->GetRect(this, sel);
                if (r.y < sy || r.y + r.height > my || r.x < sx || r.x + r.width > mx) {
                    canvas->Scroll(r.width > canvasw || r.x < sx ? r.x
                                   : r.x + r.width > mx             ? r.x + r.width - canvasw
                                                                      : sx,
                                   r.height > canvash || r.y < sy ? r.y
                                   : r.y + r.height > my             ? r.y + r.height - canvash
                                                                       : sy);
                }
            }
        }
    }

    void ScrollOrZoom(bool zoomiftiny = false) {
        if (!selected.grid) return;
        if (!canvas) return;
        auto drawroot = WalkPath(drawpath);
        // If we jumped to a cell which may be insided a folded cell, we have to unfold it
        // because the rest of the code doesn't deal with a selection that is invisible :)
        for (auto cg = selected.grid->cell; cg; cg = cg->parent) {
            // Unless we're under the drawroot, no need to unfold further.
            if (cg == drawroot) break;
            if (cg->grid->folded) {
                cg->grid->folded = false;
                cg->ResetLayout();
                cg->ResetChildren();
            }
        }
        for (auto cg = selected.grid->cell; cg; cg = cg->parent)
            if (cg == drawroot) {
                if (zoomiftiny) ZoomTiny();
                paintscrolltoselection = true;
                canvas->Refresh();
                return;
            }
        Zoom(-100, false);
        if (zoomiftiny) ZoomTiny();
    }

    void ZoomTiny() {
        if (auto c = selected.GetCell(); c && c->tiny) {
            Zoom(1);  // seems to leave selection box in a weird location?
            if (selected.GetCell() != c) ZoomTiny();
        }
    }

    void ResetCursor() {
        if (selected.grid) selected.SetCursorEdit(this, selected.TextEdit());
    }

    void SetSelect(const Selection &sel = Selection()) {
        selected = sel;
        begindrag = sel;
        image_selected = false;
        image_selection = Selection();
    }

    bool ImageSelectionMatchesSelection() const {
        return image_selected && selected.grid == image_selection.grid &&
               selected.x == image_selection.x && selected.y == image_selection.y &&
               selected.xs == image_selection.xs && selected.ys == image_selection.ys;
    }

    Cell *SelectedImageCell() {
        if (!ImageSelectionMatchesSelection() || selected.TextEdit()) return nullptr;
        auto cell = selected.GetCell();
        return cell && cell->text.image ? cell : nullptr;
    }

    bool MarkSelectedImage() {
        auto cell = selected.GetCell();
        if (!cell || !cell->text.image || selected.TextEdit()) {
            image_selected = false;
            image_selection = Selection();
            return false;
        }
        image_selected = true;
        image_selection = selected;
        return true;
    }

    void ClearImageSelection() {
        image_selected = false;
        image_selection = Selection();
    }

    void SelectUp() {
        if (!isctrlshiftdrag || isctrlshiftdrag == 3 || begindrag.EqLoc(selected)) return;
        auto cell = selected.GetCell();
        if (!cell) return;
        auto targetcell = begindrag.ThinExpand(this);
        selected = begindrag;
        if (targetcell) {
            auto is_parent = targetcell->IsParentOf(cell);
            auto targetcell_parent = targetcell->parent;  // targetcell may be deleted.
            targetcell->Paste(this, cell, begindrag);
            // If is_parent, cell has been deleted already.
            if (isctrlshiftdrag == 1 && !is_parent) {
                cell->parent->AddUndo(this);
                Selection cellselection = cell->parent->grid->FindCell(cell);
                cell->parent->grid->MultiCellDeleteSub(this, cellselection);
            }
            hover = targetcell_parent ? targetcell_parent->grid->FindCell(targetcell) : Selection();
            SetSelect(hover);
            if (canvas) {
                wxInfoDC dc(canvas);
                Layout(dc);
            }
        }
    }

    void DoubleClick() {
        SetSelect(hover);
        if (selected.Thin() && selected.grid) {
            selected.SelAll();
        } else if (Cell *c = selected.GetCell()) {
            selected.EnterEditOnly(this);
            c->text.SelectWord(selected);
            begindrag = selected;
        }
    }

    void Drop() {
        switch (dndobjc->GetReceivedFormat().GetType()) {
            case wxDF_BITMAP: PasteOrDrop(*dndobji); break;
            case wxDF_FILENAME: PasteOrDrop(*dndobjf); break;
            case wxDF_TEXT:
            case wxDF_UNICODETEXT: PasteOrDrop(*dndobjt);
            default:;
        }
    }

    auto CopyEntireCells(wxString &s, int action) {
        sys->clipboardcopy = s;
        auto html =
            selected.grid->ConvertToText(selected, 0, action == A_COPYWI ? A_EXPHTMLTI : A_EXPHTMLT,
                                         this, false, currentdrawroot);
        return new wxHTMLDataObject(html);
    }

    bool CopyImageToClipboard(Image *image) {
        if (!image || image->data.empty() || !wxTheClipboard->Open()) return false;
        auto &[it, mime] = imagetypes.at(image->type);
        auto bitmap = ConvertBufferToWxBitmap(image->data, it);
        wxTheClipboard->SetData(new wxBitmapDataObject(bitmap));
        wxTheClipboard->Close();
        return true;
    }

    bool RemoveSelectedImage() {
        auto cell = SelectedImageCell();
        if (!cell) return false;
        auto removed_image = cell->text.image;
        cell->AddUndo(this);
        cell->text.image = nullptr;
        cell->text.ResetImageScale();
        cell->text.WasEdited();
        ClearImageSelection();
        if (image_viewer_image == removed_image) ResetImageViewer();
        if (currentdrawroot) {
            currentdrawroot->ResetChildren();
            currentdrawroot->ResetLayout();
        } else {
            cell->ResetLayout();
        }
        if (canvas) canvas->Refresh();
        return true;
    }

    void Copy(int action) {
        auto c = selected.GetCell();
        sys->clipboardcopy = wxEmptyString;

        switch (action) {
            case A_DRAGANDDROP: {
                sys->cellclipboard = c ? c->Clone(nullptr) : selected.grid->CloneSel(selected);
                wxDataObjectComposite dragdata;
                if (c && !c->text.t && c->text.image) {
                    auto image = c->text.image;
                    if (!image->data.empty()) {
                        auto &[it, mime] = imagetypes.at(image->type);
                        auto bitmap = ConvertBufferToWxBitmap(image->data, it);
                        dragdata.Add(new wxBitmapDataObject(bitmap));
                    }
                } else {
                    auto s = selected.grid->ConvertToText(selected, 0, A_EXPTEXT, this, false,
                                                          currentdrawroot);
                    dragdata.Add(new wxTextDataObject(s));
                    if (!selected.TextEdit()) {
                        auto htmlobj = CopyEntireCells(s, wxID_COPY);
                        dragdata.Add(htmlobj);
                    }
                }
                wxDropSource dragsource(dragdata, canvas);
                dragsource.DoDragDrop(true);
                break;
            }
            case A_COPYCT: {
                sys->cellclipboard = nullptr;
                auto clipboardtextdata = new wxDataObjectComposite();
                wxString s = "";
                loopallcellssel(c, true) if (c->text.t.Len()) s += c->text.t + " ";
                if (!selected.TextEdit()) sys->clipboardcopy = s;
                clipboardtextdata->Add(new wxTextDataObject(s));
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(clipboardtextdata);
                    wxTheClipboard->Close();
                }
                break;
            }
            case wxID_COPY:
            case A_COPYWI:
            default: {
                sys->cellclipboard = c ? c->Clone(nullptr) : selected.grid->CloneSel(selected);
                if (auto imagecell = SelectedImageCell()) {
                    sys->cellclipboard = nullptr;
                    CopyImageToClipboard(imagecell->text.image);
                } else if (c && !c->text.t && c->text.image) {
                    CopyImageToClipboard(c->text.image);
                } else {
                    auto clipboarddata = new wxDataObjectComposite();
                    auto s = selected.grid->ConvertToText(selected, 0, A_EXPTEXT, this, false,
                                                          currentdrawroot);
                    clipboarddata->Add(new wxTextDataObject(s));
                    if (!selected.TextEdit()) {
                        auto htmlobj = CopyEntireCells(s, action);
                        clipboarddata->Add(htmlobj);
                    }
                    if (wxTheClipboard->Open()) {
                        wxTheClipboard->SetData(clipboarddata);
                        wxTheClipboard->Close();
                    }
                }
                break;
            }
        }
        return;
    }

    int ZoomDepth() const { return static_cast<int>(drawpath.size()); }

    wxString ZoomStatusText() const {
        auto depth = ZoomDepth();
        return depth ? wxString::Format(_("Zoom: Depth %d"), depth) : wxString(_("Zoom: Root"));
    }

    wxString ZoomPathCellLabel(Cell *cell, const Selection &selection) const {
        wxString label = cell ? cell->text.t : wxString();
        label.Replace("\r", " ");
        label.Replace("\n", " ");
        label.Trim(true).Trim(false);
        if (label.IsEmpty())
            label = wxString::Format("[%d,%d]", selection.x + 1, selection.y + 1);
        if (label.Len() > 24) label = label.Left(21) + "...";
        return label;
    }

    wxString ZoomStatusDetails() const {
        wxString path = _("Root");
        Cell *cell = root.get();
        loopvrev(i, drawpath) {
            const Selection &selection = drawpath[i];
            if (!cell || !cell->grid) break;
            auto child = cell->grid->C(selection.x, selection.y).get();
            path += " > " + ZoomPathCellLabel(child, selection);
            cell = child;
        }
        return drawpath.empty() ? ZoomStatusText() : ZoomStatusText() + " (" + path + ")";
    }

    bool ZoomSetDrawPath(int dir, bool fromroot = true) {
        int oldlen = drawpath.size();
        int targetlen = max(0, (fromroot ? 0 : oldlen) + dir);
        if (!targetlen && drawpath.empty()) return false;
        if (dir > 0) {
            if (!selected.grid) return false;
            auto c = selected.GetCell();
            CreatePath(c && c->grid ? c : selected.grid->cell, drawpath);
        } else if (dir < 0) {
            auto drawroot = WalkPath(drawpath);
            if (drawroot->grid && drawroot->grid->folded)
                SetSelect(drawroot->parent->grid->FindCell(drawroot));
        }
        int tail = static_cast<int>(drawpath.size()) - targetlen;
        if (tail > 0) drawpath.erase(drawpath.begin(), drawpath.begin() + tail);
        return drawpath.size() != oldlen;
    }

    bool Zoom(int dir, bool fromroot = false) {
        if (!ZoomSetDrawPath(dir, fromroot)) {
            if (sys->frame) sys->frame->UpdateZoomStatus(this);
            return false;
        }
        auto drawroot = WalkPath(drawpath);
        if (selected.GetCell() == drawroot && drawroot->grid) {
            // We can't have the drawroot selected, so we must move the selection to the children.
            SetSelect(Selection(drawroot->grid, 0, 0, drawroot->grid->xs, drawroot->grid->ys));
        }
        drawroot->ResetLayout();
        drawroot->ResetChildren();
        paintscrolltoselection = true;
        canvas->Refresh();
        if (sys->frame) sys->frame->UpdateZoomStatus(this);
        return true;
    }

    wxString NoSel() { return _("This operation requires a selection."); }
    wxString OneCell() { return _("This operation works on a single selected cell only."); }
    wxString NoThin() { return _("This operation doesn't work on thin selections."); }
    wxString NoGrid() { return _("This operation requires a cell that contains a grid."); }

    wxString TextInsert(const wxString &text) {
        if (!selected.grid) return NoSel();
        auto cell = selected.GetCell();
        if (!cell || !selected.TextEdit()) return _("only works in cell text mode");
        cell->AddUndo(this);
        cell->text.Insert(this, text, selected, false);
        paintscrolltoselection = true;
        canvas->Refresh();
        canvas->Update();
        return wxEmptyString;
    }

    wxString TextNewline() { return TextInsert("\n"); }

    wxString Wheel(int dir, bool alt, bool ctrl, bool shift, bool hierarchical = true) {
        if (!dir) return wxEmptyString;
        if (alt) {
            if (!selected.grid) return NoSel();
            if (selected.xs > 0) {
                if (!LastUndoSameCellAny(selected.grid->cell)) selected.grid->cell->AddUndo(this);
                selected.grid->ResizeColWidths(dir, selected, hierarchical);
                selected.grid->cell->ResetLayout();
                selected.grid->cell->ResetChildren();
                paintscrolltoselection = true;
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, false);
                return dir > 0 ? _("Column width increased.") : _("Column width decreased.");
            }
            return _("nothing to resize");
        } else if (shift) {
            if (!selected.grid) return NoSel();
            selected.grid->cell->AddUndo(this);
            selected.grid->ResetChildren();
            selected.grid->RelSize(-dir, selected, pathscalebias);
            paintscrolltoselection = true;
            if (canvas) canvas->Refresh();
            return dir > 0 ? _("Text size increased.") : _("Text size decreased.");
        } else if (ctrl) {
            int steps = abs(dir);
            dir = sign(dir);
            int changed = 0;
            loop(i, steps) if (Zoom(dir)) changed++;
            if (!changed)
                return (dir > 0 ? _("No deeper zoom level.") : _("Already at root zoom level.")) +
                       " " + ZoomStatusText();
            return (dir > 0 ? _("Zoomed in.") : _("Zoomed out.")) + " " +
                   ZoomStatusDetails();
        } else {
            ASSERT(0);
            return wxEmptyString;
        }
    }

    void Layout(wxReadOnlyDC &dc) {
        ResetFont();
        dc.SetUserScale(1, 1);
        currentdrawroot = WalkPath(drawpath);
        int psb = currentdrawroot == root.get() ? 0 : currentdrawroot->MinRelsize();
        if (psb < 0 || psb == INT_MAX) psb = 0;
        if (psb != pathscalebias) currentdrawroot->ResetChildren();
        pathscalebias = psb;
        currentdrawroot->LazyLayout(this, dc, 0, currentdrawroot->ColWidth(), false);
        ResetFont();
        PickFont(dc, 0, 0, 0);
        hierarchysize = 0;
        for (Cell *p = currentdrawroot->parent; p; p = p->parent)
            if (p->text.t.Len()) hierarchysize += dc.GetCharHeight();
        hierarchysize += fgutter;
        layoutxs = currentdrawroot->sx + hierarchysize + fgutter;
        layoutys = currentdrawroot->sy + hierarchysize + fgutter;
    }

    void ShiftToCenter(wxReadOnlyDC &dc) {
        int dlx = dc.DeviceToLogicalX(0);
        int dly = dc.DeviceToLogicalY(0);
        dc.SetDeviceOrigin(dlx > 0 ? -dlx : centerx, dly > 0 ? -dly : centery);
        dc.SetUserScale(currentviewscale, currentviewscale);
    }

    void Render(wxDC &dc) {
        ResetFont();
        PickFont(dc, 0, 0, 0);
        dc.SetTextForeground(*wxLIGHT_GREY);
        int i = 0;
        for (auto p = currentdrawroot->parent; p; p = p->parent)
            if (p->text.t.Len()) {
                int off = hierarchysize - dc.GetCharHeight() * ++i;
                auto s = p->text.t;
                if (static_cast<int>(s.Len()) > sys->defaultmaxcolwidth) {
                    // should take the width of these into account for layoutys, but really, the
                    // worst that can happen on a thin window is that its rendering gets cut off
                    s = s.Left(sys->defaultmaxcolwidth) + "...";
                }
                dc.DrawText(s, off, off);
            }
        dc.SetTextForeground(LightColor(0x000000));
        currentdrawroot->Render(this, hierarchysize, hierarchysize, dc, 0, 0, 0, 0, 0,
                                currentdrawroot->ColWidth(), 0);
    }

    void SelectClick(bool right = false, bool image_hit = false) {
        begindrag = Selection();
        if (image_hit) {
            hover.ExitEdit(this);
            SetSelect(hover);
            MarkSelectedImage();
            return;
        }
        ClearImageSelection();
        if (!(right && hover.IsInside(selected))) {
            if (selected.GetCell() == hover.GetCell() && hover.GetCell())
                hover.EnterEditOnly(this);
            else
                hover.ExitEdit(this);
            SetSelect(hover);
        }
    }

    void Draw(wxDC &dc) {
        if (!root) return;
        canvas->GetClientSize(&maxx, &maxy);
        Layout(dc);
        dc.SetBackground(wxBrush(LightColor(Background())));
        dc.Clear();
        double xscale = maxx / static_cast<double>(layoutxs);
        double yscale = maxy / static_cast<double>(layoutys);
        currentviewscale = min(xscale, yscale);
        if (currentviewscale > 5)
            currentviewscale = 5;
        else if (currentviewscale < 1)
            currentviewscale = 1;
        if (scaledviewingmode && currentviewscale > 1) {
            dc.SetUserScale(currentviewscale, currentviewscale);
            canvas->SetVirtualSize(maxx, maxy);
            maxx /= currentviewscale;
            maxy /= currentviewscale;
            scrollx = scrolly = 0;
        } else {
            currentviewscale = 1;
            dc.SetUserScale(1, 1);
            canvas->SetVirtualSize(layoutxs, layoutys);
            canvas->GetViewStart(&scrollx, &scrolly);
            maxx += scrollx;
            maxy += scrolly;
        }
        centerx = sys->centered && !scrollx && maxx > layoutxs
                      ? (maxx - layoutxs) / 2 * currentviewscale
                      : 0;
        centery = sys->centered && !scrolly && maxy > layoutys
                      ? (maxy - layoutys) / 2 * currentviewscale
                      : 0;
        ShiftToCenter(dc);
        Render(dc);
        DrawSelect(dc, selected);
        DrawSelectedImageOutline(dc);
        DrawImageResizeHandle(dc);
        if (paintscrolltoselection) {
            paintscrolltoselection = false;
                canvas->CallAfter([this, sel = selected, sx = scrollx, sy = scrolly, mx = maxx, my = maxy](){
                    ScrollIfSelectionOutOfView(sel, sx, sy, mx, my);
                    #ifdef __WXMAC__
                        canvas->Refresh();
                    #endif
                });
        }
        DrawImageViewer(dc);
        if (scaledviewingmode) { dc.SetUserScale(1, 1); }
    }

    void Print(wxDC &dc, wxPrintout &po) {
        Layout(dc);
        maxx = layoutxs;
        maxy = layoutys;
        scrollx = scrolly = 0;
        po.FitThisSizeToPage(printscale ? wxSize(printscale, 1) : wxSize(maxx, maxy));
        wxRect fitRect = po.GetLogicalPageRect();
        wxCoord xoff = (fitRect.width - maxx) / 2;
        wxCoord yoff = (fitRect.height - maxy) / 2;
        po.OffsetLogicalOrigin(xoff, yoff);
        while_printing = true;
        Render(dc);
        while_printing = false;
    }

    int TextSize(int depth, int relsize) { return g_textsize_for(depth, relsize, pathscalebias); }

    bool FontIsMini(int textsize) { return textsize == g_mintextsize(); }

    bool PickFont(wxReadOnlyDC &dc, int depth, int relsize, int stylebits) {
        int textsize = TextSize(depth, relsize);
        if (textsize != lasttextsize || stylebits != laststylebits) {
            wxFont font(textsize - (while_printing || scaledviewingmode),
                        stylebits & STYLE_FIXED ? wxFONTFAMILY_TELETYPE : wxFONTFAMILY_DEFAULT,
                        stylebits & STYLE_ITALIC ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
                        stylebits & STYLE_BOLD ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
                        (stylebits & STYLE_UNDERLINE) != 0,
                        stylebits & STYLE_FIXED ? sys->defaultfixedfont : sys->defaultfont);
            if (stylebits & STYLE_STRIKETHRU) font.SetStrikethrough(true);
            dc.SetFont(font);
            lasttextsize = textsize;
            laststylebits = stylebits;
        }
        return FontIsMini(textsize);
    }

    void ResetFont() {
        lasttextsize = INT_MAX;
        laststylebits = -1;
    }

    bool CheckForChanges() {
        if (modified) {
            ThreeChoiceDialog tcd(sys->frame, filename,
                                  _("Changes have been made, are you sure you wish to continue?"),
                                  _("Save and Close"), _("Discard Changes"), _("Cancel"));
            switch (tcd.Run()) {
                case 0: {
                    bool success = false;
                    Save(false, &success);
                    return !success;
                }
                case 1: return false;
                default:
                case 2: return true;
            }
        }
        return false;
    }

    void RemoveTmpFile() {
        if (!filename.empty() && ::wxFileExists(sys->TmpName(filename)))
            ::wxRemoveFile(sys->TmpName(filename));
    }

    bool CloseDocument() {
        bool keep = CheckForChanges();
        if (!keep) RemoveTmpFile();
        return keep;
    }

    wxString Export(const wxString &fmt, const wxString &pat, const wxString &message, int action) {
        wxFileName tsfn(filename);
        auto exportfilename = ::wxFileSelector(message, tsfn.GetPath(), tsfn.GetName(), fmt, pat,
                                               wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (exportfilename.empty()) return _("Export cancelled.");
        wxFileName expfn(exportfilename);
        if (!expfn.HasExt()) {
            expfn.SetExt(fmt);
            exportfilename = expfn.GetFullPath();
        }
        return ExportFile(exportfilename, action, true);
    }

    wxBitmap GetBitmap() {
        maxx = layoutxs;
        maxy = layoutys;
        scrollx = scrolly = 0;
        wxBitmap bm(maxx, maxy, 24);
        wxMemoryDC mdc(bm);
        DrawView(mdc);
        return bm;
    }

    bool DrawSVG(const wxString &filename) {
        maxx = layoutxs;
        maxy = layoutys;
        scrollx = scrolly = 0;
        wxSVGFileDC sdc(filename, maxx, maxy);
        sdc.SetBitmapHandler(new wxSVGBitmapEmbedHandler());
        DrawView(sdc);
        return sdc.IsOk();
    }

    void DrawView(wxDC &dc) {
        DrawRectangle(dc, Background(), 0, 0, maxx, maxy);
        Layout(dc);
        Render(dc);
    }

    wxBitmap GetSubBitmap(const Selection &sel) {
        wxRect r = sel.grid->GetRect(this, sel, true);
        return GetBitmap().GetSubBitmap(r);
    }

    void RefreshImageRefCount(bool includefolded) {
        loopv(i, sys->imagelist) sys->imagelist[i]->trefc = 0;
        root->ImageRefCount(includefolded);
    }

    wxString ExportFile(const wxString &filename, int action, bool currentview) {
        Cell *exportroot = currentview ? currentdrawroot : root.get();
        if (action == A_EXPIMAGE) {
            auto bitmap = GetBitmap();
            canvas->Refresh();
            if (!bitmap.SaveFile(filename, wxBITMAP_TYPE_PNG)) return _("Error writing PNG file!");
        } else if (action == A_EXPSVG) {
            if (!DrawSVG(filename)) return _("Error exporting to SVG file!");
            canvas->Refresh();
        } else {
            wxFFileOutputStream fos(filename, "w+b");
            if (!fos.IsOk()) {
                wxMessageBox(_("Error exporting file!"), filename.wx_str(), wxOK, sys->frame);
                return _("Error writing to file!");
            }
            wxTextOutputStream dos(fos);
            wxString content = exportroot->ToText(0, Selection(), action, this, true, exportroot);
            switch (action) {
                case A_EXPJSON:
                    dos.WriteString(content);
                    break;
                case A_EXPXML:
                    dos.WriteString(
                        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<!DOCTYPE cell [\n"
                        "<!ELEMENT cell (grid)>\n"
                        "<!ELEMENT grid (row*)>\n"
                        "<!ELEMENT row (cell*)>\n"
                        "]>\n");
                    dos.WriteString(content);
                    break;
                case A_EXPHTMLT:
                case A_EXPHTMLTI:
                case A_EXPHTMLTE:
                case A_EXPHTMLB:
                case A_EXPHTMLO: {
                    wxString output;
                    output
                        << "<!DOCTYPE html>\n"
                        << "<html>\n<head>\n<style>\n"
                        << "body { font-family: '" << sys->defaultfont << "', sans-serif; }\n"
                        << "table, th, td { border: 1px solid #A0A0A0; border-collapse: collapse;"
                        << " padding: 3px; vertical-align: top; }\n"
                        << "@media (prefers-color-scheme: dark) {\n"
                        << "  html { filter: invert(1); }\n"
                        << "  img { filter: invert(1); }\n"
                        << "}\n"
                        << "li { }\n</style>\n"
                        << "<title>export of TreeSheets file " << this->filename
                        << "</title>\n<meta charset=\"UTF-8\" />\n"
                        << "</head>\n<body style=\""
                        << wxString::Format("background-color: #%06X;", SwapColor(root->cellcolor))
                        << "\">" << content << "</body>\n</html>\n";
                    dos.WriteString(output);
                    break;
                }
                case A_EXPCSV:
                case A_EXPTEXT: dos.WriteString(content); break;
            }
            if (action == A_EXPHTMLTE) ExportAllImages(filename, exportroot);
        }
        return _("File exported successfully.");
    }

    wxString Save(bool saveas, bool *success = nullptr) {
        if (!saveas && !filename.empty()) { return SaveDB(success); }
        auto filename = ::wxFileSelector(_("Choose TreeSheets file to save:"), "", "", "cts",
                                         _("TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*"),
                                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (filename.empty()) return _("Save cancelled.");  // avoid name being set to ""
        ChangeFileName(filename, true);
        return SaveDB(success);
    }

    void AutoSave(bool minimized, int page) {
        if (sys->autosave && tmpsavesuccess && !filename.empty() && lastmodsinceautosave &&
            (lastmodsinceautosave + 60 < wxGetLocalTime() || lastsave + 300 < wxGetLocalTime() ||
             minimized)) {
            tmpsavesuccess = false;
            SaveDB(&tmpsavesuccess, true, page);
        }
    }

    wxString Key(int uk, int k, bool alt, bool ctrl, bool shift, bool &unprocessed) {
        if (uk == WXK_NONE || k < ' ' && k || k == WXK_DELETE) {
            switch (k) {
                case WXK_BACK:  // no menu shortcut available in wxwidgets
                    if (!ctrl) return Action(A_BACKSPACE);
                    break;  // Prevent Ctrl+H from being treated as Backspace
                case WXK_INSERT:
                    if (!alt && !ctrl && !shift) return Action(A_ENTERGRID);
                    break;
                case WXK_RETURN:
                case WXK_NUMPAD_ENTER:
                    if (alt && !ctrl) return TextNewline();
                    return Action(shift  ? ctrl ? A_ENTERGRIDN : A_ENTERGRID
                                  : ctrl ? A_ENTERCELL_JUMPTOSTART
                                         : A_ENTERCELL);
                case WXK_ESCAPE:  // docs say it can be used as a menu accelerator, but it does not
                                  // trigger from there?
                    return Action(A_CANCELEDIT);
                #ifdef WIN32  // works fine on Linux, not sure OS X
                case WXK_PAGEDOWN:
                    if (canvas) canvas->CursorScroll(0, g_scrollratecursor);
                    return wxEmptyString;
                case WXK_PAGEUP:
                    if (canvas) canvas->CursorScroll(0, -g_scrollratecursor);
                    return wxEmptyString;
                #endif
                #ifdef __WXGTK__
                // Due to limitations within GTK, wxGTK does not support specific keycodes 
                // as accelerator keys for menu items. See wxWidgets documentation for the 
                // wxMenuItem class in order to obtain more details. This is why we implement 
                // the missing handling of these accelerator keys in the following section.
                // Please be aware that the custom implementation has the downside of these
                // "accelerator keys" being suppressed in the menu items on wxGTK.
                    case WXK_DELETE: return Action(A_DELETE);
                    case WXK_LEFT:
                        return Action(shift ? (ctrl ? A_SCLEFT : A_SLEFT)
                                            : (ctrl ? A_MLEFT : A_LEFT));
                    case WXK_RIGHT:
                        return Action(shift ? (ctrl ? A_SCRIGHT : A_SRIGHT)
                                            : (ctrl ? A_MRIGHT : A_RIGHT));
                    case WXK_UP:
                        return Action(shift ? (ctrl ? A_SCUP : A_SUP) : (ctrl ? A_MUP : A_UP));
                    case WXK_DOWN:
                        return Action(shift ? (ctrl ? A_SCDOWN : A_SDOWN)
                                            : (ctrl ? A_MDOWN : A_DOWN));
                    case WXK_HOME:
                        return Action(shift ? (ctrl ? A_SHOME : A_SHOME)
                                            : (ctrl ? A_CHOME : A_HOME));
                    case WXK_END:
                        return Action(shift ? (ctrl ? A_SEND : A_SEND) : (ctrl ? A_CEND : A_END));
                    case WXK_TAB:
                        if (ctrl && !shift) {
                            // WXK_CONTROL_I (italics) arrives as the same keycode as WXK_TAB + ctrl
                            // on Linux?? They're both keycode 9 in defs.h We ignore it here, such
                            // that CTRL+I works, but it means only CTRL+SHIFT+TAB works on Linux as
                            // a way to switch tabs.
                            // Also, even though we ignore CTRL+TAB, and it is not assigned in the
                            // menus, it still has the
                            // effect of de-selecting
                            // the current tab (requires a click to re-activate). FIXME??
                            break;
                        }
                        return Action(shift ? (ctrl ? A_PREVFILE : A_PREV)
                                            : (ctrl ? A_NEXTFILE : A_NEXT));
                    case WXK_PAGEUP:
                        if (ctrl) return Action(alt ? A_INCWIDTHNH : A_ZOOMIN);
                        if (shift) return Action(A_INCSIZE);
                        if (!alt && canvas) canvas->CursorScroll(0, -g_scrollratecursor);
                        return wxEmptyString;
                    case WXK_PAGEDOWN:
                        if (ctrl) return Action(alt ? A_DECWIDTHNH : A_ZOOMOUT);
                        if (shift) return Action(A_DECSIZE);
                        if (!alt && canvas) canvas->CursorScroll(0, g_scrollratecursor);
                        return wxEmptyString;
                #endif
            }
        } else if (uk >= ' ') {
            if (!selected.grid) return NoSel();
            auto c = selected.ThinExpand(this);
            if (!c) {
                selected.Wrap(this);
                c = selected.GetCell();
            }
            c->AddUndo(this);  // FIXME: not needed for all keystrokes, or at least, merge all
                               // keystroke undos within same cell
            c->text.Key(this, uk, selected);
            paintscrolltoselection = true;
            if (canvas) {
                canvas->Refresh();
                canvas->Update();
            }
            return wxEmptyString;
        }
        unprocessed = true;
        return wxEmptyString;
    }

    wxString ScaleSelectedImagesForDisplay(double scale, bool reset = false) {
        if (!selected.grid) return NoSel();
        CollectCellsSel(true);
        bool anyimages = false;
        for (auto c : itercells) anyimages = anyimages || c->text.image;
        if (!anyimages) return _("No image found.");

        selected.grid->cell->AddUndo(this);
        for (auto c : itercells) {
            if (!c->text.image) continue;
            if (reset)
                c->text.ResetImageScale();
            else
                c->text.ScaleImageDisplay(scale);
        }
        currentdrawroot->ResetChildren();
        currentdrawroot->ResetLayout();
        if (canvas) canvas->Refresh();
        return reset ? _("Image display scale reset.")
                     : _("Image display scale changed without resampling pixels.");
    }

    wxString Action(int action) {
        switch (action) {
            case wxID_EXECUTE:
                root->AddUndo(this);
                sys->evaluator.Eval(root.get());
                root->ResetChildren();
                selected = Selection();
                begindrag = Selection();
                canvas->Refresh();
                return _("Evaluation finished.");

            case wxID_UNDO:
                if (undolist.size()) {
                    Undo(undolist, redolist);
                    return wxEmptyString;
                } else {
                    return _("Nothing more to undo.");
                }

            case wxID_REDO:
                if (redolist.size()) {
                    Undo(redolist, undolist, true);
                    return wxEmptyString;
                } else {
                    return _("Nothing more to redo.");
                }

            case wxID_SAVE: return Save(false);
            case wxID_SAVEAS: return Save(true);
            case A_SAVEALL: sys->SaveAll(); return wxEmptyString;

            case A_EXPXML: return Export("xml", "*.xml", _("Choose XML file to write"), action);
            case A_EXPJSON:
                return Export("json", "*.json", _("Choose JSON file to write"), action);
            case A_EXPHTMLT:
            case A_EXPHTMLTE:
            case A_EXPHTMLB:
            case A_EXPHTMLO:
                return Export("html", "*.html", _("Choose HTML file to write"), action);
            case A_EXPTEXT: return Export("txt", "*.txt", _("Choose Text file to write"), action);
            case A_EXPIMAGE: return Export("png", "*.png", _("Choose PNG file to write"), action);
            case A_EXPSVG: return Export("svg", "*.svg", _("Choose SVG file to write"), action);
            case A_EXPCSV: {
                int maxdepth = 0, leaves = 0;
                currentdrawroot->MaxDepthLeaves(0, maxdepth, leaves);
                if (maxdepth > 1)
                    return _(
                        "Cannot export grid that is not flat (zoom the view to the desired grid, and/or use Flatten).");
                return Export("csv", "*.csv", _("Choose CSV file to write"), action);
            }

            case A_IMPXML:
            case A_IMPXMLA:
            case A_IMPTXTI:
            case A_IMPTXTC:
            case A_IMPTXTS:
            case A_IMPTXTT: {
                wxArrayString filenames;
                GetFilesFromUser(filenames, sys->frame, _("Please select file(s) to import:"),
                                 _("*.*"));
                wxString message = "";
                for (auto &filename : filenames) message = sys->Import(filename, action);
                return message;
            }

            case wxID_OPEN: {
                wxArrayString filenames;
                GetFilesFromUser(filenames, sys->frame,
                                 _("Please select TreeSheets file(s) to load:"),
                                 _("TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*"));
                wxString message = "";
                for (auto &filename : filenames) message = sys->Open(filename);
                return message;
            }

            case wxID_CLOSE: {
                if (sys->frame->notebook->GetPageCount() <= 1) {
                    sys->frame->fromclosebox = false;
                    sys->frame->Close();
                    return wxEmptyString;
                }

                if (!CloseDocument()) {
                    int pagenumber = sys->frame->notebook->GetSelection();
                    // sys->frame->notebook->AdvanceSelection();
                    sys->frame->notebook->DeletePage(pagenumber);
                }
                return wxEmptyString;
            }

            case wxID_NEW: {
                int size = static_cast<int>(
                    ::wxGetNumberFromUser(_("What size grid would you like to start with?"),
                                          _("size:"), _("New Sheet"), 10, 1, 25, sys->frame));
                if (size < 0) return _("New file cancelled.");
                sys->InitDB(size);
                sys->frame->GetCurrentTab()->Refresh();
                return wxEmptyString;
            }

            case wxID_ABOUT: {
                wxAboutDialogInfo info;
                info.SetName("TreeSheets");
                info.SetVersion(wxT(PACKAGE_VERSION));
                info.SetCopyright("(C) 2026 Wouter van Oortmerssen and Tobias Predel");
                auto desc = wxString::Format("%s\n\n%s " wxVERSION_STRING,
                                             _("The Free Form Hierarchical Information Organizer"),
                                             _("Uses"));
                info.SetDescription(desc);
                wxAboutBox(info);
                return wxEmptyString;
            }

            case wxID_HELP: sys->LoadTutorial(); return wxEmptyString;

            case A_HELP_OP_REF: sys->LoadOpRef(); return wxEmptyString;

            case A_TUTORIALWEBPAGE: {
                wxTranslations *trans = wxTranslations::Get();
                wxString lang = trans ? trans->GetBestTranslation("ts") : wxString("");
                wxString tutorialpath;

                for (const wxString &suffix : {"-" + lang, "-" + lang.Left(2), wxString("")}) {
                    if (suffix.Length() == 1) continue;

                    wxString candidate =
                        sys->frame->app->GetDocPath("docs/tutorial" + suffix + ".html");
                    if (::wxFileExists(candidate)) {
                        tutorialpath = candidate;
                        break;
                    }
                }

                if (!tutorialpath.IsEmpty()) {
                    #ifdef __WXMAC__
                        wxLaunchDefaultBrowser("file://" + tutorialpath);
                    #else
                        wxLaunchDefaultBrowser(tutorialpath);
                    #endif
                    return wxEmptyString;
                } else {
                    return _("Tutorial web page could not be found.");
                }
            }

            #ifdef ENABLE_LOBSTER
                case A_SCRIPTREFERENCE:
                    #ifdef __WXMAC__
                    wxLaunchDefaultBrowser(
                        "file://" +
                        sys->frame->app->GetDocPath("docs/script_reference.html"));  // RbrtPntn
                    #else
                    wxLaunchDefaultBrowser(
                        sys->frame->app->GetDocPath("docs/script_reference.html"));
                    #endif
                        return wxEmptyString;
            #endif

            case A_ZOOMIN:
                return Wheel(1, false, true,
                             false);  // Zoom( 1, dc); return "zoomed in (menu)";
            case A_ZOOMOUT:
                return Wheel(-1, false, true,
                             false);  // Zoom(-1, dc); return "zoomed out (menu)";
            case A_INCSIZE: return Wheel(1, false, false, true);
            case A_DECSIZE: return Wheel(-1, false, false, true);
            case A_INCWIDTH: return Wheel(1, true, false, false);
            case A_DECWIDTH: return Wheel(-1, true, false, false);
            case A_INCWIDTHNH: return Wheel(1, true, false, false, false);
            case A_DECWIDTHNH: return Wheel(-1, true, false, false, false);

            case wxID_SELECT_FONT:
            case A_SET_FIXED_FONT: {
                wxFontData fdat;
                fdat.SetInitialFont(wxFont(
                    g_deftextsize,
                    action == wxID_SELECT_FONT ? wxFONTFAMILY_DEFAULT : wxFONTFAMILY_TELETYPE,
                    wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false,
                    action == wxID_SELECT_FONT ? sys->defaultfont : sys->defaultfixedfont));
                if (wxFontDialog fd(sys->frame, fdat); fd.ShowModal() == wxID_OK) {
                    wxFont font = fd.GetFontData().GetChosenFont();
                    g_deftextsize = min(20, max(10, font.GetPointSize()));
                    ClampTextSizeLimits();
                    sys->cfg->Write("defaultfontsize", g_deftextsize);
                    sys->cfg->Write("mintextsize", static_cast<long>(g_mintextsize()));
                    sys->cfg->Write("maxtextsize", static_cast<long>(g_maxtextsize()));
                    switch (action) {
                        case wxID_SELECT_FONT:
                            sys->defaultfont = font.GetFaceName();
                            sys->cfg->Write("defaultfont", sys->defaultfont);
                            break;
                        case A_SET_FIXED_FONT:
                            sys->defaultfixedfont = font.GetFaceName();
                            sys->cfg->Write("defaultfixedfont", sys->defaultfixedfont);
                            break;
                    }
                    // root->ResetChildren();
                    sys->frame->TabsReset();  // ResetChildren on all
                    if (canvas) canvas->Refresh();
                }
                return wxEmptyString;
            }

            case wxID_PRINT: {
                wxPrintDialogData printDialogData(printData);
                wxPrinter printer(&printDialogData);
                Printout printout(this);
                if (printer.Print(sys->frame, &printout, true)) {
                    printData = printer.GetPrintDialogData().GetPrintData();
                }
                return wxEmptyString;
            }

            case A_PRINTSCALE: {
                printscale = (uint)::wxGetNumberFromUser(
                    _("How many pixels wide should a page be? (0 for auto fit)"), _("scale:"),
                    _("Set Print Scale"), 0, 0, 5000, sys->frame);
                return wxEmptyString;
            }

            case wxID_PREVIEW: {
                wxPrintDialogData printDialogData(printData);
                auto preview =
                    new wxPrintPreview(new Printout(this), new Printout(this), &printDialogData);
                auto pframe = new wxPreviewFrame(preview, sys->frame, _("Print Preview"),
                                                 wxPoint(100, 100), wxSize(600, 650));
                pframe->Centre(wxBOTH);
                pframe->Initialize();
                pframe->Show(true);
                return wxEmptyString;
            }

            case A_PAGESETUP: {
                pageSetupData = printData;
                wxPageSetupDialog pageSetupDialog(sys->frame, &pageSetupData);
                pageSetupDialog.ShowModal();
                printData = pageSetupDialog.GetPageSetupDialogData().GetPrintData();
                pageSetupData = pageSetupDialog.GetPageSetupDialogData();
                return wxEmptyString;
            }

            case A_NEXTFILE: sys->frame->CycleTabs(1); return wxEmptyString;
            case A_PREVFILE: sys->frame->CycleTabs(-1); return wxEmptyString;

            case A_DEFBGCOL: {
                auto oldbg = Background();
                if (auto color = PickColor(sys->frame, oldbg); color != (uint)-1) {
                    root->AddUndo(this);
                    loopallcells(c) {
                        if (c->cellcolor == oldbg && (!c->parent || c->parent->cellcolor == color))
                            c->cellcolor = color;
                    }
                    if (canvas) canvas->Refresh();
                }
                return wxEmptyString;
            }

            case A_DEFCURCOL: {
                if (auto color = PickColor(sys->frame, sys->cursorcolor); color != (uint)-1) {
                    sys->cfg->Write("cursorcolor", sys->cursorcolor = color);
                    if (canvas) canvas->Refresh();
                }
                return wxEmptyString;
            }

            case A_SEARCHNEXT:
            case A_SEARCHPREV: {
                if (sys->searchstring.Len()) return SearchNext(false, true, action == A_SEARCHPREV);
                if (auto c = selected.GetCell()) {
                    auto s = c->text.ToText(0, selected, A_EXPTEXT);
                    if (!s.Len()) return _("No text to search for.");
                    sys->frame->filter->SetFocus();
                    sys->frame->filter->SetValue(s);
                    return wxEmptyString;
                } else {
                    return _("You need to select one cell if you want to search for its text.");
                }
            }

            case A_CASESENSITIVESEARCH: {
                sys->casesensitivesearch = !(sys->casesensitivesearch);
                sys->cfg->Write("casesensitivesearch", sys->casesensitivesearch);
                sys->searchstring = (sys->casesensitivesearch)
                                        ? sys->frame->filter->GetValue()
                                        : sys->frame->filter->GetValue().Lower();
                auto message = SearchNext(false, false, false);
                if (canvas) canvas->Refresh();
                return message;
            }

            case A_ROUND0:
            case A_ROUND1:
            case A_ROUND2:
            case A_ROUND3:
            case A_ROUND4:
            case A_ROUND5:
            case A_ROUND6:
                sys->cfg->Write("roundness", long(sys->roundness = action - A_ROUND0));
                if (canvas) canvas->Refresh();
                return wxEmptyString;

            case A_OPENCELLCOLOR:
                if (sys->frame->cellcolordropdown) sys->frame->cellcolordropdown->ShowPopup();
                break;
            case A_OPENTEXTCOLOR:
                if (sys->frame->textcolordropdown) sys->frame->textcolordropdown->ShowPopup();
                break;
            case A_OPENBORDCOLOR:
                if (sys->frame->bordercolordropdown) sys->frame->bordercolordropdown->ShowPopup();
                break;
            case A_OPENIMGDROPDOWN:
                if (sys->frame->imagedropdown) sys->frame->imagedropdown->ShowPopup();
                break;

            case A_REPLACEONCE:
            case A_REPLACEONCEJ:
            case A_REPLACEALL: {
                if (!sys->searchstring.Len()) return _("No search.");
                auto replaces = sys->frame->replaces->GetValue();
                auto lreplaces =
                    sys->casesensitivesearch ? (wxString)wxEmptyString : replaces.Lower();
                if (action == A_REPLACEALL) {
                    root->AddUndo(this);  // expensive?
                    root->FindReplaceAll(replaces, lreplaces);
                    root->ResetChildren();
                    canvas->Refresh();
                } else {
                    loopallcellssel(c, true) if (c->text.IsInSearch()) c->AddUndo(this);
                    selected.grid->ReplaceStr(this, replaces, lreplaces, selected);
                    if (action == A_REPLACEONCEJ) return SearchNext(false, true, false);
                }
                return _("Text has been replaced.");
            }

            case A_CLEARREPLACE: {
                sys->frame->replaces->Clear();
                canvas->SetFocus();
                return wxEmptyString;
            }

            case A_CLEARSEARCH: {
                sys->frame->filter->Clear();
                canvas->SetFocus();
                return wxEmptyString;
            }

            case A_SCALED:
                scaledviewingmode = !scaledviewingmode;
                root->ResetChildren();
                canvas->Refresh();
                return scaledviewingmode ? _("Now viewing TreeSheet to fit to the screen exactly, press F12 to return to normal.")
                                         : _("1:1 scale restored.");

            case A_FILTERRANGE: {
                DateTimeRangeDialog rd(sys->frame);
                if (rd.Run() == wxID_OK) ApplyEditRangeFilter(rd.begin, rd.end);
                return wxEmptyString;
            }

            case A_FILTER5:
                editfilter = 5;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTER10:
                editfilter = 10;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTER20:
                editfilter = 20;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTER50:
                editfilter = 50;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTERM:
                editfilter++;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTERL:
                editfilter--;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTERS: SetSearchFilter(true); return wxEmptyString;
            case A_FILTEROFF: SetSearchFilter(false); return wxEmptyString;

            case A_CUSTKEY: {
                wxArrayString strs, keys;
                for (auto &[s, k] : sys->frame->menustrings) {
                    strs.push_back(s);
                    keys.push_back(k);
                }
                wxSingleChoiceDialog choice(
                    sys->frame, _("Please pick a menu item to change the key binding for"),
                    _("Key binding"), strs);
                choice.SetSize(wxSize(500, 700));
                choice.Centre();
                if (choice.ShowModal() == wxID_OK) {
                    int sel = choice.GetSelection();
                    wxTextEntryDialog textentry(sys->frame,
                                                _("Please enter the new key binding string"),
                                                _("Key binding"), keys[sel]);
                    if (textentry.ShowModal() == wxID_OK) {
                        auto key = textentry.GetValue();
                        sys->frame->menustrings[strs[sel]] = key;
                        sys->cfg->Write(strs[sel], key);
                        return _("NOTE: key binding will take effect next run of TreeSheets.");
                    }
                }
                return _("Keybinding cancelled.");
            }

            case A_SETLANG: {
                auto trans = wxTranslations::Get();
                if (!trans) return _("Failed to get translation.");
                wxArrayString langs = trans->GetAvailableTranslations("ts");
                langs.Insert(wxEmptyString, 0);
                wxSingleChoiceDialog choice(
                    sys->frame, _("Please select the language for the interface (requires restart). Please select the empty row if you want to use the default language."),
                    _("Available languages"), langs);
                if (choice.ShowModal() == wxID_OK) {
                    sys->cfg->Write("defaultlang", choice.GetStringSelection());
                }
                return wxEmptyString;
            }
        }

        if (!selected.grid) return NoSel();

        auto cell = selected.GetCell();

        switch (action) {
            case A_BACKSPACE:
                if (selected.Thin()) {
                    if (selected.xs)
                        DelRowCol(selected.y, 0, selected.grid->ys, 1, -1, selected.y - 1, 0, -1);
                    else
                        DelRowCol(selected.x, 0, selected.grid->xs, 1, selected.x - 1, -1, -1, 0);
                } else if (cell && selected.TextEdit()) {
                    if (selected.cursorend == 0) return wxEmptyString;
                    cell->AddUndo(this);
                    cell->text.Backspace(selected);
                    if (canvas) canvas->Refresh();
                } else if (RemoveSelectedImage()) {
                    return wxEmptyString;
                } else {
                    selected.grid->MultiCellDelete(this, selected);
                    SetSelect(selected);
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case A_DELETE:
                if (selected.Thin()) {
                    if (selected.xs)
                        DelRowCol(selected.y, selected.grid->ys, selected.grid->ys, 0, -1,
                                  selected.y, 0, -1);
                    else
                        DelRowCol(selected.x, selected.grid->xs, selected.grid->xs, 0, selected.x,
                                  -1, -1, 0);
                } else if (cell && selected.TextEdit()) {
                    if (selected.cursor == cell->text.t.Len()) return wxEmptyString;
                    cell->AddUndo(this);
                    cell->text.Delete(selected);
                    if (canvas) canvas->Refresh();
                } else if (RemoveSelectedImage()) {
                    return wxEmptyString;
                } else {
                    selected.grid->MultiCellDelete(this, selected);
                    SetSelect(selected);
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case A_DELETE_WORD:
                if (cell && selected.TextEdit()) {
                    if (selected.cursor == cell->text.t.Len()) return wxEmptyString;
                    cell->AddUndo(this);
                    cell->text.DeleteWord(selected);
                    if (canvas) canvas->Refresh();
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case wxID_CUT:
            case wxID_COPY:
            case A_COPYWI:
            case A_COPYCT:
                if (selected.Thin()) return NoThin();
                if (selected.TextEdit()) {
                    if (selected.cursor == selected.cursorend) return _("No text selected.");
                }
                Copy(action);
                if (action == wxID_CUT) {
                    if (RemoveSelectedImage()) {
                        return wxEmptyString;
                    } else if (!selected.TextEdit()) {
                        selected.grid->cell->AddUndo(this);
                        selected.grid->MultiCellDelete(this, selected);
                        SetSelect(selected);
                    } else if (cell) {
                        cell->AddUndo(this);
                        cell->text.Backspace(selected);
                    }
                    if (canvas) canvas->Refresh();
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case A_COPYBM:
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxBitmapDataObject(GetSubBitmap(selected)));
                    wxTheClipboard->Close();
                    return _("Bitmap copied to clipboard");
                }
                return wxEmptyString;

            case A_COLLAPSE: {
                if (selected.xs * selected.ys == 1)
                    return _("More than one cell must be selected.");
                auto fc = selected.GetFirst();
                wxString ct = "";
                loopallcellssel(ci, true) if (ci != fc && ci->text.t.Len()) ct += " " + ci->text.t;
                if (!fc->HasContent() && !ct.Len()) return _("There is no content to collapse.");
                fc->parent->AddUndo(this);
                fc->text.t += ct;
                loopallcellssel(ci, false) if (ci != fc) ci->Clear();
                Selection deletesel(selected.grid,
                                    selected.x + int(selected.xs > 1),  // sidestep is possible?
                                    selected.y + int(selected.ys > 1),
                                    selected.xs - int(selected.xs > 1),
                                    selected.ys - int(selected.ys > 1));
                selected.grid->MultiCellDeleteSub(this, deletesel);
                SetSelect(Selection(selected.grid, selected.x, selected.y, 1, 1));
                fc->ResetLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_MERGECELLS: return selected.grid->MergeCells(this, selected);
            case A_UNMERGECELLS: return selected.grid->UnmergeCells(this, selected);

            case wxID_SELECTALL:
                selected.SelAll();
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, true);
                return wxEmptyString;

            case A_UP:
            case A_DOWN:
            case A_LEFT:
            case A_RIGHT: selected.Cursor(this, action, false, false); return wxEmptyString;

            case A_MUP:
            case A_MDOWN:
            case A_MLEFT:
            case A_MRIGHT:
                selected.Cursor(this, action - A_MUP + A_UP, true, false);
                return wxEmptyString;

            case A_SUP:
            case A_SDOWN:
            case A_SLEFT:
            case A_SRIGHT:
                selected.Cursor(this, action - A_SUP + A_UP, false, true);
                return wxEmptyString;

            case A_SCLEFT:
            case A_SCRIGHT:
            case A_SCUP:
            case A_SCDOWN:
                if (!selected.TextEdit()) {
                    bool horiz = (action == A_SCLEFT || action == A_SCRIGHT);
                    bool ismin = (action == A_SCLEFT || action == A_SCUP);

                    int &pos = horiz ? selected.x : selected.y;
                    int &ext = horiz ? selected.xs : selected.ys;
                    int gridmax = horiz ? selected.grid->xs : selected.grid->ys;

                    if (ismin) {
                        ext = pos + (selected.Thin() ? 0 : 1);
                        pos = 0;
                    } else {
                        ext = gridmax - pos;
                    }

                    if (sys->frame) sys->frame->UpdateStatus(selected, true);
                    if (canvas) canvas->Refresh();
                } else if (action == A_SCLEFT || action == A_SCRIGHT) {
                    selected.Cursor(this, action - A_SCUP + A_UP, true, true);
                }
                return wxEmptyString;

            case wxID_BOLD:
                selected.grid->SetStyle(this, selected, STYLE_BOLD);
                return wxEmptyString;
            case wxID_ITALIC:
                selected.grid->SetStyle(this, selected, STYLE_ITALIC);
                return wxEmptyString;
            case A_TT: selected.grid->SetStyle(this, selected, STYLE_FIXED); return wxEmptyString;
            case wxID_UNDERLINE:
                selected.grid->SetStyle(this, selected, STYLE_UNDERLINE);
                return wxEmptyString;
            case wxID_STRIKETHROUGH:
                selected.grid->SetStyle(this, selected, STYLE_STRIKETHRU);
                return wxEmptyString;

            case A_MARKDATA:
            case A_MARKVARD:
            case A_MARKVARU:
            case A_MARKVIEWH:
            case A_MARKVIEWV:
            case A_MARKCODE: {
                int newcelltype;
                switch (action) {
                    case A_MARKDATA: newcelltype = CT_DATA; break;
                    case A_MARKVARD: newcelltype = CT_VARD; break;
                    case A_MARKVARU: newcelltype = CT_VARU; break;
                    case A_MARKVIEWH: newcelltype = CT_VIEWH; break;
                    case A_MARKVIEWV: newcelltype = CT_VIEWV; break;
                    case A_MARKCODE: newcelltype = CT_CODE; break;
                }
                selected.grid->cell->AddUndo(this);
                loopallcellssel(c, false) {
                    c->celltype = (newcelltype == CT_CODE) ? sys->evaluator.InferCellType(c->text)
                                                           : newcelltype;
                    canvas->Refresh();
                }
                return wxEmptyString;
            }

            case A_CANCELEDIT:
                if (selected.TextEdit()) break;
                if (selected.grid->cell->parent) {
                    SetSelect(selected.grid->cell->parent->grid->FindCell(selected.grid->cell));
                } else {
                    selected.SelAll();
                }
                ScrollOrZoom();
                return wxEmptyString;

            case A_ENTERGRID:
            case A_ENTERGRIDN: {
                if (!(cell = selected.ThinExpand(this))) return OneCell();
                if (cell->grid) {
                    SetSelect(Selection(cell->grid, 0, 0, 1, 1));
                    ScrollOrZoom(true);
                    return wxEmptyString;
                }
                int size = 1;
                if (action == A_ENTERGRIDN &&
                    (size = (int)::wxGetNumberFromUser(
                         _("What subgrid size would you like to start with?"), _("size:"),
                         _("New subgrid"), 10, 1, 25, sys->frame)) < 0) {
                    return _("No subgrid created.");
                }
                cell->AddUndo(this);
                cell->AddGrid(size, size);
                SetSelect(Selection(cell->grid, 0, 0, 1, 1));
                paintscrolltoselection = true;
                if (canvas) canvas->Refresh();
                return wxEmptyString;
            }

            case wxID_PASTE:
                if (!(cell = selected.ThinExpand(this))) return OneCell();
                if (wxTheClipboard->Open()) {
                    if (wxTheClipboard->IsSupported(wxDF_FILENAME)) {
                        wxFileDataObject fdo;
                        wxTheClipboard->GetData(fdo);
                        PasteOrDrop(fdo);
                    } else {
                        wxTextDataObject tdo;
                        bool hastext = wxTheClipboard->IsSupported(wxDF_TEXT) ||
                                       wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
                        if (hastext) wxTheClipboard->GetData(tdo);

                        bool pasted = false;
                        if (hastext && (sys->clipboardcopy == tdo.GetText()) &&
                            sys->cellclipboard) {
                            PasteOrDrop(tdo);
                            pasted = true;
                        } else if (!selected.TextEdit() && wxTheClipboard->IsSupported(wxDF_HTML)) {
                            wxHTMLDataObject hdo;
                            if (wxTheClipboard->GetData(hdo)) pasted = PasteOrDrop(hdo);
                        }
                        if (!pasted && hastext) {
                            PasteOrDrop(tdo);
                            pasted = true;
                        }
                        if (!pasted && wxTheClipboard->IsSupported(wxDF_BITMAP)) {
                            wxBitmapDataObject bdo;
                            wxTheClipboard->GetData(bdo);
                            PasteOrDrop(bdo);
                        }
                    }
                    wxTheClipboard->Close();
                    canvas->Refresh();
                } else if (sys->cellclipboard) {
                    cell->Paste(this, sys->cellclipboard.get(), selected);
                    canvas->Refresh();
                }
                return wxEmptyString;

            case A_EDITNOTE: {
                if (!(cell = selected.ThinExpand(this))) return OneCell();

                wxDialog dlg(sys->frame, wxID_ANY, _("Note"), wxDefaultPosition,
                             wxSize(sys->notesizex, sys->notesizey),
                             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
                auto *sizer = new wxBoxSizer(wxVERTICAL);
                wxTextCtrl text(&dlg, wxID_ANY, cell->note, wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE);
                sizer->Add(&text, 1, wxEXPAND | wxALL, 10);
                auto *btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
                sizer->Add(btns, 0, wxALIGN_CENTER | wxBOTTOM, 10);
                dlg.SetSizer(sizer);

                text.SetFocus();

                if (dlg.ShowModal() == wxID_OK) {
                    if (cell->note != text.GetValue()) {
                        cell->AddUndo(this);
                        cell->note = text.GetValue();
                        canvas->Refresh();
                    }
                    sys->notesizex = dlg.GetSize().x;
                    sys->notesizey = dlg.GetSize().y;
                }
                return wxEmptyString;
            }

            case A_PASTESTYLE:
                if (!sys->cellclipboard) return _("No style to paste.");
                selected.grid->cell->AddUndo(this);
                selected.grid->SetStyles(selected, sys->cellclipboard.get());
                selected.grid->cell->ResetChildren();
                if (canvas) canvas->Refresh();
                return wxEmptyString;

            case A_ENTERCELL:
            case A_ENTERCELL_JUMPTOEND:
            case A_ENTERCELL_JUMPTOSTART:
            case A_PROGRESSCELL: {
                if (!(cell = selected.ThinExpand(this, action == A_ENTERCELL_JUMPTOSTART)))
                    return OneCell();
                if (selected.TextEdit()) {
                    selected.Cursor(this, action == A_PROGRESSCELL ? A_RIGHT : A_DOWN, false, false,
                                    true);
                } else {
                    selected.EnterEdit(
                        this,
                        action == A_ENTERCELL_JUMPTOEND ? static_cast<int>(cell->text.t.Len()) : 0,
                        static_cast<int>(cell->text.t.Len()));
                    paintscrolltoselection = true;
                    canvas->Refresh();
                }
                return wxEmptyString;
            }
            case A_TEXTNEWLINE: return TextNewline();

            case A_IMAGE: {
                if (!(cell = selected.ThinExpand(this))) return OneCell();
                auto filename =
                    ::wxFileSelector(_("Please select an image file:"), "", "", "", "*.*",
                                     wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
                cell->AddUndo(this);
                LoadImageIntoCell(filename, cell, sys->frame->FromDIP(1.0));
                if (canvas) canvas->Refresh();
                return wxEmptyString;
            }

            case A_IMAGER: {
                if (RemoveSelectedImage()) return wxEmptyString;
                selected.grid->cell->AddUndo(this);
                selected.grid->ClearImages(selected);
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_SORTD: return Sort(true);
            case A_SORT: return Sort(false);

            case A_SCOLS:
                selected.y = 0;
                selected.ys = selected.grid->ys;
                canvas->Refresh();
                return wxEmptyString;

            case A_SROWS:
                selected.x = 0;
                selected.xs = selected.grid->xs;
                canvas->Refresh();
                return wxEmptyString;

            case A_BORD0:
            case A_BORD1:
            case A_BORD2:
            case A_BORD3:
            case A_BORD4:
            case A_BORD5:
                selected.grid->cell->AddUndo(this);
                selected.grid->SetBorderWidth(action - A_BORD0);
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                return wxEmptyString;

            case A_SEL_BORD_OUTER_COLOR:
            case A_SEL_BORD_INNER_COLOR:
            case A_SEL_BORD_OUTER_COLOR_PICK:
            case A_SEL_BORD_INNER_COLOR_PICK: {
                if (selected.Thin()) return NoThin();
                uint color = sys->lastbordcolor;
                if (action == A_SEL_BORD_OUTER_COLOR_PICK ||
                    action == A_SEL_BORD_INNER_COLOR_PICK) {
                    auto picked = wxGetColourFromUser(sys->frame, wxColour(color));
                    if (!picked.IsOk()) return _("Color change cancelled.");
                    color = (picked.Blue() << 16) + (picked.Green() << 8) + picked.Red();
                    sys->lastbordcolor = color;
                }
                selected.grid->cell->AddUndo(this);
                if (action == A_SEL_BORD_OUTER_COLOR ||
                    action == A_SEL_BORD_OUTER_COLOR_PICK)
                    selected.grid->SetSelectionOuterBorder(selected, color, 0, false);
                else
                    selected.grid->SetSelectionInnerBorder(selected, color, 0, false);
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_SEL_BORD_OUTER_CLEAR:
            case A_SEL_BORD_INNER_CLEAR:
                if (selected.Thin()) return NoThin();
                selected.grid->cell->AddUndo(this);
                if (action == A_SEL_BORD_OUTER_CLEAR)
                    selected.grid->SetSelectionOuterBorder(selected, g_bordercolor_default, 0, true);
                else
                    selected.grid->SetSelectionInnerBorder(selected, g_bordercolor_default, 0, true);
                canvas->Refresh();
                return wxEmptyString;

            case A_SEL_BORD_OUTER0:
            case A_SEL_BORD_OUTER1:
            case A_SEL_BORD_OUTER2:
            case A_SEL_BORD_OUTER3:
            case A_SEL_BORD_OUTER4:
            case A_SEL_BORD_OUTER5:
                if (selected.Thin()) return NoThin();
                selected.grid->cell->AddUndo(this);
                selected.grid->SetSelectionOuterBorder(selected, sys->lastbordcolor,
                                                       action - A_SEL_BORD_OUTER0, true);
                canvas->Refresh();
                return wxEmptyString;

            case A_SEL_BORD_INNER0:
            case A_SEL_BORD_INNER1:
            case A_SEL_BORD_INNER2:
            case A_SEL_BORD_INNER3:
            case A_SEL_BORD_INNER4:
            case A_SEL_BORD_INNER5:
                if (selected.Thin()) return NoThin();
                selected.grid->cell->AddUndo(this);
                selected.grid->SetSelectionInnerBorder(selected, sys->lastbordcolor,
                                                       action - A_SEL_BORD_INNER0, true);
                canvas->Refresh();
                return wxEmptyString;

            case A_TEXTGRID: return layrender(-1, true, true);

            case A_V_GS: return layrender(DS_GRID, true);
            case A_V_BS: return layrender(DS_BLOBSHIER, true);
            case A_V_LS: return layrender(DS_BLOBLINE, true);
            case A_H_GS: return layrender(DS_GRID, false);
            case A_H_BS: return layrender(DS_BLOBSHIER, false);
            case A_H_LS: return layrender(DS_BLOBLINE, false);
            case A_GS: return layrender(DS_GRID, true, false, true);
            case A_BS: return layrender(DS_BLOBSHIER, true, false, true);
            case A_LS: return layrender(DS_BLOBLINE, true, false, true);

            case A_WRAP: return selected.Wrap(this);

            case A_RESETSIZE:
            case A_RESETWIDTH:
            case A_RESETSTYLE:
            case A_RESETCOLOR:
            case A_LASTCELLCOLOR:
            case A_LASTTEXTCOLOR:
            case A_LASTBORDCOLOR:
            case A_LASTIMAGE:
                selected.grid->cell->AddUndo(this);
                if (cell && selected.TextEdit() && cell->text.HasSelectionRange(selected)) {
                    bool handled = true;
                    switch (action) {
                        case A_RESETSTYLE:
                            cell->text.SetRichStyleBits(selected, 0, cell->textcolor,
                                                        cell->text.stylebits);
                            break;
                        case A_RESETCOLOR:
                            cell->text.SetRichColor(selected, g_textcolor_default, cell->textcolor,
                                                    cell->text.stylebits);
                            break;
                        case A_LASTTEXTCOLOR:
                            cell->text.SetRichColor(selected, sys->lasttextcolor, cell->textcolor,
                                                    cell->text.stylebits);
                            break;
                        default: handled = false; break;
                    }
                    if (handled) {
                        selected.grid->cell->ResetChildren();
                        if (canvas) canvas->Refresh();
                        if (sys->frame) sys->frame->UpdateStatus(selected, false);
                        return wxEmptyString;
                    }
                }
                if (action == A_RESETCOLOR) {
                    selected.grid->bordercolor = g_bordercolor_default;
                    if (!selected.Thin()) selected.grid->ClearSelectionBorders(selected);
                } else if (action == A_LASTBORDCOLOR) {
                    selected.grid->bordercolor = sys->lastbordcolor;
                }
                loopallcellssel(c, true) switch (action) {
                    case A_RESETSIZE: c->text.relsize = 0; break;
                    case A_RESETWIDTH:
                        for (int x = selected.x; x < selected.x + selected.xs; x++)
                            selected.grid->colwidths[x] = sys->defaultmaxcolwidth;
                        selected.grid->cell->ResetLayout();
                        break;
                    case A_RESETSTYLE:
                        c->text.stylebits = 0;
                        c->text.ClearRichStyleBits();
                        break;
                    case A_RESETCOLOR:
                        if (c->IsTag(this)) {
                            tags[c->text.t] = g_tagcolor_default;
                        } else {
                            c->textcolor = g_textcolor_default;
                        }
                        c->text.ClearRichColors();
                        c->cellcolor = g_cellcolor_default;
                        c->bordercolor = g_bordercolor_default;
                        if (c->grid) {
                            c->grid->bordercolor = g_bordercolor_default;
                            c->grid->ClearAllCustomBorders();
                        }
                        break;
                    case A_LASTCELLCOLOR: c->cellcolor = sys->lastcellcolor; break;
                    case A_LASTTEXTCOLOR:
                        c->textcolor = sys->lasttextcolor;
                        c->text.ClearRichColors();
                        break;
                    case A_LASTBORDCOLOR: break;
                    case A_LASTIMAGE:
                        if (sys->lastimage) c->text.image = sys->lastimage;
                        break;
                }
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, false);
                return wxEmptyString;

            case A_MINISIZE: {
                selected.grid->cell->AddUndo(this);
                CollectCellsSel(false);
                vector<Cell *> outer;
                outer.insert(outer.end(), itercells.begin(), itercells.end());
                for (auto o : outer) {
                    if (o->grid) {
                        loopcellsin(o, c) if (_i) {
                            c->text.relsize = g_deftextsize - g_mintextsize() - c->Depth();
                        }
                    }
                }
                outer.clear();
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_FOLD:
            case A_FOLDALL:
            case A_UNFOLDALL:
                loopallcellssel(c, action != A_FOLD) if (c->grid) {
                    c->AddUndo(this);
                    c->grid->folded = action == A_FOLD ? !c->grid->folded : action == A_FOLDALL;
                    c->ResetChildren();
                }
                if (canvas) canvas->Refresh();
                return wxEmptyString;

            case A_HOME:
            case A_END:
            case A_CHOME:
            case A_CEND:
                if (selected.TextEdit()) break;
                selected.HomeEnd(this, action == A_HOME || action == A_CHOME);
                return wxEmptyString;

            case A_IMAGESCP:
            case A_IMAGESCW:
            case A_IMAGESCF: {
                std::set<Image *> imagestomanipulate;
                long v = 0.0;
                loopallcellssel(c, true) {
                    if (c->text.image) { imagestomanipulate.insert(c->text.image); }
                }
                if (imagestomanipulate.empty()) return wxEmptyString;
                if (action == A_IMAGESCW) {
                    v = wxGetNumberFromUser(_("Please enter the new image width:"), _("Width"),
                                            _("Image Resize"), 500, 10, 4000, sys->frame);
                } else {
                    v = wxGetNumberFromUser(
                        _("Please enter the percentage you want the image scaled by:"), "%",
                        _("Image Resize"), 50, 5, 400, sys->frame);
                }
                if (v < 0) return wxEmptyString;
                if (action == A_IMAGESCF) return ScaleSelectedImagesForDisplay(v / 100.0);
                for (auto image : imagestomanipulate) {
                    if (action == A_IMAGESCW) {
                        int pw = image->pixel_width;
                        if (pw)
                            image->ImageRescale(static_cast<double>(v) / static_cast<double>(pw));
                    } else if (action == A_IMAGESCP) {
                        image->ImageRescale(v / 100.0);
                    }
                }
                currentdrawroot->ResetChildren();
                currentdrawroot->ResetLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_IMAGESCN: {
                return ScaleSelectedImagesForDisplay(1.0, true);
            }

            case A_VIEWIMAGE: {
                return OpenSelectedImageViewer();
            }

            case A_IMAGESVA: {
                set<Image *> imagestosave;
                loopallcellssel(c, true) if (auto image = c->text.image)
                    imagestosave.insert(image);
                if (imagestosave.empty()) return _("There are no images in the selection.");
                wxString filename = ::wxFileSelector(
                    _("Choose image file to save:"), "", "", "",
                    _("PNG file (*.png)|*.png|JPEG file (*.jpg)|*.jpg|All Files (*.*)|*.*"),
                    wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
                if (filename.empty()) return _("Save cancelled.");
                auto i = 0;
                for (auto image : imagestosave) {
                    wxFileName fn(filename);
                    wxString finalfilename = fn.GetPathWithSep() + fn.GetName() +
                                             (i == 0 ? wxString() : wxString::Format("%d", i)) +
                                             image->GetFileExtension();
                    wxFFileOutputStream os(finalfilename, "w+b");
                    if (!os.IsOk()) {
                        wxMessageBox(
                            _("Error writing image file! (try saving under new filename)."),
                            finalfilename.wx_str(), wxOK, sys->frame);
                        return _("Error writing to file.");
                    }
                    os.Write(image->data.data(), image->data.size());
                    i++;
                }
                return _("Image(s) have been saved to disk.");
            }

            case A_SAVE_AS_JPEG:
            case A_SAVE_AS_PNG: {
                wxString returnmessage = _("No image found to convert");
                loopallcellssel(c, true) {
                    auto image = c->text.image;
                    if (action == A_SAVE_AS_JPEG && image && image->type == 'I') {
                        auto transferimage = ConvertBufferToWxImage(image->data, wxBITMAP_TYPE_PNG);
                        image->ReplaceData(ConvertWxImageToBuffer(transferimage, wxBITMAP_TYPE_JPEG),
                                           'J');
                        returnmessage =
                            _("Images in selected cells have been converted to JPEG format.");
                    }
                    if (action == A_SAVE_AS_PNG && image && image->type == 'J') {
                        auto transferimage =
                            ConvertBufferToWxImage(image->data, wxBITMAP_TYPE_JPEG);
                        image->ReplaceData(ConvertWxImageToBuffer(transferimage, wxBITMAP_TYPE_PNG),
                                           'I');
                        returnmessage =
                            _("Images in selected cells have been converted to PNG format.");
                    }
                }
                return returnmessage;
            }

            case A_BROWSE: {
                wxString returnmessage = "";
                int counter = 0;
                loopallcellssel(c, false) {
                    if (counter >= g_max_launches) {
                        returnmessage = _("Maximum number of launches reached.");
                        break;
                    }
                    if (!wxLaunchDefaultBrowser(c->text.ToText(0, selected, A_EXPTEXT))) {
                        returnmessage = _("The browser could not open at least one link.");
                    } else {
                        counter++;
                    }
                }
                return returnmessage;
            }

            case A_BROWSEF: {
                wxString returnmessage = "";
                int counter = 0;
                loopallcellssel(c, false) {
                    if (counter >= g_max_launches) {
                        returnmessage = _("Maximum number of launches reached.");
                        break;
                    }
                    auto f = c->text.ToText(0, selected, A_EXPTEXT);
                    wxFileName fn(f);
                    if (fn.IsRelative()) fn.MakeAbsolute(wxFileName(filename).GetPath());
                    if (!wxLaunchDefaultApplication(fn.GetFullPath())) {
                        returnmessage = _("At least one file could not be opened.");
                    } else {
                        counter++;
                    }
                }
                return returnmessage;
            }

            case A_TAGADD: {
                loopallcellssel(c, false) {
                    if (!c->text.t.Len()) continue;
                    tags[c->text.t] = g_tagcolor_default;
                }
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_TAGREMOVE: {
                loopallcellssel(c, false) tags.erase(c->text.t);
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_TRANSPOSE: {
                if (selected.Thin()) return NoThin();
                auto ac = selected.grid->cell;
                ac->AddUndo(this);
                if (selected.IsAll()) {
                    ac->grid->Transpose();
                    SetSelect(ac->parent ? ac->parent->grid->FindCell(ac) : Selection());
                } else {
                    loopallcellssel(c, false) if (c->grid) c->grid->Transpose();
                }
                ac->ResetChildren();
                canvas->Refresh();
                return wxEmptyString;
            }
        }

        if (cell || (!cell && selected.IsAll())) {
            auto ac = cell ? cell : selected.grid->cell;
            switch (action) {
                case A_HIFY:
                    if (!ac->grid) return NoGrid();
                    if (!ac->grid->IsTable())
                        return _(
                            "Selected grid is not a table: cells must not already have sub-grids.");
                    ac->AddUndo(this);
                    ac->grid->Hierarchify(this);
                    ac->ResetChildren();
                    selected = Selection();
                    begindrag = Selection();
                    canvas->Refresh();
                    return wxEmptyString;

                case A_FLATTEN: {
                    if (!ac->grid) return NoGrid();
                    ac->AddUndo(this);
                    int maxdepth = 0, leaves = 0;
                    ac->MaxDepthLeaves(0, maxdepth, leaves);
                    auto g = make_shared<Grid>(maxdepth, leaves);
                    g->InitCells();
                    ac->grid->Flatten(0, 0, g.get());
                    ac->grid = g;
                    g->ReParent(ac);
                    ac->ResetChildren();
                    selected = Selection();
                    begindrag = Selection();
                    canvas->Refresh();
                    return wxEmptyString;
                }
            }
        }

        if (!cell) return OneCell();

        switch (action) {
            case A_NEXT:
                if (selected.TextEdit()) return TextInsert("    ");
                selected.Next(this, false);
                return wxEmptyString;
            case A_PREV: selected.Next(this, true); return wxEmptyString;

            case A_LINK:
            case A_LINKIMG:
            case A_LINKREV:
            case A_LINKIMGREV: {
                if ((action == A_LINK || action == A_LINKREV) && !cell->text.t.Len())
                    return _("No text in this cell.");
                if ((action == A_LINKIMG || action == A_LINKIMGREV) && !cell->text.image)
                    return _("No image in this cell.");
                bool t1 = false, t2 = false;
                auto link = root->FindLink(selected, cell, nullptr, t1, t2,
                                           action == A_LINK || action == A_LINKIMG,
                                           action == A_LINKIMG || action == A_LINKIMGREV);
                if (!link || !link->parent) return _("No matching cell found!");
                SetSelect(link->parent->grid->FindCell(link));
                ScrollOrZoom(true);
                return wxEmptyString;
            }

            case A_COLCELL: sys->customcolor = cell->cellcolor; return wxEmptyString;

            case A_HSWAP: {
                auto pp = cell->parent->parent;
                if (!pp) return _("Cannot move this cell up in the hierarchy.");
                if (pp->grid->xs != 1 && pp->grid->ys != 1)
                    return _("Can only move this cell into a Nx1 or 1xN grid.");
                if (cell->parent->grid->xs != 1 && cell->parent->grid->ys != 1)
                    return _("Can only move this cell from a Nx1 or 1xN grid.");
                pp->AddUndo(this);
                SetSelect(pp->grid->HierarchySwap(cell->text.t));
                pp->ResetChildren();
                pp->ResetLayout();
                if (canvas) canvas->Refresh();
                return wxEmptyString;
            }

            case A_FILTERBYCELLBG:
                loopallcells(ci) ci->text.filtered = ci->cellcolor != cell->cellcolor;
                root->ResetChildren();
                canvas->Refresh();
                return wxEmptyString;

            case A_FILTERNOTE:
                loopallcells(ci) ci->text.filtered = ci->note.IsEmpty();
                root->ResetChildren();
                canvas->Refresh();
                return wxEmptyString;

            case A_FILTERMATCHNEXT:
                bool lastsel = true;
                Cell *next = root->FindNextFilterMatch(nullptr, selected.GetCell(), lastsel);
                if (!next) return _("No matches for filter.");
                if (next->parent) SetSelect(next->parent->grid->FindCell(next));
                canvas->SetFocus();
                ScrollOrZoom(true);
                return wxEmptyString;
        }

        if (!selected.TextEdit()) return _("only works in cell text mode");

        switch (action) {
            case A_CANCELEDIT:
                if (LastUndoSameCellTextEdit(cell))
                    Undo(undolist, redolist);
                else
                    if (canvas) canvas->Refresh();
                selected.ExitEdit(this);
                return wxEmptyString;

            case A_BACKSPACE_WORD:
                if (selected.cursorend == 0) return wxEmptyString;
                cell->AddUndo(this);
                cell->text.BackspaceWord(selected);
                if (canvas) canvas->Refresh();
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case A_SHOME:
            case A_SEND:
            case A_CHOME:
            case A_CEND:
            case A_HOME:
            case A_END: {
                switch (action) {
                    case A_SHOME:  // FIXME: this functionality is really SCHOME, SHOME should be
                                   // within line
                        selected.cursor = 0;
                        break;
                    case A_SEND: selected.cursorend = static_cast<int>(cell->text.t.Len()); break;
                    case A_CHOME: selected.cursor = selected.cursorend = 0; break;
                    case A_CEND: selected.cursor = selected.cursorend = selected.MaxCursor(); break;
                    case A_HOME: cell->text.HomeEnd(selected, true); break;
                    case A_END: cell->text.HomeEnd(selected, false); break;
                }
                paintscrolltoselection = true;
                if (canvas) canvas->Refresh();
                return wxEmptyString;
            }
            default: return _("Internal error: unimplemented operation!");
        }
    }

    wxString SearchNext(bool focusmatch, bool jump, bool reverse) {
        if (!root) return wxEmptyString;  // fix crash when opening new doc
        if (!sys->searchstring.Len()) return _("No search string.");
        bool lastsel = true;
        Cell *next = root->FindNextSearchMatch(sys->searchstring, nullptr, selected.GetCell(),
                                               lastsel, reverse);
        if (!next) return _("No matches for search.");
        if (!jump) return wxEmptyString;
        SetSelect(next->parent->grid->FindCell(next));
        if (focusmatch) canvas->SetFocus();
        ScrollOrZoom(true);
        return wxEmptyString;
    }

    wxString layrender(int ds, bool vert, bool toggle = false, bool noset = false) {
        if (selected.Thin()) return NoThin();
        selected.grid->cell->AddUndo(this);
        bool v = toggle ? !selected.GetFirst()->verticaltextandgrid : vert;
        bool one_layer = sys->renderstyleonelayer;
        if (ds >= 0 && selected.IsAll() && !selected.GetCell() && !one_layer)
            selected.grid->cell->drawstyle = ds;
        selected.grid->SetGridTextLayout(ds, v, noset, selected,
                                         one_layer ? 0 : -1);
        selected.grid->cell->ResetChildren();
        if (canvas) canvas->Refresh();
        return wxEmptyString;
    }

    void ZoomOutIfNoGrid() {
        if (!WalkPath(drawpath)->grid) Zoom(-1);
    }

    struct HTMLCellStyle {
        bool hascellcolor {false};
        bool hastextcolor {false};
        bool hasstylebits {false};
        bool hasrelsize {false};
        uint cellcolor {g_cellcolor_default};
        uint textcolor {g_textcolor_default};
        int stylebits {0};
        int relsize {0};
    };

    struct HTMLTableCell {
        wxString text;
        HTMLCellStyle style;
        int colspan {1};
        int rowspan {1};
    };

    struct HTMLTablePlacement {
        wxString text;
        HTMLCellStyle style;
        int x {0};
        int y {0};
        int xs {1};
        int ys {1};
    };

    static bool IsHTMLSpace(wxChar c) {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f';
    }

    static bool IsHTMLNameChar(wxChar c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
    }

    static wxString TrimHTMLText(wxString s) {
        s.Trim(true);
        s.Trim(false);
        return s;
    }

    static int FindFrom(const wxString &s, const wxString &needle, int start) {
        if (needle.IsEmpty()) return wxNOT_FOUND;
        auto len = static_cast<int>(s.Len());
        if (start < 0) start = 0;
        if (start >= len) return wxNOT_FOUND;
        auto pos = s.Mid(start).Find(needle);
        return pos == wxNOT_FOUND ? wxNOT_FOUND : start + pos;
    }

    static int FindHTMLTagEnd(const wxString &html, int start) {
        wxChar quote = 0;
        for (auto i = start, len = static_cast<int>(html.Len()); i < len; i++) {
            auto c = html[i];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                return i;
            }
        }
        return wxNOT_FOUND;
    }

    static int FindHTMLTag(const wxString &lower, const wxString &tag, int start,
                           int limit = -1, bool closing = false) {
        auto len = static_cast<int>(lower.Len());
        if (limit < 0 || limit > len) limit = len;
        wxString pattern = closing ? wxString("</") + tag : wxString("<") + tag;
        for (auto pos = FindFrom(lower, pattern, start); pos != wxNOT_FOUND && pos < limit;
             pos = FindFrom(lower, pattern, pos + 1)) {
            auto after = pos + static_cast<int>(pattern.Len());
            if (after >= len || !IsHTMLNameChar(lower[after])) return pos;
        }
        return wxNOT_FOUND;
    }

    static int FindMatchingHTMLTagClose(const wxString &html, const wxString &lower,
                                        const wxString &tag, int openend, int limit = -1) {
        auto len = static_cast<int>(lower.Len());
        if (limit < 0 || limit > len) limit = len;
        auto depth = 1;
        for (auto pos = openend + 1; pos < limit;) {
            auto nextopen = FindHTMLTag(lower, tag, pos, limit);
            auto nextclose = FindHTMLTag(lower, tag, pos, limit, true);
            if (nextclose == wxNOT_FOUND) return wxNOT_FOUND;
            if (nextopen != wxNOT_FOUND && nextopen < nextclose) {
                auto nextopenend = FindHTMLTagEnd(html, nextopen);
                if (nextopenend == wxNOT_FOUND) return wxNOT_FOUND;
                depth++;
                pos = nextopenend + 1;
            } else {
                depth--;
                if (!depth) return nextclose;
                auto nextcloseend = FindHTMLTagEnd(html, nextclose);
                if (nextcloseend == wxNOT_FOUND) return wxNOT_FOUND;
                pos = nextcloseend + 1;
            }
        }
        return wxNOT_FOUND;
    }

    static wxString HTMLTagName(const wxString &tag) {
        auto lower = tag.Lower();
        auto len = static_cast<int>(lower.Len());
        auto i = 0;
        if (i < len && lower[i] == '<') i++;
        while (i < len && IsHTMLSpace(lower[i])) i++;
        if (i < len && lower[i] == '/') i++;
        while (i < len && IsHTMLSpace(lower[i])) i++;
        auto start = i;
        while (i < len && IsHTMLNameChar(lower[i])) i++;
        return lower.Mid(start, i - start);
    }

    static bool IsHTMLClosingTag(const wxString &tag) {
        auto lower = tag.Lower();
        auto len = static_cast<int>(lower.Len());
        auto i = 0;
        if (i < len && lower[i] == '<') i++;
        while (i < len && IsHTMLSpace(lower[i])) i++;
        return i < len && lower[i] == '/';
    }

    static bool HTMLAttributeValue(const wxString &tag, const wxString &attribute,
                                   wxString &value) {
        auto lower = tag.Lower();
        auto attr = attribute.Lower();
        auto len = static_cast<int>(lower.Len());
        for (auto pos = FindFrom(lower, attr, 0); pos != wxNOT_FOUND;
             pos = FindFrom(lower, attr, pos + 1)) {
            auto before = pos == 0 || !IsHTMLNameChar(lower[pos - 1]);
            auto after = pos + static_cast<int>(attr.Len());
            if (!before || (after < len && IsHTMLNameChar(lower[after]))) continue;
            while (after < len && IsHTMLSpace(lower[after])) after++;
            if (after >= len || lower[after] != '=') continue;
            after++;
            while (after < len && IsHTMLSpace(lower[after])) after++;

            wxChar quote = 0;
            if (after < len && (tag[after] == '"' || tag[after] == '\'')) quote = tag[after++];
            auto start = after;
            while (after < len &&
                   (quote ? tag[after] != quote
                          : (!IsHTMLSpace(tag[after]) && tag[after] != '>'))) {
                after++;
            }

            value = TrimHTMLText(tag.Mid(start, after - start));
            return true;
        }
        return false;
    }

    static int HTMLAttributeInt(const wxString &tag, const wxString &attribute, int def = 1) {
        wxString text;
        if (HTMLAttributeValue(tag, attribute, text)) {
            long value = def;
            if (text.ToLong(&value) && value > 0) return static_cast<int>(min<long>(value, 1000));
        }
        return def;
    }

    static void AddCSSDeclarations(const wxString &declarations, map<wxString, wxString> &props) {
        auto len = static_cast<int>(declarations.Len());
        auto start = 0;
        wxChar quote = 0;
        auto addsegment = [&](int end) {
            auto segment = declarations.Mid(start, end - start);
            wxChar propquote = 0;
            auto colon = wxNOT_FOUND;
            for (auto i = 0, slen = static_cast<int>(segment.Len()); i < slen; i++) {
                auto c = segment[i];
                if (propquote) {
                    if (c == propquote) propquote = 0;
                } else if (c == '"' || c == '\'') {
                    propquote = c;
                } else if (c == ':') {
                    colon = i;
                    break;
                }
            }
            if (colon == wxNOT_FOUND) return;
            auto prop = TrimHTMLText(segment.Left(colon)).Lower();
            auto value = TrimHTMLText(segment.Mid(colon + 1));
            value.Replace("!important", "");
            value = TrimHTMLText(value);
            if (!prop.IsEmpty()) props[prop] = value;
        };

        for (auto i = 0; i <= len; i++) {
            wxChar c = i < len ? wxChar(declarations[i]) : wxChar(';');
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == ';' || i == len) {
                addsegment(i);
                start = i + 1;
            }
        }
    }

    static map<wxString, wxString> ExtractHTMLClassStyles(const wxString &html) {
        map<wxString, wxString> classstyles;
        auto lower = html.Lower();
        for (auto stylepos = FindHTMLTag(lower, "style", 0); stylepos != wxNOT_FOUND;
             stylepos = FindHTMLTag(lower, "style", stylepos + 1)) {
            auto styleopenend = FindHTMLTagEnd(html, stylepos);
            if (styleopenend == wxNOT_FOUND) break;
            auto styleend = FindHTMLTag(lower, "style", styleopenend + 1, -1, true);
            if (styleend == wxNOT_FOUND) break;

            auto css = html.Mid(styleopenend + 1, styleend - styleopenend - 1);
            for (auto pos = 0;;) {
                auto brace = FindFrom(css, "{", pos);
                if (brace == wxNOT_FOUND) break;
                auto endbrace = FindFrom(css, "}", brace + 1);
                if (endbrace == wxNOT_FOUND) break;
                auto selector = css.Mid(pos, brace - pos);
                auto body = css.Mid(brace + 1, endbrace - brace - 1);
                for (auto dot = FindFrom(selector, ".", 0); dot != wxNOT_FOUND;
                     dot = FindFrom(selector, ".", dot + 1)) {
                    auto namepos = dot + 1;
                    while (namepos < static_cast<int>(selector.Len()) &&
                           IsHTMLNameChar(selector[namepos]))
                        namepos++;
                    auto name = selector.Mid(dot + 1, namepos - dot - 1).Lower();
                    if (!name.IsEmpty()) {
                        if (!classstyles[name].IsEmpty()) classstyles[name] += ";";
                        classstyles[name] += body;
                    }
                }
                pos = endbrace + 1;
            }
        }
        return classstyles;
    }

    static int HexDigit(wxChar c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static bool ParseCSSHexColor(const wxString &value, uint &rgb) {
        auto hash = value.Find('#');
        if (hash == wxNOT_FOUND) return false;
        auto pos = hash + 1;
        auto len = static_cast<int>(value.Len());
        auto end = pos;
        while (end < len && HexDigit(value[end]) >= 0) end++;
        auto count = end - pos;
        if (count >= 6) {
            unsigned long parsed = 0;
            if (!value.Mid(pos, 6).ToULong(&parsed, 16)) return false;
            rgb = static_cast<uint>(parsed & 0xFFFFFF);
            return true;
        }
        if (count == 3) {
            auto r = HexDigit(value[pos]);
            auto g = HexDigit(value[pos + 1]);
            auto b = HexDigit(value[pos + 2]);
            if (r < 0 || g < 0 || b < 0) return false;
            rgb = static_cast<uint>((r * 17) << 16 | (g * 17) << 8 | (b * 17));
            return true;
        }
        return false;
    }

    static bool CSSColorByte(wxString token, int &byte) {
        token = TrimHTMLText(token);
        auto percent = token.EndsWith("%");
        if (percent) token.RemoveLast();
        double value = 0;
        if (!token.ToDouble(&value)) return false;
        if (percent) value = value * 255.0 / 100.0;
        byte = max(0, min(255, static_cast<int>(value + 0.5)));
        return true;
    }

    static bool ParseCSSRGBColor(const wxString &value, uint &rgb) {
        auto lower = value.Lower();
        auto rgbpos = FindFrom(lower, "rgb", 0);
        if (rgbpos == wxNOT_FOUND) return false;
        auto open = FindFrom(lower, "(", rgbpos);
        auto close = open == wxNOT_FOUND ? wxNOT_FOUND : FindFrom(lower, ")", open + 1);
        if (open == wxNOT_FOUND || close == wxNOT_FOUND) return false;
        auto parts = wxStringTokenize(lower.Mid(open + 1, close - open - 1), ", \t\r\n");
        if (parts.size() < 3) return false;
        int r = 0;
        int g = 0;
        int b = 0;
        if (!CSSColorByte(parts[0], r) || !CSSColorByte(parts[1], g) ||
            !CSSColorByte(parts[2], b))
            return false;
        rgb = static_cast<uint>((r << 16) | (g << 8) | b);
        return true;
    }

    static bool NamedCSSColor(const wxString &name, uint &rgb) {
        static const map<wxString, uint> colors {
            {"aqua", 0x00FFFF},       {"black", 0x000000},     {"blue", 0x0000FF},
            {"cyan", 0x00FFFF},       {"fuchsia", 0xFF00FF},   {"gray", 0x808080},
            {"grey", 0x808080},       {"green", 0x008000},     {"lime", 0x00FF00},
            {"maroon", 0x800000},     {"navy", 0x000080},      {"olive", 0x808000},
            {"purple", 0x800080},     {"red", 0xFF0000},       {"silver", 0xC0C0C0},
            {"teal", 0x008080},       {"white", 0xFFFFFF},     {"windowtext", 0x000000},
            {"yellow", 0xFFFF00},
        };
        auto it = colors.find(name.Lower());
        if (it == colors.end()) return false;
        rgb = it->second;
        return true;
    }

    static bool ParseCSSColor(wxString value, uint &color) {
        value.Replace("!important", "");
        value = TrimHTMLText(value);
        auto lower = value.Lower();
        if (lower.Find("transparent") != wxNOT_FOUND || lower == "none" ||
            lower == "automatic")
            return false;

        uint rgb = 0;
        if (ParseCSSHexColor(lower, rgb) || ParseCSSRGBColor(lower, rgb)) {
            color = SwapColor(rgb);
            return true;
        }

        auto tokens = wxStringTokenize(lower, " \t\r\n,;/");
        loop(i, tokens.size()) {
            if (NamedCSSColor(tokens[i], rgb)) {
                color = SwapColor(rgb);
                return true;
            }
        }
        return NamedCSSColor(lower, rgb) ? (color = SwapColor(rgb), true) : false;
    }

    static bool ParseCSSLengthPoints(wxString value, double &points) {
        value = TrimHTMLText(value.Lower());
        auto start = 0;
        auto len = static_cast<int>(value.Len());
        while (start < len && !(value[start] == '-' || value[start] == '+' ||
                                value[start] == '.' || (value[start] >= '0' && value[start] <= '9')))
            start++;
        auto end = start;
        while (end < len && (value[end] == '-' || value[end] == '+' || value[end] == '.' ||
                             (value[end] >= '0' && value[end] <= '9')))
            end++;
        if (start == end) return false;
        double number = 0;
        if (!value.Mid(start, end - start).ToDouble(&number)) return false;
        auto unit = TrimHTMLText(value.Mid(end));
        if (unit.StartsWith("px"))
            points = number * 72.0 / 96.0;
        else if (unit.StartsWith("pt") || unit.IsEmpty())
            points = number;
        else
            return false;
        return points > 0;
    }

    static void ApplyCSSPropsToStyle(const map<wxString, wxString> &props, HTMLCellStyle &style) {
        auto setcolor = [&](const wxString &property, bool text) {
            auto it = props.find(property);
            if (it == props.end()) return;
            uint color = 0;
            if (!ParseCSSColor(it->second, color)) return;
            if (text) {
                style.hastextcolor = true;
                style.textcolor = color;
            } else {
                style.hascellcolor = true;
                style.cellcolor = color;
            }
        };

        setcolor("background", false);
        setcolor("background-color", false);
        setcolor("color", true);

        auto weight = props.find("font-weight");
        if (weight != props.end()) {
            style.hasstylebits = true;
            auto value = weight->second.Lower();
            long numeric = 0;
            if (value.Find("bold") != wxNOT_FOUND ||
                (TrimHTMLText(value).ToLong(&numeric) && numeric >= 600))
                style.stylebits |= STYLE_BOLD;
            else
                style.stylebits &= ~STYLE_BOLD;
        }

        auto fontstyle = props.find("font-style");
        if (fontstyle != props.end()) {
            style.hasstylebits = true;
            auto value = fontstyle->second.Lower();
            if (value.Find("italic") != wxNOT_FOUND || value.Find("oblique") != wxNOT_FOUND)
                style.stylebits |= STYLE_ITALIC;
            else
                style.stylebits &= ~STYLE_ITALIC;
        }

        auto decoration = props.find("text-decoration");
        if (decoration != props.end()) {
            style.hasstylebits = true;
            auto value = decoration->second.Lower();
            if (value.Find("underline") != wxNOT_FOUND)
                style.stylebits |= STYLE_UNDERLINE;
            else
                style.stylebits &= ~STYLE_UNDERLINE;
            if (value.Find("line-through") != wxNOT_FOUND)
                style.stylebits |= STYLE_STRIKETHRU;
            else
                style.stylebits &= ~STYLE_STRIKETHRU;
        }

        auto family = props.find("font-family");
        if (family != props.end()) {
            auto value = family->second.Lower();
            if (value.Find("monospace") != wxNOT_FOUND || value.Find("courier") != wxNOT_FOUND ||
                value.Find("consolas") != wxNOT_FOUND) {
                style.hasstylebits = true;
                style.stylebits |= STYLE_FIXED;
            }
        }

        auto fontsize = props.find("font-size");
        if (fontsize != props.end()) {
            double points = 0;
            if (ParseCSSLengthPoints(fontsize->second, points)) {
                auto minrel = g_deftextsize - g_maxtextsize();
                auto maxrel = g_deftextsize - g_mintextsize();
                style.hasrelsize = true;
                style.relsize =
                    max(minrel, min(maxrel, g_deftextsize - static_cast<int>(points + 0.5)));
            }
        }
    }

    static void ApplyHTMLStartTagStyle(const wxString &tag, const map<wxString, wxString> &classstyles,
                                       HTMLCellStyle &style) {
        map<wxString, wxString> props;
        wxString classes;
        if (HTMLAttributeValue(tag, "class", classes)) {
            auto classtokens = wxStringTokenize(classes, " \t\r\n");
            loop(i, classtokens.size()) {
                auto it = classstyles.find(classtokens[i].Lower());
                if (it != classstyles.end()) AddCSSDeclarations(it->second, props);
            }
        }
        wxString inline_style;
        if (HTMLAttributeValue(tag, "style", inline_style)) AddCSSDeclarations(inline_style, props);
        ApplyCSSPropsToStyle(props, style);

        wxString color;
        if (HTMLAttributeValue(tag, "bgcolor", color) && ParseCSSColor(color, style.cellcolor))
            style.hascellcolor = true;
        if (HTMLAttributeValue(tag, "color", color) && ParseCSSColor(color, style.textcolor))
            style.hastextcolor = true;

        auto name = HTMLTagName(tag);
        if (name == "b" || name == "strong") {
            style.hasstylebits = true;
            style.stylebits |= STYLE_BOLD;
        } else if (name == "i" || name == "em") {
            style.hasstylebits = true;
            style.stylebits |= STYLE_ITALIC;
        } else if (name == "u") {
            style.hasstylebits = true;
            style.stylebits |= STYLE_UNDERLINE;
        } else if (name == "s" || name == "strike" || name == "del") {
            style.hasstylebits = true;
            style.stylebits |= STYLE_STRIKETHRU;
        } else if (name == "font") {
            wxString face;
            if (HTMLAttributeValue(tag, "face", face)) {
                auto lower = face.Lower();
                if (lower.Find("courier") != wxNOT_FOUND || lower.Find("consolas") != wxNOT_FOUND ||
                    lower.Find("monospace") != wxNOT_FOUND) {
                    style.hasstylebits = true;
                    style.stylebits |= STYLE_FIXED;
                }
            }
        }
    }

    static HTMLCellStyle HTMLCellStyleFrom(const wxString &starttag, const wxString &content,
                                           const map<wxString, wxString> &classstyles) {
        HTMLCellStyle style;
        ApplyHTMLStartTagStyle(starttag, classstyles, style);
        for (auto pos = 0, len = static_cast<int>(content.Len()); pos < len;) {
            auto tagstart = FindFrom(content, "<", pos);
            if (tagstart == wxNOT_FOUND) break;
            auto tagend = FindHTMLTagEnd(content, tagstart);
            if (tagend == wxNOT_FOUND) break;
            auto tag = content.Mid(tagstart, tagend - tagstart + 1);
            if (!IsHTMLClosingTag(tag)) ApplyHTMLStartTagStyle(tag, classstyles, style);
            pos = tagend + 1;
        }
        return style;
    }

    static wxString CodePointToString(unsigned long code) {
        wxString decoded;
        if (!code || code > 0x10FFFF) return decoded;
        decoded += wxUniChar(code);
        return decoded;
    }

    static bool DecodeHTMLEntity(const wxString &entity, wxString &decoded) {
        auto key = entity.Lower();
        if (key == "amp") decoded = "&";
        else if (key == "lt") decoded = "<";
        else if (key == "gt") decoded = ">";
        else if (key == "quot") decoded = "\"";
        else if (key == "apos") decoded = "'";
        else if (key == "nbsp") decoded = " ";
        else if (key.StartsWith("#x")) {
            unsigned long code = 0;
            if (!key.Mid(2).ToULong(&code, 16)) return false;
            decoded = CodePointToString(code);
        } else if (key.StartsWith("#")) {
            unsigned long code = 0;
            if (!key.Mid(1).ToULong(&code, 10)) return false;
            decoded = CodePointToString(code);
        } else {
            return false;
        }
        return !decoded.IsEmpty();
    }

    static void AppendHTMLTextChar(wxString &out, wxChar c, bool &spacepending) {
        if (IsHTMLSpace(c)) {
            if (!out.IsEmpty()) spacepending = true;
            return;
        }
        if (spacepending && !out.IsEmpty() && out[out.Len() - 1] != '\n') out += ' ';
        spacepending = false;
        out += c;
    }

    static void AppendHTMLText(wxString &out, const wxString &text, bool &spacepending) {
        loop(i, text.Len()) {
            if (text[i] == '\r' || text[i] == '\n') {
                if (text[i] == '\r' && i + 1 < static_cast<int>(text.Len()) && text[i + 1] == '\n')
                    i++;
                if (!out.IsEmpty() && out[out.Len() - 1] != '\n') out += LINE_SEPARATOR;
                spacepending = false;
            } else {
                AppendHTMLTextChar(out, text[i], spacepending);
            }
        }
    }

    static void AppendHTMLNewline(wxString &out, bool &spacepending) {
        if (!out.IsEmpty() && out[out.Len() - 1] != '\n') out += LINE_SEPARATOR;
        spacepending = false;
    }

    static wxString HTMLToText(const wxString &html) {
        wxString out;
        bool spacepending = false;
        for (auto i = 0, len = static_cast<int>(html.Len()); i < len;) {
            if (html[i] == '<') {
                auto end = FindHTMLTagEnd(html, i);
                if (end == wxNOT_FOUND) {
                    AppendHTMLTextChar(out, html[i++], spacepending);
                    continue;
                }
                auto tag = html.Mid(i, end - i + 1);
                auto name = HTMLTagName(tag);
                auto closing = IsHTMLClosingTag(tag);
                if (!closing && name == "br")
                    AppendHTMLNewline(out, spacepending);
                else if (!closing && name == "img") {
                    wxString replacement;
                    if (!HTMLAttributeValue(tag, "alt", replacement) &&
                        !HTMLAttributeValue(tag, "title", replacement))
                        HTMLAttributeValue(tag, "src", replacement);
                    if (!replacement.IsEmpty()) AppendHTMLText(out, replacement, spacepending);
                }
                else if (closing && (name == "p" || name == "div" || name == "li"))
                    AppendHTMLNewline(out, spacepending);
                i = end + 1;
            } else if (html[i] == '&') {
                auto end = FindFrom(html, ";", i + 1);
                wxString decoded;
                if (end != wxNOT_FOUND && end - i <= 32 &&
                    DecodeHTMLEntity(html.Mid(i + 1, end - i - 1), decoded)) {
                    AppendHTMLText(out, decoded, spacepending);
                    i = end + 1;
                } else {
                    AppendHTMLTextChar(out, html[i++], spacepending);
                }
            } else {
                AppendHTMLTextChar(out, html[i++], spacepending);
            }
        }
        return TrimHTMLText(out);
    }

    static int FindHTMLCellStart(const wxString &lower, int start, int limit, wxString &tag) {
        auto td = FindHTMLTag(lower, "td", start, limit);
        auto th = FindHTMLTag(lower, "th", start, limit);
        if (td == wxNOT_FOUND && th == wxNOT_FOUND) {
            tag = wxEmptyString;
            return wxNOT_FOUND;
        }
        if (td == wxNOT_FOUND || (th != wxNOT_FOUND && th < td)) {
            tag = "th";
            return th;
        }
        tag = "td";
        return td;
    }

    static bool ParseHTMLTable(const wxString &html, vector<vector<HTMLTableCell>> &rows) {
        rows.clear();
        auto lower = html.Lower();
        auto classstyles = ExtractHTMLClassStyles(html);
        auto table = FindHTMLTag(lower, "table", 0);
        if (table == wxNOT_FOUND) return false;
        auto tableopenend = FindHTMLTagEnd(html, table);
        if (tableopenend == wxNOT_FOUND) return false;
        auto tableend = FindMatchingHTMLTagClose(html, lower, "table", tableopenend);
        if (tableend == wxNOT_FOUND) tableend = static_cast<int>(html.Len());

        for (auto pos = tableopenend + 1;;) {
            auto rowstart = FindHTMLTag(lower, "tr", pos, tableend);
            if (rowstart == wxNOT_FOUND) break;
            auto rowopenend = FindHTMLTagEnd(html, rowstart);
            if (rowopenend == wxNOT_FOUND) break;
            auto rowclose = FindMatchingHTMLTagClose(html, lower, "tr", rowopenend, tableend);
            int rowlimit = 0;
            int nextpos = 0;
            if (rowclose == wxNOT_FOUND) {
                auto nextrow = FindHTMLTag(lower, "tr", rowopenend + 1, tableend);
                rowlimit = nextrow == wxNOT_FOUND ? tableend : nextrow;
                nextpos = rowlimit;
            } else {
                rowlimit = rowclose;
                auto rowcloseend = FindHTMLTagEnd(html, rowclose);
                nextpos = rowcloseend == wxNOT_FOUND ? rowclose + 5 : rowcloseend + 1;
            }

            vector<HTMLTableCell> row;
            for (auto cellpos = rowopenend + 1; cellpos < rowlimit;) {
                wxString celltag;
                auto cellstart = FindHTMLCellStart(lower, cellpos, rowlimit, celltag);
                if (cellstart == wxNOT_FOUND) break;
                auto cellopenend = FindHTMLTagEnd(html, cellstart);
                if (cellopenend == wxNOT_FOUND || cellopenend >= rowlimit) break;
                auto starttag = html.Mid(cellstart, cellopenend - cellstart + 1);
                auto cellclose =
                    FindMatchingHTMLTagClose(html, lower, celltag, cellopenend, rowlimit);
                int contentend = 0;
                if (cellclose == wxNOT_FOUND) {
                    wxString nextcelltag;
                    auto nextcell = FindHTMLCellStart(lower, cellopenend + 1, rowlimit, nextcelltag);
                    contentend = nextcell == wxNOT_FOUND ? rowlimit : nextcell;
                    cellpos = contentend;
                } else {
                    contentend = cellclose;
                    auto cellcloseend = FindHTMLTagEnd(html, cellclose);
                    cellpos =
                        cellcloseend == wxNOT_FOUND ? cellclose + static_cast<int>(celltag.Len()) + 3
                                                     : cellcloseend + 1;
                }

                HTMLTableCell cell;
                auto content = html.Mid(cellopenend + 1, max(0, contentend - cellopenend - 1));
                cell.text = HTMLToText(content);
                cell.style = HTMLCellStyleFrom(starttag, content, classstyles);
                cell.colspan = HTMLAttributeInt(starttag, "colspan");
                cell.rowspan = HTMLAttributeInt(starttag, "rowspan");
                row.push_back(cell);
            }
            if (!row.empty()) rows.push_back(row);
            pos = max(nextpos, rowstart + 1);
        }
        return !rows.empty();
    }

    bool PasteHTMLTable(const wxString &html) {
        vector<vector<HTMLTableCell>> rows;
        if (!ParseHTMLTable(html, rows)) return false;

        vector<HTMLTablePlacement> placements;
        vector<vector<bool>> occupied;
        auto width = 0;
        auto height = 0;
        auto ensure = [&](int y, int xcount) {
            while (static_cast<int>(occupied.size()) <= y) occupied.push_back(vector<bool>());
            if (static_cast<int>(occupied[y].size()) < xcount) occupied[y].resize(xcount, false);
        };

        loop(y, rows.size()) {
            auto x = 0;
            ensure(y, 1);
            for (auto &cell : rows[y]) {
                while (true) {
                    ensure(y, x + 1);
                    if (!occupied[y][x]) break;
                    x++;
                }
                auto xs = max(1, cell.colspan);
                auto ys = max(1, cell.rowspan);
                loop(yy, ys) {
                    ensure(y + yy, x + xs);
                    loop(xx, xs) occupied[y + yy][x + xx] = true;
                }
                placements.push_back({cell.text, cell.style, x, y, xs, ys});
                width = max(width, x + xs);
                height = max(height, y + ys);
                x += xs;
            }
            height = max(height, y + 1);
        }
        if (!width || !height) return false;

        auto cell = selected.ThinExpand(this);
        if (!cell) return false;
        if (cell->parent)
            cell->parent->AddUndo(this);
        else
            cell->AddUndo(this);
        cell->ResetLayout();
        cell->grid = nullptr;
        auto grid = cell->AddGrid(width, height);
        for (auto &placement : placements) {
            auto c = grid->C(placement.x, placement.y).get();
            c->text.t = placement.text;
            if (placement.style.hascellcolor) c->cellcolor = placement.style.cellcolor;
            if (placement.style.hastextcolor) c->textcolor = placement.style.textcolor;
            if (placement.style.hasstylebits) c->text.stylebits = placement.style.stylebits;
            if (placement.style.hasrelsize) c->text.relsize = placement.style.relsize;
            c->mergexs = placement.xs;
            c->mergeys = placement.ys;
        }
        grid->RepairMergedCells();
        if (!cell->HasText() && cell->parent)
            cell->grid->MergeWithParent(cell->parent->grid, selected, this);
        return true;
    }

    static bool HasTabularColumns(const wxArrayString &lines) {
        loop(i, lines.size()) if (lines[i].Find('\t') != wxNOT_FOUND) return true;
        return false;
    }

    void PasteDelimitedText(Cell *cell, const wxArrayString &lines, const wxString &sep) {
        cell->parent->AddUndo(this);
        cell->ResetLayout();
        cell->grid = nullptr;
        auto grid = cell->AddGrid(1, static_cast<int>(max<size_t>(1, lines.size())));
        grid->CSVImport(lines, sep);
        if (!cell->HasText()) cell->grid->MergeWithParent(cell->parent->grid, selected, this);
    }

    void PasteSingleText(Cell *c, const wxString &s) { c->text.Insert(this, s, selected, false); }

    // Polymorphism with wxDataObjectSimple does not work on Windows; bitmap format seems to not be
    // recognized.

    void PasteOrDrop(const wxFileDataObject &filedataobject) {
        const wxArrayString &filenames = filedataobject.GetFilenames();
        if (filenames.size() != 1) {
            sys->frame->SetStatus(_("Can paste or drop only exactly one file."));
            return;
        }
        Cell *cell = selected.ThinExpand(this);
        wxString filename = filenames[0];
        wxFFileInputStream fileinputstream(filename);
        if (fileinputstream.IsOk()) {
            char buffer[4];
            fileinputstream.Read(buffer, 4);
            if (!strncmp(buffer, "TSFF", 4)) {
                ThreeChoiceDialog askuser(
                    sys->frame, filename,
                    _("It seems that you are about to paste or drop a TreeSheets file. "
                      "What would you like to do?"),
                    _("Open TreeSheets file"), _("Paste file path"), _("Cancel"));
                switch (askuser.Run()) {
                    case 0: sys->frame->SetStatus(sys->LoadDB(filename));
                    case 2: return;
                    default:
                    case 1:;
                }
            }
        }
        if (!cell) return;
        cell->AddUndo(this);
        if (!LoadImageIntoCell(filename, cell, sys->frame->FromDIP(1.0)))
            PasteSingleText(cell, filename);
    }

    void PasteOrDrop(const wxTextDataObject &textdataobject) {
        if (textdataobject.GetText() != wxEmptyString) {
            Cell *cell = selected.ThinExpand(this);
            auto text = textdataobject.GetText();
            if (!cell) return;
            if (selected.TextEdit()) {
                text.Replace("\r\n", "\n");
                text.Replace("\r", "\n");
                cell->AddUndo(this);
                PasteSingleText(cell, text);
            } else if ((sys->clipboardcopy == text) && sys->cellclipboard) {
                cell->Paste(this, sys->cellclipboard.get(), selected);
            } else {
                const wxArrayString &lines = wxStringTokenize(text, LINE_SEPARATOR);
                if (HasTabularColumns(lines)) {
                    PasteDelimitedText(cell, lines, L'\t');
                } else if (lines.size() == 1) {
                    cell->AddUndo(this);
                    cell->ResetLayout();
                    PasteSingleText(cell, lines[0]);
                } else if (lines.size() > 1) {
                    cell->parent->AddUndo(this);
                    cell->ResetLayout();
                    cell->grid = nullptr;
                    sys->FillRows(cell->AddGrid(), lines, sys->CountCol(lines[0]), 0, 0);
                    if (!cell->HasText())
                        cell->grid->MergeWithParent(cell->parent->grid, selected, this);
                }
            }
        }
    }

    bool PasteOrDrop(const wxHTMLDataObject &htmldataobject) {
        auto html = htmldataobject.GetHTML();
        return !html.IsEmpty() && PasteHTMLTable(html);
    }

    void PasteOrDrop(const wxBitmapDataObject &bitmapdataobject) {
        if (bitmapdataobject.GetBitmap().GetRefData() != wxNullBitmap.GetRefData()) {
            Cell *cell = selected.ThinExpand(this);
            cell->AddUndo(this);
            auto image = bitmapdataobject.GetBitmap().ConvertToImage();
            vector<uint8_t> buffer = ConvertWxImageToBuffer(image, wxBITMAP_TYPE_PNG);
            SetImageBM(cell, std::move(buffer), 'I', sys->frame->FromDIP(1.0));
            cell->Reset();
        }
    }

    wxString Sort(bool descending) {
        if (selected.xs != 1 && selected.ys <= 1)
            return _(
                "Can't sort: make a 1xN selection to indicate what column to sort on, and what rows to affect");
        selected.grid->cell->AddUndo(this);
        selected.grid->Sort(selected, descending);
        canvas->Refresh();
        return wxEmptyString;
    }

    void DelRowCol(int &v, int e, int gvs, int dec, int dx, int dy, int nxs, int nys) {
        if (v != e) {
            selected.grid->cell->AddUndo(this);
            if (gvs == 1) {
                selected.grid->DelSelf(this, selected);
            } else {
                selected.grid->DeleteCells(dx, dy, nxs, nys);
                v -= dec;
            }
            if (canvas) canvas->Refresh();
        }
    }

    void CreatePath(Cell *c, auto &path) {
        path.clear();
        while (c->parent) {
            const Selection &s = c->parent->grid->FindCell(c);
            ASSERT(s.grid);
            path.push_back(s);
            c = c->parent;
        }
    }

    Cell *WalkPath(auto &path) {
        Cell *c = root.get();
        loopvrev(i, path) {
            Selection &s = path[i];
            Grid *g = c->grid.get();
            if (!g) return c;
            ASSERT(g && s.x < g->xs && s.y < g->ys);
            c = g->C(s.x, s.y).get();
        }
        return c;
    }

    bool LastUndoSameCellAny(Cell *c) {
        return undolist.size() && undolist.size() != undolistsizeatfullsave &&
               undolist.back()->cloned_from == (uintptr_t)c;
    }

    bool LastUndoSameCellTextEdit(Cell *c) {
        // hacky way to detect word boundaries to stop coalescing, but works, and
        // not a big deal if selected is not actually related to this cell
        return undolist.size() && !c->grid && undolist.size() != undolistsizeatfullsave &&
               undolist.back()->sel.EqLoc(c->parent->grid->FindCell(c)) &&
               (!c->text.t.EndsWith(" ") || c->text.t.Len() != selected.cursor);
    }

    void AddUndo(Cell *c, bool newgeneration = true) {
        redolist.clear();
        lastmodsinceautosave = wxGetLocalTime();
        if (!modified) {
            modified = true;
            UpdateFileName();
        }
        if (LastUndoSameCellTextEdit(c)) return;
        auto ui = make_unique<UndoItem>();
        ui->clone = c->Clone(nullptr);
        ui->estimated_size = c->EstimatedMemoryUse();
        ui->sel = selected;
        ui->cloned_from = (uintptr_t)c;
        if (undolist.size()) ui->generation = undolist.back()->generation + (newgeneration ? 1 : 0);
        CreatePath(c, ui->path);
        if (selected.grid) CreatePath(selected.grid->cell, ui->selpath);
        undolist.push_back(std::move(ui));
        size_t total_usage = 0;
        size_t old_list_size = undolist.size();
        // Cull undolist. Always at least keeps last item.
        for (auto i = static_cast<int>(undolist.size()) - 1; i >= 0; i--) {
            // Cull old items if using more than 100MB or 1000 items, whichever comes first.
            // TODO: make configurable?
            if (total_usage < 100 * 1024 * 1024 && undolist.size() - i < 1000) {
                total_usage += undolist[i]->estimated_size;
            } else {
                undolist.erase(undolist.begin(), undolist.begin() + i + 1);
                break;
            }
        }
        size_t items_culled = old_list_size - undolist.size();
        undolistsizeatfullsave -= items_culled;  // Allowed to go < 0
    }

    void Undo(vector<unique_ptr<UndoItem>> &fromlist, vector<unique_ptr<UndoItem>> &tolist,
              bool redo = false) {
        for (bool next = true; next; ) {
            UndoEach(fromlist, tolist, redo);
            next = (fromlist.size() && tolist.size() &&
                    fromlist.back()->generation == tolist.back()->generation)
                       ? true
                       : false;
        }
        if (canvas) {
            if (selected.grid)
                ScrollOrZoom();
            else
                canvas->Refresh();
        }
        UpdateFileName();
    }

    void UndoEach(vector<unique_ptr<UndoItem>> &fromlist, vector<unique_ptr<UndoItem>> &tolist,
                  bool redo = false) {
        auto beforesel = selected;
        vector<Selection> beforepath;
        if (beforesel.grid) CreatePath(beforesel.grid->cell, beforepath);

        auto ui = std::move(fromlist.back());
        fromlist.pop_back();

        Cell *c = WalkPath(ui->path);

        if (c->parent && c->parent->grid) {
            Grid *g = c->parent->grid.get();
            Selection s = g->FindCell(c);
            std::swap(ui->clone, g->C(s.x, s.y));
            c = g->C(s.x, s.y).get();
            c->parent = ui->clone->parent;
        } else {
            std::swap(ui->clone, root);
            c = root.get();
            c->parent = nullptr;
        }
        c->ResetLayout();

        SetSelect(ui->sel);
        if (selected.grid) { selected.grid = WalkPath(ui->selpath)->grid; }

        begindrag = selected;
        ui->sel = beforesel;
        ui->selpath = std::move(beforepath);
        tolist.push_back(std::move(ui));

        if (undolistsizeatfullsave > (int)undolist.size()) undolistsizeatfullsave = -1;
        modified = undolistsizeatfullsave != (int)undolist.size();
    }

    void ColorChange(int which, int idx) {
        if (!selected.grid) return;
        auto col = idx == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[idx];
        switch (which) {
            case A_CELLCOLOR: sys->lastcellcolor = col; break;
            case A_TEXTCOLOR: sys->lasttextcolor = col; break;
            case A_BORDCOLOR: sys->lastbordcolor = col; break;
        }
        selected.grid->ColorChange(this, which, col, selected);
    }

    void SetImageBM(Cell *c, auto &&data, char type, double scale) {
        c->text.image = sys->lastimage =
            sys->imagelist[sys->AddImageToList(scale, std::move(data), type)].get();
        c->text.ResetImageScale();
        ClearImageSelection();
    }

    bool LoadImageIntoCell(const wxString &filename, Cell *c, double scale) {
        if (filename.empty()) return false;
        auto pnghandler = wxImage::FindHandler(wxBITMAP_TYPE_PNG);
        auto jpghandler = wxImage::FindHandler(wxBITMAP_TYPE_JPEG);
        wxImageHandler *activeHandler = nullptr;
        if (pnghandler && pnghandler->CanRead(filename)) {
            activeHandler = pnghandler;
        } else if (jpghandler && jpghandler->CanRead(filename)) {
            activeHandler = jpghandler;
        }
        if (activeHandler) {
            wxFile fn(filename);
            if (!fn.IsOpened()) return false;
            vector<uint8_t> buffer(fn.Length());
            fn.Read(buffer.data(), buffer.size());
            SetImageBM(c, std::move(buffer), (activeHandler == pnghandler) ? 'I' : 'J', scale);
        } else {
            wxImage image;
            if (!image.LoadFile(filename)) return false;
            auto buffer = ConvertWxImageToBuffer(image, wxBITMAP_TYPE_PNG);
            SetImageBM(c, std::move(buffer), 'I', scale);
        }
        c->Reset();
        return true;
    }

    void ImageChange(const wxString &filename, double scale) {
        if (!selected.grid) return;
        selected.grid->cell->AddUndo(this);
        loopallcellssel(c, false) LoadImageIntoCell(filename, c, scale);
        canvas->Refresh();
    }

    void RecreateTagMenu(wxMenu &menu) {
        int i = A_TAGSET;
        for (auto &[tag, color] : tags) { menu.Append(i++, tag); }
        if (tags.size()) menu.AppendSeparator();
        menu.Append(A_TAGADD, _("&Add Cell Text as Tag"));
        menu.Append(A_TAGREMOVE, _("&Remove Cell Text from Tags"));
    }

    wxString TagSet(int tagno) {
        int i = 0;
        for (auto &[tag, color] : tags)
            if (i++ == tagno) {
                selected.grid->cell->AddUndo(this);
                loopallcellssel(c, false) {
                    c->text.Clear(this, selected);
                    c->text.Insert(this, tag, selected, true);
                }
                selected.grid->cell->ResetChildren();
                selected.grid->cell->ResetLayout();
                canvas->Refresh();
                return wxEmptyString;
            }
        ASSERT(0);
        return wxEmptyString;
    }

    void CollectCells(Cell *c) {
        itercells.clear();
        c->CollectCells(itercells);
    }

    void CollectCellsSel(bool recurse) {
        itercells.clear();
        if (selected.grid) selected.grid->CollectCellsSel(itercells, selected, recurse);
    }

    void ApplyEditFilter() {
        searchfilter = false;
        paintscrolltoselection = true;
        editfilter = min(max(editfilter, 1), 99);
        CollectCells(root.get());
        ranges::sort(itercells, [](auto a, auto b) {
            // sort in descending order
            return a->text.lastedit > b->text.lastedit;
        });
        loopv(i, itercells) itercells[i]->text.filtered = i > itercells.size() * editfilter / 100;
        root->ResetChildren();
        canvas->Refresh();
    }

    void ApplyEditRangeFilter(wxDateTime &rangebegin, wxDateTime &rangeend) {
        searchfilter = false;
        paintscrolltoselection = true;
        CollectCells(root.get());
        for (auto c : itercells) {
            c->text.filtered = !c->text.lastedit.IsBetween(rangebegin, rangeend);
        }
        root->ResetChildren();
        canvas->Refresh();
    }

    wxDateTime ParseDateTimeString(const wxString &s) {
        wxDateTime dt;
        wxString::const_iterator end;
        if (!dt.ParseDateTime(s, &end)) dt = wxInvalidDateTime;
        return dt;
    }

    void SetSearchFilter(bool on) {
        searchfilter = on;
        paintscrolltoselection = true;
        loopallcells(c) c->text.filtered = on && !c->text.IsInSearch();
        root->ResetChildren();
        canvas->Refresh();
    }

    void ExportAllImages(const wxString &filename, Cell *exportroot) {
        std::set<Image *> exportimages;
        CollectCells(exportroot);
        for (auto c : itercells)
            if (c->text.image) exportimages.insert(c->text.image);
        wxFileName fn(filename);
        auto directory = fn.GetPathWithSep();
        for (auto image : exportimages)
            if (!image->ExportToDirectory(directory)) break;
    }
};
