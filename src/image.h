struct Image {
    vector<uint8_t> data;
    char type;
    wxBitmap bm_source;
    wxBitmap bm_display;
    int trefc {0};
    int savedindex {-1};
    uint64_t hash {0};

    // This indicates a relative scale, where 1.0 means bitmap pixels match display pixels on
    // a low res 96 dpi display. On a high dpi screen it will look scaled up. Higher values
    // look better on most screens.
    // This is all relative to GetContentScalingFactor.
    double display_scale;
    int pixel_width {0};

    Image(auto _hash, auto _sc, auto &&_data, auto _type)
        : hash(_hash), display_scale(_sc), data(std::move(_data)), type(_type) {}

    void ResetBitmapCache() {
        bm_source = wxNullBitmap;
        bm_display = wxNullBitmap;
        pixel_width = 0;
    }

    void ReplaceData(auto &&_data, char _type) {
        data = std::move(_data);
        type = _type;
        hash = CalculateHash(data);
        ResetBitmapCache();
    }

    void ImageRescale(double scale) {
        auto &[it, mime] = imagetypes.at(type);
        auto im = ConvertBufferToWxImage(data, it);
        im.Rescale(max(1, static_cast<int>(std::lround(im.GetWidth() * scale))),
                   max(1, static_cast<int>(std::lround(im.GetHeight() * scale))),
                   wxIMAGE_QUALITY_HIGH);
        ReplaceData(ConvertWxImageToBuffer(im, it), type);
    }

    wxBitmap &Bitmap() {
        if (!bm_source.IsOk()) {
            auto &[it, mime] = imagetypes.at(type);
            bm_source = ConvertBufferToWxBitmap(data, it);
            pixel_width = bm_source.GetWidth();
        }
        return bm_source;
    }

    void ResetDisplayCache() {
        bm_display = wxNullBitmap;
    }

    void DisplayScale(double scale) {
        display_scale /= scale;
        ResetDisplayCache();
    }

    void ResetScale(double scale) {
        display_scale = scale;
        ResetDisplayCache();
    }

    wxBitmap &Display() {
        // This might run in multiple threads in parallel
        // so this function must not touch any global resources
        // and callees must be thread-safe.
        if (!bm_display.IsOk()) {
            auto &bm = Bitmap();
            pixel_width = bm.GetWidth();
            auto dpi_scale = sys->frame ? sys->frame->FromDIP(1.0) : 1.0;
            ScaleBitmap(bm, dpi_scale / display_scale, bm_display);
        }
        return bm_display;
    }

    bool ExportToDirectory(const wxString &directory) {
        wxString targetname = directory + wxString::Format("%llu", hash) + GetFileExtension();
        wxFFileOutputStream os(targetname, "w+b");
        if (!os.IsOk()) {
            wxMessageBox(_("Error writing image file!"), targetname.wx_str(), wxOK, sys->frame);
            return false;
        }
        os.Write(data.data(), data.size());
        return true;
    }

    wxString GetFileExtension() {
        switch (type) {
            case 'J': return ".jpg";
            case 'I':
            default: return ".png";
        }
    }
};
