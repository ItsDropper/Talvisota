#include "talvisota/app/Application.hpp"
#include "talvisota/platform/UserPaths.hpp"
#include "talvisota/game/PlayerController.hpp"
#include "talvisota/presentation/VantaSceneRenderer.hpp"
#include "talvisota/presentation/SakkolaSceneBuilder.hpp"
#include "talvisota/world/WorldRegion.hpp"
#include "vanta/math/Math.hpp"
#include <Windows.h>
#include <windowsx.h>
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
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using talvisota::presentation::SceneSnapshot;
using talvisota::presentation::BuildSakkolaScene;

LONG rawMouseDeltaX=0, rawMouseDeltaY=0;
HWND mainWindow=nullptr, frontendOverlay=nullptr;
bool frontendEnabled=false;
enum class FrontendState { MainMenu, Playing, Paused };
FrontendState frontendState=FrontendState::MainMenu;
void SetFrontendState(FrontendState state) {
    frontendState=state;
    if(frontendOverlay && IsWindow(frontendOverlay)) {
        ShowWindow(frontendOverlay,state==FrontendState::Playing?SW_HIDE:SW_SHOW);
        InvalidateRect(frontendOverlay,nullptr,FALSE);
    }
    rawMouseDeltaX=0; rawMouseDeltaY=0;
    if(state==FrontendState::Playing && mainWindow) SetFocus(mainWindow);
}
RECT MenuPanel(int w,int h) {
    const int pw=std::min(520,std::max(340,w-40)), ph=frontendState==FrontendState::MainMenu?380:430;
    return {(w-pw)/2,(h-ph)/2,(w+pw)/2,(h+ph)/2};
}
RECT MenuButton(int w,int h,int i) {
    const RECT p=MenuPanel(w,h); const LONG availableWidth=p.right-p.left-56L; const int bw=static_cast<int>(availableWidth<340L?availableWidth:340L);
    const int top=p.top+(frontendState==FrontendState::MainMenu?184:198)+i*62;
    return {(w-bw)/2,top,(w+bw)/2,top+48};
}
void PaintMenu(HWND hwnd) {
    PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT client{}; GetClientRect(hwnd,&client);
    HBRUSH key=CreateSolidBrush(RGB(255,0,255)); FillRect(dc,&client,key); DeleteObject(key);
    RECT p=MenuPanel(client.right,client.bottom); HBRUSH panel=CreateSolidBrush(RGB(13,22,34));
    FillRect(dc,&p,panel); DeleteObject(panel);
    HPEN edge=CreatePen(PS_SOLID,2,RGB(85,145,177)); HGDIOBJ op=SelectObject(dc,edge);
    HGDIOBJ ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH)); Rectangle(dc,p.left,p.top,p.right,p.bottom);
    SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(edge); SetBkMode(dc,TRANSPARENT);
    HFONT title=CreateFontW(38,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HFONT body=CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HGDIOBJ old=SelectObject(dc,title); SetTextColor(dc,RGB(235,242,248));
    RECT tr{p.left+20,p.top+30,p.right-20,p.top+90};
    DrawTextW(dc,frontendState==FrontendState::MainMenu?L"TALVISOTA":L"PAUSED",-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,body); SetTextColor(dc,RGB(155,179,196));
    RECT sr{p.left+18,p.top+100,p.right-18,p.top+145};
    DrawTextW(dc,frontendState==FrontendState::MainMenu?
        L"THE WINTER WAR  |  KARELIAN ISTHMUS  |  1939":L"GAME PAUSED  -  PROGRESS PRESERVED",
        -1,&sr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    const int count=frontendState==FrontendState::MainMenu?2:3;
    for(int i=0;i<count;++i) {
        RECT b=MenuButton(client.right,client.bottom,i);
        HBRUSH fill=CreateSolidBrush(i==0?RGB(35,105,145):RGB(30,43,57));
        FillRect(dc,&b,fill); DeleteObject(fill);
        HPEN pen=CreatePen(PS_SOLID,1,i==0?RGB(100,190,225):RGB(73,96,116));
        op=SelectObject(dc,pen); ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc,b.left,b.top,b.right,b.bottom); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(pen);
        SetTextColor(dc,RGB(240,245,249));
        const wchar_t* label=frontendState==FrontendState::MainMenu?
            (i==0?L"START GAME":L"QUIT"):(i==0?L"RESUME":i==1?L"MAIN MENU":L"QUIT");
        DrawTextW(dc,label,-1,&b,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    SelectObject(dc,old); SetTextColor(dc,RGB(132,153,170));
    RECT hint{p.left+12,p.bottom-42,p.right-12,p.bottom-12};
    DrawTextW(dc,frontendState==FrontendState::MainMenu?
        L"A HISTORICAL FIRST-PERSON EXPERIENCE":L"PRESS ESC TO RESUME",-1,&hint,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DeleteObject(body); DeleteObject(title); EndPaint(hwnd,&ps);
}
LRESULT CALLBACK OverlayProcedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) { PaintMenu(hwnd); return 0; }
    if(msg==WM_SETCURSOR && LOWORD(lp)==HTCLIENT) { SetCursor(LoadCursorW(nullptr,IDC_ARROW)); return TRUE; }
    if(msg==WM_LBUTTONUP) {
        RECT c{}; GetClientRect(hwnd,&c); const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        const int count=frontendState==FrontendState::MainMenu?2:3;
        for(int i=0;i<count;++i) { RECT b=MenuButton(c.right,c.bottom,i);
            if(x<b.left||x>=b.right||y<b.top||y>=b.bottom) continue;
            if(frontendState==FrontendState::MainMenu) {
                if(i==0) SetFrontendState(FrontendState::Playing); else if(mainWindow) DestroyWindow(mainWindow);
            } else if(i==0) SetFrontendState(FrontendState::Playing);
            else if(i==1) SetFrontendState(FrontendState::MainMenu);
            else if(mainWindow) DestroyWindow(mainWindow);
            return 0;
        }
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK WindowProcedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if(msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
    if(msg==WM_KILLFOCUS) { rawMouseDeltaX=0; rawMouseDeltaY=0; }
    if(msg==WM_SETCURSOR && LOWORD(lp)==HTCLIENT) {
        SetCursor(frontendState==FrontendState::Playing?nullptr:LoadCursorW(nullptr,IDC_ARROW)); return TRUE;
    }
    if(msg==WM_INPUT) {
        RAWINPUT in{}; UINT size=sizeof(in);
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,&in,&size,sizeof(RAWINPUTHEADER))==size &&
            in.header.dwType==RIM_TYPEMOUSE && (in.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)==0U &&
            frontendState==FrontendState::Playing) {
            rawMouseDeltaX+=in.data.mouse.lLastX; rawMouseDeltaY+=in.data.mouse.lLastY;
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    if(msg==WM_KEYDOWN && wp==VK_ESCAPE && (lp&(1LL<<30))==0) {
        if(frontendEnabled && frontendState==FrontendState::Playing) SetFrontendState(FrontendState::Paused);
        else if(frontendEnabled && frontendState==FrontendState::Paused) SetFrontendState(FrontendState::Playing);
        else if(!frontendEnabled) DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
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

bool FindPlayerSpawn(const talvisota::world::WorldRegion& region,
    const talvisota::game::PlayerConfig& config,
    talvisota::game::PlayerState& player, std::string& error) {
    constexpr float originX = 0.0F, originZ = -90.0F, step = 8.0F;
    constexpr int directions = 16;
    for (int ring = 0; ring <= 12; ++ring) {
        const float radius = static_cast<float>(ring) * step;
        for (int direction = 0; direction < directions; ++direction) {
            const float angle = static_cast<float>(direction) *
                (2.0F * 3.14159265358979323846F / static_cast<float>(directions));
            const std::array<float, 3> spawn{originX + std::sin(angle) * radius,
                0.0F, originZ + std::cos(angle) * radius};
            if (talvisota::game::InitializePlayer(region, config, spawn, player, error))
                return true;
        }
    }
    error = "Could not find a collision-free first-person spawn near the Sakkola starting area: " + error;
    return false;
}

std::vector<float> BuildMeshCullRadii(const SceneSnapshot& scene)
{
    std::vector<float> radii(scene.meshes.size(), 0.0F);
    for (std::size_t meshIndex = 0U; meshIndex < scene.meshes.size(); ++meshIndex) {
        float radiusSquared = 0.0F;
        for (const auto& vertex : scene.meshes[meshIndex].vertices) {
            const float x = vertex.position[0];
            const float y = vertex.position[1];
            const float z = vertex.position[2];
            radiusSquared = std::max(radiusSquared, x*x + y*y + z*z);
        }
        radii[meshIndex] = std::sqrt(radiusSquared);
    }
    return radii;
}

void CullCaptureInstances(SceneSnapshot& scene, const std::vector<float>& meshRadii,
    const std::vector<float>& maxDrawDistances, const vanta::math::Vec3& eye,
    const vanta::math::Vec3& target, const float aspect) {
    const auto normalize=[](std::array<float,3> v) {
        const float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(length>1.0e-6F) for(float& value:v) value/=length;
        return v;
    };
    const std::array<float,3> forward=normalize({target.x-eye.x,target.y-eye.y,target.z-eye.z});
    const std::array<float,3> right=normalize({forward[2],0.0F,-forward[0]});
    const std::array<float,3> up=normalize({
        right[1]*forward[2]-right[2]*forward[1],
        right[2]*forward[0]-right[0]*forward[2],
        right[0]*forward[1]-right[1]*forward[0]});
    constexpr float tangentHalfFov=0.57735026919F;
    std::size_t visibleCount=0U;
    for(std::size_t index=0U;index<scene.instances.size();++index) {
        auto& instance=scene.instances[index];
        if(instance.meshIndex>=meshRadii.size() || index>=maxDrawDistances.size()) {
            instance.visible=false;
            continue;
        }
        const auto& m=instance.world;
        const std::array<float,3> delta{m[12]-eye.x,m[13]-eye.y,m[14]-eye.z};
        const float distanceSquared=delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2];
        const float maxDistance=maxDrawDistances[index];
        const float scale=std::max({std::sqrt(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]),
            std::sqrt(m[4]*m[4]+m[5]*m[5]+m[6]*m[6]),
            std::sqrt(m[8]*m[8]+m[9]*m[9]+m[10]*m[10])});
        const float radius=meshRadii[instance.meshIndex]*scale;
        if(distanceSquared > (maxDistance + radius)*(maxDistance + radius)) {
            instance.visible=false;
            continue;
        }
        const float depth=delta[0]*forward[0]+delta[1]*forward[1]+delta[2]*forward[2];
        const float horizontal=std::abs(delta[0]*right[0]+delta[1]*right[1]+delta[2]*right[2]);
        const float vertical=std::abs(delta[0]*up[0]+delta[1]*up[1]+delta[2]*up[2]);
        instance.visible=depth+radius>=0.1F && depth-radius<=1200.0F &&
            horizontal<=std::max(0.0F,depth)*tangentHalfFov*aspect+radius &&
            vertical<=std::max(0.0F,depth)*tangentHalfFov+radius;
        if(instance.visible) ++visibleCount;
    }
    std::cout<<"Visibility culling: "<<visibleCount<<" / "<<scene.instances.size()
        <<" instances visible (vegetation/understory 280 m, props 600 m).\n";
}

bool WriteBgraPng(const std::vector<std::uint8_t>& pixels, const UINT width,
    const UINT height, const std::filesystem::path& path, std::string& error) {
    if (width == 0U || height == 0U ||
        pixels.size() != static_cast<std::size_t>(width) * height * 4U) {
        error = "D3D12 framebuffer readback returned invalid dimensions or byte count.";
        return false;
    }
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) { error = "WIC factory creation failed."; return false; }
    ComPtr<IWICBitmap> bitmap;
    hr = factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA,
        width * 4U, static_cast<UINT>(pixels.size()),
        const_cast<BYTE*>(pixels.data()), bitmap.GetAddressOf());
    if (FAILED(hr)) { error = "Could not wrap the D3D12 framebuffer pixels for PNG encoding."; return false; }
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
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) {
        error = "PNG encoding failed with HRESULT " + std::to_string(static_cast<unsigned long>(hr));
        return false;
    }
    error.clear();
    return true;
}

struct CaptureView { const wchar_t* file; vanta::math::Vec3 eye; vanta::math::Vec3 target; };
constexpr std::array<CaptureView, 5> Views = {{
    {L"01-sakkola-overview.png", {8.0F, 58.0F, -80.0F}, {0.0F, 9.0F, 140.0F}},
    {L"02-church-village.png", {-28.0F, 16.0F, 135.0F}, {0.0F, 16.0F, 178.0F}},
    {L"03-suvanto-shoreline.png", {-85.0F, 10.0F, -240.0F}, {-85.0F, 5.0F, -150.0F}},
    {L"04-karelian-forest.png", {160.0F, 20.0F, -170.0F}, {160.0F, 10.0F, -25.0F}},
    {L"05-farmstead.png", {0.0F, 24.0F, 35.0F}, {0.0F, 12.0F, 130.0F}}
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

    talvisota::platform::UserPaths userPaths;
    std::string applicationError;
    if(!talvisota::platform::ResolveUserPaths(userPaths,applicationError)) {
        MessageBoxA(nullptr,applicationError.c_str(),"Talvisota startup failed",MB_OK|MB_ICONERROR);
        if(ownsCom) CoUninitialize();
        return 1;
    }
    talvisota::app::Application application;
    if(!application.Initialize(userPaths.configurationDirectory/"config.ini",
            userPaths.dataDirectory,applicationError)) {
        MessageBoxA(nullptr,applicationError.c_str(),"Talvisota startup failed",MB_OK|MB_ICONERROR);
        if(ownsCom) CoUninitialize();
        return 1;
    }

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
    std::ofstream captureLog;
    if (captureRequested) {
        std::error_code logDirectoryError;
        std::filesystem::create_directories(std::filesystem::path(captureDir), logDirectoryError);
        if (logDirectoryError) {
            if (ownsCom) CoUninitialize();
            return 1;
        }
        captureLog.open(std::filesystem::path(captureDir) / "renderer.log", std::ios::out | std::ios::trunc);
        if (!captureLog) {
            if (ownsCom) CoUninitialize();
            return 1;
        }
        captureLog << "Sakkola renderer backend: " << (useD3D11 ? "D3D11" : "D3D12")
            << "; capture mode enabled; output=" << std::filesystem::path(captureDir).string() << "\n";
    }
    std::cout << "Sakkola renderer backend: " << (useD3D11 ? "D3D11" : "D3D12")
        << "; capture mode: " << (captureRequested ? "enabled" : "disabled") << ".\n";

    SceneSnapshot scene;
    talvisota::world::WorldRegion gameRegion;
    std::size_t placements = 0, draws = 0;
    std::vector<float> instanceMaxDrawDistances;
    std::string error;
    if (!BuildSakkolaScene(std::filesystem::current_path(), scene,
            instanceMaxDrawDistances, placements, draws, gameRegion, error)) {
        std::cerr << error << "\n";
        if (captureLog) captureLog << "Scene build failed:\n" << error << "\n";
        if (ownsCom) CoUninitialize();
        return 2;
    }

    std::cout << "Sakkola scene composition: placements=" << placements
        << ", material-bound instances=" << draws << ", unique meshes=" << scene.meshes.size()
        << ", materials=" << scene.materials.size() << ".\n";
    if (captureLog) captureLog << "Sakkola scene composition: placements=" << placements
        << ", material-bound instances=" << draws << ", unique meshes=" << scene.meshes.size()
        << ", materials=" << scene.materials.size() << ".\n";

    const std::vector<float> meshCullRadii = BuildMeshCullRadii(scene);

    const wchar_t* className = L"TalvisotaSakkolaRenderer";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &WindowProcedure;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    WNDCLASSEXW overlayClass{}; overlayClass.cbSize=sizeof(overlayClass); overlayClass.hInstance=instance;
    overlayClass.lpfnWndProc=&OverlayProcedure; overlayClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    overlayClass.lpszClassName=L"TalvisotaFrontendOverlay";
    if(RegisterClassExW(&overlayClass)==0 && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) {
        if(ownsCom) CoUninitialize(); return 3;
    }
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::cerr << "Could not register Sakkola renderer window class.\n";
        if (ownsCom) CoUninitialize();
        return 3;
    }

    const int width = 1280;
    const int height = 720;
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
    mainWindow=window; frontendEnabled=!captureRequested; frontendState=FrontendState::MainMenu;
    RAWINPUTDEVICE mouseDevice{0x01U, 0x02U, 0U, window};
    const bool rawMouseAvailable = RegisterRawInputDevices(&mouseDevice, 1U, sizeof(mouseDevice)) != FALSE;
    if (!rawMouseAvailable)
        std::cerr << "Raw mouse registration failed; use arrow keys for camera look.\\n";
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    SetForegroundWindow(window);

    auto renderer = talvisota::presentation::CreateVantaSceneRenderer(useD3D11
        ? talvisota::presentation::RendererBackend::Direct3D11
        : talvisota::presentation::RendererBackend::Direct3D12);
    RECT client{};
    GetClientRect(window, &client);
    if (!renderer->Initialize(window, client.right - client.left, client.bottom - client.top, error)) {
        std::cerr << "Renderer initialization failed: " << error << "\n";
        if (captureLog) captureLog << "Renderer initialization failed: " << error << "\n";
        renderer.reset(); DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        return 6;
    }
    if(frontendEnabled) {
        frontendOverlay=CreateWindowExW(WS_EX_LAYERED,L"TalvisotaFrontendOverlay",L"",
            WS_CHILD|WS_VISIBLE,0,0,client.right-client.left,client.bottom-client.top,
            window,nullptr,instance,nullptr);
        if(frontendOverlay==nullptr || !SetLayeredWindowAttributes(frontendOverlay,
                RGB(255,0,255),0,LWA_COLORKEY)) {
            error="Could not initialize the main menu/pause overlay.";
            renderer.reset(); DestroyWindow(window); if(ownsCom) CoUninitialize(); return 6;
        }
        InvalidateRect(frontendOverlay,nullptr,FALSE);
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
            CullCaptureInstances(scene, meshCullRadii, instanceMaxDrawDistances,
                view.eye, view.target,
                static_cast<float>(client.right-client.left)/static_cast<float>(client.bottom-client.top));
            std::vector<double> frameTimesMs;
            std::vector<double> renderTimesMs;
            frameTimesMs.reserve(static_cast<std::size_t>(framesPerView));
            renderTimesMs.reserve(static_cast<std::size_t>(framesPerView));
            for (int frame = 0; frame < framesPerView; ++frame) {
                const auto frameStart = std::chrono::steady_clock::now();
                scene.camera.elapsedSeconds = static_cast<float>(frameNumber) / 60.0F;
                scene.camera.frameNumber = frameNumber++;
                const auto renderStart = std::chrono::steady_clock::now();
                if (!renderer->Render(scene, error)) break;
                const auto renderEnd = std::chrono::steady_clock::now();
                renderTimesMs.push_back(std::chrono::duration<double, std::milli>(
                    renderEnd - renderStart).count());
                if (!captureRequested) Sleep(16);
                frameTimesMs.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - frameStart).count());
            }
            if (!error.empty()) break;
            if (frameTimesMs.size() != static_cast<std::size_t>(framesPerView) ||
                renderTimesMs.size() != static_cast<std::size_t>(framesPerView)) {
                error = "Capture timing sample count does not match the requested frame count.";
                break;
            }
            const auto percentile95 = [](std::vector<double> samples) {
                std::sort(samples.begin(), samples.end());
                const auto index = static_cast<std::size_t>(
                    std::ceil(static_cast<double>(samples.size()) * 0.95)) - 1U;
                return samples[index];
            };
            double totalFrameMs = 0.0;
            double totalRenderMs = 0.0;
            for (const double sample : frameTimesMs) totalFrameMs += sample;
            for (const double sample : renderTimesMs) totalRenderMs += sample;
            const double averageFrameMs = totalFrameMs / static_cast<double>(frameTimesMs.size());
            const double averageRenderMs = totalRenderMs / static_cast<double>(renderTimesMs.size());
            const double p95FrameMs = percentile95(frameTimesMs);
            const double p95RenderMs = percentile95(renderTimesMs);
            const std::string timingSummary = "Performance sample " +
                std::filesystem::path(view.file).string() + ": frames=" +
                std::to_string(frameTimesMs.size()) + ", avgFrameMs=" +
                std::to_string(averageFrameMs) + ", p95FrameMs=" +
                std::to_string(p95FrameMs) + ", avgRenderCpuMs=" +
                std::to_string(averageRenderMs) + ", p95RenderCpuMs=" +
                std::to_string(p95RenderMs);
            std::cout << timingSummary << "\n";
            if (captureLog) captureLog << timingSummary << "\n";
            std::vector<std::uint8_t> framebuffer;
            std::uint32_t framebufferWidth = 0U, framebufferHeight = 0U;
            if (!renderer->ReadbackPresentedFrame(framebuffer, framebufferWidth, framebufferHeight, error))
                break;
            if (framebufferWidth < 8U || framebufferHeight < 8U ||
                framebuffer.size() != static_cast<std::size_t>(framebufferWidth) *
                    static_cast<std::size_t>(framebufferHeight) * 4U) {
                error = "Presented framebuffer has invalid BGRA dimensions or byte count.";
                break;
            }
            const std::size_t skyPixel = (static_cast<std::size_t>(framebufferHeight / 8U) *
                framebufferWidth + framebufferWidth / 4U) * 4U;
            if (!WriteBgraPng(framebuffer, framebufferWidth, framebufferHeight,
                    output / view.file, error))
                break;
            if (framebuffer[skyPixel] <= framebuffer[skyPixel + 2U] + 8U) {
                error = "Presented BGRA sky sample is not blue-dominant (B=" +
                    std::to_string(framebuffer[skyPixel]) + ", G=" +
                    std::to_string(framebuffer[skyPixel + 1U]) + ", R=" +
                    std::to_string(framebuffer[skyPixel + 2U]) +
                    "); framebuffer channel order or sky environment is incorrect.";
                break;
            }
            std::uint64_t frameHash = 1469598103934665603ULL;
            for (const std::uint8_t byte : framebuffer) {
                frameHash ^= byte;
                frameHash *= 1099511628211ULL;
            }
            static std::vector<std::uint64_t> capturedFrameHashes;
            if (std::find(capturedFrameHashes.begin(), capturedFrameHashes.end(), frameHash) !=
                capturedFrameHashes.end()) {
                error = "Capture produced a duplicate presented framebuffer across distinct viewpoints.";
                break;
            }
            capturedFrameHashes.push_back(frameHash);
            std::cout << "Viewpoint " << std::filesystem::path(view.file).string() << ": eye=(" << view.eye.x << ","
                << view.eye.y << "," << view.eye.z << ") target=(" << view.target.x << ","
                << view.target.y << "," << view.target.z << "), frame=" << frameNumber
                << ", framebuffer=" << framebufferWidth << "x" << framebufferHeight
                << ", FNV1a=0x" << std::hex << frameHash << std::dec << "\n";

        }
        renderer.reset();
        if (IsWindow(window)) DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        if (!error.empty()) {
            std::cerr << "Sakkola capture failed: " << error << "\n";
            if (captureLog) captureLog << "Capture failed: " << error << "\n";
            return 7;
        }
        if (captureLog) captureLog << "Capture completed: five PNG viewpoints rendered and validated.\n";
        return 0;
    }

    talvisota::game::PlayerConfig playerConfig;
    talvisota::game::PlayerState playerState;
    if (!FindPlayerSpawn(gameRegion, playerConfig, playerState, error)) {
        std::cerr << error << "\n";
        renderer.reset(); DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        return 8;
    }
    const talvisota::game::PlayerState initialPlayerState=playerState;
    auto playerCamera = talvisota::game::BuildPlayerCamera(playerState, playerConfig);
    vanta::math::Vec3 cameraPosition{playerCamera.position[0], playerCamera.position[1], playerCamera.position[2]};
    vanta::math::Vec3 cameraTarget{cameraPosition.x + playerCamera.forward[0] * 180.0F,
        cameraPosition.y + playerCamera.forward[1] * 180.0F,
        cameraPosition.z + playerCamera.forward[2] * 180.0F};
    if (!SetCamera(scene, cameraPosition, cameraTarget, width, height)) {
        renderer.reset(); DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        return 8;
    }
    CullCaptureInstances(scene, meshCullRadii, instanceMaxDrawDistances,
        cameraPosition, cameraTarget, static_cast<float>(width) / static_cast<float>(height));
    using Clock = std::chrono::steady_clock;
    auto previousFrame = Clock::now();
    const auto start = previousFrame;
    double accumulatedFrameMs = 0.0;
    double accumulatedRenderMs = 0.0;
    std::uint32_t profiledFrames = 0U;
    int oldWidth = client.right - client.left, oldHeight = client.bottom - client.top;
    MSG message{};
    bool running=true, jumpWasDown=false;
    FrontendState previousFrontendState=frontendState;
    std::cout << "Sakkola: Start Game; WASD, mouse look, Space jump, Shift sprint, Ctrl crouch, ESC pause.\n";
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;
        const auto now=Clock::now();
        bool cameraChanged=false;
        if(frontendState!=previousFrontendState) {
            if(frontendState==FrontendState::MainMenu) {
                playerState=initialPlayerState;
                playerCamera=talvisota::game::BuildPlayerCamera(playerState,playerConfig);
                cameraPosition={playerCamera.position[0],playerCamera.position[1],playerCamera.position[2]};
                cameraTarget={cameraPosition.x+playerCamera.forward[0]*180.0F,
                    cameraPosition.y+playerCamera.forward[1]*180.0F,cameraPosition.z+playerCamera.forward[2]*180.0F};
                cameraChanged=true;
            }
            rawMouseDeltaX=0; rawMouseDeltaY=0; jumpWasDown=false;
            previousFrame=now; previousFrontendState=frontendState;
            InvalidateRect(frontendOverlay,nullptr,FALSE);
        }
        const double frameDeltaMs =
            std::chrono::duration<double, std::milli>(now - previousFrame).count();
        const float deltaSeconds = std::clamp(
            static_cast<float>(frameDeltaMs * 0.001), 0.0F, 0.1F);
        previousFrame = now;
        if(frontendState==FrontendState::Playing) {
            talvisota::game::PlayerInput playerInput;
            playerInput.forward=((GetAsyncKeyState('W')&0x8000)!=0?1.0F:0.0F)-
                ((GetAsyncKeyState('S')&0x8000)!=0?1.0F:0.0F);
            playerInput.right=((GetAsyncKeyState('D')&0x8000)!=0?1.0F:0.0F)-
                ((GetAsyncKeyState('A')&0x8000)!=0?1.0F:0.0F);
            playerInput.sprint=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
            playerInput.crouch=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
            const bool jumpDown=(GetAsyncKeyState(VK_SPACE)&0x8000)!=0;
            playerInput.jumpPressed=jumpDown&&!jumpWasDown; jumpWasDown=jumpDown;
            const float lookStep=1.45F*deltaSeconds;
            constexpr float mouseRadiansPerCount=0.0025F;
            const LONG mouseX=std::exchange(rawMouseDeltaX,0L), mouseY=std::exchange(rawMouseDeltaY,0L);
            playerInput.lookYawRadians=static_cast<float>(mouseX)*mouseRadiansPerCount+
                ((GetAsyncKeyState(VK_RIGHT)&0x8000)!=0?lookStep:0.0F)-
                ((GetAsyncKeyState(VK_LEFT)&0x8000)!=0?lookStep:0.0F);
            playerInput.lookPitchRadians=-static_cast<float>(mouseY)*mouseRadiansPerCount+
                ((GetAsyncKeyState(VK_UP)&0x8000)!=0?lookStep:0.0F)-
                ((GetAsyncKeyState(VK_DOWN)&0x8000)!=0?lookStep:0.0F);
            const auto previousPosition=playerState.position;
            const bool wasCrouched=playerState.crouched;
            if(!talvisota::game::UpdatePlayer(gameRegion,playerConfig,playerInput,
                    std::max(deltaSeconds,1.0F/1000.0F),playerState,error)) {
                std::cerr<<"Player simulation failed: "<<error<<"\\n"; break;
            }
            cameraChanged=playerState.position!=previousPosition ||
                playerInput.lookYawRadians!=0.0F || playerInput.lookPitchRadians!=0.0F ||
                playerState.crouched!=wasCrouched;
        } else { rawMouseDeltaX=0; rawMouseDeltaY=0; jumpWasDown=false; }
        if (cameraChanged) {
            playerCamera = talvisota::game::BuildPlayerCamera(playerState, playerConfig);
            cameraPosition = {playerCamera.position[0], playerCamera.position[1], playerCamera.position[2]};
            cameraTarget = {cameraPosition.x + playerCamera.forward[0] * 180.0F,
                cameraPosition.y + playerCamera.forward[1] * 180.0F,
                cameraPosition.z + playerCamera.forward[2] * 180.0F};
        }
        GetClientRect(window, &client);
        const int w = client.right - client.left, h = client.bottom - client.top;
        if (w <= 0 || h <= 0) { Sleep(16); continue; }
        if (w != oldWidth || h != oldHeight) {
            if(frontendOverlay) SetWindowPos(frontendOverlay,nullptr,0,0,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
            if (!renderer->Resize(w, h, error)) {
                std::cerr << "Renderer resize failed: " << error << "\n";
                break;
            }
            oldWidth = w; oldHeight = h; cameraChanged = true;
        }
        if (cameraChanged) {
            if (!SetCamera(scene, cameraPosition, cameraTarget, w, h)) {
                error = "First-person camera view/projection update failed.";
                break;
            }
            CullCaptureInstances(scene, meshCullRadii, instanceMaxDrawDistances,
                cameraPosition, cameraTarget, static_cast<float>(w) / static_cast<float>(h));
        }
        scene.camera.elapsedSeconds = std::chrono::duration<float>(now - start).count();
        scene.camera.frameNumber = frameNumber++;
        const auto renderStart = Clock::now();
        const bool renderSucceeded = renderer->Render(scene, error);
        const auto renderEnd = Clock::now();
        accumulatedFrameMs += frameDeltaMs;
        accumulatedRenderMs += std::chrono::duration<double, std::milli>(
            renderEnd - renderStart).count();
        ++profiledFrames;
        if (profiledFrames == 30U) {
            const double frameMs = accumulatedFrameMs / 30.0;
            const double renderMs = accumulatedRenderMs / 30.0;
            std::cout << "Frame profile (30-frame average): " << frameMs
                << " ms (" << (frameMs > 0.0 ? 1000.0 / frameMs : 0.0)
                << " FPS), Render() CPU " << renderMs << " ms.\n";
            accumulatedFrameMs = 0.0;
            accumulatedRenderMs = 0.0;
            profiledFrames = 0U;
        }
        if (!renderSucceeded) {
            std::cerr << "Renderer failed: " << error << "\n";
            break;
        }
        Sleep(1);
    }
    renderer.reset(); frontendOverlay=nullptr; mainWindow=nullptr;
    if (IsWindow(window)) DestroyWindow(window);
    if (ownsCom) CoUninitialize();
    return error.empty() ? 0 : 9;
}
