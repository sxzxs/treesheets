struct Text {
    Cell *cell {nullptr};
    Image *image {nullptr};
    wxString t {wxEmptyString};
    int relsize {0};
    int stylebits {0};
    int extent {0};
    double image_scale {1.0};
    wxBitmap bm_image_display;
    Image *bm_image_source {nullptr};
    uint64_t bm_image_hash {0};
    char bm_image_type {0};
    double bm_image_effective_scale {0.0};
    wxDateTime lastedit;
    bool filtered {false};

    void WasEdited() { lastedit = wxDateTime::Now(); }

    Text() { WasEdited(); }

    static double ClampImageScale(double scale) {
        if (!std::isfinite(scale)) return 1.0;
        return max(0.05, min(scale, 20.0));
    }

    double EffectiveImageDisplayScale() const {
        return image ? image->display_scale / ClampImageScale(image_scale) : 1.0;
    }

    void ResetImageCache() {
        bm_image_display = wxNullBitmap;
        bm_image_source = nullptr;
        bm_image_hash = 0;
        bm_image_type = 0;
        bm_image_effective_scale = 0.0;
    }

    void SetImageScale(double scale) {
        image_scale = ClampImageScale(scale);
        ResetImageCache();
    }

    void ScaleImageDisplay(double scale) { SetImageScale(image_scale * scale); }

    void ResetImageScale() { SetImageScale(1.0); }

    wxBitmap *DisplayImage() {
        if (cell->grid && cell->grid->folded) return &sys->frame->foldicon;
        if (!image) return nullptr;

        auto effective_scale = EffectiveImageDisplayScale();
        if (!bm_image_display.IsOk() || bm_image_source != image || bm_image_hash != image->hash ||
            bm_image_type != image->type ||
            std::abs(bm_image_effective_scale - effective_scale) > 0.000001) {
            auto &[it, mime] = imagetypes.at(image->type);
            auto bm = ConvertBufferToWxBitmap(image->data, it);
            image->pixel_width = bm.GetWidth();
            auto dpi_scale = sys->frame ? sys->frame->FromDIP(1.0) : 1.0;
            ScaleBitmap(bm, dpi_scale / effective_scale, bm_image_display);
            bm_image_source = image;
            bm_image_hash = image->hash;
            bm_image_type = image->type;
            bm_image_effective_scale = effective_scale;
        }
        return &bm_image_display;
    }

    size_t EstimatedMemoryUse() {
        ASSERT(wxUSE_UNICODE);
        return sizeof(Text) + t.Length() * sizeof(wchar_t);
    }

    double GetNum() {
        std::wstringstream ss(t.ToStdWstring());
        double r;
        ss >> r;
        return r;
    }

    void SetNum(double d) {
        std::wstringstream ss;
        ss << std::fixed;

        // We're going to use at most 19 digits after '.'. Add small value round remainder.
        size_t max_significant = 10;
        d += 0.00000000005;

        ss << d;

        auto s = ss.str();
        // First trim whatever lies beyond the precision to avoid garbage digits.
        max_significant += 2;  // "0."
        if (s[0] == '-') max_significant++;
        if (s.length() > max_significant) s.erase(max_significant);
        // Now strip unnecessary trailing zeroes.
        while (s.back() == '0') s.pop_back();
        // If there were only zeroes, remove '.'.
        if (s.back() == '.') s.pop_back();

        t = s;
    }

    wxString htmlify(wxString &str) {
        wxString r;
        for (auto cref : str) {
            switch (wxChar c = cref.GetValue()) {
                case '&': r += "&amp;"; break;
                case '<': r += "&lt;"; break;
                case '>': r += "&gt;"; break;
                default: r += c;
            }
        }
        return r;
    }

    wxString ToText(int indent, const Selection &s, int format) {
        wxString str = s.cursor != s.cursorend ? t.Mid(s.cursor, s.cursorend - s.cursor) : t;
        if (format == A_EXPXML || format == A_EXPHTMLT || format == A_EXPHTMLTI ||
            format == A_EXPHTMLTE || format == A_EXPHTMLO || format == A_EXPHTMLB)
            str = htmlify(str);
        if (format == A_EXPHTMLTI && image)
            str.Prepend("<img src=\"data:" + imagetypes.at(image->type).second + ";base64," +
                        wxBase64Encode(image->data.data(), image->data.size()) + "\" />");
        else if (format == A_EXPHTMLTE && image) {
            wxString relsize = wxString::Format(
                "%d%%",
                static_cast<int>(100.0 * sys->frame->FromDIP(1.0) /
                                 EffectiveImageDisplayScale()));
            str.Prepend("<img src=\"" + wxString::Format("%llu", image->hash) +
                        image->GetFileExtension() + "\" width=\"" + relsize + "\" height=\"" +
                        relsize + "\" />");
        }
        return str;
    };

    auto MinRelsize(int rs) { return min(relsize, rs); }
    auto RelSize(int dir, int zoomdepth) {
        relsize = max(min(relsize + dir, g_deftextsize - g_mintextsize() + zoomdepth),
                      g_deftextsize - g_maxtextsize() - zoomdepth);
    }

    auto IsWord(wxChar c) { return wxIsalnum(c) || wxStrchr(L"_\"\'()", c) || wxIspunct(c); }
    bool IsLineBreak(wxChar c) { return c == '\n' || c == '\r'; }
    int NextAfterLineBreak(int pos) {
        return t[pos] == '\r' && pos + 1 < static_cast<int>(t.Len()) && t[pos + 1] == '\n'
                   ? pos + 2
                   : pos + 1;
    }

    static constexpr auto display_tab_width = 4;

    static bool IsHighSurrogate(uint code) { return code >= 0xD800 && code <= 0xDBFF; }
    static bool IsLowSurrogate(uint code) { return code >= 0xDC00 && code <= 0xDFFF; }
    static uint DecodeSurrogatePair(uint high, uint low) {
        return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
    }

    static bool IsNonCharacter(uint code) {
        return (code >= 0xFDD0 && code <= 0xFDEF) ||
               (code <= 0x10FFFF && (code & 0xFFFE) == 0xFFFE);
    }

    static bool IsInvisibleFormatChar(uint code) {
        return code == 0x00AD || code == 0x034F || code == 0x061C || code == 0x180E ||
               code == 0x200B || code == 0x200E || code == 0x200F ||
               (code >= 0x202A && code <= 0x202E) || (code >= 0x2060 && code <= 0x206F) ||
               code == 0xFEFF || (code >= 0xFFF9 && code <= 0xFFFB) ||
               (code >= 0x1BCA0 && code <= 0x1BCA3) || (code >= 0x1D173 && code <= 0x1D17A);
    }

    static bool IsCombiningMark(uint code) {
        return (code >= 0x0300 && code <= 0x036F) || (code >= 0x0483 && code <= 0x0489) ||
               (code >= 0x0591 && code <= 0x05BD) || code == 0x05BF ||
               (code >= 0x05C1 && code <= 0x05C2) || (code >= 0x05C4 && code <= 0x05C5) ||
               code == 0x05C7 || (code >= 0x0610 && code <= 0x061A) ||
               (code >= 0x064B && code <= 0x065F) || code == 0x0670 ||
               (code >= 0x06D6 && code <= 0x06DC) || (code >= 0x06DF && code <= 0x06E4) ||
               (code >= 0x06E7 && code <= 0x06E8) || (code >= 0x06EA && code <= 0x06ED) ||
               code == 0x0711 || (code >= 0x0730 && code <= 0x074A) ||
               (code >= 0x07A6 && code <= 0x07B0) || (code >= 0x07EB && code <= 0x07F3) ||
               (code >= 0x0816 && code <= 0x0819) || (code >= 0x081B && code <= 0x0823) ||
               (code >= 0x0825 && code <= 0x0827) || (code >= 0x0829 && code <= 0x082D) ||
               (code >= 0x0859 && code <= 0x085B) || (code >= 0x08D3 && code <= 0x08FF) ||
               (code >= 0x1AB0 && code <= 0x1AFF) || (code >= 0x1DC0 && code <= 0x1DFF) ||
               (code >= 0x20D0 && code <= 0x20FF) || (code >= 0xFE20 && code <= 0xFE2F);
    }

    static bool IsVariationSelector(uint code) {
        return (code >= 0xFE00 && code <= 0xFE0F) || (code >= 0xE0100 && code <= 0xE01EF);
    }

    static bool IsEmojiModifier(uint code) { return code >= 0x1F3FB && code <= 0x1F3FF; }
    static bool IsEmojiTag(uint code) { return code >= 0xE0020 && code <= 0xE007F; }
    static bool IsRegionalIndicator(uint code) { return code >= 0x1F1E6 && code <= 0x1F1FF; }

    static bool IsClusterExtender(uint code) {
        return IsCombiningMark(code) || IsVariationSelector(code) || IsEmojiModifier(code) ||
               IsEmojiTag(code) || code == 0x20E3;
    }

    static bool IsPrintable(uint code) {
        if (code < 0x20 || (code >= 0x7F && code < 0xA0)) return false;
        if (IsHighSurrogate(code) || IsLowSurrogate(code)) return false;
        if (IsNonCharacter(code)) return false;
        return code <= 0x10FFFF;
    }

    static wxString HexEscape(uint code) {
        if (code <= 0xFF) return wxString::Format("\\x%02X", code);
        if (code <= 0xFFFF) return wxString::Format("\\u%04X", code);
        return wxString::Format("\\U%08X", code);
    }

    struct SourceCodePoint {
        uint code {0};
        int start {0};
        int end {0};
    };

    struct DisplayLine {
        wxString text;
        vector<int> sourceendtodisplayend;
        vector<int> clusterboundaries;
    };

    static vector<SourceCodePoint> SourceCodePoints(const wxString &source) {
        vector<SourceCodePoint> points;
        auto len = static_cast<int>(source.Len());
        points.reserve(len);
        for (auto i = 0; i < len;) {
            auto code = wxUniChar(source[i]).GetValue();
            auto next = i + 1;
            if (IsHighSurrogate(code) && next < len) {
                auto low = wxUniChar(source[next]).GetValue();
                if (IsLowSurrogate(low)) {
                    points.push_back({DecodeSurrogatePair(code, low), i, i + 2});
                    i += 2;
                    continue;
                }
            }
            points.push_back({code, i, i + 1});
            i++;
        }
        return points;
    }

    static vector<int> ClusterBoundaries(const wxString &source) {
        auto points = SourceCodePoints(source);
        vector<int> boundaries;
        auto len = static_cast<int>(points.size());
        boundaries.reserve(len + 1);
        boundaries.push_back(0);
        for (auto i = 1; i < len; i++) {
            auto prev = points[i - 1].code;
            auto cur = points[i].code;
            auto join = prev == '\r' && cur == '\n';
            join = join || IsClusterExtender(cur) || prev == 0x200D || cur == 0x200D;
            if (IsRegionalIndicator(prev) && IsRegionalIndicator(cur)) {
                auto ricount = 1;
                for (auto j = i - 2; j >= 0 && IsRegionalIndicator(points[j].code); j--) ricount++;
                join = join || ricount % 2 == 1;
            }
            if (!join) boundaries.push_back(points[i].start);
        }
        boundaries.push_back(static_cast<int>(source.Len()));
        return boundaries;
    }

    static bool IsClusterBoundary(const vector<int> &boundaries, int pos) {
        return std::binary_search(boundaries.begin(), boundaries.end(), pos);
    }

    static int PreviousClusterBoundary(const vector<int> &boundaries, int pos) {
        auto it = std::lower_bound(boundaries.begin(), boundaries.end(), pos);
        if (it == boundaries.begin()) return boundaries.front();
        return *--it;
    }

    static int NextClusterBoundary(const vector<int> &boundaries, int pos) {
        auto it = std::upper_bound(boundaries.begin(), boundaries.end(), pos);
        return it == boundaries.end() ? boundaries.back() : *it;
    }

    static void AppendDisplay(DisplayLine &display, const wxString &append, int &displaycols) {
        display.text += append;
        displaycols += static_cast<int>(append.Len());
    }

    static void AppendDisplayChar(DisplayLine &display, uint code, const wxString &source,
                                  int &displaycols) {
        switch (code) {
            case 0: AppendDisplay(display, "\\0", displaycols); return;
            case '\b': AppendDisplay(display, "\\b", displaycols); return;
            case '\t': {
                auto spaces = display_tab_width - displaycols % display_tab_width;
                display.text += wxString(' ', spaces);
                displaycols += spaces;
                return;
            }
            case '\n': AppendDisplay(display, "\\n", displaycols); return;
            case '\r': AppendDisplay(display, "\\r", displaycols); return;
            case '\f': AppendDisplay(display, "\\f", displaycols); return;
            case '\v': AppendDisplay(display, "\\v", displaycols); return;
        }
        if (!IsPrintable(code) || IsInvisibleFormatChar(code)) {
            AppendDisplay(display, HexEscape(code), displaycols);
        } else {
            display.text += source;
            displaycols++;
        }
    }

    static DisplayLine BuildDisplayLine(const wxString &source) {
        DisplayLine display;
        auto displaycols = 0;
        auto sourcelen = static_cast<int>(source.Len());
        display.sourceendtodisplayend.resize(sourcelen + 1);
        display.clusterboundaries = ClusterBoundaries(source);
        for (auto point : SourceCodePoints(source)) {
            AppendDisplayChar(display, point.code, source.Mid(point.start, point.end - point.start),
                              displaycols);
            auto displayend = static_cast<int>(display.text.Len());
            for (auto i = point.start + 1; i <= point.end; i++)
                display.sourceendtodisplayend[i] = displayend;
        }
        return display;
    }

    static int DisplayIndexWidth(auto &dc, const DisplayLine &display, int displaychars) {
        displaychars = max(0, min(displaychars, static_cast<int>(display.text.Len())));
        if (!displaychars) return 0;
        wxArrayInt widths;
        if (dc.GetPartialTextExtents(display.text, widths) &&
            static_cast<int>(widths.size()) >= displaychars)
            return widths[displaychars - 1];
        auto x = 0;
        dc.GetTextExtent(display.text.Left(displaychars), &x, nullptr);
        return x;
    }

    static int DisplayPrefixWidth(auto &dc, const DisplayLine &display, int sourcechars) {
        sourcechars =
            max(0, min(sourcechars, static_cast<int>(display.sourceendtodisplayend.size()) - 1));
        if (!IsClusterBoundary(display.clusterboundaries, sourcechars))
            sourcechars = PreviousClusterBoundary(display.clusterboundaries, sourcechars);
        return DisplayIndexWidth(dc, display, display.sourceendtodisplayend[sourcechars]);
    }

    static int SourceCursorFromDisplayX(auto &dc, const DisplayLine &display, int xlimit) {
        if (xlimit <= 0) return 0;
        auto beforex = 0;
        for (auto i = 1; i < static_cast<int>(display.clusterboundaries.size()); i++) {
            auto afterpos = display.clusterboundaries[i];
            auto beforepos = display.clusterboundaries[i - 1];
            auto afterx = DisplayPrefixWidth(dc, display, afterpos);
            if (afterx >= xlimit) {
                return xlimit - beforex < afterx - xlimit ? beforepos : afterpos;
            }
            beforex = afterx;
        }
        return display.clusterboundaries.back();
    }

    static int PreviousCursorPos(const wxString &source, int pos) {
        auto boundaries = ClusterBoundaries(source);
        pos = max(0, min(pos, static_cast<int>(source.Len())));
        return PreviousClusterBoundary(boundaries, pos);
    }

    static int NextCursorPos(const wxString &source, int pos) {
        auto boundaries = ClusterBoundaries(source);
        pos = max(0, min(pos, static_cast<int>(source.Len())));
        return NextClusterBoundary(boundaries, pos);
    }

    static int AdjacentCursorPos(const wxString &source, int pos, int dir) {
        return dir < 0 ? PreviousCursorPos(source, pos) : NextCursorPos(source, pos);
    }

    static int NearestCursorPos(const wxString &source, int pos) {
        auto boundaries = ClusterBoundaries(source);
        pos = max(0, min(pos, static_cast<int>(source.Len())));
        if (IsClusterBoundary(boundaries, pos)) return pos;
        auto prev = PreviousClusterBoundary(boundaries, pos);
        auto next = NextClusterBoundary(boundaries, pos);
        return pos - prev < next - pos ? prev : next;
    }

    int PreviousCursorPos(int pos) const { return PreviousCursorPos(t, pos); }
    int NextCursorPos(int pos) const { return NextCursorPos(t, pos); }
    int AdjacentCursorPos(int pos, int dir) const { return AdjacentCursorPos(t, pos, dir); }
    int NearestCursorPos(int pos) const { return NearestCursorPos(t, pos); }

    struct Line {
        wxString text;
        int start {0};
        int end {0};
    };

    auto GetLinePart(int &currentpos, int breakpos, int limitpos, Line &line) {
        auto startpos = currentpos;
        currentpos = breakpos;

        for (auto j = t.begin() + startpos; currentpos < limitpos && !wxIsspace(*j) && !IsWord(*j);
             j++) {
            currentpos++;
            breakpos++;
        }
        // gobble up any trailing punctuation
        if (currentpos != startpos && currentpos < limitpos &&
            (t[currentpos] == '\"' || t[currentpos] == '\'')) {
            currentpos++;
            breakpos++;
        }  // special case: if punctuation followed by quote, quote is meant to be part of word

        for (auto k = t.begin() + currentpos;
             currentpos < limitpos && wxIsspace(*k) && !IsLineBreak(*k); k++) {
            // gobble spaces, but do not copy them
            currentpos++;
            if (currentpos == limitpos)
                breakpos = currentpos;  // happens with a space at the last line, user is most
                                        // likely about to type another word, so
            // need to show space. Alternatively could check if the cursor is actually on this spot.
            // Simply
            // showing a blank new line would not be a good idea, unless the cursor is here for
            // sure, and
            // even then, placing the cursor there again after deselect may be hard.
        }

        ASSERT(startpos != currentpos);

        line.start = startpos;
        line.end = breakpos;
        line.text = t.Mid(startpos, breakpos - startpos);
        return true;
    }

    bool GetLine(int &i, int maxcolwidth, Line &line) {
        auto l = static_cast<int>(t.Len());

        if (i > l) return false;
        if (i == l) {
            if (l && IsLineBreak(t[l - 1])) {
                line.start = line.end = l;
                line.text.clear();
                i = l + 1;
                return true;
            }
            return false;
        }

        maxcolwidth = max(1, maxcolwidth);

        auto hardbreak = -1;
        for (auto p = i; p < l; p++)
            if (IsLineBreak(t[p])) {
                hardbreak = p;
                break;
            }
        auto limit = hardbreak >= 0 ? hardbreak : l;
        auto len = limit - i;

        if (len <= maxcolwidth) {
            line.start = i;
            line.end = limit;
            line.text = t.Mid(i, len);
            i = hardbreak >= 0 ? NextAfterLineBreak(hardbreak) : limit;
            return true;
        }

        for (auto p = i + maxcolwidth; p >= i; p--)
            if (!IsWord(t[p])) return GetLinePart(i, p, limit, line);

        // A single word is > maxcolwidth. We split it up anyway.
        // This happens with long urls and e.g. Japanese text without spaces.
        // Should really do proper unicode linebreaking instead (see libunibreak),
        // but for now this is better than the old code below which allowed for arbitrary long
        // words.
        return GetLinePart(i, min(i + maxcolwidth, limit), limit, line);

        // for(int p = i+maxcolwidth; p<l;  p++) if (!IsWord(t[p])) return GetLinePart(i, p, l);  //
        // we arrive here only
        // if a single word is too big for maxcolwidth, so simply return that word
        // return GetLinePart(i, l, l);     // big word was the last one
    }

    void TextSize(wxReadOnlyDC &dc, int &sx, int &sy, int tiny, int &leftoffset, int maxcolwidth) {
        sx = sy = 0;
        auto i = 0;
        for (;;) {
            Line line;
            if (!GetLine(i, maxcolwidth, line)) break;
            auto &curl = line.text;
            auto display = BuildDisplayLine(curl);
            int x, y;
            if (tiny) {
                x = static_cast<int>(display.text.Len());
                y = 1;
            } else if (display.text.empty()) {
                x = 0;
                y = dc.GetCharHeight();
            } else
                dc.GetTextExtent(display.text, &x, &y);
            sx = max(x, sx);
            sy += y;
            leftoffset = y;
        }
        if (!tiny) sx += 4;
    }

    bool IsInSearch() {
        return sys->searchstring.Len() &&
               (sys->casesensitivesearch ? t.Find(sys->searchstring)
                                         : t.Lower().Find(sys->searchstring)) >= 0;
    }

    int Render(Document *doc, int bx, int by, int depth, wxDC &dc, int &leftoffset,
               int maxcolwidth) {
        auto ixs = 0, iys = 0;
        if (!cell->tiny) sys->ImageSize(DisplayImage(), ixs, iys);

        if (ixs && iys) {
            sys->ImageDraw(DisplayImage(), dc, bx + 1 + g_margin_extra,
                           by + (cell->tys - iys) / 2 + g_margin_extra);
            ixs += 2;
            iys += 2;
        }

        if (t.empty()) return iys;

        doc->PickFont(dc, depth, relsize, stylebits);

        auto h = cell->tiny ? 1 : dc.GetCharHeight();
        leftoffset = h;
        auto i = 0;
        auto lines = 0;
        auto searchfound = IsInSearch();
        auto istag = cell->IsTag(doc);
        if (cell->tiny) {
            if (searchfound)
                dc.SetPen(*wxRED_PEN);
            else if (filtered)
                dc.SetPen(*wxLIGHT_GREY_PEN);
            else if (istag)
                dc.SetPen(wxPen(LightColor(doc->tags[t])));
            else
                dc.SetPen(sys->pen_tinytext);
        }
        for (;;) {
            Line line;
            if (!GetLine(i, maxcolwidth, line)) break;
            auto &curl = line.text;
            auto display = BuildDisplayLine(curl);
            if (cell->tiny) {
                if (sys->fastrender) {
                    dc.DrawLine(bx + ixs, by + lines * h,
                                bx + ixs + static_cast<int>(display.text.Len()), by + lines * h);
                    /*
                    wxPoint points[] = { wxPoint(bx + ixs, by + lines * h), wxPoint(bx + ixs +
                    curl.Len(), by + lines * h) }; dc.DrawLines(1, points, 0, 0);
                     */
                } else {
                    auto word = 0;
                    loop(p, static_cast<int>(display.text.Len()) + 1) {
                        if (static_cast<int>(display.text.Len()) <= p || display.text[p] == ' ') {
                            if (word)
                                dc.DrawLine(bx + p - word + ixs, by + lines * h, bx + p,
                                            by + lines * h);
                            word = 0;
                        } else
                            word++;
                    }
                }
            } else {
                if (searchfound)
                    dc.SetTextForeground(*wxRED);
                else if (filtered)
                    dc.SetTextForeground(*wxLIGHT_GREY);
                else if (istag)
                    dc.SetTextForeground(LightColor(doc->tags[t]));
                else if (cell->textcolor)
                    dc.SetTextForeground(LightColor(cell->textcolor));  // FIXME: clean up
                auto tx = bx + 2 + ixs;
                auto ty = by + lines * h;
                dc.DrawText(display.text, tx + g_margin_extra, ty + g_margin_extra);
                if (searchfound || filtered || istag || cell->textcolor)
                    dc.SetTextForeground(LightColor(0x000000));
            }
            lines++;
        }

        return max(lines * h, iys);
    }

    void FindCursor(Document *doc, int bx, int by, wxReadOnlyDC &dc, Selection &s,
                    int maxcolwidth) {
        bx -= g_margin_extra;
        by -= g_margin_extra;

        auto ixs = 0, iys = 0;
        if (!cell->tiny) sys->ImageSize(DisplayImage(), ixs, iys);
        if (ixs) ixs += 2;

        doc->PickFont(dc, cell->Depth() - doc->drawpath.size(), relsize, stylebits);

        auto i = 0, linestart = 0;
        auto line = by / dc.GetCharHeight();
        wxString ls;

        loop(l, line + 1) {
            Line lineinfo;
            if (!GetLine(i, maxcolwidth, lineinfo)) {
                linestart = static_cast<int>(t.Len());
                ls.clear();
                break;
            }
            linestart = lineinfo.start;
            ls = lineinfo.text;
        }

        auto display = BuildDisplayLine(ls);
        s.cursor = s.cursorend = linestart + SourceCursorFromDisplayX(dc, display, bx - ixs - 2);
        ASSERT(s.cursor >= 0 && s.cursor <= static_cast<int>(t.Len()));
    }

    void DrawCursor(Document *doc, wxDC &dc, Selection &s, bool full, uint color, int maxcolwidth) {
        auto ixs = 0, iys = 0;
        if (!cell->tiny) sys->ImageSize(DisplayImage(), ixs, iys);
        if (ixs) ixs += 2;
        doc->PickFont(dc, cell->Depth() - doc->drawpath.size(), relsize, stylebits);
        auto h = dc.GetCharHeight();
        {
            auto i = 0;
            for (auto l = 0;; l++) {
                Line line;
                if (!GetLine(i, maxcolwidth, line)) break;
                auto start = line.start;
                auto ls = line.text;
                auto display = BuildDisplayLine(ls);
                auto end = line.end;

                if (s.cursor != s.cursorend) {
                    if (s.cursor <= end && s.cursorend >= start) {
                        auto x2 = DisplayPrefixWidth(dc, display, min(s.cursorend, end) - start);
                        auto x1 = DisplayPrefixWidth(dc, display, max(s.cursor, start) - start);
                        if (x1 != x2) {
                            int startx = cell->GetX(doc) + x1 + 2 + ixs + g_margin_extra;
                            int starty =
                                cell->GetY(doc) + l * h + 1 + cell->ycenteroff + g_margin_extra;
                            DrawRectangle(dc, color, startx, starty, x2 - x1, h - 1, true);
                            HintIMELocation(doc, startx, starty, h - 1, stylebits);
                        }
                    }
                } else if (s.cursor >= start && s.cursor <= end) {
                    auto x = DisplayPrefixWidth(dc, display, s.cursor - start);
                    int startx = cell->GetX(doc) + x + 1 + ixs + g_margin_extra;
                    int starty = cell->GetY(doc) + l * h + 1 + cell->ycenteroff + g_margin_extra;
                    DrawRectangle(dc, color, startx, starty, 2, h - 2);
                    HintIMELocation(doc, startx, starty, h - 2, stylebits);
                    break;
                }
            }
        }
    }

    void ExpandToWord(Selection &s) {
        if (!wxIsalnum(t[s.cursor])) return;
        while (s.cursor > 0 && wxIsalnum(t[s.cursor - 1])) s.cursor--;
        while (s.cursorend < static_cast<int>(t.Len()) && wxIsalnum(t[s.cursorend])) s.cursorend++;
    }

    void SelectWord(Selection &s) {
        if (s.cursor >= static_cast<int>(t.Len())) return;
        s.cursorend = s.cursor + 1;
        ExpandToWord(s);
    }

    void SelectWordBefore(Selection &s) {
        if (s.cursor <= 1) return;
        s.cursorend = s.cursor--;
        ExpandToWord(s);
    }

    bool RangeSelRemove(Selection &s) {
        WasEdited();
        if (s.cursor != s.cursorend) {
            t.Remove(s.cursor, s.cursorend - s.cursor);
            s.cursorend = s.cursor;
            return true;
        }
        return false;
    }

    void SetRelSize(Selection &s) {
        if (t.Len() || !cell->parent) return;
        int dd[] = {0, 1, 1, 0, 0, -1, -1, 0};
        for (auto i = 0; i < 4; i++) {
            auto x = max(0, min(s.x + dd[i * 2], s.grid->xs - 1));
            auto y = max(0, min(s.y + dd[i * 2 + 1], s.grid->ys - 1));
            auto c = s.grid->C(x, y).get();
            if (c->text.t.Len()) {
                relsize = c->text.relsize;
                break;
            }
        }
    }

    auto Insert(Document *doc, const auto &ins, Selection &s, bool keeprelsize) {
        auto prevl = t.Len();
        if (!s.TextEdit()) Clear(doc, s);
        RangeSelRemove(s);
        if (!prevl && !keeprelsize) SetRelSize(s);
        t.insert(s.cursor, ins);
        s.cursor = s.cursorend = s.cursor + static_cast<int>(ins.Len());
    }
    void Key(Document *doc, int k, Selection &s) {
        wxString ins;
        ins += k;
        Insert(doc, ins, s, false);
    }
    void Newline(Document *doc, Selection &s) {
        wxString ins;
        ins += '\n';
        Insert(doc, ins, s, false);
    }

    void Delete(Selection &s) {
        if (!RangeSelRemove(s))
            if (s.cursor < static_cast<int>(t.Len())) {
                auto next = NextCursorPos(s.cursor);
                t.Remove(s.cursor, next - s.cursor);
            };
    }
    void Backspace(Selection &s) {
        if (!RangeSelRemove(s))
            if (s.cursor > 0) {
                auto prev = PreviousCursorPos(s.cursor);
                t.Remove(prev, s.cursor - prev);
                s.cursor = s.cursorend = prev;
            };
    }
    void DeleteWord(Selection &s) {
        SelectWord(s);
        Delete(s);
    }
    void BackspaceWord(Selection &s) {
        SelectWordBefore(s);
        Backspace(s);
    }

    void ReplaceStr(const wxString &str, const wxString &lstr) {
        if (sys->casesensitivesearch) {
            for (auto i = 0, j = 0; (j = t.Mid(i).Find(sys->searchstring)) >= 0;) {
                // does this need WasEdited()?
                i += j;
                t.Remove(i, sys->searchstring.Len());
                t.insert(i, str);
                i += str.Len();
            }
        } else {
            auto lowert = t.Lower();
            for (auto i = 0, j = 0; (j = lowert.Mid(i).Find(sys->searchstring)) >= 0;) {
                // does this need WasEdited()?
                i += j;
                lowert.Remove(i, sys->searchstring.Len());
                t.Remove(i, sys->searchstring.Len());
                lowert.insert(i, lstr);
                t.insert(i, str);
                i += str.Len();
            }
        }
    }

    void Clear(Document *doc, Selection &s) {
        t.Clear();
        s.EnterEdit(doc);
    }

    void HomeEnd(Selection &s, bool home) {
        auto i = 0;
        auto cw = cell->ColWidth();
        auto findwhere = home ? s.cursor : s.cursorend;
        for (;;) {
            Line line;
            if (!GetLine(i, cw, line)) break;
            auto start = line.start;
            auto end = line.end;
            if (findwhere >= start && findwhere <= end) {
                s.cursor = s.cursorend = home ? start : end;
                break;
            }
        }
    }

    void Save(wxDataOutputStream &dos) const {
        dos.WriteString(t.wx_str());
        dos.Write32(relsize);
        dos.Write32(image ? image->savedindex : -1);
        dos.WriteDouble(image_scale);
        dos.Write32(stylebits);
        wxLongLong le = lastedit.GetValue();
        dos.Write64(&le, 1);
    }

    void Load(wxDataInputStream &dis) {
        t = dis.ReadString();

        // if (t.length() > 10000)
        //    printf("");

        if (sys->versionlastloaded <= 11) dis.Read32();  // numlines

        relsize = dis.Read32();

        int i = dis.Read32();
        image = i >= 0 ? sys->imagelist[sys->loadimageids[i]].get() : nullptr;

        image_scale = sys->versionlastloaded >= 29 ? ClampImageScale(dis.ReadDouble()) : 1.0;

        if (sys->versionlastloaded >= 7) stylebits = dis.Read32();

        wxLongLong time;
        if (sys->versionlastloaded >= 14) {
            dis.Read64(&time, 1);
        } else {
            time = sys->fakelasteditonload--;
        }
        lastedit = wxDateTime(time);
    }

    auto Eval(auto &ev) const {
        switch (cell->celltype) {
            // Load variable's data.
            case CT_VARU: {
                auto v = ev.Lookup(t);
                if (!v) {
                    v = cell->Clone(nullptr);
                    v->celltype = CT_DATA;
                    v->text.t = "**Variable Load Error**";
                }
                return v;
            }

            // Return our current data.
            case CT_DATA: return cell->Clone(nullptr);

            default: return unique_ptr<Cell>();
        }
    }
};
