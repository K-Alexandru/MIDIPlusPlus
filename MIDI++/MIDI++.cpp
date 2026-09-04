#include "PlaybackSystem.hpp"
#include "TrackControl.hpp"
#include "VelocityCurveEditor.hpp"
#include "MIDI2Key.hpp"
#include "MIDIConnect.hpp"
#include "MIDIDeviceUI.hpp"
#include "resource.h"
#include "Theme.hpp"

#include <CommCtrl.h>
#include <GdiPlus.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>     
#include <filesystem>
#include <future>
#include <functional>
#include <iostream>
#include <locale>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <cwchar>
#include <windowsx.h>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// -----------------------------------------------------------------------------
// RAII wrappers for HANDLE and GDI+ token
// -----------------------------------------------------------------------------
struct UniqueHandle {
    HANDLE handle;
    UniqueHandle(HANDLE h = nullptr) : handle(h) {}
    ~UniqueHandle() {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    operator HANDLE() const { return handle; }
};

struct GdiplusTokenWrapper {
    ULONG_PTR token;
    GdiplusTokenWrapper() : token(0) {}
    ~GdiplusTokenWrapper() {
        if (token != 0)
            Gdiplus::GdiplusShutdown(token);
    }
};

// -----------------------------------------------------------------------------
// Global objects and variables
// -----------------------------------------------------------------------------
class VirtualPianoPlayer* g_player = nullptr;
static std::unique_ptr<MIDI2Key>   g_midi2key;
static std::unique_ptr<MIDIConnect> g_midiConnect;
static TrackControl g_trackControl;
int g_sustainCutoff = 64;
static int g_selectedMidiDevice = 0;    // device index
static int g_selectedMidiChannel = -1;    // -1 means “All channels”

// Global handles and states
static HINSTANCE    g_hInst = nullptr;
static HWND         g_hMainWnd = nullptr;
static HANDLE       g_hSingleInstanceMutex = nullptr;

static std::mutex        g_logMutex;
static std::string       g_logBuffer;
static std::atomic<bool> g_guiReady{ false };

static std::chrono::steady_clock::time_point g_lastTimeUpdate;
static constexpr auto TIME_UPDATE_INTERVAL = std::chrono::milliseconds(500);

static const std::regex g_ansiPattern("\x1B\\[[0-9;]*[A-Za-z]");

static std::unordered_map<int, bool> g_toggleStates;

static bool g_randomSongEnabled = false;


// -----------------------------------------------------------------------------
// Layout: design values are authored at 96 DPI and scaled once at startup.
// Everything below the roots is derived, so nothing needs hand-tuning.
// -----------------------------------------------------------------------------
namespace Layout {
    // ============ ROOT KNOBS (design units @ 96 DPI) ============
    static const int D_PAD        = 12;   // window edge padding
    static const int D_GAP        = 10;   // gap between cards
    static const int D_SIDEBAR_W  = 440;  // <<< MIDI file browser width
    static const int D_RIGHT_W    = 640;  // right-hand control column width
    static const int D_CARD_PAD   = 12;   // padding inside a card
    static const int D_HEADER_H   = 30;   // card title strip height
    static const int D_ROW_H      = 28;   // standard control height
    static const int D_ROW_GAP    = 8;    // vertical gap between rows
    static const int D_BTN_W      = 84;   // standard button width
    static const int D_BTN_GAP    = 8;    // gap between buttons in a row
    static const int D_LABEL_H    = 20;
    static const int D_COMBO_W    = 140;
    static const int D_DET_EDIT_H = 112;
    static const int D_TRK_H      = 150;
    static const int D_LOG_H      = 170;

    // Scales an arbitrary design-unit value. Use for any literal pixel size.
    static inline int S(int v) { return Theme::S(v); }

    // ============ RUNTIME VALUES (filled in by Init) ============
    static int PAD, GAP, SIDEBAR_W, RIGHT_W, CARD_PAD, HEADER_H, ROW_H, ROW_GAP;
    static int BTN_W, BTN_GAP, LABEL_H, COMBO_W, DET_EDIT_H;
    static int LEFT_X, RIGHT_X, TOP_Y;
    static int PBASIC_H, PADV_H, CFG_H, DET_H;
    static int PBASIC_X, PBASIC_W, PBASIC_Y;
    static int PADV_X,   PADV_W,   PADV_Y;
    static int CFG_X,    CFG_W,    CFG_Y;
    static int DET_X,    DET_W,    DET_Y;
    static int FILES_X,  FILES_Y,  FILES_W, FILES_H;
    static int FULL_W;
    static int TRK_X, TRK_W, TRK_H, TRK_Y;
    static int LOG_X, LOG_W, LOG_H, LOG_Y;
    static int CLIENT_W, CLIENT_H;

    // Computes every derived value from the scaled roots. Call after Theme::SetDpi.
    static void Init() {
        PAD        = S(D_PAD);
        GAP        = S(D_GAP);
        SIDEBAR_W  = S(D_SIDEBAR_W);
        RIGHT_W    = S(D_RIGHT_W);
        CARD_PAD   = S(D_CARD_PAD);
        HEADER_H   = S(D_HEADER_H);
        ROW_H      = S(D_ROW_H);
        ROW_GAP    = S(D_ROW_GAP);
        BTN_W      = S(D_BTN_W);
        BTN_GAP    = S(D_BTN_GAP);
        LABEL_H    = S(D_LABEL_H);
        COMBO_W    = S(D_COMBO_W);
        DET_EDIT_H = S(D_DET_EDIT_H);

        LEFT_X  = PAD;
        RIGHT_X = LEFT_X + SIDEBAR_W + GAP;
        TOP_Y   = PAD;

        // Card heights expressed as the content they hold.
        PBASIC_H = HEADER_H + CARD_PAD + ROW_H + ROW_GAP + ROW_H + CARD_PAD;
        PADV_H   = PBASIC_H;
        CFG_H    = HEADER_H + CARD_PAD + ROW_H + CARD_PAD;
        DET_H    = HEADER_H + CARD_PAD + DET_EDIT_H + CARD_PAD;

        // Right column stack.
        PBASIC_X = RIGHT_X; PBASIC_W = RIGHT_W; PBASIC_Y = TOP_Y;
        PADV_X   = RIGHT_X; PADV_W   = RIGHT_W; PADV_Y   = PBASIC_Y + PBASIC_H + GAP;
        CFG_X    = RIGHT_X; CFG_W    = RIGHT_W; CFG_Y    = PADV_Y   + PADV_H   + GAP;
        DET_X    = RIGHT_X; DET_W    = RIGHT_W; DET_Y    = CFG_Y    + CFG_H    + GAP;

        // Left column height mirrors the right column so the two align exactly.
        FILES_X = LEFT_X;
        FILES_Y = TOP_Y;
        FILES_W = SIDEBAR_W;
        FILES_H = (DET_Y + DET_H) - TOP_Y;

        // Full-width rows beneath both columns.
        FULL_W = SIDEBAR_W + GAP + RIGHT_W;
        TRK_X = LEFT_X; TRK_W = FULL_W; TRK_H = S(D_TRK_H);
        TRK_Y = FILES_Y + FILES_H + GAP;
        LOG_X = LEFT_X; LOG_W = FULL_W; LOG_H = S(D_LOG_H);
        LOG_Y = TRK_Y + TRK_H + GAP;

        CLIENT_W = PAD + FULL_W + PAD;
        CLIENT_H = LOG_Y + LOG_H + PAD;
    }
}

// -----------------------------------------------------------------------------
// Small layout helpers so control placement reads as structure, not arithmetic.
// -----------------------------------------------------------------------------

// Lays controls out left-to-right on a single row.
struct RowCursor {
    int x, y, h, gap;
    RowCursor(int x_, int y_, int h_ = 0, int gap_ = 0)
        : x(x_), y(y_),
          h(h_ ? h_ : Layout::ROW_H),
          gap(gap_ ? gap_ : Layout::BTN_GAP) {}
    // Take the next slot of the given width and advance past it.
    RECT take(int w) {
        RECT r{ x, y, x + w, y + h };
        x += w + gap;
        return r;
    }
    void skip(int px) { x += px; }
};

// Describes the content area inside a card (below its title strip).
struct CardBody {
    int x, y, w;
    CardBody(int cardX, int cardY, int cardW)
        : x(cardX + Layout::CARD_PAD),
          y(cardY + Layout::HEADER_H + Layout::CARD_PAD),
          w(cardW - Layout::CARD_PAD * 2) {}
    int rowY(int index) const { return y + index * (Layout::ROW_H + Layout::ROW_GAP); }
    RowCursor row(int index, int h = 0) const { return RowCursor(x, rowY(index), h); }
    int right() const { return x + w; }
};

// A card is painted by the parent in WM_PAINT; no BS_GROUPBOX involved.
struct CardSpec {
    RECT rc;
    std::wstring title;
};
static std::vector<CardSpec> g_cards;

static void AddCard(int x, int y, int w, int h, const wchar_t* title) {
    CardSpec c;
    c.rc = RECT{ x, y, x + w, y + h };
    c.title = title;
    g_cards.push_back(c);
}

// Convenience accessors so control creation reads as (rect, w, h).
static inline int RW(const RECT& r) { return r.right - r.left; }
static inline int RH(const RECT& r) { return r.bottom - r.top; }

// Splits one card row into N equally sized columns. Used wherever a group of
// buttons should span the full card width regardless of how many there are.
struct RowSplit {
    int x, y, h, gap, w;
    RowSplit(const CardBody& b, int rowIndex, int n, int h_ = 0)
        : x(b.x), y(b.rowY(rowIndex)),
          h(h_ ? h_ : Layout::ROW_H),
          gap(Layout::BTN_GAP) {
        if (n < 1) n = 1;
        w = (b.w - gap * (n - 1)) / n;
    }
    RECT next() {
        RECT r{ x, y, x + w, y + h };
        x += w + gap;
        return r;
    }
};

// Opts the process into per-monitor DPI awareness so Windows renders the UI at
// native resolution instead of bitmap-stretching a 96 DPI window.
static void EnableDpiAwareness() {
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) {
        if (auto fn = reinterpret_cast<SetCtxFn>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"))) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
            if (fn(reinterpret_cast<HANDLE>(-4))) return;
            if (fn(reinterpret_cast<HANDLE>(-3))) return;  // PER_MONITOR_AWARE
        }
    }
    SetProcessDPIAware();
}

// Reads the system DPI, falling back to the desktop DC on older systems.
static int QuerySystemDpi() {
    using GetDpiFn = UINT(WINAPI*)(void);
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) {
        if (auto fn = reinterpret_cast<GetDpiFn>(GetProcAddress(u32, "GetDpiForSystem")))
            return static_cast<int>(fn());
    }
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi;
}

// Global sustain cutoff value box (edit control)
static HWND g_hSustainCutoffValueBox = nullptr;

// Global listbox for MIDI files (now showing folders & files) and other UI elements
static HWND g_lbMidi = nullptr;
static HWND g_editDetails = nullptr;
static HWND g_editTracks = nullptr;
static HWND g_hOpacityIndicatorBox = nullptr;

// -----------------------------------------------------------------------------
// Control IDs
// -----------------------------------------------------------------------------
enum ControlID {
    // ListBox and ComboBoxes
    ID_CB_SORT = 101,
    ID_BTN_REFRESH,
    ID_LB_MIDI,

    // Basic Playback Group
    ID_GRP_PLAY,
    ID_BTN_LOAD,
    ID_BTN_PLAY,
    ID_BTN_STOP,
    ID_BTN_SKIP,
    ID_BTN_REW,
    ID_BTN_SPEEDUP,
    ID_BTN_SPEEDDN,
    ID_BTN_RESTART,

    // MIDI -> QWERTY and device controls
    ID_BTN_MIDI2QWERTY,
    ID_CB_MIDIDEV,
    ID_CB_MIDICH,
    ID_BTN_MIDICONNECT,

    // Advanced / Extra
    ID_GRP_ADV,
    ID_BTN_88KEY,
    ID_BTN_VOLADJ,
    ID_BTN_VELOCITY,
    ID_BTN_SUSTAIN,
    ID_BTN_TRANSPOSE,
    ID_BTN_TRANSPOSEOUT,
    ID_CB_VELOCITY_CURVE,
    ID_SLIDER_SUSTAIN_CUTOFF,
    ID_STATIC_SUSTAIN_LABEL,

    // Config
    ID_GRP_CONFIG,
    ID_CHK_TOP,
    ID_CHK_RANDOM_SONG,   
    ID_SLIDER_OPACITY, 

    // Details
    ID_GRP_DETAILS,
    ID_EDIT_DETAILS,

    // Tracks
    ID_GRP_TRACKS,
    ID_EDIT_TRACKS,

    // Log
    ID_GRP_LOG,
    ID_BTN_REFRESH_VCURVE,
    ID_EDIT_LOG,
    ID_BTN_CLEARLOG,
    ID_BTN_REFRESH_MIDI,
    ID_BTN_VLCURVE,

    // Custom messages and timers
    WM_UPDATE_LOG = WM_APP + 101,
    IDT_TIMELEFT_TIMER,
    ID_STATIC_TIME,

    // Track Mute/Solo button bases
    ID_TRACK_MUTE_BASE = 2000,
    ID_TRACK_SOLO_BASE = 2500,
    ID_BTN_PREV_SONG = 3000,
    ID_BTN_NEXT_SONG = 3001
};

static bool IsToggleButtonID(int id) {
    switch (id) {
    case ID_BTN_88KEY:
    case ID_BTN_VOLADJ:
    case ID_BTN_VELOCITY:
    case ID_BTN_SUSTAIN:
    case ID_BTN_TRANSPOSEOUT:
    case ID_BTN_MIDI2QWERTY:
    case ID_BTN_MIDICONNECT:
        return true;
    default:
        return false;
    }
}

// -----------------------------------------------------------------------------
// MIDI Folder Scanning and Sorting (with folder exploration)
// -----------------------------------------------------------------------------

struct MidiItem {
    std::wstring name;
    std::wstring fullPath;
    bool isFolder;
    std::time_t lastWrite;
    uintmax_t   fileSize = 0;
};

static std::vector<MidiItem> g_midiItems;
static std::filesystem::path g_currentMidiDir = L"midi";
// bunch of kids
std::string getReadableKey(const std::string& key) {
    const std::string prefix = "VK_";
    if (key.compare(0, prefix.size(), prefix) == 0) {
        return key.substr(prefix.size());
    }
    return key;
}
static void ScanMidiFolder() {
    g_midiItems.clear();
    std::filesystem::path currentDir = g_currentMidiDir;
    if (!std::filesystem::exists(currentDir) || !std::filesystem::is_directory(currentDir)) {
        std::wcout << L"[Scan] '" << currentDir.wstring() << L"' not found.\n";
        return;
    }
  
    if (!std::filesystem::equivalent(currentDir, "midi")) {
        MidiItem parentItem;
        parentItem.name = L"..";
        parentItem.fullPath = currentDir.parent_path().wstring();
        parentItem.isFolder = true;
        parentItem.lastWrite = 0;
        g_midiItems.push_back(parentItem);
    }
    for (const auto& entry : std::filesystem::directory_iterator(currentDir)) {
        MidiItem item;
        item.name = entry.path().filename().wstring();
        item.fullPath = entry.path().wstring();
        item.isFolder = entry.is_directory();
        if (!item.isFolder) {
            auto ext = entry.path().extension().wstring();
            std::wstring lw(ext.size(), L'\0');
            std::transform(ext.begin(), ext.end(), lw.begin(), ::towlower);
            if (lw != L".mid" && lw != L".midi")
                continue; // skip non-midi files
            auto ftime = std::filesystem::last_write_time(entry.path());
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            item.lastWrite = std::chrono::system_clock::to_time_t(sctp);
            std::error_code sizeEc;
            item.fileSize = std::filesystem::file_size(entry.path(), sizeEc);
            if (sizeEc) item.fileSize = 0;
        }
        else {
            item.lastWrite = 0;
        }
        g_midiItems.push_back(item);
    }
}

static int GetSortMode() {
    HWND cbSort = GetDlgItem(g_hMainWnd, ID_CB_SORT);
    if (!cbSort)
        return 0; // default to "Name (A-Z)"
    return static_cast<int>(SendMessage(cbSort, CB_GETCURSEL, 0, 0));
}

static void SortMidiItems() {
    int sortMode = GetSortMode();

    std::vector<MidiItem> parentItems;
    std::vector<MidiItem> folders;
    std::vector<MidiItem> files;

    for (const auto& item : g_midiItems) {
        if (item.name == L"..")
            parentItems.push_back(item);
        else if (item.isFolder)
            folders.push_back(item);
        else
            files.push_back(item);
    }

    // Sorting function for folders.
    auto folderSort = [sortMode](const MidiItem& a, const MidiItem& b) -> bool {
        // Folders don’t have a valid date, so we sort them by name.
        switch (sortMode) {
        case 0: // Name (A-Z)
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        case 1: // Name (Z-A)
            return _wcsicmp(a.name.c_str(), b.name.c_str()) > 0;
        default:
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        }
        };

    auto fileSort = [sortMode](const MidiItem& a, const MidiItem& b) -> bool {
        bool aFav = (std::filesystem::path(a.fullPath).parent_path().filename() == L"favorite");
        bool bFav = (std::filesystem::path(b.fullPath).parent_path().filename() == L"favorite");
        if (aFav != bFav)
            return aFav; 

        switch (sortMode) {
        case 0: // Name (A-Z)
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        case 1: // Name (Z-A)
            return _wcsicmp(a.name.c_str(), b.name.c_str()) > 0;
        case 2: // Date (Old-New)
            return a.lastWrite < b.lastWrite;
        case 3: // Date (New-Old)
            return a.lastWrite > b.lastWrite;
        case 4: // Size (Large-Small)
            if (a.fileSize != b.fileSize)
                return a.fileSize > b.fileSize;
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        case 5: // Size (Small-Large)
            if (a.fileSize != b.fileSize)
                return a.fileSize < b.fileSize;
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        default:
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        }
        };

    std::sort(folders.begin(), folders.end(), folderSort);
    std::sort(files.begin(), files.end(), fileSort);

    g_midiItems.clear();
    for (const auto& p : parentItems)
        g_midiItems.push_back(p);
    for (const auto& f : folders)
        g_midiItems.push_back(f);
    for (const auto& f : files)
        g_midiItems.push_back(f);
}

static void RefreshVelocityCurveCombo(HWND hWnd) {
    HWND cbVelocity = GetDlgItem(hWnd, ID_CB_VELOCITY_CURVE);
    SendMessage(cbVelocity, CB_RESETCONTENT, 0, 0);

    SendMessageW(cbVelocity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Linear Coarse"));
    SendMessageW(cbVelocity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Linear Fine"));
    SendMessageW(cbVelocity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Improved Low Volume"));
    SendMessageW(cbVelocity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Logarithmic"));
    SendMessageW(cbVelocity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Exponential"));

    const auto& customCurves = midi::Config::getInstance().playback.customVelocityCurves;
    for (const auto& curve : customCurves) {
        std::wstring wCurveName(curve.name.begin(), curve.name.end());
        SendMessageW(cbVelocity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wCurveName.c_str()));
    }

    SendMessage(cbVelocity, CB_SETCURSEL, 0, 0);
}

static void PopulateMidiList() {
    if (!g_lbMidi)
        return; // Guard against a NULL listbox handle

    SendMessage(g_lbMidi, LB_RESETCONTENT, 0, 0);
    int maxWidth = 0;
    HDC hdc = GetDC(g_lbMidi);
    HFONT hFont = reinterpret_cast<HFONT>(SendMessage(g_lbMidi, WM_GETFONT, 0, 0));
    if (hFont)
        SelectObject(hdc, hFont);

    for (const auto& item : g_midiItems) {
        std::wstring displayName;
        if (item.name == L"..") {
            displayName = L".. (Back)";
        }
        else if (item.isFolder) {
            displayName = item.name + L"\\";
        }
        else {
            displayName = item.name;
            std::filesystem::path filePath(item.fullPath);
            std::filesystem::path favFolder = std::filesystem::path(L"midi") / L"favorite";
            std::filesystem::path favFile = favFolder / filePath.filename();
            if (std::filesystem::exists(favFile))
                displayName = L"★ " + displayName;
        }
        SendMessageW(g_lbMidi, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(displayName.c_str()));

        SIZE textSize;
        GetTextExtentPoint32W(hdc, displayName.c_str(), static_cast<int>(displayName.size()), &textSize);
        if (textSize.cx > maxWidth)
            maxWidth = textSize.cx;
    }

    ReleaseDC(g_lbMidi, hdc);
    SendMessage(g_lbMidi, LB_SETHORIZONTALEXTENT, maxWidth, 0);
    if (SendMessage(g_lbMidi, LB_GETCOUNT, 0, 0) > 0)
        SendMessage(g_lbMidi, LB_SETCURSEL, 0, 0);
}

static std::wstring GetSelectedMidiFullPath() {
    int sel = static_cast<int>(SendMessage(g_lbMidi, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR || sel < 0 || sel >= static_cast<int>(g_midiItems.size()))
        return L"";
    const MidiItem& item = g_midiItems[sel];
    if (item.isFolder)
        return L""; 
    return item.fullPath;
}

// -----------------------------------------------------------------------------
// Logging (Redirect std::cout)
// -----------------------------------------------------------------------------
static std::streambuf* g_oldCoutBuf = nullptr;

static std::string GetTimeStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    sprintf_s(buf, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

class LogBuf : public std::streambuf {
    static constexpr size_t BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    bool startOfLine = true;
    std::string timestampCache;
    void updateTimestampCache() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buf[32];
        sprintf_s(buf, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        timestampCache = buf;
    }
protected:
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (n <= 0)
            return 0;
        std::string chunk;
        chunk.reserve(n + (n / 20) * timestampCache.size());
        size_t start = 0;
        for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
            if (startOfLine) {
                if (timestampCache.empty()) updateTimestampCache();
                chunk.append(timestampCache);
                startOfLine = false;
            }
            if (s[i] == '\n') {
                chunk.append(s + start, i - start + 1);
                start = i + 1;
                startOfLine = true;
            }
        }
        if (start < static_cast<size_t>(n))
            chunk.append(s + start, n - start);
        chunk = std::regex_replace(chunk, g_ansiPattern, "");
        {
            std::lock_guard<std::mutex> lk(g_logMutex);
            g_logBuffer += chunk;
        }
        if (g_guiReady.load(std::memory_order_acquire))
            PostMessage(g_hMainWnd, WM_UPDATE_LOG, 0, 0);
        return n;
    }
    int overflow(int c = EOF) override {
        if (c == EOF) return c;
        char ch = static_cast<char>(c);
        return xsputn(&ch, 1);
    }
};

static LogBuf g_logBuf;

static void RedirectCout() {
    if (!g_oldCoutBuf)
        g_oldCoutBuf = std::cout.rdbuf(&g_logBuf);
}

static void RestoreCout() {
    if (g_oldCoutBuf) {
        std::cout.rdbuf(g_oldCoutBuf);
        g_oldCoutBuf = nullptr;
    }
}

// -----------------------------------------------------------------------------
// UI Helper Functions
// -----------------------------------------------------------------------------
static void SetAlwaysOnTop(HWND hwnd, bool top) {
    SetWindowPos(hwnd, (top ? HWND_TOPMOST : HWND_NOTOPMOST),
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}
static void UpdateWindowFocusability() {
    if (!g_hMainWnd) return;
    bool shouldBeNoActivate = false;
    if ((g_midiConnect && g_midiConnect->IsActive()) ||
        (g_midi2key && g_midi2key->IsActive()) ||
        (g_player && g_player->midiFileSelected.load(std::memory_order_acquire) && !g_player->paused.load(std::memory_order_relaxed)))
    {
        shouldBeNoActivate = true;
    }
    LONG exStyle = GetWindowLong(g_hMainWnd, GWL_EXSTYLE);
    bool currentNoActivate = (exStyle & WS_EX_NOACTIVATE) != 0;
    if (shouldBeNoActivate != currentNoActivate) {
        if (shouldBeNoActivate)
            exStyle |= WS_EX_NOACTIVATE;
        else
            exStyle &= ~WS_EX_NOACTIVATE;
        SetWindowLong(g_hMainWnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(g_hMainWnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

// -----------------------------------------------------------------------------
// Drawing: everything below renders from the Theme namespace, so restyling the
// app is a matter of editing colours there rather than touching this code.
// -----------------------------------------------------------------------------

static inline Gdiplus::Color GpC(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// Fills and strokes a rounded rectangle. Pass a zero alpha border to skip the stroke.
static void GpRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                        const Gdiplus::Color& fill, const Gdiplus::Color& border,
                        float borderWidth = 1.0f) {
    Gdiplus::GraphicsPath path;
    float d = radius * 2.0f;
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::SolidBrush b(fill);
    g.FillPath(&b, &path);
    if (border.GetAlpha() != 0) {
        Gdiplus::Pen p(border, borderWidth);
        g.DrawPath(&p, &path);
    }
}

// Applies the UI font to every child control, with a monospace face for the
// read-only text panes.
static void ApplyThemeFonts(HWND hWnd) {
    EnumChildWindows(hWnd, [](HWND child, LPARAM) -> BOOL {
        int id = GetDlgCtrlID(child);
        HFONT f = (id == ID_EDIT_LOG || id == ID_EDIT_DETAILS || id == ID_EDIT_TRACKS)
                      ? Theme::Mono()
                      : Theme::UI();
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
        return TRUE;
    }, 0);
}

// Paints the window background and every registered card. Controls are children
// and draw themselves on top of this.
static void DrawCards(HDC hdc, const RECT& client) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::SolidBrush bg(GpC(Theme::WINDOW_BG));
    g.FillRectangle(&bg, (INT)client.left, (INT)client.top,
                    (INT)(client.right - client.left), (INT)(client.bottom - client.top));

    for (const auto& c : g_cards) {
        Gdiplus::RectF rf((Gdiplus::REAL)c.rc.left + 0.5f,
                          (Gdiplus::REAL)c.rc.top + 0.5f,
                          (Gdiplus::REAL)(c.rc.right - c.rc.left) - 1.0f,
                          (Gdiplus::REAL)(c.rc.bottom - c.rc.top) - 1.0f);
        GpRoundRect(g, rf, (float)Theme::CardCornerRadius(),
                    GpC(Theme::CARD_BG), GpC(Theme::CARD_BORDER));
    }

    // Titles go through GDI so they get ClearType rather than GDI+ greyscale AA.
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, Theme::UITitle()));
    SetTextColor(hdc, Theme::TEXT);
    for (const auto& c : g_cards) {
        RECT t{ c.rc.left + Layout::CARD_PAD, c.rc.top,
                c.rc.right - Layout::CARD_PAD, c.rc.top + Layout::HEADER_H };
        DrawTextW(hdc, c.title.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);
}

static void DrawFancyButton(const DRAWITEMSTRUCT* dis) {
    if (dis->CtlType != ODT_BUTTON)
        return;

    int ctrlID = static_cast<int>(dis->CtlID);
    bool togglable = IsToggleButtonID(ctrlID);
    bool toggled = togglable ? g_toggleStates[ctrlID] : false;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool isHot = (dis->itemState & ODS_HOTLIGHT) != 0;
    bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
    bool isFocused = (dis->itemState & ODS_FOCUS) != 0;

    COLORREF fill = Theme::BTN_BG;
    COLORREF border = Theme::BTN_BORDER;
    COLORREF text = Theme::TEXT;

    if (ctrlID == ID_BTN_SUSTAIN && g_player != nullptr) {
        switch (g_player->currentSustainMode) {
        case SustainMode::IG:
            break;
        case SustainMode::SPACE_DOWN:
            fill = Theme::SUS_DOWN_BG; border = Theme::SUS_DOWN_BORDER; text = Theme::ON_TEXT;
            break;
        case SustainMode::SPACE_UP:
            fill = Theme::SUS_UP_BG;   border = Theme::SUS_UP_BORDER;   text = Theme::TEXT;
            break;
        }
    }
    else if (togglable && toggled) {
        fill = Theme::ON_BG; border = Theme::ON_BORDER; text = Theme::ON_TEXT;
    }

    if (isPressed) { fill = Theme::BTN_PRESS; border = Theme::ACCENT; }
    else if (isHot) { fill = Theme::BTN_HOVER; border = Theme::ACCENT; }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Paint the card colour first so the area outside the rounded corners blends in.
    Gdiplus::SolidBrush cardBg(GpC(Theme::CARD_BG));
    g.FillRectangle(&cardBg, (INT)rc.left, (INT)rc.top,
                    (INT)(rc.right - rc.left), (INT)(rc.bottom - rc.top));

    Gdiplus::RectF rf((Gdiplus::REAL)rc.left + 0.5f,
                      (Gdiplus::REAL)rc.top + 0.5f,
                      (Gdiplus::REAL)(rc.right - rc.left) - 1.0f,
                      (Gdiplus::REAL)(rc.bottom - rc.top) - 1.0f);
    GpRoundRect(g, rf, (float)Theme::CornerRadius(), GpC(fill), GpC(border));

    wchar_t txt[128] = { 0 };
    GetWindowTextW(dis->hwndItem, txt, 128);

    SetTextColor(hdc, text);
    SetBkMode(hdc, TRANSPARENT);
    HFONT hFontToUse = (togglable && toggled) ? Theme::UIBold() : Theme::UI();
    HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFontToUse));
    DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, hOldFont);

    if (isFocused) {
        RECT frc = rc;
        InflateRect(&frc, -3, -3);
        DrawFocusRect(hdc, &frc);
    }
}

// -----------------------------------------------------------------------------
// MIDI Details and Tracks Update Functions
// -----------------------------------------------------------------------------
static const char* GM_NAMES[128] = {
    "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano","Electric Piano 1","Electric Piano 2","Harpsichord","Clavi","Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
    "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion","Acoustic Guitar (nylon)","Acoustic Guitar (steel)","Electric Guitar (jazz)","Electric Guitar (clean)","Electric Guitar (muted)","Overdriven Guitar","Distortion Guitar","Guitar harmonics",
    "Acoustic Bass","Electric Bass (finger)","Electric Bass (pick)","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2","Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
    "String Ensemble 1","String Ensemble 2","SynthStrings 1","SynthStrings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit","Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","SynthBrass 1","SynthBrass 2",
    "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet","Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
    "Lead 1 (square)","Lead 2 (sawtooth)","Lead 3 (calliope)","Lead 4 (chiff)","Lead 5 (charang)","Lead 6 (voice)","Lead 7 (fifths)","Lead 8 (bass + lead)","Pad 1 (new age)","Pad 2 (warm)","Pad 3 (polysynth)","Pad 4 (choir)","Pad 5 (bowed)","Pad 6 (metallic)","Pad 7 (halo)","Pad 8 (sweep)",
    "FX 1 (rain)","FX 2 (soundtrack)","FX 3 (crystal)","FX 4 (atmosphere)","FX 5 (brightness)","FX 6 (goblins)","FX 7 (echoes)","FX 8 (sci-fi)","Sitar","Banjo","Shamisen","Koto","Kalimba","Bag pipe","Fiddle","Shanai",
    "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal","Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"
};

static void UpdateMidiDetails() {
    if (!g_player)
        return;
    MidiFile& mf = g_player->midi_file;
    SetWindowTextW(g_editDetails, L"");

    auto appendLine = [&](const std::wstring& line) {
        std::wstring s = line + L"\r\n";
        SendMessageW(g_editDetails, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(s.c_str()));
        };

    std::wstring wpath = GetSelectedMidiFullPath();
    if (!wpath.empty()) {
        std::filesystem::path p(wpath);
        appendLine(L"File: " + p.filename().wstring());
    }

    std::wostringstream oss;
    oss << std::left;
    oss.str(L"");
    oss << "Format: " << mf.format;
    switch (mf.format) {
    case 0: oss << " (single)"; break;
    case 1: oss << " (multi)"; break;
    case 2: oss << " (multi-song)"; break;
    }
    appendLine(oss.str());

    int activeTracks = 0;
    for (const auto& track : mf.tracks) {
        if (!track.events.empty())
            ++activeTracks;
    }
    oss.str(L"");
    oss << "Tracks: " << activeTracks << "/" << mf.numTracks;
    int totalNotes = 0;
    for (const auto& track : mf.tracks) {
        for (const auto& evt : track.events) {
            if ((evt.status & 0xF0) == 0x90 && evt.data2 > 0)
                ++totalNotes;
        }
    }
    oss << " (" << totalNotes << " notes)";
    appendLine(oss.str());

    if (!mf.tempoChanges.empty()) {
        double initialTempo = mf.tempoChanges[0].microsecondsPerQuarter;
        double bpm = 60000000.0 / initialTempo;
        oss.str(L"");
        oss << "Tempo: " << std::fixed << std::setprecision(1) << bpm << " BPM";
        if (mf.tempoChanges.size() > 1)
            oss << " (" << (mf.tempoChanges.size() - 1) << " changes)";
        appendLine(oss.str());
    }
    if (!mf.timeSignatures.empty()) {
        auto& ts = mf.timeSignatures[0];
        oss.str(L"");
        oss << "Time Sig: " << static_cast<int>(ts.numerator) << "/" << static_cast<int>(ts.denominator);
        if (mf.timeSignatures.size() > 1)
            oss << " (" << (mf.timeSignatures.size() - 1) << " changes)";
        appendLine(oss.str());
    }
    std::string modeStr;
    switch (midi::Config::getInstance().playback.noteHandlingMode) {
    case midi::NoteHandlingMode::FIFO: modeStr = "FIFO"; break;
    case midi::NoteHandlingMode::LIFO: modeStr = "LIFO"; break;
    default: modeStr = "None"; break;
    }
    std::string lastLine = "Note Mode: " + modeStr;
    bool filterDrums = midi::Config::getInstance().midi.DETECT_DRUMS;
    lastLine += (filterDrums ? " (Drum Detect: On)" : " (Ch10 Filter: Off)");
    SendMessageA(g_editDetails, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(lastLine.c_str()));
}
static void UpdateTrackInfo() {
    if (!g_player) {
        std::vector<TrackControl::TrackInfo> empty;
        g_trackControl.SetTracks(empty);
        return;
    }
    MidiFile& mf = g_player->midi_file;
    std::vector<TrackControl::TrackInfo> tracks;
    for (size_t trackIndex = 0; trackIndex < mf.tracks.size(); ++trackIndex) {
        TrackControl::TrackInfo info;
        info.isMuted = false;
        info.isSoloed = false;
        info.isDrums = false; // initialize flag

        if (trackIndex < g_player->trackMuted.size() && g_player->trackMuted[trackIndex])
            info.isMuted = g_player->trackMuted[trackIndex]->load(std::memory_order_acquire);
        if (trackIndex < g_player->trackSoloed.size() && g_player->trackSoloed[trackIndex])
            info.isSoloed = g_player->trackSoloed[trackIndex]->load(std::memory_order_acquire);

        const auto& track = mf.tracks[trackIndex];
        int noteCount = 0;
        std::unordered_map<int, int> channelCounts;
        std::string trackName;
        for (const auto& evt : track.events) {
            if ((evt.status & 0xF0) == 0x90 && evt.data2 > 0) {
                ++noteCount;
                channelCounts[evt.status & 0x0F]++;
            }
            if ((evt.status & 0xF0) == 0xC0)
                info.programNumber = evt.data1 & 0x7F;
            if (evt.status == 0xFF && evt.data1 == 0x03)
                trackName = std::string(evt.metaData.begin(), evt.metaData.end());
        }
        if (!channelCounts.empty()) {
            info.channel = std::max_element(channelCounts.begin(), channelCounts.end(),
                [](const auto& p1, const auto& p2) {
                    return p1.second < p2.second;
                })->first;
        }
        info.noteCount = noteCount;
        info.trackName = trackName.empty() ? ("Track " + std::to_string(trackIndex + 1)) : trackName;
        if (info.programNumber >= 0 && info.programNumber < 128) {
            info.instrumentName = GM_NAMES[info.programNumber];
            if (g_player->drum_flags.size() > trackIndex && g_player->drum_flags[trackIndex]) {
                info.instrumentName += " (Drums)";
                info.isDrums = true;
            }
        }
        else {
            info.instrumentName = "Unknown";
        }
        tracks.push_back(info);
    }
    g_trackControl.SetTracks(tracks);
}
static void FocusRobloxWindowInternal() {
    HWND hRb = FindWindowW(nullptr, L"Roblox");
    if (!hRb) {
        std::cerr << "[WARNING] Could not find Roblox window.\n";
        return;
    }
    if (!IsWindow(hRb)) {
        std::cerr << "[ERROR] Found handle is not a valid window.\n";
        return;
    }
    DWORD_PTR dwResult = 0;
    if (SendMessageTimeout(hRb, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 500, &dwResult) == 0) {
        std::cerr << "[WARNING] Roblox window is not responding; skipping focus.\n";
        return;
    }
    if (!IsWindowVisible(hRb)) {
        std::cerr << "[WARNING] Roblox window is not visible; skipping focus to avoid invasive changes.\n";
        return;
    }
    if (IsIconic(hRb)) {
        ShowWindow(hRb, SW_RESTORE);
    }
    else {
        ShowWindow(hRb, SW_SHOWNA);
    }
    if (!SetForegroundWindow(hRb)) {
        std::cerr << "[ERROR] Failed to bring Roblox window to foreground. Error code: " << GetLastError() << "\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

static void FocusRobloxWindow() {
    std::thread(FocusRobloxWindowInternal).detach();
}

static void ClearLog(HWND editLog) {
    if (!editLog) return;
    {
        std::lock_guard<std::mutex> lk(g_logMutex);
        g_logBuffer.clear();
    }
    if (!SetWindowTextW(editLog, L"")) {
        DWORD error = GetLastError();
        std::cerr << "Failed to clear log. Error code: " << error << std::endl;
    }
}

static void ToggleFavorite(int index) {
    if (index < 0 || index >= static_cast<int>(g_midiItems.size()))
        return;
    MidiItem& item = g_midiItems[index];
    if (item.isFolder)
        return;

    std::filesystem::path filePath(item.fullPath);
    std::error_code ec;
    if (filePath.parent_path().filename() == L"favorite") {
        std::filesystem::remove(filePath, ec);
        if (!ec) {
            std::filesystem::path origPath = std::filesystem::path(L"midi") / filePath.filename();
            item.fullPath = origPath.wstring();
        }
    }
    else {
        std::filesystem::path destFolder = std::filesystem::path(L"midi") / L"favorite";
        if (!std::filesystem::exists(destFolder)) {
            std::filesystem::create_directories(destFolder, ec);
            if (ec)
                return; // silently fail if folder creation fails
        }
        std::filesystem::path destFile = destFolder / filePath.filename();
        std::filesystem::copy_file(filePath, destFile, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            item.fullPath = destFile.wstring();
        }
    }

    if (g_lbMidi) {
        std::wstring displayName;
        if (item.name == L"..") {
            displayName = L".. (Back)";
        }
        else if (item.isFolder) {
            displayName = item.name + L"\\";
        }
        else {
            displayName = item.name;
            std::filesystem::path p(item.fullPath);
            if (p.parent_path().filename() == L"favorite")
                displayName = L"★ " + displayName;
        }
        SendMessageW(g_lbMidi, LB_DELETESTRING, index, 0);
        SendMessageW(g_lbMidi, LB_INSERTSTRING, index, reinterpret_cast<LPARAM>(displayName.c_str()));
    }
}

static LRESULT CALLBACK MidiListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (msg)
    {
    case WM_RBUTTONDOWN:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        int index = static_cast<int>(SendMessage(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y)));
        if (index != LB_ERR) {
            ToggleFavorite(index);
        }
        return 0;
    }
    default:
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
}
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        // temporary fix: this shit
    case WM_NCLBUTTONDOWN:
    {
        if ((g_midi2key && g_midi2key->IsActive()) ||
            (g_midiConnect && g_midiConnect->IsActive()) ||
            (g_player &&
                g_player->midiFileSelected.load(std::memory_order_acquire) &&
                !g_player->paused.load(std::memory_order_acquire) &&
                g_player->playback_started.load(std::memory_order_acquire) &&
                (g_player->buffer_index.load(std::memory_order_acquire) < g_player->note_buffer.size()) &&
                wParam == HTCAPTION))
        {
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }


    case WM_CREATE:
    {
        INITCOMMONCONTROLSEX icex = {};
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_BAR_CLASSES;
        InitCommonControlsEx(&icex);

        g_cards.clear();

        // =====================================================================
        // MIDI Files  (left column, full height)
        // =====================================================================
        AddCard(Layout::FILES_X, Layout::FILES_Y, Layout::FILES_W, Layout::FILES_H, L"MIDI Files");
        {
            CardBody body(Layout::FILES_X, Layout::FILES_Y, Layout::FILES_W);
            RowCursor r = body.row(0);

            RECT rcSort = r.take(Layout::COMBO_W);
            HWND cbSort = CreateWindowW(L"combobox", nullptr,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                rcSort.left, rcSort.top, RW(rcSort), 240,
                hWnd, reinterpret_cast<HMENU>(ID_CB_SORT), g_hInst, nullptr);
            SendMessageW(cbSort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Name (A-Z)"));
            SendMessageW(cbSort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Name (Z-A)"));
            SendMessageW(cbSort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Date (Old-New)"));
            SendMessageW(cbSort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Date (New-Old)"));
            SendMessageW(cbSort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Size (Large-Small)"));
            SendMessageW(cbSort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Size (Small-Large)"));
            SendMessageW(cbSort, CB_SETCURSEL, 0, 0);

            RECT rcRefresh = r.take(Layout::S(92));
            CreateWindowW(L"button", L"Refresh",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                rcRefresh.left, rcRefresh.top, RW(rcRefresh), RH(rcRefresh),
                hWnd, reinterpret_cast<HMENU>(ID_BTN_REFRESH), g_hInst, nullptr);

            // The list simply fills whatever height is left inside the card.
            int listTop = body.rowY(1);
            int listBottom = Layout::FILES_Y + Layout::FILES_H - Layout::CARD_PAD;
            g_lbMidi = CreateWindowW(L"listbox", nullptr,
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT,
                body.x, listTop, body.w, listBottom - listTop,
                hWnd, reinterpret_cast<HMENU>(ID_LB_MIDI), g_hInst, nullptr);
            SetWindowSubclass(g_lbMidi, MidiListSubclassProc, 0, 0);
        }

        // =====================================================================
        // Playback  (transport row, then MIDI I/O row)
        // =====================================================================
        AddCard(Layout::PBASIC_X, Layout::PBASIC_Y, Layout::PBASIC_W, Layout::PBASIC_H, L"Playback");
        {
            CardBody body(Layout::PBASIC_X, Layout::PBASIC_Y, Layout::PBASIC_W);

            // Row 0: seven equal transport buttons spanning the card width.
            RowSplit t(body, 0, 7);
            struct TransportBtn { const wchar_t* label; int id; };
            const TransportBtn transport[] = {
                { L"Load",       ID_BTN_LOAD    },
                { L"Play/Pause", ID_BTN_PLAY    },
                { L"Restart",    ID_BTN_RESTART },
                { L"Skip +10",   ID_BTN_SKIP    },
                { L"Rew -10",    ID_BTN_REW     },
                { L"Speed +",    ID_BTN_SPEEDUP },
                { L"Speed -",    ID_BTN_SPEEDDN },
            };
            for (const auto& b : transport) {
                RECT rc = t.next();
                CreateWindowW(L"button", b.label,
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    rc.left, rc.top, RW(rc), RH(rc),
                    hWnd, reinterpret_cast<HMENU>(b.id), g_hInst, nullptr);
            }

            // Row 1: MIDI input wiring, clock right-aligned.
            RowCursor r = body.row(1);
            RECT rcConnect = r.take(Layout::S(100));
            CreateWindowW(L"button", L"MidiConnect",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                rcConnect.left, rcConnect.top, RW(rcConnect), RH(rcConnect),
                hWnd, reinterpret_cast<HMENU>(ID_BTN_MIDICONNECT), g_hInst, nullptr);

            RECT rcDev = r.take(Layout::S(150));
            HWND cbMidiDev = CreateWindowW(L"combobox", nullptr,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                rcDev.left, rcDev.top, RW(rcDev), 240,
                hWnd, reinterpret_cast<HMENU>(ID_CB_MIDIDEV), g_hInst, nullptr);
            MIDIDeviceUI::PopulateMidiInDevices(cbMidiDev, g_selectedMidiDevice);

            RECT rcM2K = r.take(Layout::S(92));
            CreateWindowW(L"button", L"Midi2Key",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                rcM2K.left, rcM2K.top, RW(rcM2K), RH(rcM2K),
                hWnd, reinterpret_cast<HMENU>(ID_BTN_MIDI2QWERTY), g_hInst, nullptr);

            RECT rcCh = r.take(Layout::S(140));
            HWND cbMidiCh = CreateWindowW(L"combobox", nullptr,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                rcCh.left, rcCh.top, RW(rcCh), 240,
                hWnd, reinterpret_cast<HMENU>(ID_CB_MIDICH), g_hInst, nullptr);
            MIDIDeviceUI::PopulateChannelList(cbMidiCh, g_selectedMidiChannel);

            const int clockW = Layout::S(104);
            CreateWindowW(L"static", L"0:00 / 0:00",
                WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
                body.right() - clockW, body.rowY(1), clockW, Layout::ROW_H,
                hWnd, reinterpret_cast<HMENU>(ID_STATIC_TIME), g_hInst, nullptr);
        }

        // =====================================================================
        // Advanced
        // =====================================================================
        AddCard(Layout::PADV_X, Layout::PADV_Y, Layout::PADV_W, Layout::PADV_H, L"Advanced");
        {
            CardBody body(Layout::PADV_X, Layout::PADV_Y, Layout::PADV_W);

            RowSplit t(body, 0, 6);
            struct AdvBtn { const wchar_t* label; int id; };
            const AdvBtn adv[] = {
                { L"88-Key",    ID_BTN_88KEY        },
                { L"AutoVol",   ID_BTN_VOLADJ       },
                { L"Velocity",  ID_BTN_VELOCITY     },
                { L"Sustain",   ID_BTN_SUSTAIN      },
                { L"Transpose", ID_BTN_TRANSPOSE    },
                { L"OutRange",  ID_BTN_TRANSPOSEOUT },
            };
            for (const auto& b : adv) {
                RECT rc = t.next();
                CreateWindowW(L"button", b.label,
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    rc.left, rc.top, RW(rc), RH(rc),
                    hWnd, reinterpret_cast<HMENU>(b.id), g_hInst, nullptr);
            }

            RowCursor r = body.row(1);
            RECT rcSusLbl = r.take(Layout::S(96));
            CreateWindowW(L"static", L"Sustain Cutoff",
                WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                rcSusLbl.left, rcSusLbl.top, RW(rcSusLbl), RH(rcSusLbl),
                hWnd, reinterpret_cast<HMENU>(ID_STATIC_SUSTAIN_LABEL), g_hInst, nullptr);

            RECT rcSusSlider = r.take(Layout::S(168));
            HWND hSustainSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                rcSusSlider.left, rcSusSlider.top, RW(rcSusSlider), RH(rcSusSlider),
                hWnd, reinterpret_cast<HMENU>(ID_SLIDER_SUSTAIN_CUTOFF), g_hInst, nullptr);
            SendMessage(hSustainSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 127));
            SendMessage(hSustainSlider, TBM_SETTICFREQ, 16, 0);
            SendMessage(hSustainSlider, TBM_SETPOS, TRUE, g_sustainCutoff);

            RECT rcSusVal = r.take(Layout::S(46));
            g_hSustainCutoffValueBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"64",
                WS_CHILD | WS_VISIBLE | ES_READONLY | ES_CENTER,
                rcSusVal.left, rcSusVal.top + Layout::S(3), RW(rcSusVal), RH(rcSusVal) - Layout::S(6),
                hWnd, nullptr, g_hInst, nullptr);

            r.skip(Layout::S(12));
            RECT rcVelLbl = r.take(Layout::S(68));
            CreateWindowW(L"static", L"VelCurve",
                WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                rcVelLbl.left, rcVelLbl.top, RW(rcVelLbl), RH(rcVelLbl),
                hWnd, nullptr, g_hInst, nullptr);

            RECT rcVelCombo = r.take(Layout::S(160));
            CreateWindowW(L"combobox", nullptr,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                rcVelCombo.left, rcVelCombo.top, RW(rcVelCombo), 240,
                hWnd, reinterpret_cast<HMENU>(ID_CB_VELOCITY_CURVE), g_hInst, nullptr);
            RefreshVelocityCurveCombo(hWnd);
        }

        // =====================================================================
        // Config
        // =====================================================================
        AddCard(Layout::CFG_X, Layout::CFG_Y, Layout::CFG_W, Layout::CFG_H, L"Config");
        {
            CardBody body(Layout::CFG_X, Layout::CFG_Y, Layout::CFG_W);
            RowCursor r = body.row(0);

            RECT rcTop = r.take(Layout::S(118));
            HWND hChkAlwaysOnTop = CreateWindowW(L"button", L"Always On Top",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                rcTop.left, rcTop.top, RW(rcTop), RH(rcTop),
                hWnd, reinterpret_cast<HMENU>(ID_CHK_TOP), g_hInst, nullptr);
            bool alwaysOnTop = midi::Config::getInstance().ui.alwaysOnTop;
            SendMessage(hChkAlwaysOnTop, BM_SETCHECK, alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
            SetAlwaysOnTop(hWnd, alwaysOnTop);

            RECT rcShuffle = r.take(Layout::S(106));
            HWND hChkRandomSong = CreateWindowW(L"button", L"Shuffle Play",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                rcShuffle.left, rcShuffle.top, RW(rcShuffle), RH(rcShuffle),
                hWnd, reinterpret_cast<HMENU>(ID_CHK_RANDOM_SONG), g_hInst, nullptr);
            SendMessage(hChkRandomSong, BM_SETCHECK, g_randomSongEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

            RECT rcOpLbl = r.take(Layout::S(54));
            CreateWindowW(L"static", L"Opacity",
                WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                rcOpLbl.left, rcOpLbl.top, RW(rcOpLbl), RH(rcOpLbl),
                hWnd, nullptr, g_hInst, nullptr);

            RECT rcOpSlider = r.take(Layout::S(118));
            HWND hOpacitySlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                rcOpSlider.left, rcOpSlider.top, RW(rcOpSlider), RH(rcOpSlider),
                hWnd, reinterpret_cast<HMENU>(ID_SLIDER_OPACITY), g_hInst, nullptr);
            SendMessage(hOpacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(100, 255));
            SendMessage(hOpacitySlider, TBM_SETTICFREQ, 15, 0);
            SendMessage(hOpacitySlider, TBM_SETPOS, TRUE, 255);

            RECT rcOpVal = r.take(Layout::S(44));
            g_hOpacityIndicatorBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"255",
                WS_CHILD | WS_VISIBLE | ES_READONLY | ES_CENTER,
                rcOpVal.left, rcOpVal.top + Layout::S(3), RW(rcOpVal), RH(rcOpVal) - Layout::S(6),
                hWnd, nullptr, g_hInst, nullptr);

            // Prev / Next pinned to the right edge of the card.
            const int navW = Layout::S(56);
            CreateWindowW(L"button", L"Prev",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                body.right() - navW * 2 - Layout::BTN_GAP, body.rowY(0), navW, Layout::ROW_H,
                hWnd, reinterpret_cast<HMENU>(ID_BTN_PREV_SONG), g_hInst, nullptr);
            CreateWindowW(L"button", L"Next",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                body.right() - navW, body.rowY(0), navW, Layout::ROW_H,
                hWnd, reinterpret_cast<HMENU>(ID_BTN_NEXT_SONG), g_hInst, nullptr);
        }

        // =====================================================================
        // Details
        // =====================================================================
        AddCard(Layout::DET_X, Layout::DET_Y, Layout::DET_W, Layout::DET_H, L"Details");
        {
            CardBody body(Layout::DET_X, Layout::DET_Y, Layout::DET_W);
            g_editDetails = CreateWindowW(L"edit", L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER,
                body.x, body.y, body.w, Layout::DET_EDIT_H,
                hWnd, reinterpret_cast<HMENU>(ID_EDIT_DETAILS), g_hInst, nullptr);
        }

        // =====================================================================
        // Tracks
        // =====================================================================
        AddCard(Layout::TRK_X, Layout::TRK_Y, Layout::TRK_W, Layout::TRK_H, L"Tracks");
        {
            CardBody body(Layout::TRK_X, Layout::TRK_Y, Layout::TRK_W);
            int h = (Layout::TRK_Y + Layout::TRK_H - Layout::CARD_PAD) - body.y;
            g_trackControl.Create(hWnd, body.x, body.y, body.w, h);
        }

        // =====================================================================
        // Log
        // =====================================================================
        AddCard(Layout::LOG_X, Layout::LOG_Y, Layout::LOG_W, Layout::LOG_H, L"Log");
        {
            CardBody body(Layout::LOG_X, Layout::LOG_Y, Layout::LOG_W);
            const int sideW = Layout::S(108);
            int logH = (Layout::LOG_Y + Layout::LOG_H - Layout::CARD_PAD) - body.y;

            CreateWindowW(L"edit", L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER,
                body.x, body.y, body.w - sideW - Layout::BTN_GAP, logH,
                hWnd, reinterpret_cast<HMENU>(ID_EDIT_LOG), g_hInst, nullptr);

            struct SideBtn { const wchar_t* label; int id; };
            const SideBtn side[] = {
                { L"Clear Log",    ID_BTN_CLEARLOG       },
                { L"Refresh MIDI", ID_BTN_REFRESH_MIDI   },
                { L"Edit V-Curve", ID_BTN_VLCURVE        },
                { L"Ref V-List",   ID_BTN_REFRESH_VCURVE },
            };
            int sy = body.y;
            const int sh = Layout::S(26), sgap = Layout::S(6);
            for (const auto& b : side) {
                CreateWindowW(L"button", b.label,
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    body.right() - sideW, sy, sideW, sh,
                    hWnd, reinterpret_cast<HMENU>(b.id), g_hInst, nullptr);
                sy += sh + sgap;
            }
        }

        // Initialize Toggle States
        {
            std::vector<int> toggles = { ID_BTN_88KEY, ID_BTN_VOLADJ, ID_BTN_VELOCITY, ID_BTN_SUSTAIN, ID_BTN_TRANSPOSEOUT, ID_BTN_MIDI2QWERTY };
            for (int t : toggles)
                g_toggleStates[t] = false;
            if (g_player && g_player->eightyEightKeyModeActive)
                g_toggleStates[ID_BTN_88KEY] = true;
        }

        // One modern font across every child control.
        ApplyThemeFonts(hWnd);

        // Initial Setup
        ScanMidiFolder();
        SortMidiItems();
        PopulateMidiList();
        SetTimer(hWnd, IDT_TIMELEFT_TIMER, 200, nullptr);
        g_guiReady.store(true);
        PostMessage(hWnd, WM_UPDATE_LOG, 0, 0);
        UpdateWindowFocusability();

        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT client;
        GetClientRect(hWnd, &client);

        // Double-buffered so the cards do not flicker when controls repaint.
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HBITMAP oldBmp = reinterpret_cast<HBITMAP>(SelectObject(mem, bmp));

        DrawCards(mem, client);
        BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);

        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlType == ODT_BUTTON) {
            DrawFancyButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_HSCROLL:
    {
        HWND hwCtrl = reinterpret_cast<HWND>(lParam);
        int idCtrl = GetDlgCtrlID(hwCtrl);
        if (idCtrl == ID_SLIDER_SUSTAIN_CUTOFF) {
            switch (LOWORD(wParam)) {
            case TB_THUMBPOSITION:
            case TB_THUMBTRACK:
            case TB_LINEUP:
            case TB_LINEDOWN:
            case TB_PAGEUP:
            case TB_PAGEDOWN:
            case TB_ENDTRACK:
            {
                g_sustainCutoff = static_cast<int>(SendMessage(hwCtrl, TBM_GETPOS, 0, 0));
                wchar_t buf[16];
                swprintf_s(buf, L"%d", g_sustainCutoff);
                SetWindowTextW(g_hSustainCutoffValueBox, buf);
                break;
            }
            }
        }
        else if (idCtrl == ID_SLIDER_OPACITY) {
            int opacity = static_cast<int>(SendMessage(hwCtrl, TBM_GETPOS, 0, 0));
            if (opacity < 100) opacity = 100;
            SetLayeredWindowAttributes(g_hMainWnd, 0, static_cast<BYTE>(opacity), LWA_ALPHA);
            wchar_t opacityText[16];
            swprintf_s(opacityText, L"%d", opacity);
            SetWindowTextW(g_hOpacityIndicatorBox, opacityText);
        }
        return 0;
    }

    case WM_UPDATE_LOG:
    {
        std::string data;
        {
            std::lock_guard<std::mutex> lk(g_logMutex);
            data = g_logBuffer;
        }
        std::string replaced;
        replaced.reserve(data.size() + 50);
        for (char c : data) {
            if (c == '\n')
                replaced.append("\r\n");
            else
                replaced.push_back(c);
        }
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, replaced.c_str(), -1, nullptr, 0);
        if (wideLen > 0) {
            std::wstring wreplaced(wideLen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, replaced.c_str(), -1, &wreplaced[0], wideLen);
            if (!wreplaced.empty() && wreplaced.back() == L'\0')
                wreplaced.pop_back();
            HWND editLog = GetDlgItem(hWnd, ID_EDIT_LOG);
            SetWindowTextW(editLog, wreplaced.c_str());
            SendMessage(editLog, EM_LINESCROLL, 0, 999999);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        switch (id) {
        case ID_CB_SORT:
            if (code == CBN_SELCHANGE) {
                HWND cb = GetDlgItem(hWnd, ID_CB_SORT);
                int sel = static_cast<int>(SendMessage(cb, CB_GETCURSEL, 0, 0));
                SortMidiItems();
                PopulateMidiList();
            }
            break;

        case ID_BTN_REFRESH:
            if (code == BN_CLICKED) {
                ScanMidiFolder();
                SortMidiItems();
                PopulateMidiList();
                std::wcout << L"[Refresh] Scanned current MIDI folder: " << g_currentMidiDir.wstring() << L"\n";
            }
            break;

        case ID_CHK_TOP:
            if (code == BN_CLICKED) {
                HWND hChk = reinterpret_cast<HWND>(lParam);
                bool top = (SendMessage(hChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SetAlwaysOnTop(hWnd, top);
                midi::Config::getInstance().ui.alwaysOnTop = top;
                try {
                    midi::Config::getInstance().saveToFile("config.json");
                    std::cout << "[Config] Saved Always on Top state: " << (top ? "ENABLED" : "DISABLED") << "\n";
                }
                catch (const std::exception& ex) {
                    std::cerr << "[Config] Failed to save config: " << ex.what() << "\n";
                }
            }
            break;

        case ID_CHK_RANDOM_SONG:
            if (code == BN_CLICKED) {
                HWND hChk = reinterpret_cast<HWND>(lParam);
                LRESULT state = SendMessage(hChk, BM_GETCHECK, 0, 0);
                if (state == BST_CHECKED) {
                    if ((g_midi2key && g_midi2key->IsActive()) || (g_midiConnect && g_midiConnect->IsActive())) {
                        MessageBoxA(hWnd, "Random Song cannot be enabled while MIDI2Key or MIDIConnect is active.",
                            "Conflict", MB_OK | MB_ICONWARNING);
                        SendMessage(hChk, BM_SETCHECK, BST_UNCHECKED, 0);
                        g_randomSongEnabled = false;
                    }
                    else {
                        g_randomSongEnabled = true;
                    }
                }
                else {
                    g_randomSongEnabled = false;
                }
            }
            break;

        case ID_BTN_CLEARLOG:
            if (code == BN_CLICKED) {
                HWND editLog = GetDlgItem(hWnd, ID_EDIT_LOG);
                int ret = MessageBoxA(hWnd, "Are you sure you want to clear the log?\nThis cannot be undone.",
                    "Confirm Clear", MB_YESNO | MB_ICONQUESTION);
                if (ret == IDYES) {
                    ClearLog(editLog);
                    std::cout << "[LOG] Cleared.\n";
                }
            }
            break;

        case ID_BTN_REFRESH_VCURVE:
            if (code == BN_CLICKED) {
                RefreshVelocityCurveCombo(hWnd);
                std::cout << "[VELOCITY] Velocity curves refreshed.\n";
            }
            break;

        case ID_BTN_REFRESH_MIDI:
            if (code == BN_CLICKED) {
                std::cout << "[MIDI Devices] Refreshing device list...\n";
                if (g_midi2key && g_midi2key->IsActive())
                    g_midi2key->CloseDevice();
                int previousDevice = g_selectedMidiDevice;
                HWND cbMidiDev = GetDlgItem(hWnd, ID_CB_MIDIDEV);
                MIDIDeviceUI::PopulateMidiInDevices(cbMidiDev, g_selectedMidiDevice);
                if (g_midi2key && g_midi2key->IsActive() && g_selectedMidiDevice >= 0) {
                    if (MIDIDeviceUI::TestDeviceAccess(g_selectedMidiDevice))
                        g_midi2key->OpenDevice(g_selectedMidiDevice);
                    else
                        std::cout << "[MIDI Devices] Cannot reopen device - no longer accessible\n";
                }
            }
            break;

        case ID_BTN_LOAD:
            if (code == BN_CLICKED) {
                if (!g_player)
                    break;
                try {
                    std::cout << "[Load] Initiating load process...\n";
                    g_player->should_stop.store(true, std::memory_order_release);
                    SetEvent(g_player->command_event);
                    {
                        std::lock_guard<std::mutex> lock(g_player->playback_cv_mutex);
                        g_player->playback_cv.notify_all();
                    }
                    if (g_player->playback_thread && g_player->playback_thread->joinable()) {
                        std::cout << "[Load] Joining playback thread...\n";
                        g_player->playback_thread->join();
                        std::cout << "[Load] Playback thread joined.\n";
                    }
                    g_player->playback_thread.reset();

                    g_player->paused.store(true, std::memory_order_release);
                    g_player->release_all_keys();
                    g_player->note_events.clear();
                    g_player->tempo_changes.clear();
                    g_player->timeSignatures.clear();
                    g_player->trackMuted.clear();
                    g_player->trackSoloed.clear();

                    std::wstring wpath = GetSelectedMidiFullPath();
                    if (wpath.empty()) {
                        std::cout << "[Load] No MIDI file selected.\n";
                        SetWindowTextW(GetDlgItem(hWnd, ID_STATIC_TIME), L"0:00 / 0:00");
                        break;
                    }

                    int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), static_cast<int>(wpath.size()), nullptr, 0, nullptr, nullptr);
                    std::string path(len, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), static_cast<int>(wpath.size()), &path[0], len, nullptr, nullptr);

                    MidiParser parser;
                    g_player->midi_file = parser.parse(path);
                    g_player->process_tracks(g_player->midi_file);
                    g_player->midiFileSelected.store(true, std::memory_order_release);

                    g_player->should_stop.store(false, std::memory_order_release);
                    g_player->paused.store(true, std::memory_order_release);
                    g_player->playback_started.store(false, std::memory_order_release);
                    constexpr auto initialBuffer = std::chrono::milliseconds(50);
                    g_player->total_adjusted_time = -initialBuffer;
                    g_player->current_speed = 1.0;
                    g_player->buffer_index.store(0, std::memory_order_release);
                    unsigned long long now_tsc = __rdtsc();
                    g_player->playback_start_time = now_tsc;
                    g_player->last_resume_tsc = now_tsc;

                    size_t track_count = g_player->midi_file.tracks.size();
                    g_player->trackMuted.resize(track_count);
                    g_player->trackSoloed.resize(track_count);
                    for (size_t i = 0; i < track_count; ++i) {
                        g_player->trackMuted[i] = std::make_shared<std::atomic<bool>>(false);
                        g_player->trackSoloed[i] = std::make_shared<std::atomic<bool>>(false);
                    }

                    g_totalSongSeconds = 0.0;
                    if (!g_player->note_events.empty()) {
                        auto last_event = std::max_element(
                            g_player->note_events.begin(),
                            g_player->note_events.end(),
                            [](auto const& a, auto const& b) { return a.time < b.time; }
                        );
                        if (last_event != g_player->note_events.end()) {
                            g_totalSongSeconds = static_cast<double>(last_event->time.count()) / 1e9 + 0.5;
                        }
                    }

                    wchar_t timeStr[32];
                    int totalMins = static_cast<int>(g_totalSongSeconds) / 60;
                    int totalSecs = static_cast<int>(g_totalSongSeconds) % 60;
                    swprintf_s(timeStr, L"0:00 / %d:%02d", totalMins, totalSecs);
                    SetWindowTextW(GetDlgItem(hWnd, ID_STATIC_TIME), timeStr);

                    UpdateMidiDetails();
                    UpdateTrackInfo();

                    if (g_toggleStates[ID_BTN_VOLADJ]) {
                        FocusRobloxWindow();
                        g_player->calibrate_volume();
                    }

                    std::cout << "[Load] Loaded: " << path << "\n";
                }
                catch (const std::exception& e) {
                    std::cout << "[Load] Error: " << e.what() << "\n";
                    SetWindowTextW(GetDlgItem(hWnd, ID_STATIC_TIME), L"0:00 / 0:00");
                    UpdateMidiDetails();
                    UpdateTrackInfo();
                }
            }
            break;

        case ID_LB_MIDI:
            if (code == LBN_DBLCLK) {
                int sel = static_cast<int>(SendMessage(g_lbMidi, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR && sel >= 0 && sel < static_cast<int>(g_midiItems.size())) {
                    const MidiItem& item = g_midiItems[sel];
                    if (item.isFolder) {
                        if (item.name == L"..")
                            g_currentMidiDir = std::filesystem::path(g_currentMidiDir).parent_path();
                        else
                            g_currentMidiDir = item.fullPath;
                        ScanMidiFolder();
                        SortMidiItems();
                        PopulateMidiList();
                    }
                    else {
                        SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_LOAD, BN_CLICKED),
                            reinterpret_cast<LPARAM>(GetDlgItem(hWnd, ID_BTN_LOAD)));
                    }
                }
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case ID_BTN_VLCURVE:
            if (code == BN_CLICKED) {
                auto editor = new VelocityCurveEditor();
                editor->Create(hWnd);
            }
            break;

        case ID_BTN_PLAY:
            if (code == BN_CLICKED) {
                if ((g_midi2key && g_midi2key->IsActive()) || (g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Auto controls are disabled while MIDI input is active.", "Info", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (!g_player->midiFileSelected.load(std::memory_order_acquire)) {
                    MessageBoxA(hWnd, "Please load a MIDI file first.", "Error", MB_OK | MB_ICONERROR);
                    break;
                }
                FocusRobloxWindow();
                g_player->toggle_play_pause();
            }
            break;

        case ID_BTN_MIDICONNECT:
            if (code == BN_CLICKED) {
                if (g_toggleStates[ID_BTN_MIDI2QWERTY]) {
                    MessageBoxA(hWnd, "Please disable MIDI->QWERTY mode first.", "Conflict", MB_OK | MB_ICONWARNING);
                    break;
                }
                g_toggleStates[ID_BTN_MIDICONNECT] = !g_toggleStates[ID_BTN_MIDICONNECT];
                bool newState = g_toggleStates[ID_BTN_MIDICONNECT];
                InvalidateRect(GetDlgItem(hWnd, ID_BTN_MIDICONNECT), nullptr, TRUE);
                if (newState) {
                    if (!g_midiConnect)
                        g_midiConnect = std::make_unique<MIDIConnect>();
                    g_midiConnect->SetActive(true);
                    g_midiConnect->OpenDevice(g_selectedMidiDevice);
                    std::cout << "[MidiConnect] ENABLED\n";
                    FocusRobloxWindow();
                    g_midiConnect->ReleaseAllNumpadKeys();
                }
                else {
                    if (g_midiConnect) {
                        g_midiConnect->SetActive(false);
                        g_midiConnect->CloseDevice();
                    }
                    std::cout << "[MidiConnect] DISABLED\n";
                }
                UpdateWindowFocusability();
            }
            break;

        case ID_BTN_RESTART:
            if (code == BN_CLICKED) {
                if ((g_midi2key && g_midi2key->IsActive()) || (g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Auto controls are disabled while MIDI input is active.", "Info", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (!g_player->midiFileSelected.load(std::memory_order_acquire)) {
                    MessageBoxA(hWnd, "Please load a MIDI file first.", "Error", MB_OK | MB_ICONERROR);
                    break;
                }
                FocusRobloxWindow();
                g_player->restart_song();
            }
            break;

        case ID_BTN_SKIP:
            if (code == BN_CLICKED) {
                if ((g_midi2key && g_midi2key->IsActive()) || (g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Auto controls are disabled while MIDI input is active.", "Info", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (!g_player->midiFileSelected.load(std::memory_order_acquire)) {
                    MessageBoxA(hWnd, "Please load a MIDI file first.", "Error", MB_OK | MB_ICONERROR);
                    break;
                }
                using namespace std::chrono_literals;
                g_player->skip(10s);
            }
            break;

        case ID_BTN_REW:
            if (code == BN_CLICKED) {
                if ((g_midi2key && g_midi2key->IsActive()) || (g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Auto controls are disabled while MIDI input is active.", "Info", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (!g_player->midiFileSelected.load(std::memory_order_acquire)) {
                    MessageBoxA(hWnd, "Please load a MIDI file first.", "Error", MB_OK | MB_ICONERROR);
                    break;
                }
                using namespace std::chrono_literals;
                g_player->rewind(10s);
            }
            break;

        case ID_BTN_SPEEDUP:
            if (code == BN_CLICKED) {
                if ((g_midi2key && g_midi2key->IsActive()) || (g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Auto controls are disabled while MIDI input is active.", "Info", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (!g_player->midiFileSelected.load(std::memory_order_acquire)) {
                    MessageBoxA(hWnd, "Please load a MIDI file first.", "Error", MB_OK | MB_ICONERROR);
                    break;
                }
                g_player->speed_up();
            }
            break;

        case ID_BTN_SPEEDDN:
            if (code == BN_CLICKED) {
                if ((g_midi2key && g_midi2key->IsActive()) || (g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Auto controls are disabled while MIDI input is active.", "Info", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (!g_player->midiFileSelected.load(std::memory_order_acquire)) {
                    MessageBoxA(hWnd, "Please load a MIDI file first.", "Error", MB_OK | MB_ICONERROR);
                    break;
                }
                g_player->slow_down();
            }
            break;

        case ID_BTN_MIDI2QWERTY:
            if (code == BN_CLICKED) {
                if (g_toggleStates[ID_BTN_MIDICONNECT]) {
                    MessageBoxA(hWnd, "Please disable MidiConnect mode first.", "Conflict", MB_OK | MB_ICONWARNING);
                    break;
                }
                g_toggleStates[ID_BTN_MIDI2QWERTY] = !g_toggleStates[ID_BTN_MIDI2QWERTY];
                bool newState = g_toggleStates[ID_BTN_MIDI2QWERTY];
                InvalidateRect(GetDlgItem(hWnd, ID_BTN_MIDI2QWERTY), nullptr, TRUE);
                if (newState) {
                    if (!g_midi2key)
                        g_midi2key = std::make_unique<MIDI2Key>(g_player);
                    g_midi2key->SetActive(true);
                    g_midi2key->OpenDevice(g_selectedMidiDevice);
                    std::cout << "[MIDI->QWERTY] ENABLED\n";
                    std::cout << "[WARNING] DO NOT use MIDI2Key with spam / black MIDIs or similar\n";
                    FocusRobloxWindow();
                    g_player->release_all_keys();
                }
                else {
                    if (g_midi2key) {
                        g_midi2key->SetActive(false);
                        g_midi2key->CloseDevice();
                    }
                    std::cout << "[MIDI->QWERTY] DISABLED\n";
                }
                UpdateWindowFocusability();
            }
            break;

        case ID_CB_VELOCITY_CURVE:
            if (code == CBN_SELCHANGE) {
                if (!g_player) break;
                HWND cb = GetDlgItem(hWnd, ID_CB_VELOCITY_CURVE);
                int sel = static_cast<int>(SendMessage(cb, CB_GETCURSEL, 0, 0));
                g_player->setVelocityCurveIndex(sel);
                auto& config = midi::Config::getInstance();
                if (sel < 5)
                    config.playback.velocityCurve = static_cast<midi::VelocityCurveType>(sel);
                else
                    config.playback.velocityCurve = midi::VelocityCurveType::Custom;
                std::cout << "[VELOCITY] Changed curve to: "
                    << (sel < 5 ? g_player->getVelocityCurveName(config.playback.velocityCurve)
                        : config.playback.customVelocityCurves[sel - 5].name) << "\n";

                if (g_midiConnect && g_midiConnect->IsActive()) {
                    g_midiConnect->CloseDevice();
                    g_midiConnect->OpenDevice(g_selectedMidiDevice);
                }
                if (g_midi2key && g_midi2key->IsActive()) {
                    g_midi2key->CloseDevice();
                    g_midi2key->OpenDevice(g_selectedMidiDevice);
                }
            }
            break;

        case ID_CB_MIDIDEV:
            if (code == CBN_SELCHANGE) {
                HWND cb = GetDlgItem(hWnd, ID_CB_MIDIDEV);
                int sel = static_cast<int>(SendMessage(cb, CB_GETCURSEL, 0, 0));
                g_selectedMidiDevice = sel;
                if (g_midi2key && g_midi2key->IsActive()) {
                    g_midi2key->CloseDevice();
                    g_midi2key->OpenDevice(sel);
                }
                if (g_midiConnect && g_midiConnect->IsActive()) {
                    g_midiConnect->CloseDevice();
                    g_midiConnect->OpenDevice(sel);
                }
            }
            break;

        case ID_CB_MIDICH:
            if (code == CBN_SELCHANGE) {
                HWND cb = GetDlgItem(hWnd, ID_CB_MIDICH);
                int sel = static_cast<int>(SendMessage(cb, CB_GETCURSEL, 0, 0));
                g_selectedMidiChannel = (sel <= 0 ? -1 : sel - 1);
            }
            break;

        case ID_BTN_PREV_SONG:
            if (code == BN_CLICKED) {
                int sel = static_cast<int>(SendMessage(g_lbMidi, LB_GETCURSEL, 0, 0));
                int count = static_cast<int>(SendMessage(g_lbMidi, LB_GETCOUNT, 0, 0));
                int newSel = sel;
                for (int i = sel - 1; i >= 0; i--) {
                    if (!g_midiItems[i].isFolder) {
                        newSel = i;
                        break;
                    }
                }
                if (newSel == sel) {
                    for (int i = count - 1; i >= 0; i--) {
                        if (!g_midiItems[i].isFolder) {
                            newSel = i;
                            break;
                        }
                    }
                }
                SendMessage(g_lbMidi, LB_SETCURSEL, newSel, 0);
                PostMessage(g_hMainWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_LOAD, BN_CLICKED),
                    reinterpret_cast<LPARAM>(GetDlgItem(g_hMainWnd, ID_BTN_LOAD)));
                PostMessage(g_hMainWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_PLAY, BN_CLICKED),
                    reinterpret_cast<LPARAM>(GetDlgItem(g_hMainWnd, ID_BTN_PLAY)));
            }
            break;

        case ID_BTN_NEXT_SONG:
            if (code == BN_CLICKED) {
                int sel = static_cast<int>(SendMessage(g_lbMidi, LB_GETCURSEL, 0, 0));
                int count = static_cast<int>(SendMessage(g_lbMidi, LB_GETCOUNT, 0, 0));
                int newSel = sel;
                for (int i = sel + 1; i < count; i++) {
                    if (!g_midiItems[i].isFolder) {
                        newSel = i;
                        break;
                    }
                }
                if (newSel == sel) {
                    for (int i = 0; i < count; i++) {
                        if (!g_midiItems[i].isFolder) {
                            newSel = i;
                            break;
                        }
                    }
                }
                SendMessage(g_lbMidi, LB_SETCURSEL, newSel, 0);
                PostMessage(g_hMainWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_LOAD, BN_CLICKED),
                    reinterpret_cast<LPARAM>(GetDlgItem(g_hMainWnd, ID_BTN_LOAD)));
                PostMessage(g_hMainWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_PLAY, BN_CLICKED),
                    reinterpret_cast<LPARAM>(GetDlgItem(g_hMainWnd, ID_BTN_PLAY)));
            }
            break;

        default:
            if (IsToggleButtonID(id) && code == BN_CLICKED && id != ID_BTN_MIDI2QWERTY) {
                g_toggleStates[id] = !g_toggleStates[id];
                InvalidateRect(GetDlgItem(hWnd, id), nullptr, TRUE);
                if (!g_player->midiFileSelected.load(std::memory_order_acquire) &&
                    !(g_midi2key && g_midi2key->IsActive()) &&
                    !(g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Load a MIDI file first or enable MIDI->QWERTY to use advanced features.",
                        "Warning", MB_OK | MB_ICONWARNING);
                    g_toggleStates[id] = !g_toggleStates[id];
                    InvalidateRect(GetDlgItem(hWnd, id), nullptr, TRUE);
                    break;
                }
                switch (id) {
                case ID_BTN_88KEY:
                    g_player->toggle_88_key_mode();
                    break;
                case ID_BTN_VOLADJ:
                {
                    bool newState = g_toggleStates[ID_BTN_VOLADJ];
                    g_player->toggle_volume_adjustment();
                    if (newState) {
                        FocusRobloxWindow();
                        g_player->calibrate_volume();
                    }
                    break;
                }
                case ID_BTN_VELOCITY:
                    g_player->toggle_velocity_keypress();
                    break;
                case ID_BTN_SUSTAIN:
                    g_player->toggleSustainMode();
                    break;
                case ID_BTN_TRANSPOSEOUT:
                    g_player->toggle_out_of_range_transpose();
                    break;
                }
            }
            else if (id == ID_BTN_TRANSPOSE && code == BN_CLICKED) {
                if (!g_player->midiFileSelected.load(std::memory_order_acquire) &&
                    !(g_midi2key && g_midi2key->IsActive()) &&
                    !(g_midiConnect && g_midiConnect->IsActive())) {
                    MessageBoxA(hWnd, "Load a MIDI file first or enable MIDI->QWERTY to use this feature.",
                        "Error", MB_OK | MB_ICONERROR);
                    break;
                }
                if (!g_player->midiFileSelected.load(std::memory_order_acquire)) {
                    MessageBoxA(hWnd, "No loaded MIDI file to analyze for transpose suggestions.\n"
                        "But if you only need the function for direct key press in 'midi2key' mode, ignore this message.",
                        "Info", MB_OK | MB_ICONINFORMATION);
                }
                int best = g_player->toggle_transpose_adjustment();
                char buf[128];
                sprintf_s(buf, "Suggested transpose: [%d]", best);
                MessageBoxA(hWnd, buf, "Transpose Suggestion", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        int cid = GetDlgCtrlID(ctrl);
        if (cid == ID_EDIT_LOG || cid == ID_EDIT_TRACKS || cid == ID_EDIT_DETAILS) {
            static HFONT hMonospaceFont = nullptr;
            if (!hMonospaceFont) {
                LOGFONT lf = {};
                lf.lfHeight = -14;
                wcscpy_s(lf.lfFaceName, L"Consolas");
                hMonospaceFont = Theme::Mono();
            }
            if (cid == ID_EDIT_LOG) {
                static HBRUSH hbrLogBg = nullptr;
                if (!hbrLogBg)
                    hbrLogBg = CreateSolidBrush(Theme::LOG_BG);
                SetBkMode(hdc, OPAQUE);
                SetBkColor(hdc, Theme::LOG_BG);
                SetTextColor(hdc, Theme::LOG_TEXT);
                SelectObject(hdc, hMonospaceFont);
                return reinterpret_cast<LRESULT>(hbrLogBg);
            }
            else {
                static HBRUSH hbrWhite = (HBRUSH)GetStockObject(WHITE_BRUSH);
                SetBkMode(hdc, OPAQUE);
                SetBkColor(hdc, Theme::EDIT_BG);
                SetTextColor(hdc, Theme::EDIT_TEXT);
                SelectObject(hdc, hMonospaceFont);
                return reinterpret_cast<LRESULT>(hbrWhite);
            }
        }
        SetBkMode(hdc, TRANSPARENT);
        static HBRUSH hbrWhite = (HBRUSH)GetStockObject(WHITE_BRUSH);
        return reinterpret_cast<LRESULT>(hbrWhite);
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        int cid = GetDlgCtrlID(ctrl);
        if (cid == ID_EDIT_LOG || cid == ID_EDIT_TRACKS || cid == ID_EDIT_DETAILS) {
            SendMessage(hWnd, WM_CTLCOLORSTATIC, wParam, lParam);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    case WM_TIMER:
    {
        if (wParam == IDT_TIMELEFT_TIMER) {
            auto now = std::chrono::steady_clock::now();
            bool shouldUpdate = (now - g_lastTimeUpdate) >= TIME_UPDATE_INTERVAL;
            static bool lastPlaybackState = false;
            bool currentPlaybackState = g_player && !g_player->paused.load(std::memory_order_relaxed);
            if (currentPlaybackState != lastPlaybackState) {
                shouldUpdate = true;
                lastPlaybackState = currentPlaybackState;
            }
            if (!shouldUpdate)
                return 0;
            if (!g_player || !g_player->midiFileSelected.load(std::memory_order_relaxed)) {
                static bool lastWasDefault = false;
                if (!lastWasDefault) {
                    SetWindowTextW(GetDlgItem(hWnd, ID_STATIC_TIME), L"0:00 / 0:00");
                    lastWasDefault = true;
                }
                UpdateWindowFocusability();
                return 0;
            }
            static wchar_t timeStr[32];
            g_lastTimeUpdate = now;
            double currentSeconds = 0.0;
            if (!g_player->paused.load(std::memory_order_relaxed))
                currentSeconds = std::chrono::duration<double>(g_player->get_adjusted_time()).count();
            else
                currentSeconds = std::chrono::duration<double>(g_player->total_adjusted_time).count(); // Use last stored time when paused
            currentSeconds = std::min(currentSeconds, g_totalSongSeconds);
            int currentMins = static_cast<int>(currentSeconds) / 60;
            int currentSecs = static_cast<int>(currentSeconds) % 60;
            int totalMins = static_cast<int>(g_totalSongSeconds) / 60;
            int totalSecs = static_cast<int>(g_totalSongSeconds) % 60;
            static int lastCurrentMins = -1, lastCurrentSecs = -1;
            static int lastTotalMins = -1, lastTotalSecs = -1;
            if (currentMins != lastCurrentMins || currentSecs != lastCurrentSecs ||
                totalMins != lastTotalMins || totalSecs != lastTotalSecs) {
                swprintf_s(timeStr, L"%d:%02d / %d:%02d",
                    std::min(currentMins, 999), currentSecs,
                    std::min(totalMins, 999), totalSecs);
                SetWindowTextW(GetDlgItem(hWnd, ID_STATIC_TIME), timeStr);
                lastCurrentMins = currentMins;
                lastCurrentSecs = currentSecs;
                lastTotalMins = totalMins;
                lastTotalSecs = totalSecs;
            }
            static bool randomTriggered = false;
            if (g_randomSongEnabled && currentSeconds >= g_totalSongSeconds && !randomTriggered) {
                randomTriggered = true;
                std::vector<int> fileIndices;
                for (int i = 0; i < static_cast<int>(g_midiItems.size()); ++i) {
                    if (!g_midiItems[i].isFolder)
                        fileIndices.push_back(i);
                }
                if (!fileIndices.empty()) {
                    int currentSelection = static_cast<int>(SendMessage(g_lbMidi, LB_GETCURSEL, 0, 0));
                    bool currentInFiles = (std::find(fileIndices.begin(), fileIndices.end(), currentSelection) != fileIndices.end());
                    int randomIndex = currentSelection;
                    if (fileIndices.size() == 1) {
                        randomIndex = fileIndices[0];
                    }
                    else {
                        do {
                            randomIndex = fileIndices[rand() % fileIndices.size()];
                        } while (currentInFiles && randomIndex == currentSelection);
                    }
                    SendMessage(g_lbMidi, LB_SETCURSEL, randomIndex, 0);
                    PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_LOAD, BN_CLICKED),
                        reinterpret_cast<LPARAM>(GetDlgItem(hWnd, ID_BTN_LOAD)));
                    PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_PLAY, BN_CLICKED),
                        reinterpret_cast<LPARAM>(GetDlgItem(hWnd, ID_BTN_PLAY)));
                }
            }
            if (currentSeconds < g_totalSongSeconds)
                randomTriggered = false;
            UpdateWindowFocusability();
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        if (g_player) {
            g_player->should_stop.store(true, std::memory_order_release);
            SetEvent(g_player->command_event);
            if (g_player->playback_thread && g_player->playback_thread->joinable()) {
                try {
                    g_player->playback_thread->join();
                    g_player->playback_thread.reset();
                    std::cout << "[GRACEFUL EXIT] Playback thread joined successfully.\n";
                }
                catch (const std::exception& e) {
                    std::cerr << "[GRACEFUL EXIT] Exception while joining playback thread: " << e.what() << "\n";
                }
                catch (...) {
                    std::cerr << "[GRACEFUL EXIT] Unknown exception while joining playback thread.\n";
                }
            }
            if (g_midi2key) {
                g_midi2key->SetActive(false);
                g_midi2key->CloseDevice();
                g_midi2key.reset();
            }
            if (g_midiConnect) {
                g_midiConnect->SetActive(false);
                g_midiConnect->CloseDevice();
                g_midiConnect.reset();
            }
            {
                std::lock_guard<std::mutex> lk(g_logMutex);
                g_logBuffer.clear();
            }
            g_toggleStates.clear();
            std::cout << "[GRACEFUL EXIT] Global states cleared.\n";
            PostQuitMessage(0);
            return 0;
        }
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Main Entry Point
// -----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Opt into DPI awareness before any window exists, then bake the scale
    // factor into the theme and layout. Without this Windows bitmap-stretches
    // the whole UI on a >100% display and every glyph goes soft.
    EnableDpiAwareness();
    Theme::SetDpi(QuerySystemDpi());
    Layout::Init();
    TrackControl::SetDpi();

    srand(static_cast<unsigned int>(time(NULL)));
    UniqueHandle singleInstanceMutex(CreateMutexW(nullptr, TRUE, L"Global\\MIDI++_On_Top"));
    g_hSingleInstanceMutex = singleInstanceMutex;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWindow = FindWindowW(L"MIDI++", L"MIDI++ v1.0.4.R5");
        if (existingWindow) {
            if (IsIconic(existingWindow))
                ShowWindow(existingWindow, SW_RESTORE);
            SetForegroundWindow(existingWindow);
        }
        return 0;
    }
    GdiplusTokenWrapper gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken.token, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        MessageBoxA(nullptr, "Failed to initialize GDI+.", "Error", MB_ICONERROR);
        return -1;
    }
    static VirtualPianoPlayer player;
    g_player = &player;
    RedirectCout();
    auto& cfg = midi::Config::getInstance();
    std::cout << " ===== MIDI++ v1.0.4.R5 | Developed by Zeph, Tested by Gene =====\n";
    std::cout << "Hotkeys:\n";
    std::cout << "  Play/Pause:     " << getReadableKey(cfg.hotkeys.PLAY_PAUSE_KEY) << "\n";
    std::cout << "  Rewind:         " << getReadableKey(cfg.hotkeys.REWIND_KEY) << "\n";
    std::cout << "  Skip:           " << getReadableKey(cfg.hotkeys.SKIP_KEY) << "\n";
    std::cout << "  Play Stop:      " << getReadableKey(cfg.hotkeys.EMERGENCY_EXIT_KEY) << "\n";
    g_hInst = hInstance;
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    HICON hIconSmall = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON_SMALL));
    if (!hIcon || !hIconSmall) {
        MessageBoxA(nullptr, "Failed to load application icons!", "Error", MB_ICONERROR);
        return -1;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(Theme::WINDOW_BG);
    wc.lpszClassName = L"MIDI++";
    wc.hIcon = hIcon;
    wc.hIconSm = hIconSmall;
    RegisterClassExW(&wc);
    // Outer window size is derived from the layout client size, so the frame
    // never eats into the content and changing Layout is enough to resize.
    const DWORD kStyle   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD kExStyle = WS_EX_APPWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST;
    RECT wr{ 0, 0, Layout::CLIENT_W, Layout::CLIENT_H };
    AdjustWindowRectEx(&wr, kStyle, FALSE, kExStyle);
    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;

    g_hMainWnd = CreateWindowExW(kExStyle,
        wc.lpszClassName,
        L"MIDI++ v1.0.4.R5",
        kStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winW, winH,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    SetLayeredWindowAttributes(g_hMainWnd, 0, 255, LWA_ALPHA);

    if (!g_hMainWnd) {
        MessageBoxA(nullptr, "Failed to create main window!", "Error", MB_ICONERROR);
        return -1;
    }
    SetTimer(NULL, 1, 100, [](HWND, UINT, UINT_PTR timerId, DWORD) {
        KillTimer(NULL, timerId);
        if (g_hMainWnd) {
            if (IsIconic(g_hMainWnd)) {
                ShowWindow(g_hMainWnd, SW_RESTORE);
            }
        }
        });
    ShowWindow(g_hMainWnd, SW_SHOW);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}
