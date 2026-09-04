#pragma once
#define NOMINMAX
#include <Windows.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include "Theme.hpp"

class VirtualPianoPlayer;

class TrackControl {
public:
    struct TrackInfo {
        std::string trackName;
        std::string instrumentName;
        int noteCount = 0;
        int channel = -1;
        int programNumber = -1;
        bool isMuted = false;
        bool isSoloed = false;
        bool isDrums = false;
    };

    TrackControl();
    ~TrackControl();
    bool Create(HWND hParentWnd, int x, int y, int width, int height);
    void SetTracks(const std::vector<TrackInfo>& tracks);
    void SetPlayer(VirtualPianoPlayer* player);
    void RefreshDisplay();
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    HWND GetWindowHandle() const { return m_hWnd; }

    // Rescales all metrics for the current display DPI. Call before Create().
    static void SetDpi();

private:
    // Metrics are design units at 96 DPI, rescaled once by SetDpi().
    static int HEADER_HEIGHT;
    static int TRACK_HEIGHT;
    static int SCROLLBAR_WIDTH;
    static int BUTTON_WIDTH;
    static int BUTTON_HEIGHT;
    static int BUTTON_MARGIN;
    RECT m_rcTrackUpdate = {};

    enum class Column {
        TrackNumber,
        TrackName,
        Channel,
        Program,
        NoteCount,
        Mute,
        Solo
    };
    int GetColumnWidth(Column col) const;
    int GetColumnPosition(Column col) const;
    void DrawControl(HDC hdc);
    void DrawHeaders(HDC hdc);
    void DrawTrack(HDC hdc, int index, int yPos);
    void DrawButton(HDC hdc, const RECT& rect, bool isPressed, bool isToggled, const char* text);
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void UpdateScrollInfo();
    void ScrollTracks(int amount);
    int HitTest(int x, int y);
    bool IsButtonHit(Column col, int trackIndex, int x, int y);

    HWND m_hWnd;
    HWND m_hParentWnd;
    int m_x, m_y, m_width, m_height;

    std::vector<TrackInfo> m_tracks;
    int m_scrollPos;
    int m_maxVisibleTracks;
    int m_hoveredTrack;
    int m_selectedTrack;

    struct ButtonState {
        bool isHovered;
        bool isPressed;
    };

    std::unordered_map<int, ButtonState> m_muteButtonStates;
    std::unordered_map<int, ButtonState> m_soloButtonStates;

    HFONT m_hFont;
    HFONT m_hBoldFont;
    HBRUSH m_hBackgroundBrush;
    HBRUSH m_hTrackBrush;
    HBRUSH m_hSelectedTrackBrush;
    HBRUSH m_hHeaderBrush;

    VirtualPianoPlayer* m_player;

    void InitializeDefaultTracks();
};