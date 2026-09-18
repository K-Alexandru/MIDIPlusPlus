// Render the actual panels through the shipped DX11 backend at several DPIs.
// No desktop window, UI input, or keyboard injection is needed.
#include "../ui/Panels.hpp"
#include "backends/imgui_impl_dx11.h"
#include "imgui_internal.h"
#include "TrackFixture.hpp"
#include "../MIDI++/InputHeader.h"
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
namespace {
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX11/WIC failure: " + std::to_string(hr)); }
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void SavePng(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
             const std::filesystem::path& path) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    Check(device->CreateTexture2D(&desc, nullptr, &readback));
    context->CopyResource(readback.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream));
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame, nullptr));
    Check(frame->Initialize(nullptr));
    Check(frame->SetSize(desc.Width, desc.Height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format));
    Require(format == GUID_WICPixelFormat32bppBGRA, "unexpected PNG pixel format");
    std::vector<BYTE> pixels(desc.Width * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) for (UINT x = 0; x < desc.Width; ++x) {
        const auto* source = static_cast<const BYTE*>(mapped.pData) + y * mapped.RowPitch + x * 4;
        auto* dest = pixels.data() + (y * desc.Width + x) * 4;
        dest[0] = source[2]; dest[1] = source[1]; dest[2] = source[0]; dest[3] = source[3];
    }
    Check(frame->WritePixels(desc.Height, desc.Width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    Check(frame->Commit()); Check(encoder->Commit());
    context->Unmap(readback.Get(), 0);
}
}

int wmain() {
    // Render scenarios may arm a countdown. Capture before constructing any
    // player, just as ShellTests does, so even a regression cannot type out.
    InjectInput = [](ULONG count, LPINPUT, int) -> UINT { return count; };
    try {
        Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                               D3D11_SDK_VERSION, &device, nullptr, &context));
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        shell::Fonts fonts; fonts.Load();
        Require(ImGui_ImplDX11_Init(device.Get(), context.Get()), "DX11 init failed");
        Require((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0, "dynamic font backend missing");
        const auto folder = std::filesystem::current_path();
        const auto fixture = folder / L"tracks.mid";
        WriteTrackFixture(fixture);
        shell::ShellEngine engine(folder / L"config.json");
        engine.Send({shell::ShellEngine::Action::Load, fixture, 0, 0, true});
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (engine.Snapshot()->loaded.empty() && engine.Snapshot()->error.empty() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        Require(!engine.Snapshot()->loaded.empty(), "fixture failed to load");
        engine.Send({shell::ShellEngine::Action::LiveScan});
        engine.Send({shell::ShellEngine::Action::Scan, folder});
        const auto scanDeadline = std::chrono::steady_clock::now() + 15s;
        while (engine.Snapshot()->folder != folder && std::chrono::steady_clock::now() < scanDeadline)
            std::this_thread::sleep_for(5ms);
        Require(engine.Snapshot()->folder == folder, "render fixture scan did not complete");
        { shell::ShellEngine::Command media{shell::ShellEngine::Action::Hotkey};
          media.track = 5; media.key = "VK_MEDIA_NEXT_TRACK"; engine.Send(std::move(media)); }
        shell::Panels panels;
        panels.preferences.keyMappingOpen = false;
        panels.transportKeysAvailable.fill(true);
        panels.stopHotkeyAvailable = true;
        // Counted, not answered: returning a path would load a file mid-capture
        // and change every later scenario. Reaching the hook is the assertion.
        int pickerCalls = 0;
        shell::PickMidiFile = [&](HWND) { ++pickerCalls; return std::filesystem::path{}; };
        const auto skins = skin::All();
        // Returning to 100% catches cumulative scaling after a monitor move.
        for (const float dpi : {1.f, 1.25f, 1.5f, 2.f, 1.f}) for (int i = 0; i < 4; ++i)
        for (int mode = 0; mode < 19; ++mode) {
            panels.preferences.skin = i;
            panels.miniMode = mode == 1 || mode == 2 || mode == 8 || mode == 9 || mode == 10;
            panels.miniAutoplay = mode == 2 || mode == 8 || mode == 10;
            panels.logOpen = mode == 3 || mode == 9;
            panels.velocityExpanded = mode == 11;
            // "minimum" is the smallest window WM_GETMINMAXINFO allows, where
            // the right column and the strip are at their narrowest.
            const char* variants[]{"full", "mini-live", "mini-autoplay", "log", "settings", "sort", "export",
                                   "countdown", "mini-countdown", "mini-log", "mini-open", "velocity-editor", "convert", "minimum",
                                   "settings-switches",
                                   // Two windows rather than popups: the key mapping
                                   // panel below the main window and the AutoVol window.
                                   "key-mapping", "autovol",
                                   // The library save's confirmation, over the full window.
                                   "library-save",
                                   // Settings at its Hotkeys section: Back armed, Forward
                                   // held by another program, Next song on a media key.
                                   "settings-hotkeys"};
            const int picksBefore = pickerCalls;
            if (mode == 7 || mode == 8) {
                engine.Send({shell::ShellEngine::Action::PlaybackDelay, {}, 0, 0, false, 10});
                engine.Send({shell::ShellEngine::Action::PlayCountdown, {}, engine.Snapshot()->generation});
                const auto armedBy = std::chrono::steady_clock::now() + 2s;
                while (!engine.Snapshot()->playbackCountdown && std::chrono::steady_clock::now() < armedBy)
                    std::this_thread::sleep_for(5ms);
                Require(engine.Snapshot()->playbackCountdown > 0, "render countdown did not arm");
            }
            shell::ShellLog::Instance().Clear();
            if (panels.logOpen) shell::ShellLog::Instance().Append("[error] Kernel Streaming read failed, live input has stopped.\n");
            skin::ApplyStyle(skins[i], dpi);
            ImGui::GetIO().FontDefault = fonts.Get(skins[i]);
            const auto desired = mode == 13 ? shell::Panels::MinimumSize() : panels.DesiredSize();
            const UINT width = static_cast<UINT>(desired.x * dpi);
            const UINT height = static_cast<UINT>(desired.y * dpi);
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11RenderTargetView> target;
            Check(device->CreateTexture2D(&desc, nullptr, &texture));
            Check(device->CreateRenderTargetView(texture.Get(), nullptr, &target));
            for (int frame = 0; frame < 7; ++frame) {
                auto& io = ImGui::GetIO();
                const auto s = skin::ScaleGeometry(skins[i], dpi);
                const float leftEdge = s.spacing.windowPad + std::clamp(width - 2 * s.spacing.windowPad - s.spacing.s3 - 600 * dpi, 240 * dpi, 336 * dpi) - s.spacing.panelPad;
                const float buttonY = 24 * dpi + s.metric.controlHeight + s.spacing.windowPad + s.spacing.panelPad + s.metric.controlHeight / 2;
                // Second control from the right on mini's first body row: the
                // strip, one row gap, and back past Solo Piano and one spacing.
                const float openX = width - s.spacing.windowPad - 1.5f * s.metric.controlHeight - s.spacing.s2;
                const float openY = 24 * dpi + 2 * s.metric.controlHeight + 8 * dpi + s.metric.controlHeight / 2;
                if (mode == 5) io.AddMousePosEvent(leftEdge - 1.5f * s.metric.controlHeight - s.spacing.s2, buttonY);
                else if (mode == 6) io.AddMousePosEvent(width - s.spacing.windowPad - s.spacing.panelPad - 40 * dpi, buttonY);
                else if (mode == 10) io.AddMousePosEvent(openX, openY);
                else io.AddMousePosEvent(-1000, -1000);
                io.AddMouseButtonEvent(0, (mode == 5 || mode == 6 || mode == 10) && frame == 3);
                ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
                ImGui::GetIO().DeltaTime = 1.f / 60;
                ImGui_ImplDX11_NewFrame();
                ImGui::NewFrame();
                // Each capture is independent. Escape only closes a focused
                // popup and can leave the preceding scenario on screen.
                if (frame == 0) ImGui::ClosePopupsOverWindow(nullptr, false);
                ImGui::PushFont(fonts.Get(skins[i]), skins[i].type.body);
                // ImGui rounds baked font sizes to whole pixels after scaling.
                Require(std::abs(ImGui::GetFontSize() - skins[i].type.body * dpi) <= .5f, "font scaled more than once");
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
                ImGui::Begin("##shell", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar);
                ImGui::PopStyleVar(2);
                if ((mode == 4 || mode == 14 || mode == 18) && frame == 2) ImGui::OpenPopup("Settings");
                panels.revealSettingsHotkeys = mode == 18;
                panels.hotkeyCapture = mode == 18 && frame > 2 ? 1 : -1;
                panels.transportKeysAvailable[2] = mode != 18;
                panels.openConvert = mode == 12 && frame == 2;
                panels.openLibrarySave = mode == 17 && frame == 2;
                // Settings scrolls, and the drum and auto-transpose switches
                // sit below the fold, so this scenario scrolls to them.
                panels.revealSettingsSwitches = mode == 14;
                panels.preferences.keyMappingOpen = mode == 15;
                panels.autoVolumeOpen = mode == 16;
                panels.Draw(nullptr, fonts, skins[i], dpi, engine);
                if (frame == 6) Require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) == ((mode >= 4 && mode <= 6) || mode == 12 || mode == 14 || mode == 17 || mode == 18),
                                        "render scenario popup did not open or leaked from a previous capture");
                ImGui::End(); ImGui::PopFont(); ImGui::Render();
                Require(ImGui::GetDrawData()->TotalVtxCount > 1000, "blank or incomplete frame");
                const float clear[4]{1, 0, 1, 1};
                context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
                context->ClearRenderTargetView(target.Get(), clear);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }
            SavePng(device.Get(), context.Get(), texture.Get(),
                    folder / ("skin-" + std::to_string(i) + "-" + std::to_string(static_cast<int>(dpi * 100)) + "-" + variants[mode] + ".png"));
            if (mode == 10) Require(pickerCalls > picksBefore, "mini Open MIDI file did not reach the picker");
            if (mode == 7 || mode == 8) {
                engine.Send({shell::ShellEngine::Action::Stop});
                const auto stoppedBy = std::chrono::steady_clock::now() + 2s;
                while (engine.Snapshot()->playbackCountdown && std::chrono::steady_clock::now() < stoppedBy)
                    std::this_thread::sleep_for(5ms);
                Require(!engine.Snapshot()->playbackCountdown, "render countdown did not cancel");
            }
            std::cout << "PASS " << skins[i].name << " at " << static_cast<int>(dpi * 100) << "% " << variants[mode] << '\n';
        }
        ImGui_ImplDX11_Shutdown(); ImGui::DestroyContext(); CoUninitialize();
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
