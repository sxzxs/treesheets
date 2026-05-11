struct TSCanvas : public wxScrolledCanvas {
    TSFrame *frame;
    unique_ptr<Document> doc {nullptr};
    int mousewheelaccum {0};
    bool lastrmbwaswithctrl {false};
    bool altenterhandled {false};
    bool spacepanactive {false};
    bool spacepandragging {false};
    wxPoint lastmousepos;
    wxPoint spacepanlastpos;
    wxTimer spacepantimer;

    TSCanvas(TSFrame *fr, wxWindow *parent, const wxSize &size = wxDefaultSize)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, size,
                           wxScrolledWindowStyle | wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
          frame(fr),
          spacepantimer(this) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        DisableKeyboardScrolling();
        // Without this, canvas does its own scrolling upon mousewheel events, which
        // interferes with our own.
        EnableScrolling(false, false);
        Bind(wxEVT_TIMER, &TSCanvas::OnSpacePanTimer, this, spacepantimer.GetId());
    }

    ~TSCanvas() { frame = nullptr; }

    void OnPaint(wxPaintEvent &event) {
        #ifdef __WXMSW__
            auto sz = GetClientSize();
            if (sz.GetX() <= 0 || sz.GetY() <= 0) return;
            wxBitmap bmp;
            auto sf = GetDPIScaleFactor();
            bmp.CreateWithDIPSize(sz, sf, 24);
            wxBufferedPaintDC dc(this, bmp);
        #else
            wxPaintDC dc(this);
        #endif
        DoPrepareDC(dc);
        doc->Draw(dc);
    };

    void OnMotion(wxMouseEvent &me) {
        if (EndSpacePanIfReleased()) {
            lastmousepos = me.GetPosition();
            return;
        }

        if (doc->image_resize_dragging) {
            if (me.LeftIsDown()) {
                doc->UpdateImageResize(me.GetX(), me.GetY());
            } else {
                doc->FinishImageResize();
                if (HasCapture()) ReleaseMouse();
            }
            lastmousepos = me.GetPosition();
            return;
        }

        if (frame->borderpaintmode) {
            wxInfoDC dc(this);
            doc->UpdateHover(dc, me.GetX(), me.GetY());
            if (me.LeftIsDown()) {
                if (doc->PaintHoveredBorderLine()) {
                    if (!HasCapture()) CaptureMouse();
                    sys->frame->UpdateStatus(doc->hover, true);
                    Refresh();
                }
            } else {
                SetCursor(wxCursor(wxCURSOR_CROSS));
                if (doc->hover != doc->prev && !doc->hover.Thin())
                    sys->frame->UpdateStatus(doc->hover, false);
            }
            lastmousepos = me.GetPosition();
            return;
        }

        if (spacepanactive) {
            if (me.LeftIsDown()) {
                if (!spacepandragging) BeginSpacePanDrag(me.GetPosition());
                wxPoint p = me.GetPosition() - spacepanlastpos;
                CursorScroll(-p.x, -p.y);
                spacepanlastpos = me.GetPosition();
            } else {
                EndSpacePanDrag();
                SetSpacePanCursor();
            }
            lastmousepos = me.GetPosition();
            return;
        }

        wxInfoDC dc(this);
        doc->UpdateHover(dc, me.GetX(), me.GetY());
        if (me.LeftIsDown() || me.RightIsDown()) {
            if (me.AltDown() && me.ShiftDown()) {
                doc->Copy(A_DRAGANDDROP);
                Refresh();
            } else {
                if (doc->isctrlshiftdrag) {
                    doc->begindrag = doc->hover;
                } else if (!doc->hover.Thin()) {
                    if (doc->begindrag.Thin() || doc->selected.Thin()) {
                        doc->SetSelect(doc->hover);
                        doc->ResetCursor();
                        Refresh();
                    } else {
                        Selection old = doc->selected;
                        doc->selected.Merge(doc->begindrag, doc->hover);
                        if (!(old == doc->selected)) {
                            doc->ResetCursor();
                            Refresh();
                        }
                    }
                }
            }
            sys->frame->UpdateStatus(doc->selected, true);
        } else if (me.MiddleIsDown()) {
            wxPoint p = me.GetPosition() - lastmousepos;
            CursorScroll(-p.x, -p.y);
        } else {
            if (doc->hover != doc->prev && !doc->hover.Thin()) sys->frame->UpdateStatus(doc->hover, false);
            if (doc->ImageResizeHitTest(me.GetX(), me.GetY()))
                SetCursor(wxCursor(wxCURSOR_SIZENWSE));
            else
                doc->ResetCursor();
        }
        lastmousepos = me.GetPosition();
    }

    void SelectClick(int mx, int my, bool right, int isctrlshift) {
        wxInfoDC dc(this);
        if (mx < 0 || my < 0)
            return;  // for some reason, using just the "menu" key sends a right-click at (-1, -1)
        doc->isctrlshiftdrag = isctrlshift;
        doc->UpdateHover(dc, mx, my);
        auto image_hit = doc->ImageCellAtDevicePoint(mx, my) != nullptr;
        doc->SelectClick(right, image_hit);
        sys->frame->UpdateStatus(doc->selected, true);
        Refresh();
    }

    void OnLeftDown(wxMouseEvent &me) {
        #ifndef __WXMSW__
        // seems to not want to give the canvas focus otherwise (thinks its already in focus
        // when its not?)
        if (frame->filter) frame->filter->SetFocus();
        #endif
        SetFocus();
        if (spacepanactive) {
            if (!EndSpacePanIfReleased()) {
                BeginSpacePanDrag(me.GetPosition());
                return;
            }
        }
        if (frame->borderpaintmode) {
            wxInfoDC dc(this);
            doc->UpdateHover(dc, me.GetX(), me.GetY());
            doc->BeginBorderPaint();
            if (!HasCapture()) CaptureMouse();
            if (doc->PaintHoveredBorderLine()) {
                sys->frame->UpdateStatus(doc->hover, true);
                Refresh();
            }
            SetCursor(wxCursor(wxCURSOR_CROSS));
            return;
        }
        if (!me.ShiftDown() && doc->StartImageResize(me.GetX(), me.GetY())) {
            if (!HasCapture()) CaptureMouse();
            Refresh();
            return;
        }
        if (me.ShiftDown())
            OnMotion(me);
        else
            SelectClick(me.GetX(), me.GetY(), false, me.CmdDown() + me.AltDown() * 2);
    }

    void OnLeftUp(wxMouseEvent &me) {
        if (spacepanactive || spacepandragging) {
            if (!EndSpacePanIfReleased()) {
                EndSpacePanDrag();
                SetSpacePanCursor();
            }
            return;
        }
        if (doc->FinishImageResize()) {
            if (HasCapture()) ReleaseMouse();
            sys->frame->UpdateStatus(doc->selected, true);
            Refresh();
            return;
        }
        if (frame->borderpaintmode) {
            doc->FinishBorderPaint();
            if (HasCapture()) ReleaseMouse();
            Refresh();
            return;
        }
        if (me.CmdDown() || me.AltDown()) {
            wxInfoDC dc(this);
            doc->UpdateHover(dc, me.GetX(), me.GetY());
            doc->SelectUp();
            sys->frame->UpdateStatus(doc->selected, true);
            Refresh();
        }
    }

    void OnRightDown(wxMouseEvent &me) {
        SetFocus();
        SelectClick(me.GetX(), me.GetY(), true, 0);
        lastrmbwaswithctrl = me.CmdDown();
        #ifndef __WXMSW__
        me.Skip();  // otherwise EVT_CONTEXT_MENU won't be triggered?
        #endif
    }

    void OnLeftDoubleClick(wxMouseEvent &me) {
        if (frame->borderpaintmode) return;
        wxInfoDC dc(this);
        doc->UpdateHover(dc, me.GetX(), me.GetY());
        doc->DoubleClick();
        sys->frame->UpdateStatus(doc->selected, true);
        Refresh();
    }

    bool IsAltEnter(wxKeyEvent &ce) {
        auto key = ce.GetKeyCode();
        return ce.AltDown() && !ce.CmdDown() &&
               (key == WXK_RETURN || key == WXK_NUMPAD_ENTER);
    }

    bool HandleAltEnter(wxKeyEvent &ce) {
        if (!IsAltEnter(ce)) return false;
        if (altenterhandled) return true;
        altenterhandled = true;
        bool unprocessed = false;
        sys->frame->SetStatus(doc->Key(WXK_NONE, ce.GetKeyCode(), ce.AltDown(), ce.CmdDown(),
                                       ce.ShiftDown(), unprocessed));
        if (unprocessed) ce.Skip();
        return !unprocessed;
    }

    bool IsPlainSpace(wxKeyEvent &ce) const {
        return ce.GetKeyCode() == WXK_SPACE && !ce.AltDown() && !ce.CmdDown() && !ce.ShiftDown();
    }

    bool SpacePanAvailable() const {
        return doc && !doc->selected.TextEdit() && !doc->image_resize_dragging &&
               !frame->borderpaintmode;
    }

    bool SpaceKeyStillDown() const {
        #ifdef __WXMSW__
            return (::GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        #else
            return wxGetKeyState(WXK_SPACE);
        #endif
    }

    bool SpacePanShouldContinue() const { return SpacePanAvailable() && SpaceKeyStillDown(); }

    bool EndSpacePanIfReleased() {
        if (!spacepanactive && !spacepandragging) return false;
        if (spacepanactive && SpacePanShouldContinue()) return false;
        EndSpacePan();
        return true;
    }

    void SetCursorNow(wxStockCursor cursor) {
        wxCursor c(cursor);
        #ifdef __WXMSW__
            ::SetCursor((HCURSOR)c.GetHCURSOR());
        #endif
        SetCursor(c);
    }

    void SetSpacePanCursor() {
        SetCursorNow(spacepandragging ? wxCURSOR_CLOSED_HAND : wxCURSOR_OPEN_HAND);
    }

    void RestoreCursorAfterSpacePan() {
        if (doc) doc->ResetCursor();
        SetCursorNow(doc && doc->selected.TextEdit() ? wxCURSOR_IBEAM : wxCURSOR_ARROW);
    }

    void BeginSpacePan() {
        if (!SpacePanAvailable()) return;
        spacepanactive = true;
        if (!spacepantimer.IsRunning()) spacepantimer.Start(30);
        SetSpacePanCursor();
    }

    void BeginSpacePanDrag(const wxPoint &pos) {
        if (!spacepanactive) BeginSpacePan();
        if (!spacepanactive) return;
        spacepandragging = true;
        spacepanlastpos = pos;
        SetSpacePanCursor();
        if (!HasCapture()) CaptureMouse();
    }

    void EndSpacePanDrag() {
        if (!spacepandragging) return;
        spacepandragging = false;
        if (HasCapture()) ReleaseMouse();
    }

    void EndSpacePan() {
        if (!spacepanactive && !spacepandragging) return;
        auto wasdragging = spacepandragging;
        spacepandragging = false;
        spacepanactive = false;
        if (spacepantimer.IsRunning()) spacepantimer.Stop();
        if (wasdragging && HasCapture()) ReleaseMouse();
        RestoreCursorAfterSpacePan();
    }

    bool HandleSpacePanKeyDown(wxKeyEvent &ce) {
        if (!IsPlainSpace(ce) || !SpacePanAvailable()) return false;
        BeginSpacePan();
        return true;
    }

    void OnCharHook(wxKeyEvent &ce) {
        if (HandleAltEnter(ce)) return;
        if (HandleSpacePanKeyDown(ce)) return;
        ce.Skip();
    }

    void OnKeyDown(wxKeyEvent &ce) {
        if (HandleAltEnter(ce)) return;
        if (HandleSpacePanKeyDown(ce)) return;
        ce.Skip();
    }
    void OnKeyUp(wxKeyEvent &ce) {
        auto key = ce.GetKeyCode();
        if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) altenterhandled = false;
        if (key == WXK_SPACE) {
            EndSpacePan();
            return;
        }
        ce.Skip();
    }
    void OnChar(wxKeyEvent &ce) {
        if (HandleAltEnter(ce)) return;
        if (IsPlainSpace(ce) && (spacepanactive || SpacePanAvailable())) {
            BeginSpacePan();
            return;
        }
        /*
        if (sys->insidefiledialog)
        {
            ce.Skip();
            return;
        }
        */
        #ifndef __WXMAC__
            // Without this check, Alt+[Alphanumericals], Alt+Shift+[Alphanumericals] and
            // Alt+[Shift]+cursor (scrolling) don't work. The 128 makes sure unicode entry on e.g.
            // Polish keyboards still works. (on Linux in particular).
            if ((ce.GetModifiers() == wxMOD_ALT || ce.GetModifiers() == (wxMOD_ALT | wxMOD_SHIFT)) &&
                ce.GetKeyCode() != WXK_RETURN && ce.GetKeyCode() != WXK_NUMPAD_ENTER &&
                (ce.GetUnicodeKey() < 128)) {
                ce.Skip();
                return;
            }
        #endif

        bool unprocessed = false;
        sys->frame->SetStatus(doc->Key(ce.GetUnicodeKey(), ce.GetKeyCode(), ce.AltDown(),
                                       ce.CmdDown(), ce.ShiftDown(), unprocessed));
        if (unprocessed) ce.Skip();
    }

    void OnMouseWheel(wxMouseEvent &me) {
        bool ctrl = me.CmdDown();
        if (sys->zoomscroll) ctrl = !ctrl;
        if (me.AltDown() || ctrl || me.ShiftDown()) {
            mousewheelaccum += me.GetWheelRotation();
            int steps = mousewheelaccum / me.GetWheelDelta();
            if (!steps) return;
            mousewheelaccum -= steps * me.GetWheelDelta();
            sys->frame->SetStatus(doc->Wheel(steps, me.AltDown(), ctrl, me.ShiftDown()));
        } else if (me.GetWheelAxis()) {
            CursorScroll(me.GetWheelRotation() * g_scrollratewheel, 0);
        } else {
            CursorScroll(0, -me.GetWheelRotation() * g_scrollratewheel);
        }
    }

    void OnSize(wxSizeEvent &se) {}
    void OnContextMenuClick(wxContextMenuEvent &cme) {
        if (lastrmbwaswithctrl) {
            auto tagmenu = make_unique<wxMenu>();
            doc->RecreateTagMenu(*tagmenu);
            PopupMenu(tagmenu.get());
        } else if (doc->SelectedImageCell()) {
            wxMenu imagemenu;
            imagemenu.Append(wxID_COPY, _("Copy Image"));
            imagemenu.Append(wxID_CUT, _("Cut Image"));
            imagemenu.Append(A_IMAGER, _("Remove Image"));
            imagemenu.AppendSeparator();
            imagemenu.Append(A_IMAGESVA, _("Save Image As..."));
            imagemenu.Append(A_IMAGESCF, _("Scale Display Only..."));
            imagemenu.Append(A_IMAGESCN, _("Reset Display Scale"));
            imagemenu.AppendSeparator();
            imagemenu.Append(A_IMAGESCP, _("Resample Pixels By %..."));
            imagemenu.Append(A_IMAGESCW, _("Resample Pixels By Width..."));
            imagemenu.AppendSeparator();
            imagemenu.Append(A_SAVE_AS_JPEG, _("Embed as JPEG"));
            imagemenu.Append(A_SAVE_AS_PNG, _("Embed as PNG"));
            PopupMenu(&imagemenu);
        } else {
            PopupMenu(frame->editmenupopup);
        }
    }

    void OnKillFocus(wxFocusEvent &event) {
        EndSpacePan();
        event.Skip();
    }

    void OnMouseCaptureLost(wxMouseCaptureLostEvent &) {
        spacepandragging = false;
        if (!spacepanactive) return;
        if (SpacePanShouldContinue())
            SetSpacePanCursor();
        else
            EndSpacePan();
    }

    void OnSetCursor(wxSetCursorEvent &event) {
        if (!spacepanactive) {
            event.Skip();
            return;
        }
        event.SetCursor(wxCursor(spacepandragging ? wxCURSOR_CLOSED_HAND : wxCURSOR_OPEN_HAND));
    }

    void OnSpacePanTimer(wxTimerEvent &) {
        if (!spacepanactive) {
            if (spacepantimer.IsRunning()) spacepantimer.Stop();
            return;
        }
        EndSpacePanIfReleased();
    }

    void OnScrollWin(wxScrollWinEvent &swe) {
        // This only gets called when scrolling using the scroll bar, not with mousewheel.
        swe.Skip();  // Use default scrolling behavior.
    }

    void CursorScroll(int dx, int dy) {
        int x, y;
        GetViewStart(&x, &y);
        x += dx;
        y += dy;
        // EnableScrolling(true, true);
        Scroll(x, y);
        // EnableScrolling(false, false);
    }

    DECLARE_EVENT_TABLE()
};
