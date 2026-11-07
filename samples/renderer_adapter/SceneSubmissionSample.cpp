#include "talvisota/presentation/VantaSceneRenderer.hpp"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
LRESULT CALLBACK SampleWindowProcedure(
    const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

talvisota::presentation::SceneSnapshot BuildSampleScene()
{
    using namespace talvisota::presentation;
    SceneSnapshot scene;
    RenderMesh cube;

    const auto appendFace = [&cube](const std::array<float, 3>& normal,
        const std::array<float, 3>& a, const std::array<float, 3>& b,
        const std::array<float, 3>& c, const std::array<float, 3>& d) {
        const std::uint32_t base = static_cast<std::uint32_t>(cube.vertices.size());
        cube.vertices.push_back({a, normal, 0.0F, 1.0F});
        cube.vertices.push_back({b, normal, 0.0F, 0.0F});
        cube.vertices.push_back({c, normal, 1.0F, 0.0F});
        cube.vertices.push_back({d, normal, 1.0F, 1.0F});
        cube.indices.insert(cube.indices.end(),
            {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
    };

    constexpr float half = 0.42F;
    constexpr float depth = 0.55F;
    appendFace({0.0F, 0.0F, 1.0F}, {-half, -half, depth + half},
        {-half, half, depth + half}, {half, half, depth + half}, {half, -half, depth + half});
    appendFace({0.0F, 0.0F, -1.0F}, {half, -half, depth - half},
        {half, half, depth - half}, {-half, half, depth - half}, {-half, -half, depth - half});
    appendFace({0.0F, 1.0F, 0.0F}, {-half, half, depth + half},
        {-half, half, depth - half}, {half, half, depth - half}, {half, half, depth + half});
    appendFace({0.0F, -1.0F, 0.0F}, {-half, -half, depth - half},
        {-half, -half, depth + half}, {half, -half, depth + half}, {half, -half, depth - half});
    appendFace({1.0F, 0.0F, 0.0F}, {half, -half, depth + half},
        {half, half, depth + half}, {half, half, depth - half}, {half, -half, depth - half});
    appendFace({-1.0F, 0.0F, 0.0F}, {-half, -half, depth - half},
        {-half, half, depth - half}, {-half, half, depth + half}, {-half, -half, depth + half});

    scene.meshes.push_back(std::move(cube));
    scene.materials.push_back({{0.12F, 0.46F, 0.86F}, 0.42F, 0.04F});
    scene.instances.push_back({0U, 0U});
    scene.camera.position = {0.0F, 0.0F, 0.0F};
    scene.camera.elapsedSeconds = 0.0F;
    return scene;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand)
{
    using namespace talvisota::presentation;
    const wchar_t* className = L"TalvisotaRendererAdapterSample";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = &SampleWindowProcedure;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Could not register the sample window class.", L"Talvisota renderer sample", MB_ICONERROR);
        return 1;
    }

    RECT bounds{0, 0, 1280, 720};
    if (!AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE))
        return 1;
    HWND window = CreateWindowExW(0, className, L"Talvisota | Game-owned scene adapter sample",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr)
        return 1;
    ShowWindow(window, showCommand);

    const bool useD3D11 = commandLine != nullptr &&
        std::wstring_view{commandLine}.find(L"--backend=d3d11") != std::wstring_view::npos;
    auto renderer = CreateVantaSceneRenderer(useD3D11 ? RendererBackend::Direct3D11 :
        RendererBackend::Direct3D12);
    RECT client{};
    GetClientRect(window, &client);
    std::string error;
    if (!renderer->Initialize(window, client.right - client.left, client.bottom - client.top, error)) {
        MessageBoxA(window, error.c_str(), "Talvisota renderer sample", MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }

    SceneSnapshot scene = BuildSampleScene();
    if (!ValidateSceneSnapshot(scene, error)) {
        MessageBoxA(window, error.c_str(), "Talvisota renderer sample", MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    std::uint64_t frameNumber = 0;
    int previousWidth = client.right - client.left;
    int previousHeight = client.bottom - client.top;
    MSG message{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running)
            break;

        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) {
            Sleep(16);
            continue;
        }
        if (width != previousWidth || height != previousHeight) {
            if (!renderer->Resize(width, height, error)) {
                MessageBoxA(window, error.c_str(), "Talvisota renderer sample", MB_ICONERROR);
                break;
            }
            previousWidth = width;
            previousHeight = height;
        }

        scene.camera.elapsedSeconds = std::chrono::duration<float>(Clock::now() - start).count();
        scene.camera.frameNumber = frameNumber++;
        if (!renderer->Render(scene, error)) {
            MessageBoxA(window, error.c_str(), "Talvisota renderer sample", MB_ICONERROR);
            break;
        }
        Sleep(1);
    }

    renderer.reset();
    if (IsWindow(window))
        DestroyWindow(window);
    return 0;
}
