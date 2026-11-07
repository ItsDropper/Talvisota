#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/presentation/VantaSceneRenderer.hpp"
#include "talvisota/world/WorldRegion.hpp"
#include "vanta/assets/ObjLoader.hpp"
#include "vanta/math/Math.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using talvisota::presentation::RenderInstance;
using talvisota::presentation::RenderMaterial;
using talvisota::presentation::RenderMesh;
using talvisota::presentation::SceneSnapshot;

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}

template<class Diagnostics>
std::string FormatDiagnostics(const Diagnostics& diagnostics) {
    std::string result;
    for (const auto& item : diagnostics)
        result += item.file.string() + ":" + std::to_string(item.line) + " [" +
            item.field + "] " + item.message + "\n";
    return result;
}

bool ResolveTexture(const talvisota::content::ContentRegistry& registry,
    const std::string& id, std::string& output, std::string& error) {
    if (id.empty()) { output.clear(); return true; }
    const auto* asset = registry.Find(id);
    if (asset == nullptr || asset->kind != talvisota::content::AssetKind::Texture) {
        error = "Typed material references a missing or non-texture asset: " + id;
        return false;
    }
    std::error_code ec;
    const auto path = std::filesystem::weakly_canonical(registry.Root() / asset->relativePath, ec);
    if (ec || !std::filesystem::is_regular_file(path, ec) || ec) {
        error = "Typed material texture is not a readable regular file: " + id;
        return false;
    }
    output = path.string();
    return true;
}

bool ResolveImportedTexture(const std::filesystem::path& source,
    std::string& output, std::string& error) {
    if (source.empty()) { output.clear(); return true; }
    std::error_code ec;
    const auto path = std::filesystem::weakly_canonical(source, ec);
    if (ec || !std::filesystem::is_regular_file(path, ec) || ec) {
        error = "Imported material texture is missing or unreadable: " + source.string();
        return false;
    }
    output = path.string();
    return true;
}

bool ApplyTypedMaterial(const talvisota::content::MaterialDefinition& definition,
    const talvisota::content::ContentRegistry& registry, RenderMaterial& material, std::string& error) {
    material.baseColor = definition.baseColor;
    material.roughness = definition.roughness;
    material.metallic = definition.metallic;
    if (!ResolveTexture(registry, definition.baseColorTexture, material.baseColorTexture, error) ||
        !ResolveTexture(registry, definition.normalTexture, material.normalTexture, error))
        return false;
    if (!definition.ormTexture.empty()) {
        std::string orm;
        if (!ResolveTexture(registry, definition.ormTexture, orm, error)) return false;
        material.roughnessTexture = orm;
        material.ambientOcclusionTexture = orm;
        material.packedMetallicRoughness = true;
    }
    return true;
}

struct SubmeshBinding { std::uint32_t meshIndex; std::uint32_t materialIndex; };

bool BuildSakkolaScene(const std::filesystem::path& root, SceneSnapshot& scene,
    std::size_t& placementCount, std::size_t& drawCount, std::string& error) {
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> contentDiagnostics;
    if (!talvisota::content::LoadContentManifest(root / "content/catalog.tmanifest",
            root, registry, contentDiagnostics)) {
        error = "Sakkola content manifest failed validation:\n" + FormatDiagnostics(contentDiagnostics);
        return false;
    }
    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    if (!talvisota::content::LoadWorldDefinition(root / "content/worlds/sakkola.tworld",
            registry, world, worldDiagnostics)) {
        error = "Sakkola world definition failed validation:\n" + FormatDiagnostics(worldDiagnostics);
        return false;
    }
    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
    if (!talvisota::world::LoadWorldRegion(root / "content/worlds/sakkola-church-village.tregion",
            registry, world, region, regionDiagnostics)) {
        error = "Sakkola region failed validation:\n" + FormatDiagnostics(regionDiagnostics);
        return false;
    }

    std::map<std::string, std::vector<SubmeshBinding>> importedModels;
    std::map<std::string, talvisota::content::MaterialDefinition> typedMaterials;
    std::size_t importedModelCount = 0;
    for (const auto& placement : region.placements) {
        auto model = importedModels.find(placement.modelAssetId);
        if (model == importedModels.end()) {
            const auto* modelAsset = registry.Find(placement.modelAssetId);
            if (modelAsset == nullptr || modelAsset->kind != talvisota::content::AssetKind::Model) {
                error = "Placement references a missing model asset: " + placement.modelAssetId;
                return false;
            }
            const auto modelPath = registry.Root() / modelAsset->relativePath;
            vanta::assets::CpuModel imported;
            std::string importError;
            if (!vanta::assets::LoadWavefrontObj(modelPath, imported, importError)) {
                error = "Could not import " + placement.modelAssetId + ": " + importError;
                return false;
            }
            if (imported.submeshes.empty() || imported.materials.empty()) {
                error = "Model has no materialized submeshes: " + placement.modelAssetId;
                return false;
            }
            std::vector<SubmeshBinding> bindings;
            bindings.reserve(imported.submeshes.size());
            for (const auto& submesh : imported.submeshes) {
                if (submesh.materialIndex >= imported.materials.size() || submesh.indexCount == 0U ||
                    static_cast<std::size_t>(submesh.firstIndex) + submesh.indexCount > imported.indices.size()) {
                    error = "Model has an invalid submesh/material range: " + placement.modelAssetId;
                    return false;
                }
                const auto& sourceMaterial = imported.materials[submesh.materialIndex];
                RenderMesh mesh;
                mesh.vertices.reserve(imported.vertices.size());
                for (const auto& vertex : imported.vertices)
                    mesh.vertices.push_back({{vertex.position.x, vertex.position.y, vertex.position.z},
                        {vertex.normal.x, vertex.normal.y, vertex.normal.z}, vertex.u, vertex.v});
                const auto first = imported.indices.begin() + submesh.firstIndex;
                mesh.indices.assign(first, first + submesh.indexCount);

                RenderMaterial material;
                material.baseColor = {sourceMaterial.baseColor.x, sourceMaterial.baseColor.y,
                    sourceMaterial.baseColor.z};
                material.roughness = sourceMaterial.roughness;
                material.metallic = sourceMaterial.metallic;
                if (!ResolveImportedTexture(sourceMaterial.diffuseTexture, material.baseColorTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.normalTexture, material.normalTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.roughnessTexture, material.roughnessTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.metallicTexture, material.metallicTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.ambientOcclusionTexture, material.ambientOcclusionTexture, error))
                    return false;

                if (placement.materialAssetId != "-" && sourceMaterial.name == "snow") {
                    auto typed = typedMaterials.find(placement.materialAssetId);
                    if (typed == typedMaterials.end()) {
                        const auto* typedAsset = registry.Find(placement.materialAssetId);
                        if (typedAsset == nullptr || typedAsset->kind != talvisota::content::AssetKind::Material) {
                            error = "Placement references a missing typed material: " + placement.materialAssetId;
                            return false;
                        }
                        talvisota::content::MaterialDefinition definition;
                        std::vector<talvisota::content::MaterialDiagnostic> diagnostics;
                        if (!talvisota::content::LoadMaterialDefinition(
                                registry.Root() / typedAsset->relativePath, registry, definition, diagnostics)) {
                            error = "Typed material failed validation:\n" + FormatDiagnostics(diagnostics);
                            return false;
                        }
                        typed = typedMaterials.emplace(placement.materialAssetId, std::move(definition)).first;
                    }
                    if (!ApplyTypedMaterial(typed->second, registry, material, error)) return false;
                }

                const auto meshIndex = static_cast<std::uint32_t>(scene.meshes.size());
                const auto materialIndex = static_cast<std::uint32_t>(scene.materials.size());
                scene.meshes.push_back(std::move(mesh));
                scene.materials.push_back(std::move(material));
                bindings.push_back({meshIndex, materialIndex});
            }
            model = importedModels.emplace(placement.modelAssetId, std::move(bindings)).first;
            ++importedModelCount;
        }
        const auto transform = talvisota::world::BuildPlacementTransform(placement);
        for (const auto& binding : model->second) {
            RenderInstance instance;
            instance.meshIndex = binding.meshIndex;
            instance.materialIndex = binding.materialIndex;
            instance.world = transform;
            scene.instances.push_back(instance);
        }
    }
    placementCount = region.placements.size();
    drawCount = scene.instances.size();
    if (placementCount == 0U || scene.meshes.empty() || scene.materials.empty() || scene.instances.empty()) {
        error = "Sakkola scene import produced no renderable world content.";
        return false;
    }
    if (world.hasEnvironment) {
        scene.environment.enabled = true;
        scene.environment.sunDirection = world.environment.sunDirection;
        scene.environment.sunColor = world.environment.sunColor;
        scene.environment.sunIntensity = world.environment.sunIntensity;
        scene.environment.ambientColor = world.environment.ambientColor;
        scene.environment.ambientIntensity = world.environment.ambientIntensity;
        scene.environment.fogColor = world.environment.fogColor;
        scene.environment.fogDensity = world.environment.fogDensity;
        scene.environment.fogStart = world.environment.fogStart;
        scene.environment.windDirection = world.environment.windDirection;
        scene.environment.windStrength = world.environment.windStrength;
    }

    std::cout << "Loaded " << region.id << ": " << placementCount << " placements, "
        << importedModelCount << " distinct OBJ models, " << scene.meshes.size()
        << " submeshes, " << drawCount << " material-bound draw instances.\n";
    return true;
}

bool SetCamera(SceneSnapshot& scene, const vanta::math::Vec3& eye,
    const vanta::math::Vec3& target, int width, int height) {
    const auto view = vanta::math::Mat4::LookAtLH(eye, target, {0.0F, 1.0F, 0.0F});
    if (!view || width <= 0 || height <= 0) return false;
    constexpr float fov = 60.0F * 3.14159265358979323846F / 180.0F;
    const auto projection = vanta::math::Mat4::Perspective(
        fov, static_cast<float>(width) / static_cast<float>(height), 0.1F, 1200.0F);
    std::copy(std::begin(view->elements), std::end(view->elements), scene.camera.view.begin());
    std::copy(std::begin(projection.elements), std::end(projection.elements), scene.camera.projection.begin());
    scene.camera.position = {eye.x, eye.y, eye.z};
    return true;
}

bool CaptureClientPng(HWND window, const std::filesystem::path& path, std::string& error) {
    RECT client{};
    if (!GetClientRect(window, &client)) { error = "Could not query client rectangle."; return false; }
    const int width = client.right - client.left, height = client.bottom - client.top;
    if (width <= 0 || height <= 0) { error = "Renderer window has no drawable client area."; return false; }
    HDC windowDc = GetDC(window);
    if (!windowDc) { error = "Could not acquire renderer window DC."; return false; }
    HDC memoryDc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, width, height);
    if (!memoryDc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memoryDc) DeleteDC(memoryDc);
        ReleaseDC(window, windowDc);
        error = "Could not allocate screenshot surface.";
        return false;
    }
    HGDIOBJ previous = SelectObject(memoryDc, bitmap);
    const BOOL copied = BitBlt(memoryDc, 0, 0, width, height, windowDc, 0, 0, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    if (previous && previous != HGDI_ERROR) SelectObject(memoryDc, previous);
    DeleteDC(memoryDc);
    ReleaseDC(window, windowDc);
    if (!copied) { DeleteObject(bitmap); error = "Could not copy rendered client surface."; return false; }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) { DeleteObject(bitmap); error = "WIC factory creation failed."; return false; }
    ComPtr<IWICBitmap> source;
    hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUseAlpha, source.GetAddressOf());
    DeleteObject(bitmap);
    if (FAILED(hr)) { error = "Could not convert screenshot bitmap to WIC."; return false; }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { error = "Could not normalize the screenshot to 32-bit BGRA."; return false; }
    const UINT stride = static_cast<UINT>(width) * 4U;
    const std::size_t pixelBytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    if (pixelBytes > std::numeric_limits<UINT>::max()) {
        error = "Screenshot dimensions exceed WIC's supported pixel buffer range.";
        return false;
    }
    std::vector<BYTE> pixels(pixelBytes);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixelBytes), pixels.data());
    if (FAILED(hr)) { error = "Could not read back the captured framebuffer pixels."; return false; }
    std::array<bool, 32768> colorBins{};
    std::size_t distinctColorBins = 0U;
    std::uint64_t luminanceSum = 0U;
    std::uint64_t luminanceSquaredSum = 0U;
    std::size_t visiblePixels = 0U;
    for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
        const std::uint32_t blue = pixels[offset];
        const std::uint32_t green = pixels[offset + 1U];
        const std::uint32_t red = pixels[offset + 2U];
        const std::uint32_t luminance = (54U * red + 183U * green + 19U * blue) >> 8U;
        luminanceSum += luminance;
        luminanceSquaredSum += static_cast<std::uint64_t>(luminance) * luminance;
        if (luminance > 18U) ++visiblePixels;
        const std::size_t bin = ((red >> 3U) << 10U) | ((green >> 3U) << 5U) | (blue >> 3U);
        if (!colorBins[bin]) { colorBins[bin] = true; ++distinctColorBins; }
    }
    const std::uint64_t pixelCount = pixels.size() / 4U;
    const double meanLuminance = static_cast<double>(luminanceSum) / static_cast<double>(pixelCount);
    const double meanSquared = static_cast<double>(luminanceSquaredSum) / static_cast<double>(pixelCount);
    const double luminanceVariance = std::max(0.0, meanSquared - meanLuminance * meanLuminance);
    if (distinctColorBins < 64U || visiblePixels < pixelCount / 100U || luminanceVariance < 36.0) {
        error = "Captured framebuffer failed the non-blank image-content check (distinct colors=" +
            std::to_string(distinctColorBins) + ", visible pixels=" +
            std::to_string(visiblePixels) + ", luminance deviation=" +
            std::to_string(std::sqrt(luminanceVariance)) + ").";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create screenshot directory: " + ec.message(); return false; }
    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(converter.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) { error = "PNG encoding failed with HRESULT " +
        std::to_string(static_cast<unsigned long>(hr)); return false; }
    std::error_code sizeError;
    const auto bytes = std::filesystem::file_size(path, sizeError);
    if (sizeError || bytes < 1024U) {
        error = "Screenshot encoder returned success but the PNG file is absent or implausibly small.";
        return false;
    }
    error.clear();
    std::cout << "Captured " << path.string() << " (" << width << "x" << height
        << ", " << bytes << " bytes).\n";
    return true;
}

struct CaptureView { const wchar_t* file; vanta::math::Vec3 eye; vanta::math::Vec3 target; };
constexpr std::array<CaptureView, 5> Views = {{
    {L"01-sakkola-overview.png", {0.0F, 86.0F, -236.0F}, {0.0F, 5.0F, 38.0F}},
    {L"02-church-village.png", {-72.0F, 25.0F, 82.0F}, {0.0F, 11.0F, 174.0F}},
    {L"03-suvanto-shoreline.png", {-148.0F, 19.0F, -58.0F}, {-76.0F, 2.0F, -150.0F}},
    {L"04-karelian-forest.png", {132.0F, 23.0F, -112.0F}, {106.0F, 10.0F, 30.0F}},
    {L"05-farmstead.png", {104.0F, 21.0F, 66.0F}, {18.0F, 10.0F, 136.0F}}
}};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* console = nullptr;
        (void)freopen_s(&console, "CONOUT$", "w", stdout);
        (void)freopen_s(&console, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);
    }
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool ownsCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return 1;

    std::wstring captureDir;
    int framesPerView = 5;
    bool useD3D11 = false;
    bool captureRequested = false;
    int argumentCount = 0;
    LPWSTR* parsedArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (parsedArguments == nullptr) {
        if (ownsCom) CoUninitialize();
        return 1;
    }
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring_view token(parsedArguments[index]);
        if (token.rfind(L"--capture-dir=", 0) == 0) {
            captureRequested = true;
            captureDir = token.substr(14);
        } else if (token.rfind(L"--frames-per-view=", 0) == 0) {
            try { framesPerView = std::clamp(std::stoi(std::wstring(token.substr(18))), 1, 120); }
            catch (...) {
                LocalFree(parsedArguments);
                if (ownsCom) CoUninitialize();
                return 1;
            }
        } else if (token == L"--backend=d3d11") {
            useD3D11 = true;
        } else if (token == L"--backend=d3d12") {
            useD3D11 = false;
        } else {
            LocalFree(parsedArguments);
            if (ownsCom) CoUninitialize();
            return 1;
        }
    }
    LocalFree(parsedArguments);
    if (captureRequested && captureDir.empty()) {
        if (ownsCom) CoUninitialize();
        return 1;
    }
    std::cout << "Sakkola renderer backend: " << (useD3D11 ? "D3D11" : "D3D12")
        << "; capture mode: " << (captureRequested ? "enabled" : "disabled") << ".\\n";

    SceneSnapshot scene;
    std::size_t placements = 0, draws = 0;
    std::string error;
    if (!BuildSakkolaScene(std::filesystem::current_path(), scene, placements, draws, error)) {
        std::cerr << error << "\n";
        if (ownsCom) CoUninitialize();
        return 2;
    }

    const wchar_t* className = L"TalvisotaSakkolaRenderer";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &WindowProcedure;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::cerr << "Could not register Sakkola renderer window class.\n";
        if (ownsCom) CoUninitialize();
        return 3;
    }

    const int width = captureDir.empty() ? 1280 : 1920;
    const int height = captureDir.empty() ? 720 : 1080;
    RECT bounds{0, 0, width, height};
    if (!AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE)) {
        if (ownsCom) CoUninitialize();
        return 4;
    }
    HWND window = CreateWindowExW(0, className, L"Talvisota | Sakkola church village and Suvanto",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
    if (!window) {
        std::cerr << "Could not create Sakkola renderer window.\n";
        if (ownsCom) CoUninitialize();
        return 5;
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    SetForegroundWindow(window);

    auto renderer = talvisota::presentation::CreateVantaSceneRenderer(d3d11
        ? talvisota::presentation::RendererBackend::Direct3D11
        : talvisota::presentation::RendererBackend::Direct3D12);
    RECT client{};
    GetClientRect(window, &client);
    if (!renderer->Initialize(window, client.right - client.left, client.bottom - client.top, error)) {
        std::cerr << "Renderer initialization failed: " << error << "\n";
        renderer.reset(); DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        return 6;
    }

    std::uint64_t frameNumber = 0;
    if (!captureDir.empty()) {
        const std::filesystem::path output(captureDir);
        std::error_code directoryError;
        std::filesystem::create_directories(output, directoryError);
        if (directoryError)
            error = "Could not create screenshot output directory: " + directoryError.message();
        for (const auto& view : Views) {
            if (!error.empty()) break;
            GetClientRect(window, &client);
            if (!SetCamera(scene, view.eye, view.target, client.right - client.left, client.bottom - client.top)) {
                error = "Could not construct a valid Sakkola capture camera.";
                break;
            }
            for (int frame = 0; frame < framesPerView; ++frame) {
                scene.camera.elapsedSeconds = static_cast<float>(frameNumber) / 60.0F;
                scene.camera.frameNumber = frameNumber++;
                if (!renderer->Render(scene, error)) break;
                Sleep(16);
            }
            if (!error.empty()) break;
            (void)DwmFlush();
            Sleep(350);
            if (!CaptureClientPng(window, output / view.file, error)) break;
        }
        renderer.reset();
        if (IsWindow(window)) DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        if (!error.empty()) {
            std::cerr << "Sakkola capture failed: " << error << "\n";
            return 7;
        }
        return 0;
    }

    if (!SetCamera(scene, {0.0F, 24.0F, -90.0F}, {0.0F, 8.0F, 90.0F}, width, height)) {
        renderer.reset(); DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        return 8;
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    int oldWidth = client.right - client.left, oldHeight = client.bottom - client.top;
    MSG message{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;
        GetClientRect(window, &client);
        const int w = client.right - client.left, h = client.bottom - client.top;
        if (w <= 0 || h <= 0) { Sleep(16); continue; }
        if (w != oldWidth || h != oldHeight) {
            if (!renderer->Resize(w, h, error) || !SetCamera(scene,
                    {0.0F, 24.0F, -90.0F}, {0.0F, 8.0F, 90.0F}, w, h)) {
                std::cerr << "Renderer resize failed: " << error << "\n";
                break;
            }
            oldWidth = w; oldHeight = h;
        }
        scene.camera.elapsedSeconds = std::chrono::duration<float>(Clock::now() - start).count();
        scene.camera.frameNumber = frameNumber++;
        if (!renderer->Render(scene, error)) {
            std::cerr << "Renderer failed: " << error << "\n";
            break;
        }
        Sleep(1);
    }
    renderer.reset();
    if (IsWindow(window)) DestroyWindow(window);
    if (ownsCom) CoUninitialize();
    return error.empty() ? 0 : 9;
}
