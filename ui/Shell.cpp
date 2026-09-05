// ImGui shell for the MIDI++ successor UI.
//
// The four skins, MIDI library, Tracks and basic autoplay. Other panels are
// still ported one at a time against skin-system.html.
//
// Built as its own executable so the working app is never broken by UI work in
// progress. ShellEngine owns PlaybackCore on a separate command thread.
//
// Hard constraint from HANDOFF.md section 4: injection must never share a
// thread with the message loop. The engine keeps its own threads.
// Do not reintroduce the upstream bug where
// dragging the window fights the injection syscall.

#include "SkinDraw.hpp"
#include "Fonts.hpp"
#include "Panels.hpp"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include <d3d11.h>
#include <tchar.h>
#include <shellapi.h>

static ID3D11Device*           g_device = nullptr;
static ID3D11DeviceContext*    g_context = nullptr;
static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11RenderTargetView* g_target = nullptr;
static float g_dpi = 1.f;
static shell::ShellEngine* g_engine = nullptr;
static shell::Panels* g_panels = nullptr;

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
    case WM_HOTKEY:
        if (wp == 1 && g_engine && g_panels) {
            g_engine->Send({shell::ShellEngine::Action::Stop});
        }
        return 0;
    case WM_DROPFILES: {
        const auto drop = reinterpret_cast<HDROP>(wp);
        wchar_t path[32768]{};
        if (g_engine && g_panels && DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))))
            g_engine->Send({shell::ShellEngine::Action::Load, path, 0, 0, g_panels->preferences.autoSolo});
        DragFinish(drop);
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = static_cast<float>(HIWORD(wp)) / 96.f;
        const RECT& rect = *reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        RECT rect{0, 0, static_cast<LONG>(900.f * g_dpi), static_cast<LONG>(580.f * g_dpi)};
        AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                static_cast<UINT>(96.f * g_dpi));
        info->ptMinTrackSize = {rect.right - rect.left, rect.bottom - rect.top};
        return 0;
    }
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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    const auto directory = std::filesystem::path(executable).parent_path();
    const auto preferencesPath = directory / L"shell-settings.json";
    shell::Panels panels;
    panels.LoadPreferences(preferencesPath);
    shell::ShellEngine engine(directory / L"config.json");
    g_engine = &engine;
    g_panels = &panels;
    g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst,
                       nullptr, nullptr, nullptr, nullptr, L"MIDIShell", nullptr };
    ::RegisterClassExW(&wc);
    RECT initial{0, 0, static_cast<LONG>(1090.f * g_dpi), static_cast<LONG>((panels.preferences.skin < 2 ? 635.f : 728.f) * g_dpi)};
    AdjustWindowRectExForDpi(&initial, WS_OVERLAPPEDWINDOW, FALSE, 0, static_cast<UINT>(96.f * g_dpi));
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"MIDI++ shell (ImGui)",
                                WS_OVERLAPPEDWINDOW, 100, 100, initial.right - initial.left, initial.bottom - initial.top,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDevice(hwnd)) {
        CleanupDevice();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    DragAcceptFiles(hwnd, TRUE);
    panels.stopHotkeyAvailable = RegisterHotKey(hwnd, 1, MOD_NOREPEAT, VK_F4) != FALSE;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    shell::Fonts fonts;
    fonts.Load();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    auto skins = skin::All();
    int appliedSkin = -1;
    float appliedDpi = 0.f;
    if (panels.preferences.folder.empty()) {
        auto folder = directory / L"midi";
        if (!std::filesystem::is_directory(folder)) folder = directory.parent_path().parent_path() / L"x64" / L"Release" / L"midi";
        if (std::filesystem::is_directory(folder)) panels.preferences.folder = std::filesystem::weakly_canonical(folder);
    }
    if (!panels.preferences.folder.empty()) engine.Send({shell::ShellEngine::Action::Scan, panels.preferences.folder});
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) engine.Send({shell::ShellEngine::Action::Load, argv[1], 0, 0, panels.preferences.autoSolo});
        LocalFree(argv);
    }

    bool running = true;
    while (running) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (IsIconic(hwnd)) { WaitMessage(); continue; }
        const int active = panels.preferences.skin;
        if (appliedSkin != active || appliedDpi != g_dpi) {
            if (appliedSkin >= 0 && appliedSkin / 2 != active / 2) {
                RECT client{};
                GetClientRect(hwnd, &client);
                const LONG previousHeight = static_cast<LONG>((appliedSkin < 2 ? 635.f : 728.f) * g_dpi);
                if (std::abs(client.bottom - previousHeight) <= 1) {
                    RECT rect{0, 0, client.right, static_cast<LONG>((active < 2 ? 635.f : 728.f) * g_dpi)};
                    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, static_cast<UINT>(96.f * g_dpi));
                    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            skin::ApplyStyle(skins[active], g_dpi);
            ImGui::GetIO().FontDefault = fonts.Get(skins[active]);
            appliedSkin = active;
            appliedDpi = g_dpi;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::PushFont(fonts.Get(skins[appliedSkin]), skins[appliedSkin].type.body);

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("##shell", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);
        panels.Draw(hwnd, fonts, skins[appliedSkin], g_dpi, engine);
        ImGui::End();
        ImGui::PopFont();

        ImGui::Render();
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = { bg.x, bg.y, bg.z, 1.f };
        g_context->OMSetRenderTargets(1, &g_target, nullptr);
        g_context->ClearRenderTargetView(g_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        g_swapChain->Present(1, 0);
        // Playback never depends on frame rate. Cap idle redraw cost beside a game
        // and wake immediately for window input instead of spinning when unfocused.
        MsgWaitForMultipleObjectsEx(0, nullptr, GetForegroundWindow() == hwnd ? 16 : 80,
                                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (panels.stopHotkeyAvailable) UnregisterHotKey(hwnd, 1);
    panels.SavePreferences(preferencesPath);
    g_engine = nullptr;
    g_panels = nullptr;
    CleanupDevice();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}
