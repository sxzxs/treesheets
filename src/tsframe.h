struct TSFrame : wxFrame {
    static constexpr int toolbarlayoutversion = 4;
    TSApp *app;
    wxIcon icon;
    wxTaskBarIcon taskbaricon;
    wxMenu *editmenupopup;
    wxFileHistory filehistory;
    #ifdef ENABLE_LOBSTER
        wxFileHistory scripts {A_MAXACTION - A_SCRIPT, A_SCRIPT};
    #endif
    wxFileSystemWatcher *watcher {nullptr};
    wxAuiNotebook *notebook {nullptr};
    wxAuiManager aui {this};
    wxPanel *explorerpane {nullptr};
    wxTreeCtrl *explorertree {nullptr};
    wxSearchCtrl *explorersearch {nullptr};
    wxListBox *explorerresults {nullptr};
    wxString explorerroot;
    wxString explorercontextpath;
    wxString explorerpendingrefreshpath;
    vector<wxString> explorerresultpaths;
    std::set<wxString> explorerwatchedpaths;
    wxBitmap line_nw;
    wxBitmap line_sw;
    wxBitmap foldicon;
    bool fromclosebox {true};
    bool watcherwaitingforuser {false};
    bool explorerrefreshpending {false};
    bool explorerautohide {false};
    wxColour toolbarbackgroundcolor {0xD8C7BC};
    wxTextCtrl *filter {nullptr};
    wxTextCtrl *replaces {nullptr};
    ColorDropdown *cellcolordropdown {nullptr};
    ColorDropdown *textcolordropdown {nullptr};
    ColorDropdown *bordercolordropdown {nullptr};
    ImageDropdown *imagedropdown {nullptr};
    wxString imagepath;
    int refreshhack {0};
    int refreshhackinstances {0};
    bool globalshowhotkeyregistered {false};
    std::map<wxString, wxString> menustrings;
    std::map<int, wxString> menuaccelerators;

    struct ExplorerTreeItemData : wxTreeItemData {
        wxString path;
        explicit ExplorerTreeItemData(const wxString &_path) : path(_path) {}
    };

    struct ExplorerSearchToken {
        wxString text;
        ::pinyin::String pinyin;
    };

    struct ExplorerSearchText {
        wxString text;
        ::pinyin::String pinyin;
        ::pinyin::String initials;
        ::pinyin::String alternatives;
    };

    TSFrame(TSApp *_app)
        : wxFrame((wxFrame *)nullptr, wxID_ANY, "TreeSheets", wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_FRAME_STYLE),
          app(_app) {
        sys->frame = this;

        class MyLog : public wxLog {
            void DoLogString(const wxChar *message, time_t timestamp) { DoLogText(*message); }
            void DoLogText(const wxString &message) {
                #ifdef WIN32
                OutputDebugString(message.c_str());
                OutputDebugString(L"\n");
                #else
                fputws(message.c_str(), stderr);
                fputws(L"\n", stderr);
                #endif
            }
        };

        wxLog::SetActiveTarget(new MyLog());

        wxLogMessage("%s", wxVERSION_STRING);

        aui.SetManagedWindow(this);

        wxInitAllImageHandlers();

        wxIconBundle icons;
        wxIcon iconbig;
        #ifdef WIN32
            int iconsmall = ::GetSystemMetrics(SM_CXSMICON);
            int iconlarge = ::GetSystemMetrics(SM_CXICON);
        #endif
        icon.LoadFile(app->GetDataPath("images/icon16.png"), wxBITMAP_TYPE_PNG
            #ifdef WIN32
                , iconsmall, iconsmall
            #endif
        );
        iconbig.LoadFile(app->GetDataPath("images/icon32.png"), wxBITMAP_TYPE_PNG
            #ifdef WIN32
                , iconlarge, iconlarge
            #endif
        );
        if (!icon.IsOk() || !iconbig.IsOk()) {
            wxMessageBox(_("Error loading core data file (TreeSheets not installed correctly?)"),
                         _("Initialization Error"), wxOK, this);
            exit(1);
        }
        icons.AddIcon(icon);
        icons.AddIcon(iconbig);
        SetIcons(icons);

        RenderFolderIcon();
        line_nw.LoadFile(app->GetDataPath("images/render/line_nw.png"), wxBITMAP_TYPE_PNG);
        line_sw.LoadFile(app->GetDataPath("images/render/line_sw.png"), wxBITMAP_TYPE_PNG);

        imagepath = app->GetDataPath("images/nuvola/dropdown/");

        if (sys->singletray)
            taskbaricon.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_UP,
                        wxTaskBarIconEventHandler(TSFrame::OnTBIDBLClick), nullptr, this);
        else
            taskbaricon.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_DCLICK,
                        wxTaskBarIconEventHandler(TSFrame::OnTBIDBLClick), nullptr, this);
        taskbaricon.Connect(wxID_ANY, wxEVT_TASKBAR_RIGHT_UP,
                    wxTaskBarIconEventHandler(TSFrame::OnTrayMenu), nullptr, this);
        UpdateTrayIcon();

        bool showtbar, showsbar, lefttabs;

        sys->cfg->Read("showtbar", &showtbar, true);
        sys->cfg->Read("showsbar", &showsbar, true);
        sys->cfg->Read("lefttabs", &lefttabs, true);
        sys->cfg->Read("explorerautohide", &explorerautohide, false);

        filehistory.Load(*sys->cfg);
        #ifdef ENABLE_LOBSTER
            auto oldpath = sys->cfg->GetPath();
            sys->cfg->SetPath("/scripts");
            scripts.Load(*sys->cfg);
            sys->cfg->SetPath(oldpath);
        #endif

        #ifdef __WXMAC__
            #define CTRLORALT "CTRL"
        #else
            #define CTRLORALT "ALT"
        #endif

        #ifdef __WXMAC__
            #define ALTORCTRL "ALT"
        #else
            #define ALTORCTRL "CTRL"
        #endif

        #ifdef __WXMAC__
            #define PROGRESSCELLKEY CTRLORALT "+ENTER"
        #else
            #define PROGRESSCELLKEY "CTRL+ALT+ENTER"
        #endif

        auto expmenu = new wxMenu();
        MyAppend(expmenu, A_EXPXML, _("&XML..."),
                 _("Export the current view as XML (which can also be reimported without losing structure)"));
        MyAppend(expmenu, A_EXPJSON, _("&JSON..."),
                 _("Export the current view as JSON for scripts and web tools"));
        expmenu->AppendSeparator();
        MyAppend(expmenu, A_EXPHTMLT, _("&HTML (Tables+Styling)..."),
                 _("Export the current view as HTML using nested tables, that will look somewhat like the TreeSheet"));
        MyAppend(expmenu, A_EXPHTMLTE, _("&HTML (Tables+Styling+Images)..."),
                 _("Export the curent view as HTML using nested tables and exported images"));
        MyAppend(expmenu, A_EXPHTMLB, _("HTML (&Bullet points)..."),
                 _("Export the current view as HTML as nested bullet points."));
        MyAppend(expmenu, A_EXPHTMLO, _("HTML (&Outline)..."),
                 _("Export the current view as HTML as nested headers, suitable for importing into Word's outline mode"));
        expmenu->AppendSeparator();
        MyAppend(
            expmenu, A_EXPTEXT, _("Indented &Text..."),
            _("Export the current view as tree structured text, using spaces for each indentation level. Suitable for importing into mindmanagers and general text programs"));
        MyAppend(
            expmenu, A_EXPCSV, _("&Comma delimited text (CSV)..."),
            _("Export the current view as CSV. Good for spreadsheets and databases. Only works on grids with no sub-grids (use the Flatten operation first if need be)"));
        expmenu->AppendSeparator();
        MyAppend(expmenu, A_EXPIMAGE, _("&Image..."),
                 _("Export the current view as an image. Useful for faithful renderings of the TreeSheet, and programs that don't accept any of the above options"));
        MyAppend(expmenu, A_EXPSVG, _("&Vector graphics..."),
                _("Export the current view to a SVG vector file."));

        auto impmenu = new wxMenu();
        MyAppend(impmenu, A_IMPXML, _("XML..."));
        MyAppend(impmenu, A_IMPXMLA, _("XML (attributes too, for OPML etc)..."));
        MyAppend(impmenu, A_IMPTXTI, _("Indented text..."));
        MyAppend(impmenu, A_IMPTXTC, _("Comma delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTS, _("Semi-Colon delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTT, _("Tab delimited text..."));

        auto recentmenu = new wxMenu();
        filehistory.UseMenu(recentmenu);
        filehistory.AddFilesToMenu();

        auto filemenu = new wxMenu();
        MyAppend(filemenu, wxID_NEW, _("&New") + "\tCTRL+N", _("Create a new document"));
        MyAppend(filemenu, wxID_OPEN, _("&Open...") + "\tCTRL+O", _("Open an existing document"));
        MyAppend(filemenu, A_EXPLORERROOT, _("Open &Folder..."),
                 _("Choose the folder shown in the Explorer"));
        MyAppend(filemenu, wxID_CLOSE, _("&Close") + "\tCTRL+W", _("Close current document"));
        filemenu->AppendSubMenu(recentmenu, _("&Recent files"));
        MyAppend(filemenu, wxID_SAVE, _("&Save") + "\tCTRL+S", _("Save current document"));
        MyAppend(filemenu, wxID_SAVEAS, _("Save &As..."),
                 _("Save current document with a different filename"));
        MyAppend(filemenu, A_SAVEALL, _("Save All"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, A_PAGESETUP, _("Page Setup..."));
        MyAppend(filemenu, A_PRINTSCALE, _("Set Print Scale..."));
        MyAppend(filemenu, wxID_PREVIEW, _("Print preview..."));
        MyAppend(filemenu, wxID_PRINT, _("&Print...") + "\tCTRL+P");
        filemenu->AppendSeparator();
        filemenu->AppendSubMenu(expmenu, _("Export &view as"));
        filemenu->AppendSubMenu(impmenu, _("Import from"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, wxID_EXIT, _("&Exit") + "\tCTRL+Q", _("Quit this program"));

        wxMenu *editmenu;
        loop(twoeditmenus, 2) {
            auto sizemenu = new wxMenu();
            MyAppend(sizemenu, A_INCSIZE,
                     _("&Increase text size (SHIFT+mousewheel)") + "\tSHIFT+PGUP");
            MyAppend(sizemenu, A_DECSIZE,
                     _("&Decrease text size (SHIFT+mousewheel)") + "\tSHIFT+PGDN");
            MyAppend(sizemenu, A_RESETSIZE, _("&Reset text sizes") + "\tCTRL+SHIFT+S");
            MyAppend(sizemenu, A_MINISIZE, _("&Shrink text of all sub-grids") + "\tCTRL+SHIFT+M");
            sizemenu->AppendSeparator();
            MyAppend(sizemenu, A_INCWIDTH,
                     _("Increase column width (ALT+mousewheel)") + "\tALT+PGUP");
            MyAppend(sizemenu, A_DECWIDTH,
                     _("Decrease column width (ALT+mousewheel)") + "\tALT+PGDN");
            MyAppend(sizemenu, A_INCWIDTHNH,
                     _("Increase column width (no sub grids)") + "\tCTRL+ALT+PGUP");
            MyAppend(sizemenu, A_DECWIDTHNH,
                     _("Decrease column width (no sub grids)") + "\tCTRL+ALT+PGDN");
            MyAppend(sizemenu, A_RESETWIDTH, _("Reset column widths") + "\tCTRL+R",
                     _("Reset the column widths in the selection to the default column width"));

            auto gridbordwidthmenu = new wxMenu();
            MyAppend(gridbordwidthmenu, A_BORD0, _("Border &0") + "\tCTRL+SHIFT+9");
            MyAppend(gridbordwidthmenu, A_BORD1, _("Border &1") + "\tCTRL+SHIFT+1");
            MyAppend(gridbordwidthmenu, A_BORD2, _("Border &2") + "\tCTRL+SHIFT+2");
            MyAppend(gridbordwidthmenu, A_BORD3, _("Border &3") + "\tCTRL+SHIFT+3");
            MyAppend(gridbordwidthmenu, A_BORD4, _("Border &4") + "\tCTRL+SHIFT+4");
            MyAppend(gridbordwidthmenu, A_BORD5, _("Border &5") + "\tCTRL+SHIFT+5");

            auto selouterbordwidthmenu = new wxMenu();
            MyAppend(selouterbordwidthmenu, A_SEL_BORD_OUTER0, _("Border &0"));
            MyAppend(selouterbordwidthmenu, A_SEL_BORD_OUTER1, _("Border &1"));
            MyAppend(selouterbordwidthmenu, A_SEL_BORD_OUTER2, _("Border &2"));
            MyAppend(selouterbordwidthmenu, A_SEL_BORD_OUTER3, _("Border &3"));
            MyAppend(selouterbordwidthmenu, A_SEL_BORD_OUTER4, _("Border &4"));
            MyAppend(selouterbordwidthmenu, A_SEL_BORD_OUTER5, _("Border &5"));

            auto selinnerbordwidthmenu = new wxMenu();
            MyAppend(selinnerbordwidthmenu, A_SEL_BORD_INNER0, _("Border &0"));
            MyAppend(selinnerbordwidthmenu, A_SEL_BORD_INNER1, _("Border &1"));
            MyAppend(selinnerbordwidthmenu, A_SEL_BORD_INNER2, _("Border &2"));
            MyAppend(selinnerbordwidthmenu, A_SEL_BORD_INNER3, _("Border &3"));
            MyAppend(selinnerbordwidthmenu, A_SEL_BORD_INNER4, _("Border &4"));
            MyAppend(selinnerbordwidthmenu, A_SEL_BORD_INNER5, _("Border &5"));

            auto bordmenu = new wxMenu();
            MyAppend(bordmenu, A_SEL_BORD_OUTER_COLOR_PICK,
                     _("Selection outline border color..."));
            MyAppend(bordmenu, A_SEL_BORD_INNER_COLOR_PICK,
                     _("Selection inner border color..."));
            bordmenu->AppendSeparator();
            MyAppend(bordmenu, A_SEL_BORD_OUTER_COLOR,
                     _("Apply last border color to selection outline"));
            MyAppend(bordmenu, A_SEL_BORD_INNER_COLOR,
                     _("Apply last border color to selection inner borders"));
            bordmenu->AppendSeparator();
            bordmenu->AppendSubMenu(gridbordwidthmenu, _("Set grid outer border width"));
            bordmenu->AppendSubMenu(selouterbordwidthmenu,
                                    _("Set selection outline border width"));
            bordmenu->AppendSubMenu(selinnerbordwidthmenu,
                                    _("Set selection inner border width"));

            auto selmenu = new wxMenu();
            MyAppend(selmenu, A_NEXT,
                #ifdef __WXGTK__
                    _("Move to next cell (TAB)")
                #else
                     _("Move to next cell") + "\tTAB"
                #endif
            );
            MyAppend(selmenu, A_PREV,
                #ifdef __WXGTK__
                    _("Move to previous cell (SHIFT+TAB)")
                #else
                     _("Move to previous cell") + "\tSHIFT+TAB"
                #endif
            );
            selmenu->AppendSeparator();
            MyAppend(selmenu, wxID_SELECTALL, _("Select &all in current grid/cell") + "\tCTRL+A");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LEFT,
                #ifdef __WXGTK__
                    _("Move Selection Left (LEFT)")
                #else
                     _("Move Selection Left") + "\tLEFT"
                #endif
            );
            MyAppend(selmenu, A_RIGHT,
                #ifdef __WXGTK__
                    _("Move Selection Right (RIGHT)")
                #else
                     _("Move Selection Right") + "\tRIGHT"
                #endif
            );
            MyAppend(selmenu, A_UP,
                #ifdef __WXGTK__
                    _("Move Selection Up (UP)")
                #else
                     _("Move Selection Up") + "\tUP"
                #endif
            );
            MyAppend(selmenu, A_DOWN,
                #ifdef __WXGTK__
                    _("Move Selection Down (DOWN)")
                #else
                     _("Move Selection Down") + "\tDOWN"
                #endif
            );
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_MLEFT, _("Move Cells Left") + "\tCTRL+LEFT");
            MyAppend(selmenu, A_MRIGHT, _("Move Cells Right") + "\tCTRL+RIGHT");
            MyAppend(selmenu, A_MUP, _("Move Cells Up") + "\tCTRL+UP");
            MyAppend(selmenu, A_MDOWN, _("Move Cells Down") + "\tCTRL+DOWN");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SLEFT, _("Extend Selection Left") + "\tSHIFT+LEFT");
            MyAppend(selmenu, A_SRIGHT, _("Extend Selection Right") + "\tSHIFT+RIGHT");
            MyAppend(selmenu, A_SUP, _("Extend Selection Up") + "\tSHIFT+UP");
            MyAppend(selmenu, A_SDOWN, _("Extend Selection Down") + "\tSHIFT+DOWN");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SROWS, _("Extend Selection Full Rows") + "\tCTRL+SHIFT+B");
            MyAppend(selmenu, A_SCLEFT, _("Extend Selection Rows Left") + "\tCTRL+SHIFT+LEFT");
            MyAppend(selmenu, A_SCRIGHT, _("Extend Selection Rows Right") + "\tCTRL+SHIFT+RIGHT");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SCOLS, _("Extend Selection Full Columns") + "\tCTRL+SHIFT+A");
            MyAppend(selmenu, A_SCUP, _("Extend Selection Columns Up") + "\tCTRL+SHIFT+UP");
            MyAppend(selmenu, A_SCDOWN, _("Extend Selection Columns Down") + "\tCTRL+SHIFT+DOWN");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_CANCELEDIT, _("Select &Parent") + "\tESC");
            MyAppend(selmenu, A_ENTERGRID, _("Select First &Child") + "\tSHIFT+ENTER");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LINK, _("Go To &Matching Cell (Text)") + "\tF6");
            MyAppend(selmenu, A_LINKREV, _("Go To Matching Cell (Text, Reverse)") + "\tSHIFT+F6");
            MyAppend(selmenu, A_LINKIMG, _("Go To Matching Cell (Image)") + "\tF7");
            MyAppend(selmenu, A_LINKIMGREV,
                     _("Go To Matching Cell (Image, Reverse)") + "\tSHIFT+F7");

            auto temenu = new wxMenu();
            MyAppend(temenu, A_LEFT, _("Cursor Left") + "\tLEFT");
            MyAppend(temenu, A_RIGHT, _("Cursor Right") + "\tRIGHT");
            MyAppend(temenu, A_MLEFT, _("Word Left") + "\tCTRL+LEFT");
            MyAppend(temenu, A_MRIGHT, _("Word Right") + "\tCTRL+RIGHT");
            temenu->AppendSeparator();
            MyAppend(temenu, A_SLEFT, _("Extend Selection Left") + "\tSHIFT+LEFT");
            MyAppend(temenu, A_SRIGHT, _("Extend Selection Right") + "\tSHIFT+RIGHT");
            MyAppend(temenu, A_SCLEFT, _("Extend Selection Word Left") + "\tCTRL+SHIFT+LEFT");
            MyAppend(temenu, A_SCRIGHT, _("Extend Selection Word Right") + "\tCTRL+SHIFT+RIGHT");
            MyAppend(temenu, A_SHOME, _("Extend Selection to Start") + "\tSHIFT+HOME");
            MyAppend(temenu, A_SEND, _("Extend Selection to End") + "\tSHIFT+END");
            temenu->AppendSeparator();
            MyAppend(temenu, A_HOME, _("Start of line of text") + "\tHOME");
            MyAppend(temenu, A_END, _("End of line of text") + "\tEND");
            MyAppend(temenu, A_CHOME, _("Start of text") + "\tCTRL+HOME");
            MyAppend(temenu, A_CEND, _("End of text") + "\tCTRL+END");
            temenu->AppendSeparator();
            MyAppend(temenu, A_ENTERCELL, _("Enter/exit text edit mode") + "\tENTER");
            MyAppend(temenu, A_ENTERCELL_JUMPTOEND,
                     _("...and jump to the end of the text") + "\tF2");
            MyAppend(
                temenu, A_ENTERCELL_JUMPTOSTART,
                _("...and progress to the first cell in the new row") + "\t" ALTORCTRL "+ENTER");
            MyAppend(temenu, A_TEXTNEWLINE,
                     _("Insert line break in cell text (Alt+Enter)"));
            MyAppend(temenu, A_PROGRESSCELL,
                     _("...and progress to the next cell on the right") + "\t" PROGRESSCELLKEY);
            MyAppend(temenu, A_CANCELEDIT, _("Cancel text edits") + "\tESC");

            auto stmenu = new wxMenu();
            MyAppend(stmenu, wxID_BOLD, _("Toggle cell &BOLD") + "\tCTRL+B");
            MyAppend(stmenu, wxID_ITALIC, _("Toggle cell &ITALIC") + "\tCTRL+I");
            MyAppend(stmenu, A_TT, _("Toggle cell &typewriter") + "\tCTRL+ALT+T");
            MyAppend(stmenu, wxID_UNDERLINE, _("Toggle cell &underlined") + "\tCTRL+U");
            MyAppend(stmenu, wxID_STRIKETHROUGH, _("Toggle cell &strikethrough") + "\tCTRL+T");
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_RESETSTYLE, _("&Reset text styles") + "\tCTRL+SHIFT+R");
            MyAppend(stmenu, A_RESETCOLOR, _("Reset &colors") + "\tCTRL+SHIFT+C");
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_LASTCELLCOLOR, _("Apply last cell color") + "\tSHIFT+ALT+C");
            MyAppend(stmenu, A_LASTTEXTCOLOR, _("Apply last text color") + "\tSHIFT+ALT+T");
            MyAppend(stmenu, A_LASTBORDCOLOR, _("Apply last border color") + "\tSHIFT+ALT+B");
            MyAppend(stmenu, A_OPENCELLCOLOR, _("Open cell colors") + "\tSHIFT+ALT+F9");
            MyAppend(stmenu, A_OPENTEXTCOLOR, _("Open text colors") + "\tSHIFT+ALT+F10");
            MyAppend(stmenu, A_OPENBORDCOLOR, _("Open border colors") + "\tSHIFT+ALT+F11");
            MyAppend(stmenu, A_OPENIMGDROPDOWN, _("Open image dropdown") + "\tSHIFT+ALT+F12");

            auto tagmenu = new wxMenu();
            MyAppend(tagmenu, A_TAGADD, _("&Add Cell Text as Tag"));
            MyAppend(tagmenu, A_TAGREMOVE, _("&Remove Cell Text from Tags"));
            MyAppend(tagmenu, A_NOP, _("&Set Cell Text to tag (use CTRL+RMB)"),
                     _("Hold CTRL while pressing right mouse button to quickly set a tag for the current cell using a popup menu"));

            auto orgmenu = new wxMenu();
            MyAppend(orgmenu, A_TRANSPOSE, _("&Transpose") + "\tCTRL+SHIFT+T",
                     _("changes the orientation of a grid"));
            MyAppend(orgmenu, A_SORT, _("Sort &Ascending"),
                     _("Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_SORTD, _("Sort &Descending"),
                     _("Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_HSWAP, _("Hierarchy &Swap") + "\tF8",
                     _("Swap all cells with this text at this level (or above) with the parent"));
            MyAppend(orgmenu, A_HIFY, _("&Hierarchify"),
                     _("Convert an NxN grid with repeating elements per column into an 1xN grid with hierarchy, useful to convert data from spreadsheets"));
            MyAppend(orgmenu, A_FLATTEN, _("&Flatten"),
                     _("Takes a hierarchy (nested 1xN or Nx1 grids) and converts it into a flat NxN grid, useful for export to spreadsheets"));

            auto imgmenu = new wxMenu();
            MyAppend(imgmenu, A_IMAGE, _("&Add..."), _("Add an image to the selected cell"));
            MyAppend(imgmenu, A_IMAGESVA, _("&Save as..."),
                     _("Save image(s) from selected cell(s) to disk. Multiple images will be saved with a counter appended to each file name."));
            imgmenu->AppendSeparator();
            MyAppend(
                imgmenu, A_IMAGESCP, _("Scale (re-sa&mple pixels, by %)"),
                _("Change the image(s) size if it is too big, by reducing the amount of pixels"));
            MyAppend(
                imgmenu, A_IMAGESCW, _("Scale (re-sample pixels, by &width)"),
                _("Change the image(s) size if it is too big, by reducing the amount of pixels"));
            MyAppend(imgmenu, A_IMAGESCF, _("Scale (&display only, lossless)"),
                     _("Change the selected image display size without resampling pixels."));
            MyAppend(imgmenu, A_IMAGESCN, _("&Reset Scale (display only)"),
                     _("Reset the selected image display size without resampling pixels."));
            imgmenu->AppendSeparator();
            MyAppend(
                imgmenu, A_SAVE_AS_JPEG, _("Embed as &JPEG"),
                _("Embed the image(s) in the selected cells in JPEG format (reduces data size)"));
            MyAppend(imgmenu, A_SAVE_AS_PNG, _("Embed as &PNG"),
                     _("Embed the image(s) in the selected cells in PNG format (default)"));
            imgmenu->AppendSeparator();
            MyAppend(imgmenu, A_LASTIMAGE, _("Insert last image") + "\tSHIFT+ALT+i",
                     _("Insert the last image that has been inserted before in TreeSheets."));
            MyAppend(imgmenu, A_IMAGER, _("Remo&ve"),
                     _("Remove image(s) from the selected cells"));

            auto navmenu = new wxMenu();
            MyAppend(navmenu, A_BROWSE, _("Open link in &browser") + "\tF5",
                     _("Opens up the text from the selected cell in browser (should start be a valid URL)"));
            MyAppend(navmenu, A_BROWSEF, _("Open &file") + "\tF4",
                     _("Opens up the text from the selected cell in default application for the file type"));

            auto laymenu = new wxMenu();
            MyAppend(laymenu, A_V_GS,
                     _("Vertical Layout with Grid Style Rendering") + "\t" CTRLORALT "+1");
            MyAppend(laymenu, A_V_BS,
                     _("Vertical Layout with Bubble Style Rendering") + "\t" CTRLORALT "+2");
            MyAppend(laymenu, A_V_LS,
                     _("Vertical Layout with Line Style Rendering") + "\t" CTRLORALT "+3");
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_H_GS,
                     _("Horizontal Layout with Grid Style Rendering") + "\t" CTRLORALT "+4");
            MyAppend(laymenu, A_H_BS,
                     _("Horizontal Layout with Bubble Style Rendering") + "\t" CTRLORALT "+5");
            MyAppend(laymenu, A_H_LS,
                     _("Horizontal Layout with Line Style Rendering") + "\t" CTRLORALT "+6");
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_GS, _("Grid Style Rendering") + "\t" CTRLORALT "+7");
            MyAppend(laymenu, A_BS, _("Bubble Style Rendering") + "\t" CTRLORALT "+8");
            MyAppend(laymenu, A_LS, _("Line Style Rendering") + "\t" CTRLORALT "+9");
            laymenu->AppendSeparator();
            laymenu->AppendCheckItem(
                A_RENDERSTYLEONELAYER, _("Apply render style to selected layer only"),
                _("When checked, layout and render style commands affect only selected cells, not descendant cells"));
            laymenu->Check(A_RENDERSTYLEONELAYER, sys->renderstyleonelayer);
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_TEXTGRID, _("Toggle Vertical Layout") + "\t" CTRLORALT "+0",
                     _("Make a hierarchy layout more vertical (default) or more horizontal"));

            editmenu = new wxMenu();
            MyAppend(editmenu, wxID_CUT, _("Cu&t") + "\tCTRL+X", _("Cut selection"));
            MyAppend(editmenu, wxID_COPY, _("&Copy") + "\tCTRL+C", _("Copy selection"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_COPYWI, _("Copy with &Images") + "\tCTRL+ALT+C");
            MyAppend(editmenu, A_COPYBM, _("&Copy as Bitmap"));
            MyAppend(editmenu, A_COPYCT, _("Copy As Continuous Text"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, wxID_PASTE, _("&Paste") + "\tCTRL+V", _("Paste clipboard contents"));
            MyAppend(editmenu, A_PASTESTYLE, _("Paste Style Only") + "\tCTRL+SHIFT+V",
                     _("only sets the colors and style of the copied cell, and keeps the text"));
            MyAppend(editmenu, A_COLLAPSE, _("Collapse Ce&lls") + "\tCTRL+L");
            MyAppend(editmenu, A_MERGECELLS, _("&Merge Cells"),
                     _("Merge the selected cells into one visual cell"));
            MyAppend(editmenu, A_UNMERGECELLS, _("&Unmerge Cells"),
                     _("Split merged cells back into regular cells"));
            editmenu->AppendSeparator();

            MyAppend(editmenu, A_EDITNOTE, _("Edit &Note") + "\tCTRL+E",
                     _("Edit the note of the selected cell"));
            MyAppend(editmenu, wxID_UNDO, _("&Undo") + "\tCTRL+Z",
                     _("revert the changes, one step at a time"));
            MyAppend(editmenu, wxID_REDO, _("&Redo") + "\tCTRL+Y",
                     _("redo any undo steps, if you haven't made changes since"));
            editmenu->AppendSeparator();
            MyAppend(
                editmenu, A_DELETE, _("&Delete After") + "\tDEL",
                _("Deletes the column of cells after the selected grid line, or the row below"));
            MyAppend(
                editmenu, A_BACKSPACE, _("Delete Before") + "\tBACK",
                _("Deletes the column of cells before the selected grid line, or the row above"));
            MyAppend(editmenu, A_DELETE_WORD, _("Delete Word After") + "\tCTRL+DEL",
                     _("Deletes the entire word after the cursor"));
            MyAppend(editmenu, A_BACKSPACE_WORD, _("Delete Word Before") + "\tCTRL+BACK",
                     _("Deletes the entire word before the cursor"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_ENTERGRID,
                     #ifdef __WXMAC__
                     _("&Insert New Grid") + "\tCTRL+G",
                     #else
                     _("&Insert New Grid") + "\tINS",
                     #endif
                     _("Adds a grid to the selected cell"));
            MyAppend(editmenu, A_ENTERGRIDN, _("Insert New &NxN Grid") + "\tCTRL+SHIFT+ENTER",
                     _("Adds a NxN grid to the selected cell"));
            MyAppend(editmenu, A_WRAP, _("&Wrap in new parent") + "\tF9",
                     _("Creates a new level of hierarchy around the current selection"));
            editmenu->AppendSeparator();
            // F10 is tied to the OS on both Ubuntu and OS X, and SHIFT+F10 is now right
            // click on all platforms?
            MyAppend(editmenu, A_FOLD,
                     #ifndef WIN32
                     _("Toggle Fold") + "\tCTRL+F10",
                     #else
                     _("Toggle Fold") + "\tF10",
                     #endif
                     _("Toggles showing the grid of the selected cell(s)"));
            MyAppend(editmenu, A_FOLDALL, _("Fold All") + "\tCTRL+SHIFT+F10",
                     _("Folds the grid of the selected cell(s) recursively"));
            MyAppend(editmenu, A_UNFOLDALL, _("Unfold All") + "\tCTRL+ALT+F10",
                     _("Unfolds the grid of the selected cell(s) recursively"));
            editmenu->AppendSeparator();
            editmenu->AppendSubMenu(selmenu, _("&Selection"));
            editmenu->AppendSubMenu(orgmenu, _("&Grid Reorganization"));
            editmenu->AppendSubMenu(laymenu, _("&Layout && Render Style"));
            editmenu->AppendSubMenu(imgmenu, _("&Images"));
            editmenu->AppendSubMenu(navmenu, _("&Browsing"));
            editmenu->AppendSubMenu(temenu, _("Text &Editing"));
            editmenu->AppendSubMenu(sizemenu, _("Text Sizing"));
            editmenu->AppendSubMenu(stmenu, _("Text Style"));
            editmenu->AppendSubMenu(bordmenu, _("Borders"));
            editmenu->AppendSubMenu(tagmenu, _("Tag"));

            if (!twoeditmenus) editmenupopup = editmenu;
        }

        auto semenu = new wxMenu();
        MyAppend(semenu, wxID_FIND, _("&Search") + "\tCTRL+F", _("Find in document"));
        semenu->AppendCheckItem(A_CASESENSITIVESEARCH, _("Case-sensitive search"));
        semenu->Check(A_CASESENSITIVESEARCH, sys->casesensitivesearch);
        semenu->AppendSeparator();
        MyAppend(semenu, A_SEARCHNEXT, _("&Next Match") + "\tF3", _("Go to next search match"));
        MyAppend(semenu, A_SEARCHPREV, _("&Previous Match") + "\tSHIFT+F3",
                 _("Go to previous search match"));
        semenu->AppendSeparator();
        MyAppend(semenu, wxID_REPLACE, _("&Replace") + "\tCTRL+H",
                 _("Find and replace in document"));
        MyAppend(semenu, A_REPLACEONCE, _("Replace in Current &Selection") + "\tCTRL+K");
        MyAppend(semenu, A_REPLACEONCEJ,
                 _("Replace in Current Selection && &Jump Next") + "\tCTRL+J");
        MyAppend(semenu, A_REPLACEALL, _("Replace &All"));

        auto scrollmenu = new wxMenu();
        MyAppend(scrollmenu, A_AUP, _("Scroll Up (mousewheel)") + "\tPGUP");
        MyAppend(scrollmenu, A_AUP, _("Scroll Up (mousewheel)") + "\tALT+UP");
        MyAppend(scrollmenu, A_ADOWN, _("Scroll Down (mousewheel)") + "\tPGDN");
        MyAppend(scrollmenu, A_ADOWN, _("Scroll Down (mousewheel)") + "\tALT+DOWN");
        MyAppend(scrollmenu, A_ALEFT, _("Scroll Left") + "\tALT+LEFT");
        MyAppend(scrollmenu, A_ARIGHT, _("Scroll Right") + "\tALT+RIGHT");

        auto filtermenu = new wxMenu();
        MyAppend(filtermenu, A_FILTEROFF, _("Turn filter &off") + "\tCTRL+SHIFT+F");
        MyAppend(filtermenu, A_FILTERS, _("Show only cells in current search"));
        MyAppend(filtermenu, A_FILTERRANGE, _("Show last edits in specific date range"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER5, _("Show 5% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER10, _("Show 10% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER20, _("Show 20% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER50, _("Show 50% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTERM, _("Show 1% more than the last filter"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTERL, _("Show 1% less than the last filter"));
        MyAppend(filtermenu, A_FILTERNOTE, _("Show cells with notes"));
        MyAppend(filtermenu, A_FILTERBYCELLBG, _("Filter by the same cell color"));
        MyAppend(filtermenu, A_FILTERMATCHNEXT, _("Go to next filter match") + "\tCTRL+F3");

        auto viewmenu = new wxMenu();
        MyAppend(viewmenu, A_ZOOMIN, _("Zoom &In (CTRL+mousewheel)") + "\tCTRL+PGUP");
        MyAppend(viewmenu, A_ZOOMOUT, _("Zoom &Out (CTRL+mousewheel)") + "\tCTRL+PGDN");
        viewmenu->AppendSeparator();
        MyAppend(viewmenu, A_EXPLORER, _("Explorer") + "\tCTRL+SHIFT+E",
                 _("Toggle the file Explorer panel"));
        MyAppend(viewmenu, A_EXPLORERSEARCH, _("Search Explorer") + "\tCTRL+ALT+E",
                 _("Focus the Explorer search box"));
        MyAppend(viewmenu, A_EXPLORERROOT, _("Open Explorer Folder..."),
                 _("Choose the folder shown in the Explorer"));
        MyAppend(viewmenu, A_EXPLORERREVEALACTIVE, _("Reveal Active File in Explorer"));
        MyAppend(viewmenu, A_EXPLORERREFRESH, _("Refresh Explorer") + "\tF5");
        MyAppend(viewmenu, A_EXPLORERCOLLAPSEALL, _("Collapse Explorer Folders"));
        auto explorerautohideitem =
            viewmenu->AppendCheckItem(A_EXPLORERAUTOHIDE, _("Auto-hide Explorer"),
                                      _("Hide the Explorer after opening a file"));
        explorerautohideitem->Check(explorerautohide);
        viewmenu->AppendSeparator();
        MyAppend(
            viewmenu, A_NEXTFILE,
            _("&Next tab")
                 #ifndef __WXGTK__
                    // On Linux, this conflicts with CTRL+I, see Document::Key()
                    // CTRL+SHIFT+TAB below still works, so that will have to be used to switch tabs.
                     + "\tCTRL+TAB"
                 #endif
            ,
            _("Go to the document in the next tab"));
        MyAppend(viewmenu, A_PREVFILE, _("Previous tab") + "\tCTRL+SHIFT+TAB",
                 _("Go to the document in the previous tab"));
        viewmenu->AppendSeparator();
        MyAppend(viewmenu, A_FULLSCREEN,
                 #ifdef __WXMAC__
                 _("Toggle &Fullscreen View") + "\tCTRL+F11");
                 #else
                 _("Toggle &Fullscreen View") + "\tF11");
                 #endif
        MyAppend(viewmenu, A_SCALED,
                 #ifdef __WXMAC__
                 _("Toggle &Scaled Presentation View") + "\tCTRL+F12");
                 #else
                 _("Toggle &Scaled Presentation View") + "\tF12");
                 #endif
        viewmenu->AppendSeparator();
        viewmenu->AppendSubMenu(scrollmenu, _("Scroll Sheet"));
        viewmenu->AppendSubMenu(filtermenu, _("Filter"));

        auto roundmenu = new wxMenu();
        roundmenu->AppendRadioItem(A_ROUND0, _("Radius &0"));
        roundmenu->AppendRadioItem(A_ROUND1, _("Radius &1"));
        roundmenu->AppendRadioItem(A_ROUND2, _("Radius &2"));
        roundmenu->AppendRadioItem(A_ROUND3, _("Radius &3"));
        roundmenu->AppendRadioItem(A_ROUND4, _("Radius &4"));
        roundmenu->AppendRadioItem(A_ROUND5, _("Radius &5"));
        roundmenu->AppendRadioItem(A_ROUND6, _("Radius &6"));
        roundmenu->Check(sys->roundness + A_ROUND0, true);

        auto autoexportmenu = new wxMenu();
        autoexportmenu->AppendRadioItem(A_AUTOEXPORT_HTML_NONE, _("No autoexport"));
        autoexportmenu->AppendRadioItem(A_AUTOEXPORT_HTML_WITH_IMAGES, _("Export with images"),
                                        _("Export to a HTML file with exported images alongside "
                                          "the original TreeSheets file when document is saved"));
        autoexportmenu->AppendRadioItem(A_AUTOEXPORT_HTML_WITHOUT_IMAGES,
                                        _("Export without images"),
                                        _("Export to a HTML file alongside the original "
                                          "TreeSheets file when document is saved"));
        autoexportmenu->Check(sys->autohtmlexport + A_AUTOEXPORT_HTML_NONE, true);

        auto optmenu = new wxMenu();
        MyAppend(optmenu, wxID_SELECT_FONT, _("Font..."),
                 _("Set the font the document text is displayed with"));
        MyAppend(optmenu, A_SET_FIXED_FONT, _("Typewriter font..."),
                 _("Set the font the typewriter text is displayed with."));
        MyAppend(optmenu, A_CUSTKEY, _("Key bindings..."),
                 _("Change the key binding of a menu item"));
        MyAppend(optmenu, A_SETLANG, _("Change language..."), _("Change interface language"));
        MyAppend(optmenu, A_DEFAULTMAXCOLWIDTH, _("Default column width..."),
                 _("Set the default column width for a new grid"));
        optmenu->AppendSeparator();
        MyAppend(optmenu, A_CUSTCOL, _("Custom &color..."),
                 _("Set a custom color for the color dropdowns"));
        MyAppend(
            optmenu, A_COLCELL, _("&Set custom color from cell background"),
            _("Set a custom color for the color dropdowns from the selected cell background"));
        MyAppend(optmenu, A_DEFBGCOL, _("Background color..."),
                 _("Set the color for the document background"));
        MyAppend(optmenu, A_DEFCURCOL, _("Cu&rsor color..."),
                 _("Set the color for the text cursor"));
        optmenu->AppendSeparator();
        MyAppend(optmenu, A_RESETPERSPECTIVE, _("Reset toolbar"),
                 _("Reset the toolbar appearance"));
        MyAppend(optmenu, A_CUSTOMIZETOOLBAR, _("Customize toolbar..."),
                 _("Choose which toolbar sections are shown"));
        optmenu->AppendCheckItem(
            A_SHOWTBAR, _("Toolbar"),
            _("Toggle whether toolbar is shown between menu bar and documents"));
        optmenu->Check(A_SHOWTBAR, sys->showtoolbar);
        optmenu->AppendCheckItem(A_SHOWSBAR, _("Statusbar"),
                                 _("Toggle whether statusbar is shown below the documents"));
        optmenu->Check(A_SHOWSBAR, sys->showstatusbar);
        optmenu->AppendCheckItem(
            A_LEFTTABS, _("File Tabs on the bottom"),
            _("Toggle whether file tabs are shown on top or on bottom of the documents"));
        optmenu->Check(A_LEFTTABS, lefttabs);
        optmenu->AppendCheckItem(A_TOTRAY, _("Minimize to tray"),
                                 _("Toogle whether window is minimized to system tray"));
        optmenu->Check(A_TOTRAY, sys->totray);
        optmenu->AppendCheckItem(A_MINCLOSE, _("Minimize on close"),
                                 _("Toggle whether the window is minimized instead of closed"));
        optmenu->Check(A_MINCLOSE, sys->minclose);
        optmenu->AppendCheckItem(
            A_SINGLETRAY, _("Single click maximize from tray"),
            _("Toggle whether only one click is required to maximize from system tray"));
        optmenu->Check(A_SINGLETRAY, sys->singletray);
        optmenu->AppendCheckItem(A_STARTMINIMIZED, _("Start minimized"),
                                 _("Start the application minimized"));
        optmenu->Check(A_STARTMINIMIZED, sys->startminimized);
        optmenu->AppendCheckItem(
            A_GLOBALSHOWHOTKEY,
            wxString::Format(_("Global show hotkey (%s)"), sys->globalshowhotkey),
            _("Toggle whether the global hotkey shows the TreeSheets window"));
        optmenu->Check(A_GLOBALSHOWHOTKEY, sys->globalshowhotkeyenabled);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_ZOOMSCR, _("Swap mousewheel scrolling and zooming"));
        optmenu->Check(A_ZOOMSCR, sys->zoomscroll);
        optmenu->AppendCheckItem(A_THINSELC, _("Navigate in between cells with cursor keys"),
                                 _("Toggle whether the cursor keys are used for navigation in addition to text editing"));
        optmenu->Check(A_THINSELC, sys->thinselc);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_MAKEBAKS, _("Backup files"),
                                 _("Create backup file before document is saved to file"));
        optmenu->Check(A_MAKEBAKS, sys->makebaks);
        optmenu->AppendCheckItem(A_AUTOSAVE, _("Autosave"),
                                 _("Save open documents periodically to temporary files"));
        optmenu->Check(A_AUTOSAVE, sys->autosave);
        optmenu->AppendCheckItem(
            A_FSWATCH, _("Autoreload documents"),
            _("Reload when another computer has changed a file (if you have made changes, asks)"));
        optmenu->Check(A_FSWATCH, sys->fswatch);
        optmenu->AppendSubMenu(autoexportmenu, _("Autoexport to HTML"));
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(
            A_CENTERED, _("Render document centered"),
            _("Toggle whether documents are rendered centered or left aligned"));
        optmenu->Check(A_CENTERED, sys->centered);
        optmenu->AppendCheckItem(
            A_FASTRENDER, _("Faster line rendering"),
            _("Toggle whether lines are drawn solid (faster rendering) or dashed"));
        optmenu->Check(A_FASTRENDER, sys->fastrender);
        optmenu->AppendCheckItem(A_INVERTRENDER, _("Invert in dark mode"),
                                 _("Invert the document in dark mode"));
        optmenu->Check(A_INVERTRENDER, sys->followdarkmode);
        optmenu->AppendSubMenu(roundmenu, _("&Roundness of grid borders"));

        #ifdef ENABLE_LOBSTER
            auto scriptmenu = new wxMenu();
            MyAppend(scriptmenu, A_ADDSCRIPT, _("Add...") + "\tCTRL+ALT+L",
                     _("Add Lobster scripts to the menu"));
            MyAppend(scriptmenu, A_DETSCRIPT, _("Remove...") + "\tCTRL+SHIFT+ALT+L",
                     _("Remove script from list in the menu"));
            scripts.UseMenu(scriptmenu);
            scripts.SetMenuPathStyle(wxFH_PATH_SHOW_NEVER);
            scripts.AddFilesToMenu();

            auto scriptpath = app->GetDataPath("scripts/");
            auto sf = wxFindFirstFile(scriptpath + "*.lobster");
            int sidx = 0;
            while (!sf.empty()) {
                auto fn = wxFileName::FileName(sf).GetFullName();
                scripts.AddFileToHistory(fn);
                sf = wxFindNextFile();
            }
        #endif

        auto markmenu = new wxMenu();
        MyAppend(markmenu, A_MARKDATA, _("&Data") + "\tCTRL+ALT+D");
        MyAppend(markmenu, A_MARKCODE, _("&Operation") + "\tCTRL+ALT+O");
        MyAppend(markmenu, A_MARKVARD, _("Variable &Assign") + "\tCTRL+ALT+A");
        MyAppend(markmenu, A_MARKVARU, _("Variable &Read") + "\tCTRL+ALT+R");
        MyAppend(markmenu, A_MARKVIEWH, _("&Horizontal View") + "\tCTRL+ALT+.");
        MyAppend(markmenu, A_MARKVIEWV, _("&Vertical View") + "\tCTRL+ALT+,");

        auto langmenu = new wxMenu();
        MyAppend(langmenu, wxID_EXECUTE, _("&Run") + "\tCTRL+ALT+F5");
        langmenu->AppendSubMenu(markmenu, _("&Mark as"));
        MyAppend(langmenu, A_CLRVIEW, _("&Clear Views"));

        auto helpmenu = new wxMenu();
        MyAppend(helpmenu, wxID_ABOUT, _("&About..."), _("Show About dialog"));
        helpmenu->AppendSeparator();
        MyAppend(helpmenu, wxID_HELP, _("Interactive &tutorial") + "\tF1",
                 _("Load an interactive tutorial in TreeSheets"));
        MyAppend(helpmenu, A_HELP_OP_REF, _("Operation reference") + "\tCTRL+ALT+F1",
                 _("Load an interactive program operation reference in TreeSheets"));
        helpmenu->AppendSeparator();
        MyAppend(helpmenu, A_TUTORIALWEBPAGE, _("Tutorial &web page"),
                 _("Open the tutorial web page in browser"));
        #ifdef ENABLE_LOBSTER
            MyAppend(helpmenu, A_SCRIPTREFERENCE, _("&Script reference"),
                 _("Open the Lobster script reference in browser"));
        #endif

        wxAcceleratorEntry entries[3];
        entries[0].Set(wxACCEL_SHIFT, WXK_DELETE, wxID_CUT);
        entries[1].Set(wxACCEL_SHIFT, WXK_INSERT, wxID_PASTE);
        entries[2].Set(wxACCEL_CTRL, WXK_INSERT, wxID_COPY);
        wxAcceleratorTable accel(3, entries);
        SetAcceleratorTable(accel);

        auto menubar = new wxMenuBar();
        menubar->Append(filemenu, _("&File"));
        menubar->Append(editmenu, _("&Edit"));
        menubar->Append(semenu, _("&Search"));
        menubar->Append(viewmenu, _("&View"));
        menubar->Append(optmenu, _("&Options"));
        #ifdef ENABLE_LOBSTER
            menubar->Append(scriptmenu, _("S&cript"));
        #endif
        menubar->Append(langmenu, _("&Program"));
        menubar->Append(helpmenu,
                        #ifdef __WXMAC__
                        wxApp::s_macHelpMenuTitleName  // so merges with osx provided help
                        #else
                        _("&Help")
                        #endif
                        );
        #ifdef __WXMAC__
        // these don't seem to work anymore in the newer wxWidgets, handled in the menu event
        // handler below instead
        wxApp::s_macAboutMenuItemId = wxID_ABOUT;
        wxApp::s_macExitMenuItemId = wxID_EXIT;
        wxApp::s_macPreferencesMenuItemId =
            wxID_SELECT_FONT;  // we have no prefs, so for now just select the font
        #endif
        SetMenuBar(menubar);

        auto sb = CreateStatusBar(5);
        SetStatusBarPane(0);
        SetDPIAwareStatusWidths();
        sb->Show(sys->showstatusbar);

        notebook =
            new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxAUI_NB_TAB_MOVE | wxAUI_NB_TAB_SPLIT | wxAUI_NB_SCROLL_BUTTONS |
                                  wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_CLOSE_ON_ALL_TABS |
                                  (lefttabs ? wxAUI_NB_BOTTOM : wxAUI_NB_TOP));

        int display_id = wxDisplay::GetFromWindow(this);
        wxRect disprect = wxDisplay(display_id == wxNOT_FOUND ? 0 : display_id).GetClientArea();
        const int boundary = 64;
        const int defx = disprect.width - 2 * boundary;
        const int defy = disprect.height - 2 * boundary;
        int resx, resy, posx, posy;
        sys->cfg->Read("resx", &resx, defx);
        sys->cfg->Read("resy", &resy, defy);
        sys->cfg->Read("posx", &posx, boundary + disprect.x);
        sys->cfg->Read("posy", &posy, boundary + disprect.y);
        #ifndef __WXGTK__
        // On X11, disprect only refers to the primary screen. Thus, for a multi-head display,
        // the conditions below might be fulfilled (e.g. large window spanning multiple screens
        // or being on the secondary screen), so just ignore them.
        if (resx > disprect.width || resy > disprect.height || posx < disprect.x ||
            posy < disprect.y || posx + resx > disprect.width + disprect.x ||
            posy + resy > disprect.height + disprect.y) {
            // Screen res has been resized since we last ran, set sizes to default to avoid being
            // off-screen.
            resx = defx;
            resy = defy;
            posx = posy = boundary;
            posx += disprect.x;
            posy += disprect.y;
        }
        #endif
        SetSize(resx, resy);
        Move(posx, posy);

        bool ismax;
        sys->cfg->Read("maximized", &ismax, true);

        CreateExplorerPane();
        aui.AddPane(
            explorerpane,
            wxAuiPaneInfo()
                .Name("explorer")
                .Caption(_("Explorer"))
                .Left()
                .Layer(1)
                .Position(0)
                .BestSize(FromDIP(wxSize(280, 500)))
                .MinSize(FromDIP(wxSize(220, 180)))
                .CloseButton(true)
                .MinimizeButton(true)
                .MaximizeButton(true));
        aui.AddPane(
            notebook,
            wxAuiPaneInfo().Name("notebook").Caption("Notebook").CenterPane().PaneBorder(false));
        RefreshToolBar();
        LoadSavedPerspective();
        aui.Update();

        Show(!IsIconized());

        // needs to be after Show() to avoid scrollbars rendered in the wrong place?
        if (ismax && !IsIconized()) Maximize(true);

        if (sys->startminimized)
            #ifdef __WXGTK__
                CallAfter([this]() { Iconize(true); });
            #else
                Iconize(true);
            #endif

        RegisterGlobalShowHotKey();

        SetFileAssoc(app->exename);

        wxSafeYield();
    }

    wxString ReadExplorerRoot() {
        auto root = sys->cfg->Read("explorerroot", wxGetCwd());
        if (!wxDirExists(root)) root = wxGetCwd();
        return NormalizeExplorerDir(root);
    }

    void CreateExplorerPane() {
        explorerroot = ReadExplorerRoot();
        explorerpane = new wxPanel(this, wxID_ANY);

        auto sizer = new wxBoxSizer(wxVERTICAL);
        auto header = new wxBoxSizer(wxHORIZONTAL);
        auto title = new wxStaticText(explorerpane, wxID_ANY, _("Explorer"));
        auto titlefont = title->GetFont();
        titlefont.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(titlefont);
        header->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

        auto newfile = new wxButton(explorerpane, wxID_ANY, _("File+"), wxDefaultPosition,
                                    wxDefaultSize, wxBU_EXACTFIT);
        auto newfolder = new wxButton(explorerpane, wxID_ANY, _("Folder+"), wxDefaultPosition,
                                      wxDefaultSize, wxBU_EXACTFIT);
        auto refresh = new wxButton(explorerpane, wxID_ANY, _("Refresh"), wxDefaultPosition,
                                    wxDefaultSize, wxBU_EXACTFIT);
        auto openfolder = new wxButton(explorerpane, wxID_ANY, _("Open..."),
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        header->Add(newfile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        header->Add(newfolder, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        header->Add(refresh, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        header->Add(openfolder, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        sizer->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(6));

        explorersearch = new wxSearchCtrl(explorerpane, A_EXPLORERSEARCH, wxEmptyString,
                                          wxDefaultPosition, wxDefaultSize,
                                          wxTE_PROCESS_ENTER);
        explorersearch->ShowCancelButton(true);
        explorersearch->SetDescriptiveText(_("Search files"));
        sizer->Add(explorersearch, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

        explorertree = new wxTreeCtrl(explorerpane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxTR_DEFAULT_STYLE | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
        explorerresults = new wxListBox(explorerpane, wxID_ANY);
        explorerresults->Hide();
        sizer->Add(explorertree, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
        sizer->Add(explorerresults, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
        explorerpane->SetSizer(sizer);
        explorerpane->SetToolTip(explorerroot);

        openfolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ChooseExplorerRoot(); });
        refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RefreshExplorerTree(); });
        newfile->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            explorercontextpath = ExplorerTargetDirectory();
            ExplorerNewFile();
        });
        newfolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            explorercontextpath = ExplorerTargetDirectory();
            ExplorerNewFolder();
        });
        explorersearch->Bind(wxEVT_TEXT, &TSFrame::OnExplorerSearch, this);
        explorersearch->Bind(wxEVT_TEXT_ENTER, &TSFrame::OnExplorerSearchEnter, this);
        explorersearch->Bind(wxEVT_KEY_DOWN, &TSFrame::OnExplorerSearchKeyDown, this);
        explorersearch->Bind(wxEVT_CHAR_HOOK, &TSFrame::OnExplorerSearchKeyDown, this);
        explorersearch->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) {
            explorersearch->Clear();
            RefreshExplorerSearch();
        });
        BuildExplorerTree();
        explorertree->Bind(wxEVT_TREE_ITEM_EXPANDING, &TSFrame::OnExplorerTreeExpanding, this);
        explorertree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &TSFrame::OnExplorerTreeActivated, this);
        explorertree->Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, &TSFrame::OnExplorerTreeContext, this);
        explorertree->Bind(wxEVT_KEY_DOWN, &TSFrame::OnExplorerKeyDown, this);
        explorerresults->Bind(wxEVT_LISTBOX_DCLICK, &TSFrame::OnExplorerResultActivated, this);
        explorerresults->Bind(wxEVT_CONTEXT_MENU, &TSFrame::OnExplorerResultsContext, this);
        explorerresults->Bind(wxEVT_KEY_DOWN, &TSFrame::OnExplorerKeyDown, this);
        explorerpane->Bind(wxEVT_CHAR_HOOK, &TSFrame::OnExplorerSearchKeyDown, this);
        explorerpane->Bind(wxEVT_CONTEXT_MENU, &TSFrame::OnExplorerPaneContext, this);
    }

    void SetExplorerRoot(const wxString &root) {
        if (!wxDirExists(root)) return;
        explorerroot = NormalizeExplorerDir(root);
        explorercontextpath.clear();
        sys->cfg->Write("explorerroot", explorerroot);
        BuildExplorerTree();
        if (explorerpane) explorerpane->SetToolTip(explorerroot);
        RefreshExplorerSearch();
        RefreshExplorerWatches();
    }

    void ChooseExplorerRoot() {
        wxDirDialog dialog(this, _("Choose Explorer folder"), explorerroot,
                           wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK) {
            if (explorersearch) explorersearch->Clear();
            SetExplorerRoot(dialog.GetPath());
            ShowExplorer();
        }
    }

    void ShowExplorer() {
        auto &pane = aui.GetPane("explorer");
        if (!pane.IsOk()) return;
        pane.Show(true);
        aui.Update();
    }

    void ToggleExplorer() {
        auto &pane = aui.GetPane("explorer");
        if (!pane.IsOk()) return;
        pane.Show(!pane.IsShown());
        aui.Update();
    }

    void HideExplorer() {
        auto &pane = aui.GetPane("explorer");
        if (!pane.IsOk()) return;
        pane.Show(false);
        aui.Update();
    }

    void FocusExplorerSearch() {
        ShowExplorer();
        if (!explorersearch) return;
        explorersearch->SetFocus();
        explorersearch->SelectAll();
    }

    void MaybeAutoHideExplorer() {
        if (explorerautohide) HideExplorer();
    }

    wxString NormalizeExplorerPath(const wxString &path) const {
        if (path.IsEmpty()) return wxEmptyString;
        wxFileName filename(path);
        filename.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_TILDE);
        return filename.GetFullPath();
    }

    wxString NormalizeExplorerDir(const wxString &path) const {
        if (path.IsEmpty()) return wxEmptyString;
        wxFileName filename = wxFileName::DirName(path);
        filename.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_TILDE);
        return filename.GetPath(wxPATH_GET_VOLUME);
    }

    bool PathIsInsideDirectory(const wxString &path, const wxString &directory) const {
        auto normalizedpath = NormalizeExplorerPath(path);
        auto normalizeddir = NormalizeExplorerDir(directory);
        if (normalizedpath.IsEmpty() || normalizeddir.IsEmpty()) return false;
        #ifdef __WXMSW__
            normalizedpath.MakeLower();
            normalizeddir.MakeLower();
        #endif
        if (normalizedpath == normalizeddir) return true;
        if (!normalizeddir.EndsWith(wxString(wxFILE_SEP_PATH))) normalizeddir += wxFILE_SEP_PATH;
        return normalizedpath.StartsWith(normalizeddir);
    }

    bool ExplorerPathIsInsideRoot(const wxString &path) const {
        return PathIsInsideDirectory(path, explorerroot);
    }

    bool ShouldSkipExplorerDir(const wxString &dirname) const {
        auto lower = dirname.Lower();
        return lower == ".git" || lower == ".hg" || lower == ".svn" || lower == ".vs" ||
               lower == "_build" || lower == "_install" || lower == "build" ||
               lower == "node_modules";
    }

    bool ExplorerPathIsSkipped(const wxString &path) const {
        if (path.IsEmpty()) return false;
        wxFileName relative(path);
        if (!relative.MakeRelativeTo(explorerroot)) return false;
        for (const auto &dir : relative.GetDirs()) {
            if (ShouldSkipExplorerDir(dir)) return true;
        }
        if (wxDirExists(path) && ShouldSkipExplorerDir(wxFileName(path).GetFullName()))
            return true;
        return false;
    }

    bool IsExplorerVisibleFile(const wxString &path) const {
        return wxFileName(path).GetExt().Lower() == "cts";
    }

    wxString ExplorerPathFromTreeItem(const wxTreeItemId &item) const {
        if (!explorertree || !item.IsOk()) return wxEmptyString;
        auto data = static_cast<ExplorerTreeItemData *>(explorertree->GetItemData(item));
        if (data) return data->path;
        return wxEmptyString;
    }

    bool ExplorerTreeItemIsDummy(const wxTreeItemId &item) const {
        return ExplorerPathFromTreeItem(item).IsEmpty();
    }

    bool ExplorerDirHasVisibleChildren(const wxString &directory) const {
        wxDir dir(directory);
        if (!dir.IsOpened()) return false;

        wxString entry;
        bool more = dir.GetFirst(&entry, wxEmptyString, wxDIR_FILES | wxDIR_DIRS);
        while (more) {
            wxFileName entrypath(directory, entry);
            auto fullpath = entrypath.GetFullPath();
            if (wxDirExists(fullpath)) {
                if (!ShouldSkipExplorerDir(entry)) return true;
            } else if (wxFileExists(fullpath) && IsExplorerVisibleFile(fullpath)) {
                return true;
            }
            more = dir.GetNext(&entry);
        }
        return false;
    }

    void WatchExplorerDirectory(const wxString &directory) {
        if (!watcher || !wxDirExists(directory)) return;
        auto normalized = NormalizeExplorerDir(directory);
        if (normalized.IsEmpty() || !explorerwatchedpaths.insert(normalized).second) return;
        watcher->Add(wxFileName::DirName(normalized), wxFSW_EVENT_ALL);
    }

    void WatchLoadedExplorerDirectories(const wxTreeItemId &item) {
        auto path = ExplorerPathFromTreeItem(item);
        if (wxDirExists(path)) WatchExplorerDirectory(path);
        if (!explorertree || !item.IsOk() || !explorertree->ItemHasChildren(item)) return;
        wxTreeItemIdValue cookie;
        auto child = explorertree->GetFirstChild(item, cookie);
        while (child.IsOk()) {
            if (!ExplorerTreeItemIsDummy(child)) WatchLoadedExplorerDirectories(child);
            child = explorertree->GetNextChild(item, cookie);
        }
    }

    void RefreshExplorerWatches() {
        if (!watcher || !explorertree || !explorertree->GetRootItem().IsOk()) return;
        WatchLoadedExplorerDirectories(explorertree->GetRootItem());
    }

    wxTreeItemId AppendExplorerTreeItem(const wxTreeItemId &parent, const wxString &path) {
        wxFileName filename(path);
        auto label = filename.GetFullName();
        if (label.IsEmpty()) label = path;

        wxTreeItemId item;
        if (parent.IsOk())
            item = explorertree->AppendItem(parent, label, -1, -1, new ExplorerTreeItemData(path));
        else
            item = explorertree->AddRoot(label, -1, -1, new ExplorerTreeItemData(path));

        if (wxDirExists(path) && ExplorerDirHasVisibleChildren(path))
            explorertree->AppendItem(item, "...", -1, -1, new ExplorerTreeItemData(wxEmptyString));
        return item;
    }

    void LoadExplorerTreeChildren(const wxTreeItemId &item) {
        if (!explorertree || !item.IsOk()) return;
        auto directory = ExplorerPathFromTreeItem(item);
        if (directory.IsEmpty() || !wxDirExists(directory)) return;
        WatchExplorerDirectory(directory);

        explorertree->DeleteChildren(item);
        wxDir dir(directory);
        if (!dir.IsOpened()) return;

        vector<wxString> directories;
        vector<wxString> files;
        wxString entry;
        bool more = dir.GetFirst(&entry, wxEmptyString, wxDIR_FILES | wxDIR_DIRS);
        while (more) {
            wxFileName entrypath(directory, entry);
            auto fullpath = entrypath.GetFullPath();
            if (wxDirExists(fullpath)) {
                if (!ShouldSkipExplorerDir(entry)) directories.push_back(fullpath);
            } else if (wxFileExists(fullpath) && IsExplorerVisibleFile(fullpath)) {
                files.push_back(fullpath);
            }
            more = dir.GetNext(&entry);
        }

        auto sortpaths = [](const wxString &a, const wxString &b) {
            return wxFileName(a).GetFullName().CmpNoCase(wxFileName(b).GetFullName()) < 0;
        };
        sort(directories.begin(), directories.end(), sortpaths);
        sort(files.begin(), files.end(), sortpaths);

        for (const auto &path : directories) AppendExplorerTreeItem(item, path);
        for (const auto &path : files) AppendExplorerTreeItem(item, path);
    }

    bool ExplorerTreeHasDummyChild(const wxTreeItemId &item) const {
        if (!explorertree || !item.IsOk() || !explorertree->ItemHasChildren(item)) return false;
        wxTreeItemIdValue cookie;
        auto child = explorertree->GetFirstChild(item, cookie);
        return child.IsOk() && ExplorerTreeItemIsDummy(child);
    }

    void CollectExpandedExplorerPaths(const wxTreeItemId &item, std::set<wxString> &paths) const {
        if (!explorertree || !item.IsOk()) return;
        auto path = ExplorerPathFromTreeItem(item);
        if (!path.IsEmpty() && explorertree->IsExpanded(item)) paths.insert(NormalizeExplorerPath(path));
        if (!explorertree->ItemHasChildren(item)) return;

        wxTreeItemIdValue cookie;
        auto child = explorertree->GetFirstChild(item, cookie);
        while (child.IsOk()) {
            if (!ExplorerTreeItemIsDummy(child)) CollectExpandedExplorerPaths(child, paths);
            child = explorertree->GetNextChild(item, cookie);
        }
    }

    wxTreeItemId FindExplorerTreeItemByPath(const wxTreeItemId &item,
                                            const wxString &path) const {
        if (!explorertree || !item.IsOk() || path.IsEmpty()) return wxTreeItemId();
        auto itempath = ExplorerPathFromTreeItem(item);
        if (!itempath.IsEmpty() && NormalizeExplorerPath(itempath) == NormalizeExplorerPath(path))
            return item;
        if (!explorertree->ItemHasChildren(item)) return wxTreeItemId();

        wxTreeItemIdValue cookie;
        auto child = explorertree->GetFirstChild(item, cookie);
        while (child.IsOk()) {
            if (!ExplorerTreeItemIsDummy(child)) {
                auto found = FindExplorerTreeItemByPath(child, path);
                if (found.IsOk()) return found;
            }
            child = explorertree->GetNextChild(item, cookie);
        }
        return wxTreeItemId();
    }

    void RestoreExplorerExpansion(const wxTreeItemId &item, const std::set<wxString> &paths) {
        if (!explorertree || !item.IsOk()) return;
        auto path = ExplorerPathFromTreeItem(item);
        if (!path.IsEmpty() && paths.find(NormalizeExplorerPath(path)) != paths.end() &&
            wxDirExists(path)) {
            if (ExplorerTreeHasDummyChild(item)) LoadExplorerTreeChildren(item);
            explorertree->Expand(item);
        }
        if (!explorertree->ItemHasChildren(item)) return;

        wxTreeItemIdValue cookie;
        auto child = explorertree->GetFirstChild(item, cookie);
        while (child.IsOk()) {
            if (!ExplorerTreeItemIsDummy(child)) RestoreExplorerExpansion(child, paths);
            child = explorertree->GetNextChild(item, cookie);
        }
    }

    wxString ExplorerSelectedPath() const {
        if (explorerresults && explorerresults->IsShown()) {
            auto selection = explorerresults->GetSelection();
            if (selection >= 0 && static_cast<size_t>(selection) < explorerresultpaths.size())
                return explorerresultpaths[selection];
        }
        if (explorertree) return ExplorerPathFromTreeItem(explorertree->GetSelection());
        return wxEmptyString;
    }

    wxString ExplorerTargetDirectory() const {
        auto path = ExplorerSelectedPath();
        if (path.IsEmpty()) path = explorercontextpath;
        if (path.IsEmpty()) path = explorerroot;
        if (wxDirExists(path)) return path;
        return wxFileName(path).GetPath(wxPATH_GET_VOLUME);
    }

    void BuildExplorerTree(const wxString &selectedpath = wxEmptyString,
                           const std::set<wxString> *expandedpaths = nullptr) {
        if (!explorertree) return;
        explorertree->DeleteAllItems();
        auto root = AppendExplorerTreeItem(wxTreeItemId(), explorerroot);
        LoadExplorerTreeChildren(root);
        explorertree->Expand(root);
        if (expandedpaths) RestoreExplorerExpansion(root, *expandedpaths);
        auto path = selectedpath.IsEmpty() ? explorerroot : selectedpath;
        auto selected = FindExplorerTreeItemByPath(root, path);
        explorertree->SelectItem(selected.IsOk() ? selected : root);
        RefreshExplorerWatches();
    }

    void RefreshExplorerTree(const wxString &selectedpath = wxEmptyString) {
        if (!explorertree) return;
        std::set<wxString> expandedpaths;
        auto root = explorertree->GetRootItem();
        if (root.IsOk()) CollectExpandedExplorerPaths(root, expandedpaths);
        expandedpaths.insert(NormalizeExplorerPath(explorerroot));
        auto path = selectedpath.IsEmpty() ? ExplorerSelectedPath() : selectedpath;
        BuildExplorerTree(path, &expandedpaths);
        RefreshExplorerSearch();
    }

    wxString ExplorerDisplayPath(const wxString &path) const {
        wxFileName filename(path);
        if (filename.MakeRelativeTo(explorerroot)) return filename.GetFullPath();
        return path;
    }

    static ::pinyin::Char LowerExplorerSearchChar(::pinyin::Char ch) {
        if (ch >= U'A' && ch <= U'Z') return ch - U'A' + U'a';
        return ch;
    }

    static bool IsExplorerSearchLiteralChar(::pinyin::Char ch) {
        return (ch >= U'a' && ch <= U'z') || (ch >= U'0' && ch <= U'9');
    }

    static void AppendPinyinView(::pinyin::String &target, ::pinyin::StringView value) {
        target.append(value.begin(), value.end());
    }

    static bool ContainsPinyinText(const ::pinyin::String &haystack,
                                   const ::pinyin::String &needle) {
        return !needle.empty() && haystack.find(needle) != ::pinyin::String::npos;
    }

    static ::pinyin::String SearchTokenToPinyinText(const wxString &text) {
        ::pinyin::String result;
        result.reserve(text.length());
        for (const auto &chref : text) {
            auto ch = static_cast<::pinyin::Char>(chref.GetValue());
            result.push_back(LowerExplorerSearchChar(ch));
        }
        return result;
    }

    static void EnsureExplorerPinyinReady() {
        static once_flag initialized;
        call_once(initialized, [] {
            ::pinyin::init(::pinyin::PinyinFlag::PinyinAscii |
                           ::pinyin::PinyinFlag::InitialLetter);
        });
    }

    template<class Visitor>
    static bool VisitExplorerPinyinCandidates(uint16_t index, const Visitor &visitor) {
        constexpr auto basecount = sizeof(::pinyin::pinyins) / sizeof(::pinyin::pinyins[0]);
        if (index < basecount) {
            visitor(::pinyin::pinyins[index]);
            return true;
        }

        auto combinationindex = static_cast<size_t>(index) - basecount;
        constexpr auto combinationcount =
            sizeof(::pinyin::pinyin_combinations) / sizeof(::pinyin::pinyin_combinations[0]);
        if (combinationindex >= combinationcount) return false;

        bool visited = false;
        const auto &combination = ::pinyin::pinyin_combinations[combinationindex];
        for (uint16_t i = 0; i < combination.n; i++) {
            auto pinyinindex = combination.pinyin[i];
            if (pinyinindex < basecount) {
                visitor(::pinyin::pinyins[pinyinindex]);
                visited = true;
            }
        }
        return visited;
    }

    ExplorerSearchText BuildExplorerSearchText(const wxString &displaypath) const {
        EnsureExplorerPinyinReady();

        ExplorerSearchText result;
        result.text = displaypath.Lower();
        result.pinyin.reserve(displaypath.length() * 4);
        result.initials.reserve(displaypath.length());
        result.alternatives.reserve(displaypath.length() * 4);

        for (const auto &chref : displaypath) {
            auto ch = static_cast<::pinyin::Char>(chref.GetValue());
            auto index = ::pinyin::get_pinyin_index(static_cast<char32_t>(ch));
            bool appendedprimary = false;

            if (index != 0xFFFF) {
                bool firstcandidate = true;
                VisitExplorerPinyinCandidates(index, [&](const ::pinyin::Pinyin &candidate) {
                    if (candidate.pinyin_ascii.empty()) return;

                    result.alternatives.push_back(U' ');
                    AppendPinyinView(result.alternatives, candidate.pinyin_ascii);
                    if (candidate.initial_letter != 0) {
                        result.alternatives.push_back(U' ');
                        result.alternatives.push_back(
                            LowerExplorerSearchChar(candidate.initial_letter));
                    }

                    if (firstcandidate) {
                        AppendPinyinView(result.pinyin, candidate.pinyin_ascii);
                        if (candidate.initial_letter != 0) {
                            result.initials.push_back(
                                LowerExplorerSearchChar(candidate.initial_letter));
                        }
                        appendedprimary = true;
                        firstcandidate = false;
                    }
                });
            }

            if (appendedprimary) continue;

            auto lower = LowerExplorerSearchChar(ch);
            if (IsExplorerSearchLiteralChar(lower)) {
                result.pinyin.push_back(lower);
                result.initials.push_back(lower);
            }
        }

        return result;
    }

    vector<ExplorerSearchToken> BuildExplorerSearchTokens(wxString query) const {
        query.Trim(true).Trim(false);
        auto parts = wxStringTokenize(query.Lower(), " \t\r\n", wxTOKEN_STRTOK);
        vector<ExplorerSearchToken> tokens;
        tokens.reserve(parts.size());
        for (auto &part : parts) {
            part.Trim(true).Trim(false);
            if (part.IsEmpty()) continue;
            tokens.push_back({part, SearchTokenToPinyinText(part)});
        }
        return tokens;
    }

    bool ExplorerPathMatchesSearch(const wxString &path,
                                   const vector<ExplorerSearchToken> &tokens) const {
        auto haystack = BuildExplorerSearchText(ExplorerDisplayPath(path));
        for (const auto &token : tokens) {
            auto matches = haystack.text.Find(token.text) != wxNOT_FOUND ||
                           ContainsPinyinText(haystack.pinyin, token.pinyin) ||
                           ContainsPinyinText(haystack.initials, token.pinyin) ||
                           ContainsPinyinText(haystack.alternatives, token.pinyin);
            if (!matches) return false;
        }
        return true;
    }

    void CollectExplorerMatches(const wxString &directory,
                                const vector<ExplorerSearchToken> &tokens,
                                size_t limit) {
        if (explorerresultpaths.size() >= limit) return;
        wxDir dir(directory);
        if (!dir.IsOpened()) return;

        wxString entry;
        bool more = dir.GetFirst(&entry, wxEmptyString, wxDIR_FILES | wxDIR_DIRS);
        while (more && explorerresultpaths.size() < limit) {
            wxFileName entrypath(directory, entry);
            auto fullpath = entrypath.GetFullPath();
            if (wxDirExists(fullpath)) {
                if (!ShouldSkipExplorerDir(entry)) CollectExplorerMatches(fullpath, tokens, limit);
            } else if (wxFileExists(fullpath) && IsExplorerVisibleFile(fullpath)) {
                if (ExplorerPathMatchesSearch(fullpath, tokens)) explorerresultpaths.push_back(fullpath);
            }
            more = dir.GetNext(&entry);
        }
    }

    void RefreshExplorerSearch() {
        if (!explorersearch || !explorertree || !explorerresults) return;

        auto tokens = BuildExplorerSearchTokens(explorersearch->GetValue());
        if (tokens.empty()) {
            explorerresultpaths.clear();
            explorerresults->Clear();
            explorerresults->Hide();
            explorertree->Show();
            explorerpane->Layout();
            return;
        }

        explorerresultpaths.clear();
        explorerresults->Clear();
        const size_t resultlimit = 1000;
        CollectExplorerMatches(explorerroot, tokens, resultlimit);
        sort(explorerresultpaths.begin(), explorerresultpaths.end(),
             [this](const wxString &a, const wxString &b) {
                 return ExplorerDisplayPath(a).CmpNoCase(ExplorerDisplayPath(b)) < 0;
             });

        wxArrayString labels;
        for (const auto &path : explorerresultpaths) labels.Add(ExplorerDisplayPath(path));
        if (labels.IsEmpty()) labels.Add(_("No files found"));
        if (explorerresultpaths.size() >= resultlimit) {
            labels.Add(wxString::Format(_("Showing first %d matches"),
                                        static_cast<int>(explorerresultpaths.size())));
        }
        explorerresults->Append(labels);
        if (!explorerresultpaths.empty()) {
            explorerresults->SetSelection(0);
            explorerresults->EnsureVisible(0);
        } else {
            explorerresults->SetSelection(wxNOT_FOUND);
        }
        explorertree->Hide();
        explorerresults->Show();
        explorerpane->Layout();
    }

    bool ExplorerSearchHasOpenableResult() const {
        return explorerresults && explorerresults->IsShown() && !explorerresultpaths.empty();
    }

    void MoveExplorerSearchSelection(int delta) {
        if (!ExplorerSearchHasOpenableResult()) return;
        int last = static_cast<int>(explorerresultpaths.size()) - 1;
        int selection = explorerresults->GetSelection();
        if (selection < 0 || selection > last) selection = delta < 0 ? last : 0;
        else selection = min(last, max(0, selection + delta));
        explorerresults->SetSelection(selection);
        explorerresults->EnsureVisible(selection);
    }

    bool OpenSelectedExplorerSearchResult() {
        if (!ExplorerSearchHasOpenableResult()) return false;
        int selection = explorerresults->GetSelection();
        if (selection < 0 || static_cast<size_t>(selection) >= explorerresultpaths.size())
            selection = 0;
        explorerresults->SetSelection(selection);
        OpenExplorerPath(explorerresultpaths[selection]);
        return true;
    }

    bool MoveExplorerTreeSelection(int delta) {
        if (!explorertree) return false;
        if (explorerresults && explorerresults->IsShown() && explorerresultpaths.empty()) {
            explorerresults->Hide();
            explorertree->Show();
            explorerpane->Layout();
        }

        auto selection = explorertree->GetSelection();
        if (!selection.IsOk()) selection = explorertree->GetRootItem();
        if (!selection.IsOk()) return false;

        auto next = delta < 0 ? explorertree->GetPrevVisible(selection)
                              : explorertree->GetNextVisible(selection);
        if (!next.IsOk()) return false;

        explorertree->SelectItem(next);
        explorertree->EnsureVisible(next);
        return true;
    }

    bool OpenExplorerSelectedPath() {
        auto path = ExplorerSelectedPath();
        if (path.IsEmpty()) return false;
        OpenExplorerPath(path);
        return true;
    }

    bool MoveExplorerSelectionFromSearch(int delta) {
        if (ExplorerSearchHasOpenableResult()) {
            MoveExplorerSearchSelection(delta);
            return true;
        }
        return MoveExplorerTreeSelection(delta);
    }

    bool OpenExplorerSelectionFromSearch() {
        if (OpenSelectedExplorerSearchResult()) return true;
        return OpenExplorerSelectedPath();
    }

    bool SelectExplorerTreePath(const wxString &path) {
        if (!explorertree || path.IsEmpty()) return false;
        auto root = explorertree->GetRootItem();
        if (!root.IsOk()) return false;

        auto target = NormalizeExplorerPath(path);
        auto parentpath = wxDirExists(path) ? target : NormalizeExplorerDir(
                                                    wxFileName(path).GetPath(wxPATH_GET_VOLUME));
        wxFileName relative(parentpath);
        if (!relative.MakeRelativeTo(explorerroot)) return false;

        auto item = root;
        for (const auto &part : relative.GetDirs()) {
            if (ExplorerTreeHasDummyChild(item)) LoadExplorerTreeChildren(item);
            explorertree->Expand(item);

            wxTreeItemIdValue cookie;
            auto child = explorertree->GetFirstChild(item, cookie);
            wxTreeItemId next;
            while (child.IsOk()) {
                auto childpath = ExplorerPathFromTreeItem(child);
                if (wxDirExists(childpath) && wxFileName(childpath).GetFullName() == part) {
                    next = child;
                    break;
                }
                child = explorertree->GetNextChild(item, cookie);
            }
            if (!next.IsOk()) return false;
            item = next;
        }

        if (ExplorerTreeHasDummyChild(item)) LoadExplorerTreeChildren(item);
        auto targetitem = FindExplorerTreeItemByPath(item, target);
        if (!targetitem.IsOk()) targetitem = item;
        explorertree->SelectItem(targetitem);
        explorertree->EnsureVisible(targetitem);
        return true;
    }

    void RevealExplorerPath(const wxString &path) {
        if (path.IsEmpty()) return;
        auto normalized = NormalizeExplorerPath(path);
        if (!ExplorerPathIsInsideRoot(normalized)) {
            auto directory = wxDirExists(normalized) ? normalized
                                                     : wxFileName(normalized).GetPath(
                                                           wxPATH_GET_VOLUME);
            if (wxDirExists(directory)) SetExplorerRoot(directory);
        }
        if (!SelectExplorerTreePath(normalized)) {
            RefreshExplorerTree(normalized);
            SelectExplorerTreePath(normalized);
        }
        ShowExplorer();
    }

    void RevealCurrentDocumentInExplorer() {
        auto canvas = GetCurrentTab();
        if (!canvas || canvas->doc->filename.IsEmpty()) {
            SetStatus(_("Current document has not been saved yet."));
            return;
        }
        RevealExplorerPath(canvas->doc->filename);
    }

    void RevealCurrentDocumentInExplorerIfVisible() {
        auto &pane = aui.GetPane("explorer");
        auto canvas = GetCurrentTab();
        if (!pane.IsOk() || !pane.IsShown() || !canvas || canvas->doc->filename.IsEmpty() ||
            !explorersearch || !explorersearch->GetValue().IsEmpty() ||
            !ExplorerPathIsInsideRoot(canvas->doc->filename))
            return;
        SelectExplorerTreePath(canvas->doc->filename);
    }

    void CollapseExplorerChildren(const wxTreeItemId &item) {
        if (!explorertree || !item.IsOk() || !explorertree->ItemHasChildren(item)) return;
        wxTreeItemIdValue cookie;
        auto child = explorertree->GetFirstChild(item, cookie);
        while (child.IsOk()) {
            if (!ExplorerTreeItemIsDummy(child)) {
                CollapseExplorerChildren(child);
                if (wxDirExists(ExplorerPathFromTreeItem(child))) explorertree->Collapse(child);
            }
            child = explorertree->GetNextChild(item, cookie);
        }
    }

    void CollapseExplorerTree() {
        if (!explorertree) return;
        auto root = explorertree->GetRootItem();
        if (!root.IsOk()) return;
        CollapseExplorerChildren(root);
        explorertree->Expand(root);
        explorertree->EnsureVisible(root);
    }

    void OpenExplorerPath(const wxString &path) {
        if (path.IsEmpty()) return;
        if (wxDirExists(path)) {
            SetExplorerRoot(path);
            return;
        }
        if (!wxFileExists(path)) {
            SetStatus(_("File does not exist."));
            return;
        }

        wxFileName filename(path);
        auto fullpath = filename.GetFullPath();
        if (filename.GetExt().Lower() == "cts") {
            SetStatus(sys->Open(fullpath));
            MaybeAutoHideExplorer();
        } else if (!wxLaunchDefaultApplication(fullpath)) {
            SetStatus(_("File could not be opened."));
        } else {
            MaybeAutoHideExplorer();
        }
    }

    bool ExplorerNameIsValid(const wxString &name) const {
        if (name.IsEmpty() || name == "." || name == "..") return false;
        auto forbidden = wxFileName::GetForbiddenChars();
        for (auto c : forbidden) {
            if (name.Find(c) != wxNOT_FOUND) return false;
        }
        return true;
    }

    wxString PromptExplorerName(const wxString &message, const wxString &value) {
        wxTextEntryDialog dialog(this, message, _("Explorer"), value);
        if (dialog.ShowModal() != wxID_OK) return wxEmptyString;
        auto name = dialog.GetValue();
        name.Trim(true).Trim(false);
        if (!ExplorerNameIsValid(name)) {
            wxMessageBox(_("Please enter a valid file or folder name."), _("Explorer"), wxOK, this);
            return wxEmptyString;
        }
        return name;
    }

    wxString ExplorerChildPath(const wxString &directory, const wxString &name) const {
        wxFileName filename(directory, name);
        filename.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_TILDE);
        return filename.GetFullPath();
    }

    void RefreshExplorerAfterFileOperation(const wxString &path) {
        RefreshExplorerTree(path);
        if (explorersearch && !explorersearch->GetValue().IsEmpty()) RefreshExplorerSearch();
        if (!path.IsEmpty() && (wxFileExists(path) || wxDirExists(path))) RevealExplorerPath(path);
    }

    void ExplorerNewFile() {
        auto directory = ExplorerTargetDirectory();
        if (!wxDirExists(directory)) return;
        auto name = PromptExplorerName(_("New file name:"), "Untitled.cts");
        if (name.IsEmpty()) return;
        auto target = ExplorerChildPath(directory, name);
        wxFileName targetname(target);
        if (!targetname.HasExt()) {
            targetname.SetExt("cts");
            target = targetname.GetFullPath();
        }
        if (wxFileExists(target) || wxDirExists(target)) {
            wxMessageBox(_("A file or folder with that name already exists."), _("Explorer"), wxOK,
                         this);
            return;
        }

        if (targetname.GetExt().Lower() == "cts") {
            sys->InitDB(10);
            auto canvas = GetCurrentTab();
            canvas->doc->ChangeFileName(target, false);
            bool success = false;
            SetStatus(canvas->doc->SaveDB(&success));
            if (!success) return;
        } else {
            wxFFileOutputStream stream(target);
            if (!stream.IsOk()) {
                SetStatus(_("File could not be created."));
                return;
            }
        }
        RefreshExplorerAfterFileOperation(target);
    }

    void ExplorerNewFolder() {
        auto directory = ExplorerTargetDirectory();
        if (!wxDirExists(directory)) return;
        auto name = PromptExplorerName(_("New folder name:"), _("New Folder"));
        if (name.IsEmpty()) return;
        auto target = ExplorerChildPath(directory, name);
        if (wxFileExists(target) || wxDirExists(target)) {
            wxMessageBox(_("A file or folder with that name already exists."), _("Explorer"), wxOK,
                         this);
            return;
        }
        if (!wxMkdir(target)) {
            SetStatus(_("Folder could not be created."));
            return;
        }
        RefreshExplorerAfterFileOperation(target);
    }

    void UpdateOpenDocumentsAfterExplorerRename(const wxString &oldpath, const wxString &newpath) {
        if (!notebook) return;
        auto oldisdir = wxDirExists(newpath) || !wxFileExists(newpath);
        loop(i, notebook->GetPageCount()) {
            auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
            auto filename = canvas->doc->filename;
            if (filename.IsEmpty()) continue;
            wxString updated;
            if (NormalizeExplorerPath(filename) == NormalizeExplorerPath(oldpath)) {
                updated = newpath;
            } else if (oldisdir && PathIsInsideDirectory(filename, oldpath)) {
                wxFileName relative(filename);
                if (relative.MakeRelativeTo(oldpath)) {
                    wxFileName updatedname(newpath + wxFILE_SEP_PATH + relative.GetFullPath());
                    updatedname.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE |
                                          wxPATH_NORM_TILDE);
                    updated = updatedname.GetFullPath();
                }
            }
            if (!updated.IsEmpty()) canvas->doc->ChangeFileName(updated, false);
        }
    }

    void ExplorerRename() {
        auto path = explorercontextpath.IsEmpty() ? ExplorerSelectedPath() : explorercontextpath;
        if (path.IsEmpty() || NormalizeExplorerPath(path) == NormalizeExplorerDir(explorerroot)) return;
        if (!wxFileExists(path) && !wxDirExists(path)) {
            SetStatus(_("File or folder does not exist."));
            return;
        }
        wxFileName filename(path);
        auto name = PromptExplorerName(_("Rename to:"), filename.GetFullName());
        if (name.IsEmpty()) return;
        auto target = ExplorerChildPath(filename.GetPath(wxPATH_GET_VOLUME), name);
        if (NormalizeExplorerPath(path) == NormalizeExplorerPath(target)) return;
        if (wxFileExists(target) || wxDirExists(target)) {
            wxMessageBox(_("A file or folder with that name already exists."), _("Explorer"), wxOK,
                         this);
            return;
        }
        if (!wxRenameFile(path, target, false)) {
            SetStatus(_("File or folder could not be renamed."));
            return;
        }
        UpdateOpenDocumentsAfterExplorerRename(path, target);
        if (wxFileName(target).GetExt().Lower() == "cts") filehistory.AddFileToHistory(target);
        RefreshExplorerAfterFileOperation(target);
    }

    wxString ExplorerDuplicateName(const wxString &path) const {
        wxFileName filename(path);
        auto directory = filename.GetPath(wxPATH_GET_VOLUME);
        auto name = filename.GetName();
        auto ext = filename.GetExt();
        for (int i = 1; i < 1000; i++) {
            auto suffix = i == 1 ? wxString(" copy") : wxString::Format(" copy %d", i);
            wxFileName candidate(directory, name + suffix, ext);
            auto fullpath = candidate.GetFullPath();
            if (!wxFileExists(fullpath) && !wxDirExists(fullpath)) return fullpath;
        }
        return wxEmptyString;
    }

    void ExplorerDuplicate() {
        auto path = explorercontextpath.IsEmpty() ? ExplorerSelectedPath() : explorercontextpath;
        if (path.IsEmpty() || !wxFileExists(path)) return;
        auto target = ExplorerDuplicateName(path);
        if (target.IsEmpty() || !wxCopyFile(path, target, false)) {
            SetStatus(_("File could not be duplicated."));
            return;
        }
        RefreshExplorerAfterFileOperation(target);
    }

    void ExplorerDelete() {
        auto path = explorercontextpath.IsEmpty() ? ExplorerSelectedPath() : explorercontextpath;
        if (path.IsEmpty() || NormalizeExplorerPath(path) == NormalizeExplorerDir(explorerroot)) return;
        auto isdir = wxDirExists(path);
        if (!isdir && !wxFileExists(path)) {
            SetStatus(_("File or folder does not exist."));
            return;
        }
        auto message = isdir
                           ? wxString::Format(
                                 _("Delete folder \"%s\" and everything inside it?"),
                                 wxFileName(path).GetFullName())
                           : wxString::Format(_("Delete file \"%s\"?"),
                                              wxFileName(path).GetFullName());
        if (wxMessageBox(message, _("Delete"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) !=
            wxYES)
            return;

        bool ok = isdir ? wxFileName::Rmdir(path, wxPATH_RMDIR_RECURSIVE) : wxRemoveFile(path);
        if (!ok) {
            SetStatus(_("File or folder could not be deleted."));
            return;
        }
        RefreshExplorerAfterFileOperation(wxFileName(path).GetPath(wxPATH_GET_VOLUME));
    }

    void CopyExplorerPath(bool relative) {
        auto path = explorercontextpath.IsEmpty() ? ExplorerSelectedPath() : explorercontextpath;
        if (path.IsEmpty()) return;
        auto text = path;
        if (relative) {
            wxFileName filename(path);
            filename.MakeRelativeTo(explorerroot);
            text = filename.GetFullPath();
        }
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(text));
            wxTheClipboard->Close();
            SetStatus(_("Path copied."));
        }
    }

    void RevealExplorerPathInSystem(const wxString &path) {
        if (path.IsEmpty()) return;
        #ifdef __WXMSW__
            if (wxFileExists(path))
                wxExecute("explorer.exe /select,\"" + path + "\"", wxEXEC_ASYNC);
            else if (wxDirExists(path))
                wxExecute("explorer.exe \"" + path + "\"", wxEXEC_ASYNC);
        #elif defined(__WXMAC__)
            wxExecute("open -R \"" + path + "\"", wxEXEC_ASYNC);
        #else
            auto directory = wxDirExists(path) ? path : wxFileName(path).GetPath(wxPATH_GET_VOLUME);
            if (!wxLaunchDefaultApplication(directory))
                SetStatus(_("Folder could not be opened."));
        #endif
    }

    void PopupExplorerContextMenu(const wxString &path) {
        explorercontextpath = path.IsEmpty() ? explorerroot : path;
        auto isdir = wxDirExists(explorercontextpath);
        auto isfile = wxFileExists(explorercontextpath);
        auto isroot = NormalizeExplorerPath(explorercontextpath) == NormalizeExplorerDir(explorerroot);

        wxMenu menu;
        auto open = menu.Append(A_EXPLOREROPEN, isdir ? _("Use as Explorer Root") : _("Open"));
        open->Enable(isdir || isfile);
        auto reveal = menu.Append(A_EXPLORERREVEAL, _("Show in File Explorer"));
        reveal->Enable(isdir || isfile);
        menu.AppendSeparator();
        menu.Append(A_EXPLORERNEWFILE, _("New File..."));
        menu.Append(A_EXPLORERNEWFOLDER, _("New Folder..."));
        auto rename = menu.Append(A_EXPLORERRENAME, _("Rename...") + "\tF2");
        rename->Enable((isdir || isfile) && !isroot);
        auto duplicate = menu.Append(A_EXPLORERDUPLICATE, _("Duplicate File"));
        duplicate->Enable(isfile);
        auto del = menu.Append(A_EXPLORERDELETE, _("Delete") + "\tDel");
        del->Enable((isdir || isfile) && !isroot);
        menu.AppendSeparator();
        auto parent = menu.Append(A_EXPLOREROPENPARENT, _("Use Parent as Explorer Root"));
        parent->Enable(!isroot && (isdir || isfile));
        menu.AppendSeparator();
        menu.Append(A_EXPLORERCOPYPATH, _("Copy Path"));
        menu.Append(A_EXPLORERCOPYRELPATH, _("Copy Relative Path"));
        menu.AppendSeparator();
        menu.Append(A_EXPLORERREFRESH, _("Refresh") + "\tF5");
        menu.Append(A_EXPLORERCOLLAPSEALL, _("Collapse Folders"));
        auto autohide =
            menu.AppendCheckItem(A_EXPLORERAUTOHIDE, _("Auto-hide After Open"));
        autohide->Check(explorerautohide);
        menu.Append(A_EXPLORERHIDE, _("Hide Explorer"));
        PopupMenu(&menu);
    }

    bool ExplorerSearchHasFocus() const {
        auto focus = wxWindow::FindFocus();
        while (focus) {
            if (focus == explorersearch) return true;
            focus = focus->GetParent();
        }
        return false;
    }

    void OnExplorerSearch(wxCommandEvent &) { RefreshExplorerSearch(); }

    void OnExplorerSearchEnter(wxCommandEvent &) { OpenExplorerSelectionFromSearch(); }

    void OnExplorerSearchKeyDown(wxKeyEvent &event) {
        if (!ExplorerSearchHasFocus()) {
            event.Skip();
            return;
        }

        switch (event.GetKeyCode()) {
            case WXK_UP:
                if (MoveExplorerSelectionFromSearch(-1)) return;
                break;
            case WXK_DOWN:
                if (MoveExplorerSelectionFromSearch(1)) return;
                break;
            case WXK_RETURN:
            case WXK_NUMPAD_ENTER:
                if (OpenExplorerSelectionFromSearch()) return;
                break;
        }
        event.Skip();
    }

    void OnExplorerTreeExpanding(wxTreeEvent &event) {
        auto item = event.GetItem();
        if (ExplorerTreeHasDummyChild(item)) LoadExplorerTreeChildren(item);
        event.Skip();
    }

    void OnExplorerTreeActivated(wxTreeEvent &event) {
        auto item = event.GetItem();
        auto path = ExplorerPathFromTreeItem(item);
        if (path.IsEmpty()) return;
        if (wxDirExists(path)) {
            if (ExplorerTreeHasDummyChild(item)) LoadExplorerTreeChildren(item);
            if (explorertree->IsExpanded(item))
                explorertree->Collapse(item);
            else
                explorertree->Expand(item);
            return;
        }
        OpenExplorerPath(path);
    }

    void OnExplorerResultActivated(wxCommandEvent &event) {
        auto selection = event.GetSelection();
        if (selection >= 0 && static_cast<size_t>(selection) < explorerresultpaths.size())
            OpenExplorerPath(explorerresultpaths[selection]);
    }

    void OnExplorerTreeContext(wxTreeEvent &event) {
        auto item = event.GetItem();
        if (item.IsOk()) explorertree->SelectItem(item);
        PopupExplorerContextMenu(ExplorerPathFromTreeItem(item));
    }

    void OnExplorerResultsContext(wxContextMenuEvent &) {
        PopupExplorerContextMenu(ExplorerSelectedPath());
    }

    void OnExplorerPaneContext(wxContextMenuEvent &event) {
        if (event.GetEventObject() != explorerpane) return;
        PopupExplorerContextMenu(ExplorerSelectedPath());
    }

    void OnExplorerKeyDown(wxKeyEvent &event) {
        switch (event.GetKeyCode()) {
            case WXK_F5:
                RefreshExplorerTree();
                return;
            case WXK_F2:
                explorercontextpath = ExplorerSelectedPath();
                ExplorerRename();
                return;
            case WXK_DELETE:
                explorercontextpath = ExplorerSelectedPath();
                ExplorerDelete();
                return;
            case WXK_RETURN:
            case WXK_NUMPAD_ENTER:
                OpenExplorerPath(ExplorerSelectedPath());
                return;
            default:
                event.Skip();
                return;
        }
    }

    wxArrayString GetToolbarPaneNames() {
        wxArrayString toolbarNames;
        wxAuiPaneInfoArray &all_panes = aui.GetAllPanes();
        for (size_t i = 0; i < all_panes.GetCount(); ++i) {
            wxAuiPaneInfo &pane = all_panes.Item(i);
            if (pane.IsToolbar()) { toolbarNames.Add(pane.name); }
        }
        return toolbarNames;
    }

    void DestroyToolbarPane(const wxString &name) {
        wxAuiPaneInfo &pane = aui.GetPane(name);
        if (pane.IsOk()) {
            wxWindow *wnd = pane.window;
            aui.DetachPane(wnd);
            if (wnd) { wnd->Destroy(); }
        }
    }

    vector<pair<wxString, wxString>> ToolbarSectionDefinitions() const {
        return {{"filetb", _("File")},
                {"edittb", _("Edit")},
                {"viewtb", _("View")},
                {"browsetb", _("Browse")},
                {"structuretb", _("Structure")},
                {"rendertb", _("Render")},
                {"formattb", _("Text format")},
                {"cellcolortb", _("Cell color")},
                {"textcolortb", _("Text color")},
                {"bordercolortb", _("Border color")},
                {"imagetb", _("Image")},
                {"findtb", _("Search")},
                {"repltb", _("Replace")}};
    }

    std::set<wxString> HiddenToolbarSections() const {
        wxString hiddenconfig;
        sys->cfg->Read("toolbarhiddenpanes", &hiddenconfig, "");
        auto names = wxStringTokenize(hiddenconfig, "|", wxTOKEN_STRTOK);
        std::set<wxString> hidden;
        for (const auto &name : names) {
            if (!name.IsEmpty()) hidden.insert(name);
        }
        return hidden;
    }

    void ApplyToolbarVisibility() {
        auto hidden = HiddenToolbarSections();
        for (const auto &section : ToolbarSectionDefinitions()) {
            auto &pane = aui.GetPane(section.first);
            if (!pane.IsOk()) continue;
            pane.Show(sys->showtoolbar && !hidden.count(section.first));
        }
    }

    bool PerspectiveHasCurrentToolbarLayout(const wxString &perspective) const {
        if (perspective.IsEmpty()) return false;
        for (const auto &section : ToolbarSectionDefinitions()) {
            if (perspective.Find(section.first) == wxNOT_FOUND) return false;
        }
        return true;
    }

    void LoadSavedPerspective() {
        wxString perspective = sys->cfg->Read("perspective", "");
        int savedversion = 0;
        sys->cfg->Read("toolbarlayoutversion", &savedversion, 0);
        if (savedversion == toolbarlayoutversion &&
            PerspectiveHasCurrentToolbarLayout(perspective)) {
            aui.LoadPerspective(perspective);
        } else {
            sys->cfg->Write("perspective", "");
            sys->cfg->Write("toolbarhiddenpanes", "");
            sys->cfg->Write("toolbarlayoutversion", toolbarlayoutversion);
        }
        ApplyToolbarVisibility();
    }

    void RefreshToolBarKeepingPerspective() {
        auto perspective = aui.SavePerspective();
        RefreshToolBar();
        aui.LoadPerspective(perspective);
        ApplyToolbarVisibility();
    }

    wxString ToolbarTooltip(int action, const wxString &tooltip) const {
        auto it = menuaccelerators.find(action);
        if (it == menuaccelerators.end() || it->second.IsEmpty()) return tooltip;
        if (tooltip.Upper().Find(it->second.Upper()) != wxNOT_FOUND) return tooltip;
        return tooltip + "\n" + _("Shortcut: ") + it->second;
    }

    void CustomizeToolbar() {
        auto sections = ToolbarSectionDefinitions();
        auto hidden = HiddenToolbarSections();
        wxArrayString choices;
        wxArrayInt selections;
        for (size_t i = 0; i < sections.size(); i++) {
            choices.Add(sections[i].second);
            if (!hidden.count(sections[i].first)) selections.Add(static_cast<int>(i));
        }

        wxMultiChoiceDialog dialog(this, _("Select toolbar sections to show:"),
                                   _("Customize toolbar"), choices);
        dialog.SetSelections(selections);
        if (dialog.ShowModal() != wxID_OK) return;

        std::set<int> selected;
        auto selectedindices = dialog.GetSelections();
        for (size_t i = 0; i < selectedindices.GetCount(); i++) {
            selected.insert(selectedindices[i]);
        }

        wxString newhidden;
        for (size_t i = 0; i < sections.size(); i++) {
            if (selected.count(static_cast<int>(i))) continue;
            if (!newhidden.IsEmpty()) newhidden += "|";
            newhidden += sections[i].first;
        }
        sys->cfg->Write("toolbarhiddenpanes", newhidden);
        sys->cfg->Write("showtoolbar", sys->showtoolbar = true);
        if (GetMenuBar()) GetMenuBar()->Check(A_SHOWTBAR, true);
        ApplyToolbarVisibility();
        aui.Update();
    }

    void RefreshToolBar() {
        for (const auto &name : GetToolbarPaneNames()) { DestroyToolbarPane(name); }
        auto iconpath = app->GetDataPath("images/material/toolbar/");
        auto hiddentoolbarsections = HiddenToolbarSections();
        auto toolbarstyle = wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND;

        auto NewToolbar = [&]() {
            auto tb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       toolbarstyle);
            tb->SetToolBitmapSize(FromDIP(wxSize(24, 24)));
            return tb;
        };

        auto AddToolbarIcon = [&](wxAuiToolBar *tb, const wxChar *name, int action,
                                  wxString lighticon, wxString darkicon) {
            auto tooltip = ToolbarTooltip(action, name);
            tb->AddTool(
                action, name,
                wxBitmapBundle::FromSVGFile(
                    iconpath + (wxSystemSettings::GetAppearance().IsDark() ? darkicon : lighticon),
                    wxSize(24, 24)),
                tooltip, wxITEM_NORMAL);
        };

        auto MakeTextToolBitmap = [&](const wxString &label, bool bold = false,
                                      bool italic = false, bool underline = false,
                                      bool strikethrough = false) {
            const auto size = FromDIP(24);
            wxBitmap bitmap(size, size);
            wxMemoryDC dc(bitmap);
            auto bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
            auto fg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
            auto border = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
            dc.SetBackground(wxBrush(bg));
            dc.Clear();
            dc.SetPen(wxPen(border));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRoundedRectangle(FromDIP(2), FromDIP(2), size - FromDIP(4),
                                    size - FromDIP(4), FromDIP(2));
            auto font = GetFont();
            font.SetPointSize(8);
            font.SetWeight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
            font.SetStyle(italic ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL);
            font.SetUnderlined(underline);
            dc.SetFont(font);
            dc.SetTextForeground(fg);
            wxCoord tw = 0;
            wxCoord th = 0;
            dc.GetTextExtent(label, &tw, &th);
            dc.DrawText(label, (size - tw) / 2, (size - th) / 2);
            if (strikethrough) {
                dc.SetPen(wxPen(fg));
                dc.DrawLine(FromDIP(5), size / 2, size - FromDIP(5), size / 2);
            }
            dc.SelectObject(wxNullBitmap);
            return wxBitmapBundle::FromBitmap(bitmap);
        };

        auto MakePngToolBitmap = [&](const wxString &filename, const wxString &fallback) {
            wxImage image;
            if (image.LoadFile(imagepath + filename, wxBITMAP_TYPE_PNG) && image.IsOk()) {
                image.Rescale(FromDIP(24), FromDIP(24), wxIMAGE_QUALITY_HIGH);
                return wxBitmapBundle::FromBitmap(wxBitmap(image));
            }
            return MakeTextToolBitmap(fallback);
        };

        enum class ToolbarGlyph {
            SaveAll,
            Redo,
            PasteStyle,
            DeleteAfter,
            WidthIncrease,
            WidthDecrease,
            WidthReset,
            OpenFile,
            OpenBrowser,
            GridNxN,
            Wrap,
            Merge,
            Split,
            Fold,
            FoldAll,
            UnfoldAll,
            RenderGrid,
            RenderBubble,
            RenderLine,
            VerticalGrid,
            VerticalBubble,
            VerticalLine,
            HorizontalGrid,
            HorizontalBubble,
            HorizontalLine,
            OneLayer,
            Typewriter,
            ResetStyle,
            ResetSize,
            DisplayScale,
            OneToOne,
            SaveImage,
            PreviousSearch,
        };

        auto MakeGlyphToolBitmap = [&](ToolbarGlyph glyph) {
            const auto size = FromDIP(24);
            auto bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
            auto fg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
            wxBitmap bitmap(size, size);
            wxMemoryDC dc(bitmap);
            dc.SetBackground(wxBrush(bg));
            dc.Clear();

            auto D = [&](int v) { return FromDIP(v); };
            auto Pen = [&](int width = 2) {
                auto penwidth = D(width);
                dc.SetPen(wxPen(fg, penwidth < 1 ? 1 : penwidth, wxPENSTYLE_SOLID));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
            };
            auto Brush = [&]() {
                auto penwidth = D(1);
                dc.SetPen(wxPen(fg, penwidth < 1 ? 1 : penwidth, wxPENSTYLE_SOLID));
                dc.SetBrush(wxBrush(fg));
            };
            auto Line = [&](int x1, int y1, int x2, int y2, int width = 2) {
                Pen(width);
                dc.DrawLine(D(x1), D(y1), D(x2), D(y2));
            };
            auto Rect = [&](int x, int y, int w, int h, int width = 2) {
                Pen(width);
                dc.DrawRectangle(D(x), D(y), D(w), D(h));
            };
            auto FillRect = [&](int x, int y, int w, int h) {
                Brush();
                dc.DrawRectangle(D(x), D(y), D(w), D(h));
            };
            auto Circle = [&](int x, int y, int r, int width = 2) {
                Pen(width);
                dc.DrawCircle(D(x), D(y), D(r));
            };
            auto Plus = [&](int x, int y) {
                Line(x - 3, y, x + 4, y);
                Line(x, y - 3, x, y + 4);
            };
            auto Minus = [&](int x, int y) { Line(x - 4, y, x + 5, y); };
            auto Cross = [&](int x, int y) {
                Line(x - 4, y - 4, x + 5, y + 5);
                Line(x + 5, y - 4, x - 4, y + 5);
            };
            auto ArrowRight = [&](int x1, int y, int x2) {
                Line(x1, y, x2, y);
                Line(x2 - 4, y - 4, x2, y);
                Line(x2 - 4, y + 4, x2, y);
            };
            auto ArrowLeft = [&](int x1, int y, int x2) {
                Line(x1, y, x2, y);
                Line(x1 + 4, y - 4, x1, y);
                Line(x1 + 4, y + 4, x1, y);
            };
            auto ArrowDown = [&](int x, int y1, int y2) {
                Line(x, y1, x, y2);
                Line(x - 4, y2 - 4, x, y2);
                Line(x + 4, y2 - 4, x, y2);
            };
            auto Grid = [&](int x, int y, int w, int h) {
                Rect(x, y, w, h, 1);
                Line(x + w / 3, y, x + w / 3, y + h, 1);
                Line(x + 2 * w / 3, y, x + 2 * w / 3, y + h, 1);
                Line(x, y + h / 3, x + w, y + h / 3, 1);
                Line(x, y + 2 * h / 3, x + w, y + 2 * h / 3, 1);
            };

            switch (glyph) {
                case ToolbarGlyph::SaveAll:
                    Rect(5, 4, 10, 12);
                    Rect(9, 8, 10, 12);
                    Line(11, 12, 17, 12, 1);
                    Line(11, 15, 17, 15, 1);
                    break;
                case ToolbarGlyph::Redo:
                    Line(7, 6, 15, 6);
                    Line(15, 6, 15, 12);
                    ArrowLeft(6, 12, 15);
                    break;
                case ToolbarGlyph::PasteStyle:
                    Rect(6, 5, 9, 12);
                    Line(9, 4, 12, 4);
                    Line(8, 8, 13, 8, 1);
                    Line(14, 18, 19, 13);
                    Line(16, 12, 20, 16);
                    break;
                case ToolbarGlyph::DeleteAfter:
                    Line(6, 7, 18, 7);
                    Line(10, 5, 14, 5);
                    Rect(8, 8, 8, 11);
                    Line(10, 10, 10, 17, 1);
                    Line(14, 10, 14, 17, 1);
                    break;
                case ToolbarGlyph::WidthIncrease:
                    ArrowLeft(4, 9, 12);
                    ArrowRight(12, 9, 20);
                    Plus(12, 17);
                    break;
                case ToolbarGlyph::WidthDecrease:
                    ArrowRight(4, 9, 10);
                    ArrowLeft(14, 9, 20);
                    Minus(12, 17);
                    break;
                case ToolbarGlyph::WidthReset:
                    ArrowLeft(4, 8, 12);
                    ArrowRight(12, 8, 20);
                    Cross(12, 17);
                    break;
                case ToolbarGlyph::OpenFile:
                    Rect(4, 7, 16, 12);
                    Line(4, 7, 9, 7, 1);
                    Line(9, 7, 11, 10, 1);
                    Line(11, 10, 20, 10, 1);
                    ArrowRight(9, 15, 18);
                    break;
                case ToolbarGlyph::OpenBrowser:
                    Circle(12, 12, 8);
                    Line(4, 12, 20, 12, 1);
                    Line(12, 4, 12, 20, 1);
                    Pen(1);
                    dc.DrawArc(D(8), D(5), D(8), D(19), D(12), D(12));
                    dc.DrawArc(D(16), D(5), D(16), D(19), D(12), D(12));
                    ArrowRight(13, 17, 21);
                    break;
                case ToolbarGlyph::GridNxN:
                    Grid(4, 4, 14, 14);
                    Plus(18, 18);
                    break;
                case ToolbarGlyph::Wrap:
                    Rect(5, 6, 14, 11);
                    Rect(8, 9, 8, 5, 1);
                    ArrowDown(12, 2, 7);
                    break;
                case ToolbarGlyph::Merge:
                    Rect(4, 7, 7, 10, 1);
                    Rect(13, 7, 7, 10, 1);
                    ArrowRight(5, 12, 11);
                    ArrowLeft(13, 12, 19);
                    break;
                case ToolbarGlyph::Split:
                    Rect(5, 6, 14, 12);
                    Line(12, 6, 12, 18, 1);
                    ArrowLeft(4, 12, 10);
                    ArrowRight(14, 12, 20);
                    break;
                case ToolbarGlyph::Fold:
                    Rect(5, 5, 14, 14);
                    Minus(12, 12);
                    break;
                case ToolbarGlyph::FoldAll:
                    Rect(4, 4, 12, 12, 1);
                    Rect(8, 8, 12, 12, 1);
                    Minus(14, 14);
                    break;
                case ToolbarGlyph::UnfoldAll:
                    Rect(4, 4, 12, 12, 1);
                    Rect(8, 8, 12, 12, 1);
                    Plus(14, 14);
                    break;
                case ToolbarGlyph::RenderGrid:
                    Grid(5, 5, 14, 14);
                    break;
                case ToolbarGlyph::RenderBubble:
                    Circle(7, 8, 3, 1);
                    Circle(16, 8, 3, 1);
                    Circle(12, 16, 3, 1);
                    Line(9, 9, 14, 9, 1);
                    Line(8, 11, 11, 14, 1);
                    Line(15, 11, 13, 14, 1);
                    break;
                case ToolbarGlyph::RenderLine:
                    Circle(12, 5, 2, 1);
                    Circle(7, 17, 2, 1);
                    Circle(17, 17, 2, 1);
                    Line(12, 7, 12, 12, 1);
                    Line(7, 12, 17, 12, 1);
                    Line(7, 12, 7, 15, 1);
                    Line(17, 12, 17, 15, 1);
                    break;
                case ToolbarGlyph::VerticalGrid:
                    Rect(7, 4, 10, 5, 1);
                    Rect(7, 10, 10, 5, 1);
                    Rect(7, 16, 10, 5, 1);
                    break;
                case ToolbarGlyph::VerticalBubble:
                    Circle(12, 5, 2, 1);
                    Circle(12, 12, 2, 1);
                    Circle(12, 19, 2, 1);
                    Line(12, 7, 12, 10, 1);
                    Line(12, 14, 12, 17, 1);
                    break;
                case ToolbarGlyph::VerticalLine:
                    Rect(7, 4, 10, 4, 1);
                    Line(12, 8, 12, 12, 1);
                    Rect(7, 12, 10, 4, 1);
                    Line(12, 16, 12, 20, 1);
                    Line(8, 20, 16, 20, 1);
                    break;
                case ToolbarGlyph::HorizontalGrid:
                    Rect(4, 7, 5, 10, 1);
                    Rect(10, 7, 5, 10, 1);
                    Rect(16, 7, 5, 10, 1);
                    break;
                case ToolbarGlyph::HorizontalBubble:
                    Circle(5, 12, 2, 1);
                    Circle(12, 12, 2, 1);
                    Circle(19, 12, 2, 1);
                    Line(7, 12, 10, 12, 1);
                    Line(14, 12, 17, 12, 1);
                    break;
                case ToolbarGlyph::HorizontalLine:
                    Rect(4, 7, 4, 10, 1);
                    Line(8, 12, 12, 12, 1);
                    Rect(12, 7, 4, 10, 1);
                    Line(16, 12, 20, 12, 1);
                    Line(20, 8, 20, 16, 1);
                    break;
                case ToolbarGlyph::OneLayer:
                    Line(6, 8, 18, 5, 1);
                    Line(6, 12, 18, 9, 1);
                    Line(6, 16, 18, 13, 1);
                    FillRect(7, 7, 10, 2);
                    break;
                case ToolbarGlyph::Typewriter:
                    Rect(4, 7, 16, 11);
                    Line(7, 10, 17, 10, 1);
                    Line(7, 13, 17, 13, 1);
                    Line(8, 16, 16, 16, 1);
                    break;
                case ToolbarGlyph::ResetStyle:
                    Line(7, 18, 12, 5);
                    Line(12, 5, 17, 18);
                    Line(9, 13, 15, 13, 1);
                    Line(5, 5, 19, 19);
                    break;
                case ToolbarGlyph::ResetSize:
                    Line(6, 18, 10, 8);
                    Line(10, 8, 14, 18);
                    Line(14, 18, 18, 5);
                    ArrowRight(14, 6, 20);
                    break;
                case ToolbarGlyph::DisplayScale:
                    Rect(5, 5, 14, 14);
                    Line(8, 15, 11, 12, 1);
                    Line(11, 12, 14, 14, 1);
                    ArrowRight(12, 8, 18);
                    ArrowDown(16, 6, 12);
                    break;
                case ToolbarGlyph::OneToOne:
                    Rect(6, 6, 12, 12);
                    Rect(9, 9, 6, 6, 1);
                    Line(5, 3, 8, 3, 1);
                    Line(3, 5, 3, 8, 1);
                    Line(16, 21, 19, 21, 1);
                    Line(21, 16, 21, 19, 1);
                    break;
                case ToolbarGlyph::SaveImage:
                    Rect(5, 5, 14, 11);
                    Circle(9, 9, 2, 1);
                    Line(7, 15, 11, 12, 1);
                    Line(11, 12, 15, 15, 1);
                    ArrowDown(12, 15, 21);
                    break;
                case ToolbarGlyph::PreviousSearch:
                    Circle(10, 10, 5);
                    Line(14, 14, 19, 19);
                    ArrowLeft(4, 18, 12);
                    break;
            }

            dc.SelectObject(wxNullBitmap);
            return wxBitmapBundle::FromBitmap(bitmap);
        };

        auto AddTextTool = [&](wxAuiToolBar *tb, const wxString &label, int action,
                               const wxString &tooltip, bool bold = false,
                               bool italic = false, bool underline = false,
                               bool strikethrough = false,
                               wxItemKind kind = wxITEM_NORMAL) {
            tb->AddTool(action, tooltip,
                        MakeTextToolBitmap(label, bold, italic, underline, strikethrough),
                        ToolbarTooltip(action, tooltip), kind);
        };

        auto AddGlyphTool = [&](wxAuiToolBar *tb, ToolbarGlyph glyph, int action,
                                const wxString &tooltip,
                                wxItemKind kind = wxITEM_NORMAL) {
            tb->AddTool(action, tooltip, MakeGlyphToolBitmap(glyph),
                        ToolbarTooltip(action, tooltip), kind);
        };

        auto AddPngTool = [&](wxAuiToolBar *tb, const wxString &name, int action,
                              const wxString &filename, const wxString &fallback) {
            tb->AddTool(action, name, MakePngToolBitmap(filename, fallback),
                        ToolbarTooltip(action, name), wxITEM_NORMAL);
        };

        auto AddToolbarPane = [&](wxAuiToolBar *tb, const wxString &name,
                                  const wxString &caption, int row) {
            tb->Realize();
            auto paneinfo = wxAuiPaneInfo()
                                .Name(name)
                                .Caption(caption)
                                .ToolbarPane()
                                .Top()
                                .Row(row)
                                .LeftDockable(false)
                                .RightDockable(false)
                                .Gripper(true);
            if (!sys->showtoolbar || hiddentoolbarsections.count(name)) paneinfo.Hide();
            aui.AddPane(tb, paneinfo);
        };

        auto filetb = NewToolbar();
        AddToolbarIcon(filetb, _("New (CTRL+n)"), wxID_NEW, "filenew.svg", "filenew_dark.svg");
        AddToolbarIcon(filetb, _("Open (CTRL+o)"), wxID_OPEN, "fileopen.svg",
                       "fileopen_dark.svg");
        AddToolbarIcon(filetb, _("Save (CTRL+s)"), wxID_SAVE, "filesave.svg",
                       "filesave_dark.svg");
        AddToolbarIcon(filetb, _("Save as..."), wxID_SAVEAS, "filesaveas.svg",
                       "filesaveas_dark.svg");
        AddGlyphTool(filetb, ToolbarGlyph::SaveAll, A_SAVEALL, _("Save all open documents"));

        auto edittb = NewToolbar();
        AddToolbarIcon(edittb, _("Undo (CTRL+z)"), wxID_UNDO, "undo.svg", "undo_dark.svg");
        AddGlyphTool(edittb, ToolbarGlyph::Redo, wxID_REDO, _("Redo (CTRL+y)"));
        AddToolbarIcon(edittb, _("Copy (CTRL+c)"), wxID_COPY, "editcopy.svg",
                       "editcopy_dark.svg");
        AddToolbarIcon(edittb, _("Paste (CTRL+v)"), wxID_PASTE, "editpaste.svg",
                       "editpaste_dark.svg");
        AddGlyphTool(edittb, ToolbarGlyph::PasteStyle, A_PASTESTYLE, _("Paste style only"));
        AddGlyphTool(edittb, ToolbarGlyph::DeleteAfter, A_DELETE,
                     _("Delete after selection (DEL)"));

        auto viewtb = NewToolbar();
        AddToolbarIcon(viewtb, _("Zoom In (CTRL+mousewheel)"), A_ZOOMIN, "zoomin.svg",
                       "zoomin_dark.svg");
        AddToolbarIcon(viewtb, _("Zoom Out (CTRL+mousewheel)"), A_ZOOMOUT, "zoomout.svg",
                       "zoomout_dark.svg");
        AddGlyphTool(viewtb, ToolbarGlyph::WidthIncrease, A_INCWIDTH,
                     _("Increase column width"));
        AddGlyphTool(viewtb, ToolbarGlyph::WidthDecrease, A_DECWIDTH,
                     _("Decrease column width"));
        AddGlyphTool(viewtb, ToolbarGlyph::WidthReset, A_RESETWIDTH,
                     _("Reset column widths"));

        auto browsetb = NewToolbar();
        AddGlyphTool(browsetb, ToolbarGlyph::OpenFile, A_BROWSEF,
                     _("Open file from selected cell text (F4)"));
        AddGlyphTool(browsetb, ToolbarGlyph::OpenBrowser, A_BROWSE,
                     _("Open link in browser from selected cell text (F5)"));

        auto structuretb = NewToolbar();
        AddToolbarIcon(structuretb, _("New Grid (INS)"), A_ENTERGRID, "newgrid.svg",
                       "newgrid_dark.svg");
        AddGlyphTool(structuretb, ToolbarGlyph::GridNxN, A_ENTERGRIDN,
                     _("Insert new NxN grid"));
        AddGlyphTool(structuretb, ToolbarGlyph::Wrap, A_WRAP, _("Wrap in new parent (F9)"));
        AddGlyphTool(structuretb, ToolbarGlyph::Merge, A_MERGECELLS,
                     _("Merge selected cells"));
        AddGlyphTool(structuretb, ToolbarGlyph::Split, A_UNMERGECELLS,
                     _("Unmerge selected cells"));
        AddGlyphTool(structuretb, ToolbarGlyph::Fold, A_FOLD, _("Toggle fold"));
        AddGlyphTool(structuretb, ToolbarGlyph::FoldAll, A_FOLDALL,
                     _("Fold all recursively"));
        AddGlyphTool(structuretb, ToolbarGlyph::UnfoldAll, A_UNFOLDALL,
                     _("Unfold all recursively"));

        auto rendertb = NewToolbar();
        AddGlyphTool(rendertb, ToolbarGlyph::RenderGrid, A_GS, _("Grid style rendering"));
        AddGlyphTool(rendertb, ToolbarGlyph::RenderBubble, A_BS,
                     _("Bubble style rendering"));
        AddGlyphTool(rendertb, ToolbarGlyph::RenderLine, A_LS, _("Line style rendering"));
        AddGlyphTool(rendertb, ToolbarGlyph::VerticalGrid, A_V_GS,
                     _("Vertical grid layout"));
        AddGlyphTool(rendertb, ToolbarGlyph::VerticalBubble, A_V_BS,
                     _("Vertical bubble layout"));
        AddGlyphTool(rendertb, ToolbarGlyph::VerticalLine, A_V_LS,
                     _("Vertical line layout"));
        AddGlyphTool(rendertb, ToolbarGlyph::HorizontalGrid, A_H_GS,
                     _("Horizontal grid layout"));
        AddGlyphTool(rendertb, ToolbarGlyph::HorizontalBubble, A_H_BS,
                     _("Horizontal bubble layout"));
        AddGlyphTool(rendertb, ToolbarGlyph::HorizontalLine, A_H_LS,
                     _("Horizontal line layout"));
        AddGlyphTool(rendertb, ToolbarGlyph::OneLayer, A_RENDERSTYLEONELAYER,
                     _("Apply render style to selected layer only"), wxITEM_CHECK);
        rendertb->ToggleTool(A_RENDERSTYLEONELAYER, sys->renderstyleonelayer);

        auto formattb = NewToolbar();
        AddTextTool(formattb, "B", wxID_BOLD, _("Toggle bold"), true);
        AddTextTool(formattb, "I", wxID_ITALIC, _("Toggle italic"), false, true);
        AddTextTool(formattb, "U", wxID_UNDERLINE, _("Toggle underline"), false, false, true);
        AddTextTool(formattb, "S", wxID_STRIKETHROUGH, _("Toggle strikethrough"), false, false,
                    false, true);
        AddGlyphTool(formattb, ToolbarGlyph::Typewriter, A_TT, _("Toggle typewriter"));
        AddGlyphTool(formattb, ToolbarGlyph::ResetStyle, A_RESETSTYLE,
                     _("Reset text styles"));
        AddGlyphTool(formattb, ToolbarGlyph::ResetSize, A_RESETSIZE, _("Reset text sizes"));

        auto GetColorIndex = [&](int targetcolor, int defaultindex) {
            for (auto i = 1; i < celltextcolors.size(); ++i) {
                if (celltextcolors[i] == targetcolor) return i;
            }
            if (sys->customcolor == targetcolor) return 0;
            return defaultindex;
        };

        auto cellcolortb = NewToolbar();
        cellcolortb->AddControl(new wxStaticText(cellcolortb, wxID_ANY, _("Cell ")));
        cellcolordropdown =
            new ColorDropdown(cellcolortb, A_CELLCOLOR, GetColorIndex(sys->lastcellcolor, 1));
        cellcolortb->AddControl(cellcolordropdown);
        AddPngTool(cellcolortb, _("Apply last cell color"), A_LASTCELLCOLOR, "apply.png", "A");
        AddPngTool(cellcolortb, _("Pick custom color"), A_CUSTCOL, "kcoloredit.png", "C");

        auto textcolortb = NewToolbar();
        textcolortb->AddControl(new wxStaticText(textcolortb, wxID_ANY, _("Text ")));
        textcolordropdown =
            new ColorDropdown(textcolortb, A_TEXTCOLOR, GetColorIndex(sys->lasttextcolor, 2));
        textcolortb->AddControl(textcolordropdown);
        AddPngTool(textcolortb, _("Apply last text color"), A_LASTTEXTCOLOR, "apply.png", "A");

        auto bordercolortb = NewToolbar();
        bordercolortb->AddControl(new wxStaticText(bordercolortb, wxID_ANY, _("Border ")));
        bordercolordropdown =
            new ColorDropdown(bordercolortb, A_BORDCOLOR, GetColorIndex(sys->lastbordcolor, 7));
        bordercolortb->AddControl(bordercolordropdown);
        AddPngTool(bordercolortb, _("Apply last border color"), A_LASTBORDCOLOR, "apply.png",
                   "A");
        AddToolbarIcon(bordercolortb, _("Reset colors"), A_RESETCOLOR, "cancel.svg",
                       "cancel_dark.svg");

        auto imagetb = NewToolbar();
        AddToolbarIcon(imagetb, _("Add Image"), A_IMAGE, "image.svg", "image_dark.svg");
        imagetb->AddControl(new wxStaticText(imagetb, wxID_ANY, _("Image ")));
        imagedropdown = new ImageDropdown(imagetb, imagepath);
        imagetb->AddControl(imagedropdown);
        AddPngTool(imagetb, _("Insert last image"), A_LASTIMAGE, "apply.png", "A");
        AddPngTool(imagetb, _("Remove selected image"), A_IMAGER, "edit_remove.png", "X");
        AddGlyphTool(imagetb, ToolbarGlyph::DisplayScale, A_IMAGESCF,
                     _("Scale display size without resampling"));
        AddGlyphTool(imagetb, ToolbarGlyph::OneToOne, A_IMAGESCN, _("Reset display scale"));
        AddGlyphTool(imagetb, ToolbarGlyph::SaveImage, A_IMAGESVA,
                     _("Save selected image"));
        AddToolbarIcon(imagetb, _("Run"), wxID_EXECUTE, "run.svg", "run_dark.svg");

        auto findtb = NewToolbar();
        findtb->AddControl(new wxStaticText(findtb, wxID_ANY, _("Search ")));
        findtb->AddControl(filter = new wxTextCtrl(findtb, A_SEARCH, "", wxDefaultPosition,
                                                   FromDIP(wxSize(140, 24)),
                                                   wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddToolbarIcon(findtb, _("Clear search"), A_CLEARSEARCH, "cancel.svg", "cancel_dark.svg");
        AddGlyphTool(findtb, ToolbarGlyph::PreviousSearch, A_SEARCHPREV,
                     _("Go to previous search result"));
        AddToolbarIcon(findtb, _("Next search result"), A_SEARCHNEXT, "search.svg",
                       "search_dark.svg");

        auto repltb = NewToolbar();
        repltb->AddControl(new wxStaticText(repltb, wxID_ANY, _("Replace ")));
        repltb->AddControl(replaces = new wxTextCtrl(repltb, A_REPLACE, "", wxDefaultPosition,
                                                     FromDIP(wxSize(140, 24)),
                                                     wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddToolbarIcon(repltb, _("Clear replace"), A_CLEARREPLACE, "cancel.svg",
                       "cancel_dark.svg");
        AddToolbarIcon(repltb, _("Replace in selection"), A_REPLACEONCE, "replace.svg",
                       "replace_dark.svg");
        AddToolbarIcon(repltb, _("Replace All"), A_REPLACEALL, "replaceall.svg",
                       "replaceall_dark.svg");

        AddToolbarPane(filetb, "filetb", "File operations", 0);
        AddToolbarPane(edittb, "edittb", "Edit operations", 0);
        AddToolbarPane(viewtb, "viewtb", "View operations", 0);
        AddToolbarPane(browsetb, "browsetb", "Browse operations", 0);
        AddToolbarPane(structuretb, "structuretb", "Structure operations", 0);
        AddToolbarPane(rendertb, "rendertb", "Render operations", 1);
        AddToolbarPane(formattb, "formattb", "Text format operations", 1);
        AddToolbarPane(cellcolortb, "cellcolortb", "Cell color operations", 1);
        AddToolbarPane(textcolortb, "textcolortb", "Text color operations", 1);
        AddToolbarPane(bordercolortb, "bordercolortb", "Border color operations", 1);
        AddToolbarPane(imagetb, "imagetb", "Image operations", 1);
        AddToolbarPane(findtb, "findtb", "Find operations", 2);
        AddToolbarPane(repltb, "repltb", "Replace operations", 2);

        auto artprovider = aui.GetArtProvider();
        artprovider->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR,
                               wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
        artprovider->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);
    }

    void AppOnEventLoopEnter() {
        watcher = new wxFileSystemWatcher();
        watcher->SetOwner(this);
        Connect(wxEVT_FSWATCHER, wxFileSystemWatcherEventHandler(TSFrame::OnFileSystemEvent));
        RefreshExplorerWatches();
    }

    bool ExplorerShouldRefreshForEvent(const wxFileSystemWatcherEvent &event) const {
        if (!explorertree || explorerroot.IsEmpty()) return false;
        auto path = event.GetPath().GetFullPath();
        auto newpath = event.GetNewPath().GetFullPath();
        return (ExplorerPathIsInsideRoot(path) && !ExplorerPathIsSkipped(path)) ||
               (ExplorerPathIsInsideRoot(newpath) && !ExplorerPathIsSkipped(newpath));
    }

    void ScheduleExplorerRefresh(const wxString &path) {
        if (!explorertree) return;
        if (!path.IsEmpty()) explorerpendingrefreshpath = path;
        if (explorerrefreshpending) return;
        explorerrefreshpending = true;
        CallAfter([this]() {
            explorerrefreshpending = false;
            auto path = explorerpendingrefreshpath;
            explorerpendingrefreshpath.clear();
            RefreshExplorerTree(path);
        });
    }

    bool SameNormalizedPath(const wxString &a, const wxString &b) const {
        auto left = NormalizeExplorerPath(a);
        auto right = NormalizeExplorerPath(b);
        #ifdef __WXMSW__
            left.MakeLower();
            right.MakeLower();
        #endif
        return !left.IsEmpty() && left == right;
    }

    bool ReloadDocumentIfChangedOnDisk(Document *doc, int page) {
        wxDateTime modtime;
        if (!sys->DocumentChangedOnDisk(doc, &modtime)) return false;
        if (watcherwaitingforuser) return true;

        if (doc->modified) {
            auto message = wxString::Format(
                _("%s\nhas been modified on disk by another program / computer:\nWould you like to discard your changes and re-load from disk?"),
                doc->filename);
            watcherwaitingforuser = true;
            int res = wxMessageBox(message, _("File modification conflict!"),
                                   wxYES_NO | wxICON_QUESTION, this);
            watcherwaitingforuser = false;
            if (res != wxYES) {
                doc->lastmodificationtime = modtime;
                SetStatus(_("External file modification ignored."));
                return true;
            }
        }

        auto filename = doc->filename;
        auto message = sys->LoadDB(filename, true, page);
        if (!message.IsEmpty()) {
            doc->lastmodificationtime = modtime;
            SetStatus(message);
        } else {
            notebook->DeletePage(page + 1);
            ::wxRemoveFile(sys->TmpName(filename));
            SetStatus(
                _("File has been re-loaded because of modifications of another program / computer"));
        }
        return true;
    }

    void CheckForExternallyModifiedDocuments() {
        if (!notebook || watcherwaitingforuser || !sys->fswatch) return;
        loop(i, notebook->GetPageCount()) {
            auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
            if (ReloadDocumentIfChangedOnDisk(canvas->doc.get(), i)) return;
        }
    }

    // event handling functions

    void OnMenu(wxCommandEvent &ce) {
        wxTextEntryBase *tc = nullptr;
        wxWindow *tcwindow = nullptr;
        auto canvas = GetCurrentTab();
        auto focus = wxWindow::FindFocus();
        auto CheckTextEntryFocus = [&](wxWindow *window, wxTextEntryBase *entry) {
            if (!window || !entry || !focus) return false;
            if (window != focus && !window->IsDescendant(focus)) return false;
            tc = entry;
            tcwindow = window;
            return true;
        };
        if (CheckTextEntryFocus(filter, filter) || CheckTextEntryFocus(replaces, replaces) ||
            CheckTextEntryFocus(explorersearch, explorersearch)) {
            long from, to;
            tc->GetSelection(&from, &to);
            auto end = tc->GetLastPosition();
            switch (ce.GetId()) {
                #if defined(__WXMSW__) || defined(__WXMAC__)
                // FIXME: have to emulate this behavior on Windows and Mac because menu always captures these events (??)
                case A_MLEFT:
                case A_LEFT:
                    if (from != to)
                        tc->SetInsertionPoint(from);
                    else if (from)
                        tc->SetInsertionPoint(from - 1);
                    return;
                case A_MRIGHT:
                case A_RIGHT:
                    if (from != to)
                        tc->SetInsertionPoint(to);
                    else if (to < end)
                        tc->SetInsertionPoint(to + 1);
                    return;

                case A_SHOME: tc->SetSelection(0, to); return;
                case A_SEND: tc->SetSelection(from, end); return;

                case A_SCLEFT:
                case A_SLEFT:
                    if (from) tc->SetSelection(from - 1, to);
                    return;
                case A_SCRIGHT:
                case A_SRIGHT:
                    if (to < end) tc->SetSelection(from, to + 1);
                    return;

                case A_BACKSPACE:
                    if (from != to)
                        tc->Remove(from, to);
                    else if (from > 0)
                        tc->Remove(from - 1, to);
                    return;
                case A_DELETE:
                    if (from != to)
                        tc->Remove(from, to);
                    else if (to < end)
                        tc->Remove(from, to + 1);
                    return;
                case A_HOME: tc->SetSelection(0, 0); return;
                case A_END: tc->SetSelection(end, end); return;
                case wxID_SELECTALL: tc->SelectAll(); return;
                #endif
                #ifdef __WXMSW__
                case A_ENTERCELL: {
                    if (tcwindow == filter) {
                        // OnSearchEnter equivalent implementation for MSW
                        // as EVT_TEXT_ENTER event is not generated.
                        if (sys->searchstring.IsEmpty()) {
                            canvas->SetFocus();
                        } else {
                            canvas->doc->Action(A_SEARCHNEXT);
                        }
                    } else if (tcwindow == replaces) {
                        // OnReplaceEnter equivalent implementation for MSW
                        // as EVT_TEXT_ENTER event is not generated.
                        canvas->doc->Action(A_REPLACEONCEJ);
                    }
                    return;
                }
                #endif
                case A_CANCELEDIT:
                    tc->Clear();
                    if (tcwindow == explorersearch) {
                        RefreshExplorerSearch();
                    } else {
                        canvas->SetFocus();
                    }
                    return;
            }
        }
        auto Check = [&](const wxString &cfg) {
            sys->cfg->Write(cfg, ce.IsChecked());
            SetStatus(_("change will take effect next run of TreeSheets"));
        };
        switch (ce.GetId()) {
            case A_NOP: break;
            case A_EXPLORER:
                ToggleExplorer();
                break;
            case A_EXPLORERSEARCH:
                FocusExplorerSearch();
                break;
            case A_EXPLORERROOT:
                ChooseExplorerRoot();
                break;
            case A_EXPLOREROPEN:
                OpenExplorerPath(explorercontextpath.IsEmpty() ? ExplorerSelectedPath()
                                                               : explorercontextpath);
                break;
            case A_EXPLORERNEWFILE:
                ExplorerNewFile();
                break;
            case A_EXPLORERNEWFOLDER:
                ExplorerNewFolder();
                break;
            case A_EXPLORERRENAME:
                ExplorerRename();
                break;
            case A_EXPLORERDELETE:
                ExplorerDelete();
                break;
            case A_EXPLORERDUPLICATE:
                ExplorerDuplicate();
                break;
            case A_EXPLORERREFRESH:
                RefreshExplorerTree();
                break;
            case A_EXPLORERCOLLAPSEALL:
                CollapseExplorerTree();
                break;
            case A_EXPLOREROPENPARENT: {
                auto path = explorercontextpath.IsEmpty() ? ExplorerSelectedPath() : explorercontextpath;
                auto directory = wxFileName(path).GetPath(wxPATH_GET_VOLUME);
                if (wxDirExists(directory)) SetExplorerRoot(directory);
                break;
            }
            case A_EXPLORERREVEAL:
                RevealExplorerPathInSystem(explorercontextpath.IsEmpty() ? ExplorerSelectedPath()
                                                                         : explorercontextpath);
                break;
            case A_EXPLORERCOPYPATH:
                CopyExplorerPath(false);
                break;
            case A_EXPLORERCOPYRELPATH:
                CopyExplorerPath(true);
                break;
            case A_EXPLORERHIDE:
                HideExplorer();
                break;
            case A_EXPLORERAUTOHIDE:
                sys->cfg->Write("explorerautohide", explorerautohide = ce.IsChecked());
                if (GetMenuBar()) GetMenuBar()->Check(A_EXPLORERAUTOHIDE, explorerautohide);
                break;
            case A_EXPLORERREVEALACTIVE:
                RevealCurrentDocumentInExplorer();
                break;

            case A_ALEFT: canvas->CursorScroll(-g_scrollratecursor, 0); break;
            case A_ARIGHT: canvas->CursorScroll(g_scrollratecursor, 0); break;
            case A_AUP: canvas->CursorScroll(0, -g_scrollratecursor); break;
            case A_ADOWN: canvas->CursorScroll(0, g_scrollratecursor); break;
            case A_RESETPERSPECTIVE:
                sys->cfg->Write("perspective", "");
                sys->cfg->Write("toolbarhiddenpanes", "");
                sys->cfg->Write("toolbarlayoutversion", toolbarlayoutversion);
                RefreshToolBar();
                sys->showtoolbar = true;
                ApplyToolbarVisibility();
                aui.Update();
                break;
            case A_CUSTOMIZETOOLBAR:
                CustomizeToolbar();
                break;
            case A_SHOWSBAR:
                if (!IsFullScreen()) {
                    sys->cfg->Write("showstatusbar", sys->showstatusbar = ce.IsChecked());
                    auto wsb = GetStatusBar();
                    wsb->Show(sys->showstatusbar);
                    SendSizeEvent();
                    Refresh();
                    wsb->Refresh();
                }
                break;
            case A_SHOWTBAR:
                if (!IsFullScreen()) {
                    sys->cfg->Write("showtoolbar", sys->showtoolbar = ce.IsChecked());
                    ApplyToolbarVisibility();
                    aui.Update();
                }
                break;
            case A_CUSTCOL: {
                if (auto color = PickColor(sys->frame, sys->customcolor); color != (uint)-1)
                    sys->cfg->Write("customcolor", sys->customcolor = color);
                break;
            }

            #ifdef ENABLE_LOBSTER
                case A_ADDSCRIPT: {
                    wxArrayString filenames;
                    GetFilesFromUser(filenames, this, _("Please select Lobster script file(s):"),
                                     _("Lobster Files (*.lobster)|*.lobster|All Files (*.*)|*.*"));
                    for (auto &filename : filenames) scripts.AddFileToHistory(filename);
                    break;
                }

                case A_DETSCRIPT: {
                    wxArrayString filenames;
                    for (int i = 0, n = scripts.GetCount(); i < n; i++) {
                        filenames.Add(scripts.GetHistoryFile(i));
                    }
                    auto dialog = wxSingleChoiceDialog(
                        this, _("Please select the script you want to remove from the list:"),
                        _("Remove script from list..."), filenames);
                    if (dialog.ShowModal() == wxID_OK)
                        scripts.RemoveFileFromHistory(dialog.GetSelection());
                    break;
                }
            #endif

            case A_DEFAULTMAXCOLWIDTH: {
                int w = wxGetNumberFromUser(_("Please enter the default column width:"),
                                            _("Width"), _("Default column width"),
                                            sys->defaultmaxcolwidth, 1, 1000, sys->frame);
                if (w > 0) sys->cfg->Write("defaultmaxcolwidth", sys->defaultmaxcolwidth = w);
                break;
            }

            case A_LEFTTABS: Check("lefttabs"); break;
            case A_SINGLETRAY: Check("singletray"); break;
            case A_MAKEBAKS: sys->cfg->Write("makebaks", sys->makebaks = ce.IsChecked()); break;
            case A_TOTRAY:
                sys->cfg->Write("totray", sys->totray = ce.IsChecked());
                UpdateTrayIcon();
                break;
            case A_MINCLOSE: sys->cfg->Write("minclose", sys->minclose = ce.IsChecked()); break;
            case A_STARTMINIMIZED:
                sys->cfg->Write("startminimized", sys->startminimized = ce.IsChecked());
                break;
            case A_GLOBALSHOWHOTKEY:
                sys->cfg->Write("globalshowhotkeyenabled",
                                sys->globalshowhotkeyenabled = ce.IsChecked());
                if (sys->globalshowhotkeyenabled)
                    RegisterGlobalShowHotKey();
                else
                    UnregisterGlobalShowHotKey();
                break;
            case A_ZOOMSCR: sys->cfg->Write("zoomscroll", sys->zoomscroll = ce.IsChecked()); break;
            case A_THINSELC: sys->cfg->Write("thinselc", sys->thinselc = ce.IsChecked()); break;
            case A_AUTOSAVE: sys->cfg->Write("autosave", sys->autosave = ce.IsChecked()); break;
            case A_RENDERSTYLEONELAYER:
                sys->cfg->Write("renderstyleonelayer",
                                sys->renderstyleonelayer = ce.IsChecked());
                if (GetMenuBar()) GetMenuBar()->Check(A_RENDERSTYLEONELAYER,
                                                      sys->renderstyleonelayer);
                break;
            case A_CENTERED:
                sys->cfg->Write("centered", sys->centered = ce.IsChecked());
                Refresh();
                break;
            case A_FSWATCH:
                Check("fswatch");
                sys->fswatch = ce.IsChecked();
                break;
            case A_AUTOEXPORT_HTML_NONE:
            case A_AUTOEXPORT_HTML_WITH_IMAGES:
            case A_AUTOEXPORT_HTML_WITHOUT_IMAGES:
                sys->cfg->Write(
                    "autohtmlexport",
                    static_cast<long>(sys->autohtmlexport = ce.GetId() - A_AUTOEXPORT_HTML_NONE));
                break;
            case A_FASTRENDER:
                sys->cfg->Write("fastrender", sys->fastrender = ce.IsChecked());
                Refresh();
                break;
            case A_INVERTRENDER:
                sys->cfg->Write("followdarkmode", sys->followdarkmode = ce.IsChecked());
                sys->colormask = (sys->followdarkmode && wxSystemSettings::GetAppearance().IsDark())
                                     ? 0x00FFFFFF
                                     : 0;
                Refresh();
                break;
            case A_FULLSCREEN:
                ShowFullScreen(!IsFullScreen());
                if (IsFullScreen()) SetStatus(_("Press F11 to exit fullscreen mode."));
                break;
            case wxID_FIND:
                if (filter) {
                    filter->SetFocus();
                    filter->SetSelection(0, 1000);
                } else {
                    SetStatus(_("Please enable (Options -> Show Toolbar) to use search."));
                }
                break;
            case wxID_REPLACE:
                if (replaces) {
                    replaces->SetFocus();
                    replaces->SetSelection(0, 1000);
                } else {
                    SetStatus(_("Please enable (Options -> Show Toolbar) to use replace."));
                }
                break;
            #ifdef __WXMAC__
                case wxID_OSX_HIDE: Iconize(true); break;
                case wxID_OSX_HIDEOTHERS: SetStatus("NOT IMPLEMENTED"); break;
                case wxID_OSX_SHOWALL: Iconize(false); break;
                case wxID_ABOUT: canvas->doc->Action(wxID_ABOUT); break;
                case wxID_PREFERENCES: canvas->doc->Action(wxID_SELECT_FONT); break;
            #endif
            case wxID_EXIT:
                fromclosebox = false;
                Close();
                break;
            case wxID_CLOSE:
                canvas->doc->Action(ce.GetId());
                break;  // canvas dangling pointer on return
            default:
                if (ce.GetId() >= wxID_FILE1 && ce.GetId() <= wxID_FILE9) {
                    wxString filename(filehistory.GetHistoryFile(ce.GetId() - wxID_FILE1));
                    SetStatus(sys->Open(filename));
                #ifdef ENABLE_LOBSTER
                    } else if (ce.GetId() >= A_TAGSET && ce.GetId() < A_SCRIPT) {
                        SetStatus(canvas->doc->TagSet(ce.GetId() - A_TAGSET));
                    } else if (ce.GetId() >= A_SCRIPT && ce.GetId() < A_MAXACTION) {
                        auto message =
                            tssi.ScriptRun(scripts.GetHistoryFile(ce.GetId() - A_SCRIPT).c_str());
                        message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
                        SetStatus(wxString(message));
                #else
                    } else if (ce.GetId() >= A_TAGSET && ce.GetId() < A_MAXACTION) {
                        SetStatus(canvas->doc->TagSet(ce.GetId() - A_TAGSET));
                #endif
                } else {
                    SetStatus(canvas->doc->Action(ce.GetId()));
                    break;
                }
        }
    }

    void OnTabChange(wxAuiNotebookEvent &nbe) {
        auto canvas = static_cast<TSCanvas *>(notebook->GetPage(nbe.GetSelection()));
        ClearStatus();
        sys->TabChange(canvas->doc.get());
        RevealCurrentDocumentInExplorerIfVisible();
        nbe.Skip();
    }

    void OnTabClose(wxAuiNotebookEvent &nbe) {
        auto canvas = static_cast<TSCanvas *>(notebook->GetPage(nbe.GetSelection()));
        if (notebook->GetPageCount() <= 1) {
            nbe.Veto();
            Close();
        } else if (canvas->doc->CloseDocument()) {
            nbe.Veto();
        } else {
            nbe.Skip();
        }
    }

    void OnSearch(wxCommandEvent &ce) {
        auto searchstring = ce.GetString();
        sys->darkennonmatchingcells = searchstring.Len() != 0;
        sys->searchstring = sys->casesensitivesearch ? searchstring : searchstring.Lower();
        TSCanvas *canvas = GetCurrentTab();
        Document *doc = canvas->doc.get();
        if (doc->searchfilter) {
            doc->SetSearchFilter(sys->searchstring.Len() != 0);
            doc->searchfilter = true;
        }
        canvas->Refresh();
    }

    void OnSearchReplaceEnter(wxCommandEvent &ce) {
        auto canvas = GetCurrentTab();
        if (ce.GetId() == A_SEARCH && ce.GetString().IsEmpty())
            canvas->SetFocus();
        else
            canvas->doc->Action(ce.GetId() == A_SEARCH ? A_SEARCHNEXT : A_REPLACEONCEJ);
    }

    void OnChangeColor(wxCommandEvent &ce) {
        GetCurrentTab()->doc->ColorChange(ce.GetId(), ce.GetInt());
        ReFocus();
    }

    void OnDDImage(wxCommandEvent &ce) {
        GetCurrentTab()->doc->ImageChange(imagedropdown->filenames[ce.GetInt()], dd_icon_res_scale);
        ReFocus();
    }

    void OnActivate(wxActivateEvent &ae) {
        // This causes warnings in the debug log, but without it keyboard entry upon window select
        // doesn't work.
        if (ExplorerSearchHasFocus()) return;
        ReFocus();
    }

    void OnGlobalShowHotKey(wxKeyEvent &) { ShowMainInterface(true); }

    void OnSizing(wxSizeEvent &se) { se.Skip(); }

    void OnMaximize(wxMaximizeEvent &me) {
        ReFocus();
        me.Skip();
    }

    void OnIconize(wxIconizeEvent &me) {
        if (me.IsIconized()) {
            #ifndef __WXMAC__
            if (sys->totray) {
                UpdateTrayIcon();
                Show(false);
                Iconize();
            }
            #endif
        } else {
            #ifdef __WXGTK__
            if (sys->totray) {
                Show(true);
            }
            #endif
            if (TSCanvas *canvas = GetCurrentTab()) canvas->SetFocus();
        }
    }

    void OnTBIDBLClick(wxTaskBarIconEvent &e) { DeIconize(); }

    void UpdateTrayIcon() {
        #ifndef __WXMAC__
        if (sys->totray) {
            taskbaricon.SetIcon(icon, "TreeSheets");
        } else if (taskbaricon.IsIconInstalled()) {
            taskbaricon.RemoveIcon();
        }
        #endif
    }

    void OnTrayMenu(wxTaskBarIconEvent &) {
        wxMenu menu;
        MyAppend(&menu, A_TRAY_RESTORE,
                 IsShown() && !IsIconized() ? _("&Hide to Tray") : _("&Restore"));
        menu.AppendSeparator();
        MyAppend(&menu, wxID_NEW, _("&New") + "\tCTRL+N");
        MyAppend(&menu, wxID_OPEN, _("&Open...") + "\tCTRL+O");
        menu.AppendSeparator();
        MyAppend(&menu, wxID_SAVE, _("&Save") + "\tCTRL+S");
        MyAppend(&menu, A_SAVEALL, _("Save All"));
        menu.AppendSeparator();
        MyAppend(&menu, wxID_EXIT, _("&Exit") + "\tCTRL+Q");
        menu.Bind(wxEVT_MENU, &TSFrame::OnTrayMenuCommand, this);
        taskbaricon.PopupMenu(&menu);
    }

    void OnTrayMenuCommand(wxCommandEvent &ce) {
        switch (ce.GetId()) {
            case A_TRAY_RESTORE:
                if (IsShown() && !IsIconized())
                    Iconize(true);
                else
                    DeIconize();
                break;
            case wxID_NEW:
            case wxID_OPEN:
                DeIconize();
                OnMenu(ce);
                break;
            default: OnMenu(ce); break;
        }
    }

    void OnClosing(wxCloseEvent &ce) {
        bool fcb = fromclosebox;
        fromclosebox = true;
        if (fcb && sys->minclose) {
            ce.Veto();
            Iconize();
            return;
        }
        sys->RememberOpenFiles();
        if (ce.CanVeto()) {
            // ask to save/discard all files before closing any
            loop(i, notebook->GetPageCount()) {
                auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
                if (canvas->doc->modified) {
                    notebook->SetSelection(i);
                    if (canvas->doc->CheckForChanges()) {
                        ce.Veto();
                        return;
                    }
                }
            }
            // all files have been saved/discarded
            while (notebook->GetPageCount()) {
                GetCurrentTab()->doc->RemoveTmpFile();
                notebook->DeletePage(notebook->GetSelection());
            }
        }
        sys->every_second_timer.Stop();
        filehistory.Save(*sys->cfg);
        #ifdef ENABLE_LOBSTER
            auto oldpath = sys->cfg->GetPath();
            sys->cfg->SetPath("/scripts");
            scripts.Save(*sys->cfg);
            sys->cfg->SetPath(oldpath);
        #endif
        if (!IsIconized()) {
            sys->cfg->Write("maximized", IsMaximized());
            if (!IsMaximized()) {
                sys->cfg->Write("resx", GetSize().x);
                sys->cfg->Write("resy", GetSize().y);
                sys->cfg->Write("posx", GetPosition().x);
                sys->cfg->Write("posy", GetPosition().y);
            }
        }
        sys->cfg->Write("notesizex", sys->notesizex);
        sys->cfg->Write("notesizey", sys->notesizey);
        sys->cfg->Write("toolbarlayoutversion", toolbarlayoutversion);
        sys->cfg->Write("perspective", aui.SavePerspective());
        sys->cfg->Write("lastcellcolor", sys->lastcellcolor);
        sys->cfg->Write("lasttextcolor", sys->lasttextcolor);
        sys->cfg->Write("lastbordcolor", sys->lastbordcolor);
        UnregisterGlobalShowHotKey();
        aui.ClearEventHashTable();
        aui.UnInit();
        DELETEP(editmenupopup);
        DELETEP(watcher);
        Destroy();
    }

    void OnFileSystemEvent(wxFileSystemWatcherEvent &event) {
        // 0xF == create/delete/rename/modify
        if ((event.GetChangeType() & 0xF) == 0 || !notebook) return;
        if (ExplorerShouldRefreshForEvent(event))
            ScheduleExplorerRefresh(event.GetNewPath().GetFullPath());
        if (watcherwaitingforuser) return;
        const auto &modfile = event.GetPath().GetFullPath();
        loop(i, notebook->GetPageCount()) {
            Document *doc = static_cast<TSCanvas *>(notebook->GetPage(i))->doc.get();
            if (SameNormalizedPath(modfile, doc->filename)) {
                ReloadDocumentIfChangedOnDisk(doc, i);
                return;
            }
        }
    }

    void OnDPIChanged(wxDPIChangedEvent &dce) {
        // block all other events until we finished preparing
        wxEventBlocker blocker(this);
        wxBusyCursor wait;
        {
            ThreadPool pool(std::thread::hardware_concurrency());
            for (const auto &image : sys->imagelist) {
                pool.enqueue(
                    [](auto img) {
                        img->bm_display = wxNullBitmap;
                        img->Display();
                    },
                    image.get());
            }
        }  // wait until all tasks are finished
        RenderFolderIcon();
        dce.Skip();
    }

    void OnSysColourChanged(wxSysColourChangedEvent &se) {
        sys->colormask =
            (sys->followdarkmode && wxSystemSettings::GetAppearance().IsDark()) ? 0x00FFFFFF : 0;
        RefreshToolBarKeepingPerspective();
        aui.Update();
        se.Skip();
    }

    // helper functions

    void CycleTabs(int offset = 1) {
        auto numtabs = static_cast<int>(notebook->GetPageCount());
        offset = offset >= 0 ? 1 : numtabs - 1;  // normalize to non-negative wrt modulo
        notebook->SetSelection((notebook->GetSelection() + offset) % numtabs);
    }

    void DeIconize() {
        if (IsShown() && !IsIconized()) {
            Raise();
            RequestUserAttention();
            ReFocus();
            return;
        }
        ShowMainInterface();
    }

    void ShowMainInterface(bool focus_explorer_search = false) {
        Show(true);
        if (IsIconized()) Iconize(false);
        UpdateTrayIcon();
        Raise();
        #ifdef __WXMSW__
            if (GetHWND()) ::SetForegroundWindow((HWND)GetHWND());
        #endif
        if (focus_explorer_search) {
            FocusExplorerSearch();
            CallAfter([this] { FocusExplorerSearch(); });
        } else {
            ReFocus();
        }
    }

    TSCanvas *GetCurrentTab() {
        return notebook ? static_cast<TSCanvas *>(notebook->GetCurrentPage()) : nullptr;
    }

    TSCanvas *GetTabByFileName(const wxString &filename) {
        if (notebook) loop(i, notebook->GetPageCount()) {
                auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
                if (canvas->doc->filename == filename) {
                    notebook->SetSelection(i);
                    return canvas;
                }
            }
        return nullptr;
    }

    void MyAppend(wxMenu *menu, int tag, const wxString &contents, const wxString &help = "") {
        auto item = contents;
        wxString key = "";
        if (int pos = contents.Find("\t"); pos >= 0) {
            item = contents.Mid(0, pos);
            key = contents.Mid(pos + 1);
        }
        key = sys->cfg->Read(item, key);
        auto newcontents = item;
        if (key.Length()) newcontents += "\t" + key;
        menu->Append(tag, newcontents, help);
        menustrings[item] = key;
        if (key.Length()) menuaccelerators[tag] = key;
    }

    TSCanvas *NewTab(unique_ptr<Document> doc, bool append = false, int insert_at = -1) {
        TSCanvas *canvas = new TSCanvas(this, notebook);
        doc->canvas = canvas;
        canvas->doc = std::move(doc);
        canvas->SetScrollRate(1, 1);
        if (insert_at >= 0)
            notebook->InsertPage(insert_at, canvas, _("<unnamed>"), true, wxNullBitmap);
        else if (append)
            notebook->AddPage(canvas, _("<unnamed>"), true, wxNullBitmap);
        else
            notebook->InsertPage(0, canvas, _("<unnamed>"), true, wxNullBitmap);
        canvas->SetDropTarget(new DropTarget(canvas->doc->dndobjc));
        canvas->SetFocus();
        return canvas;
    }

    void ReFocus() {
        if (TSCanvas *canvas = GetCurrentTab()) canvas->SetFocus();
    }

    bool ParseGlobalHotKey(const wxString &spec, int &modifiers, int &keycode) {
        modifiers = wxMOD_NONE;
        keycode = 0;
        auto parts = wxStringTokenize(spec.Upper(), "+", wxTOKEN_STRTOK);
        for (auto part : parts) {
            part.Trim(true);
            part.Trim(false);
            if (part.empty()) continue;

            if (part == "ALT") {
                modifiers |= wxMOD_ALT;
            } else if (part == "CTRL" || part == "CONTROL") {
                modifiers |= wxMOD_CONTROL;
            } else if (part == "SHIFT") {
                modifiers |= wxMOD_SHIFT;
            } else if (part == "CMD" || part == "COMMAND") {
                modifiers |= wxMOD_CMD;
            } else if (part == "WIN" || part == "META" || part == "SUPER") {
                modifiers |= wxMOD_WIN;
            } else if (part == "ESC" || part == "ESCAPE") {
                keycode = WXK_ESCAPE;
            } else if (part == "SPACE") {
                keycode = WXK_SPACE;
            } else if (part == "TAB") {
                keycode = WXK_TAB;
            } else if (part == "ENTER" || part == "RETURN") {
                keycode = WXK_RETURN;
            } else {
                wxString number;
                long function_key = 0;
                if (part.StartsWith("F", &number) && number.ToLong(&function_key) &&
                    function_key >= 1 && function_key <= 24) {
                    keycode = WXK_F1 + static_cast<int>(function_key) - 1;
                } else if (part.Length() == 1) {
                    keycode = static_cast<int>(part.GetChar(0).GetValue());
                } else {
                    return false;
                }
            }
        }
        return modifiers != wxMOD_NONE && keycode != 0;
    }

    bool RegisterGlobalShowHotKey() {
        UnregisterGlobalShowHotKey();
        if (!sys->globalshowhotkeyenabled) return false;

        int modifiers = wxMOD_NONE;
        int keycode = 0;
        if (!ParseGlobalHotKey(sys->globalshowhotkey, modifiers, keycode)) {
            wxLogWarning("Invalid global show hotkey: %s", sys->globalshowhotkey);
            return false;
        }

        #if wxUSE_HOTKEY
            globalshowhotkeyregistered =
                RegisterHotKey(A_GLOBALSHOWHOTKEY, modifiers, keycode);
            if (!globalshowhotkeyregistered)
                wxLogWarning("Could not register global show hotkey: %s",
                             sys->globalshowhotkey);
            return globalshowhotkeyregistered;
        #else
            wxLogWarning("Global show hotkeys are not supported by this wxWidgets build.");
            return false;
        #endif
    }

    void UnregisterGlobalShowHotKey() {
        #if wxUSE_HOTKEY
            if (globalshowhotkeyregistered) {
                UnregisterHotKey(A_GLOBALSHOWHOTKEY);
                globalshowhotkeyregistered = false;
            }
        #endif
    }

    void RenderFolderIcon() {
        wxImage foldiconi;
        foldiconi.LoadFile(app->GetDataPath("images/nuvola/fold.png"));
        foldicon = wxBitmap(foldiconi);
        ScaleBitmap(foldicon, FromDIP(1.0) / 3.0, foldicon);
    }

    void SetDPIAwareStatusWidths() {
        int statusbarfieldwidths[] = {-1, FromDIP(300), FromDIP(120), FromDIP(100), FromDIP(150)};
        SetStatusWidths(5, statusbarfieldwidths);
    }

    void SetFileAssoc(const wxString &exename) {
        #ifdef WIN32
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\.cts", "TreeSheets");
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets", "TreeSheets file");
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\Shell\\Open\\Command",
                       "\"" + exename + "\" \"%1\"");
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\DefaultIcon",
                       "\"" + exename + "\",0");
        #else
        // TODO: do something similar for mac/kde/gnome?
        #endif
    }

    void SetPageTitle(const wxString &filename, wxString mods, int page = -1) {
        if (page < 0) page = notebook->GetSelection();
        if (page < 0) return;
        if (page == notebook->GetSelection()) SetTitle("TreeSheets - " + filename + mods);
        notebook->SetPageText(
            page,
            (filename.empty() ? wxString(_("<unnamed>")) : wxFileName(filename).GetName()) + mods);
    }

    #ifdef WIN32
    void SetRegistryKey(const wxString &key, wxString value) {
        wxRegKey registrykey(key);
        registrykey.Create();
        registrykey.SetValue("", value);
    }
    #endif

    void SetStatus(const wxString &message) {
        if (GetStatusBar() && !message.IsEmpty()) SetStatusText(message, 0);
    }

    void ClearStatus() {
        if (GetStatusBar()) SetStatusText("", 0);
    }

    void TabsReset() {
        if (notebook) loop(i, notebook->GetPageCount()) {
                auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
                canvas->doc->root->ResetChildren();
            }
    }

    void UpdateStatus(const Selection &s, bool updateamount) {
        if (GetStatusBar()) {
            if (Cell *c = s.GetCell(); c && s.xs) {
                SetStatusText(wxString::Format(_("Size %d"), -c->text.relsize), 3);
                SetStatusText(wxString::Format(_("Width %d"), s.grid->colwidths[s.x]), 2);
                SetStatusText(wxString::Format(_("Edited %s %s"), c->text.lastedit.FormatDate(),
                                               c->text.lastedit.FormatTime()),
                              1);
            } else
                for (int field : {1, 2, 3}) SetStatusText("", field);
            if (updateamount) SetStatusText(wxString::Format(_("%d cell(s)"), s.xs * s.ys), 4);
        }
    }

    DECLARE_EVENT_TABLE()
};
