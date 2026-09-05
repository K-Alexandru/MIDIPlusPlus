// ImGui shell for the MIDI++ successor UI.
//
// The shell and the skin system only: a window, the four skins applied to
// ImGuiStyle, the raised/recessed drawing helpers, and the layout the mockup
// specifies. No feature panels. Those get ported one at a time against
// skin-system.html, starting with Tracks.
//
// Built as its own executable so the working app is never broken by UI work in
// progress. Nothing here links against PlaybackCore yet.
//
// Hard constraint from HANDOFF.md section 4: injection must never share a
// thread with the message loop. When this shell grows a real engine behind it,
// the engine keeps its own threads. Do not reintroduce the upstream bug where
// dragging the window fights the injection syscall.

#include "SkinDraw.hpp"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include <d3d11.h>
#include <tchar.h>

static ID3D11Device*           g_device = nullptr;
static ID3D11DeviceContext*    g_context = nullptr;
static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11RenderTargetView* g_target = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void CreateTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_target);
        back->Release();
    }
}

static void ReleaseTarget() {
    if (g_target) { g_target->Release(); g_target = nullptr; }
}

static bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, wanted, 2,
        D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, wanted, 2,
            D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &level, &g_context);
    }
    if (FAILED(hr)) return false;
    CreateTarget();
    return true;
}

static void CleanupDevice() {
    ReleaseTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context)   { g_context->Release();   g_context = nullptr; }
    if (g_device)    { g_device->Release();    g_device = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_device && wp != SIZE_MINIMIZED) {
            ReleaseTarget();
            g_swapChain->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp),
                                       DXGI_FORMAT_UNKNOWN, 0);
            CreateTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // no ALT menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Layout. Measurements come from the rendered mockup, not from guesswork:
// Classic is 1090 x 635 with the velocity editor collapsed, the primary strip
// is 81px, and the left file list is 336px wide.
// ---------------------------------------------------------------------------
namespace {

constexpr float kStripHeight = 81.f;
constexpr float kLeftColumn  = 336.f;
constexpr float kStatusBar   = 28.f;

void DrawStrip(const skin::Skin& s, ImVec2 origin, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = origin;
    const ImVec2 max = ImVec2(origin.x + width, origin.y + kStripHeight);
    dl->AddRectFilled(min, max, IM_COL32(
        (s.surface.structure >> 16) & 0xFF, (s.surface.structure >> 8) & 0xFF,
        s.surface.structure & 0xFF, 0xFF));
    dl->AddLine(ImVec2(min.x, max.y), max, IM_COL32(0, 0, 0, 24));

    // The strip carries only what changes while you play: the device, the
    // toggles, and the three icon buttons. The MIDI transport chooser lives in
    // Settings, because it is picked once.
    ImGui::SetCursorScreenPos(ImVec2(min.x + s.spacing.windowPad,
                                     min.y + s.spacing.s2));
    ImGui::BeginGroup();
    ImGui::Button("Casio PX-160", ImVec2(0, s.metric.controlHeight));
    ImGui::EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(min.x + s.spacing.windowPad,
                                     min.y + s.spacing.s2 + s.metric.controlHeight + s.spacing.s2));
    const char* toggles[] = { "Midi2Key", "Velocity", "Sustain", "88 Keys", "MidiConnect" };
    for (int i = 0; i < IM_ARRAYSIZE(toggles); ++i) {
        if (i) ImGui::SameLine();
        ImGui::Button(toggles[i], ImVec2(0, s.metric.controlHeight));
    }
}

void DrawShell(const skin::Skin& s, ImVec2 size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    DrawStrip(s, origin, size.x);

    const float bodyTop = origin.y + kStripHeight + s.spacing.windowPad;
    const float bodyBottom = origin.y + size.y - kStatusBar - s.spacing.windowPad;

    // Left: the file list, a recessed surface inside a raised card.
    const ImVec2 leftMin(origin.x + s.spacing.windowPad, bodyTop);
    const ImVec2 leftMax(leftMin.x + kLeftColumn, bodyBottom);
    skin::RaisedPanel(leftMin, leftMax, s);
    skin::RecessedField(ImVec2(leftMin.x + s.spacing.panelPad,
                               leftMin.y + s.spacing.panelPad + s.metric.controlHeight),
                        ImVec2(leftMax.x - s.spacing.panelPad,
                               leftMax.y - s.spacing.panelPad), s);

    // Right column: stacked cards. Feature panels go in these.
    const ImVec2 rightMin(leftMax.x + s.spacing.s3, bodyTop);
    const ImVec2 rightMax(origin.x + size.x - s.spacing.windowPad, bodyBottom);
    const float third = (rightMax.y - rightMin.y - s.spacing.s3 * 2) / 3.f;
    for (int i = 0; i < 3; ++i) {
        const float top = rightMin.y + i * (third + s.spacing.s3);
        skin::RaisedPanel(ImVec2(rightMin.x, top),
                          ImVec2(rightMax.x, top + third), s);
    }

    // Status bar: state that does not move.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(origin.x, origin.y + size.y - kStatusBar),
                      ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32((s.surface.structure >> 16) & 0xFF,
                               (s.surface.structure >> 8) & 0xFF,
                               s.surface.structure & 0xFF, 0xFF));
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst,
                       nullptr, nullptr, nullptr, nullptr, L"MIDIShell", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"MIDI++ shell (ImGui)",
                                WS_OVERLAPPEDWINDOW, 100, 100, 1090, 635,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDevice(hwnd)) {
        CleanupDevice();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    auto skins = skin::All();
    int active = 0;
    skin::ApplyStyle(skins[active]);

    bool running = true;
    while (running) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##shell", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

        // Skin picker, so the four parameter sets can be compared the way the
        // mockup compares them. Not part of the shipped UI.
        for (int i = 0; i < (int)skins.size(); ++i) {
            if (i) ImGui::SameLine();
            char label[32];
            snprintf(label, sizeof(label), "%.*s",
                     (int)skins[i].name.size(), skins[i].name.data());
            if (ImGui::RadioButton(label, active == i)) {
                active = i;
                skin::ApplyStyle(skins[active]);
            }
        }
        ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
        DrawShell(skins[active], ImGui::GetContentRegionAvail());
        ImGui::End();

        ImGui::Render();
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = { bg.x, bg.y, bg.z, 1.f };
        g_context->OMSetRenderTargets(1, &g_target, nullptr);
        g_context->ClearRenderTargetView(g_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
