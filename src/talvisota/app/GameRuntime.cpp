#include "talvisota/app/Application.hpp"
#include "talvisota/platform/UserPaths.hpp"
#include "talvisota/game/PlayerController.hpp"
#include "talvisota/gameplay/GameplaySimulation.hpp"
#include "talvisota/missions/MissionRuntime.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/presentation/VantaSceneRenderer.hpp"
#include "talvisota/presentation/SakkolaSceneBuilder.hpp"
#include "talvisota/presentation/CaptureValidation.hpp"
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
using talvisota::presentation::PresentedSkyRegionStats;
using talvisota::presentation::ValidatePresentedSkyRegion;

LONG rawMouseDeltaX=0, rawMouseDeltaY=0;
HWND mainWindow=nullptr, frontendOverlay=nullptr;
bool frontendEnabled=false;
bool restartPatrolRequested=false;
int selectedMenuButton=0;
enum class FrontendState { MainMenu, Playing, Paused };
FrontendState frontendState=FrontendState::MainMenu;

struct MissionOverlayData final {
    std::wstring title=L"FIRST PATROL";
    std::wstring description=L"Check the farmstead well and woodpile before reporting back to the Sakkola church.";
    std::wstring objective=L"INSPECT THE FARMSTEAD WELL";
    std::wstring distance;
    std::wstring progress=L"0 / 3 OBJECTIVES";
    std::wstring prompt=L"FOLLOW THE OBJECTIVE";
    std::wstring feedback;
    bool completed=false;
    bool hasProgress=false;
};
MissionOverlayData missionOverlay;
std::chrono::steady_clock::time_point missionFeedbackUntil{};

std::wstring Utf8ToWide(const std::string_view value) {
    if(value.empty()) return {};
    const int sourceLength=static_cast<int>(std::min<std::size_t>(value.size(),static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const int required=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),sourceLength,nullptr,0);
    if(required<=0) return std::wstring(value.begin(),value.begin()+sourceLength);
    std::wstring result(static_cast<std::size_t>(required),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),sourceLength,result.data(),required)!=required)
        return std::wstring(value.begin(),value.begin()+sourceLength);
    return result;
}

std::wstring ObjectiveTitle(const std::string_view id) {
    if(id=="objective.inspect-well") return L"INSPECT THE FARMSTEAD WELL";
    if(id=="objective.inspect-woodpile") return L"CHECK THE WESTERN WOODPILE";
    if(id=="objective.report-church") return L"REPORT BACK TO THE CHURCH";
    return Utf8ToWide(id);
}

void SetMissionFeedback(std::wstring message,const std::chrono::milliseconds duration) {
    missionOverlay.feedback=std::move(message);
    missionFeedbackUntil=std::chrono::steady_clock::now()+duration;
    if(frontendOverlay&&IsWindow(frontendOverlay)) InvalidateRect(frontendOverlay,nullptr,FALSE);
}

bool RefreshMissionOverlay(const talvisota::gameplay::SimulationState& simulation,
    const float interactionRadius,std::string& error) {
    MissionOverlayData updated=missionOverlay;
    updated.completed=simulation.mission.status==talvisota::missions::MissionStatus::Completed;
    updated.hasProgress=simulation.mission.completedObjectives>0U;
    updated.progress=std::to_wstring(simulation.mission.completedObjectives)+L" / "+
        std::to_wstring(simulation.mission.objectives.size())+L" OBJECTIVES";
    if(updated.completed) {
        updated.objective=L"PATROL COMPLETE";
        updated.distance.clear();
        updated.prompt=L"PRESS ESC TO RETURN TO THE MENU";
    } else {
        const auto active=std::find_if(simulation.mission.objectives.begin(),simulation.mission.objectives.end(),
            [](const talvisota::missions::ObjectiveState& objective) {
                return objective.status==talvisota::missions::ObjectiveStatus::Active;
            });
        if(active==simulation.mission.objectives.end()) {
            error="Active First Patrol has no active objective for the HUD.";
            return false;
        }
        talvisota::gameplay::ActiveObjectiveTarget target;
        if(!talvisota::gameplay::FindActiveObjectiveTarget(simulation,simulation.playerEntityId,target,error))
            return false;
        updated.objective=ObjectiveTitle(active->id);
        const long distance=std::max(0L,std::lround(target.distanceMeters));
        updated.distance=std::to_wstring(distance)+L" m";
        updated.prompt=target.distanceMeters<=interactionRadius?L"[E]  INTERACT":L"FOLLOW THE OBJECTIVE";
    }
    if(std::chrono::steady_clock::now()>=missionFeedbackUntil) updated.feedback.clear();
    const bool changed=updated.title!=missionOverlay.title||updated.description!=missionOverlay.description||
        updated.objective!=missionOverlay.objective||updated.distance!=missionOverlay.distance||
        updated.progress!=missionOverlay.progress||updated.prompt!=missionOverlay.prompt||
        updated.feedback!=missionOverlay.feedback||updated.completed!=missionOverlay.completed||
        updated.hasProgress!=missionOverlay.hasProgress;
    missionOverlay=std::move(updated);
    if(changed&&frontendOverlay&&IsWindow(frontendOverlay)) InvalidateRect(frontendOverlay,nullptr,FALSE);
    error.clear();
    return true;
}

void SetFrontendState(const FrontendState state) {
    frontendState=state;
    if(state!=FrontendState::Playing) selectedMenuButton=0;
    if(frontendOverlay && IsWindow(frontendOverlay)) {
        ShowWindow(frontendOverlay,SW_SHOW);
        InvalidateRect(frontendOverlay,nullptr,FALSE);
    }
    rawMouseDeltaX=0; rawMouseDeltaY=0;
    if(state==FrontendState::Playing && mainWindow) SetFocus(mainWindow);
}
RECT MenuPanel(int w,int h) {
    const int pw=std::min(620,std::max(360,w-40));
    const int ph=frontendState==FrontendState::MainMenu?500:440;
    return {(w-pw)/2,(h-ph)/2,(w+pw)/2,(h+ph)/2};
}
RECT MenuButton(int w,int h,int i) {
    const RECT p=MenuPanel(w,h);
    const int bw=std::min(420,std::max(280,static_cast<int>(p.right-p.left-72L)));
    const int top=p.top+(frontendState==FrontendState::MainMenu?292:210)+i*62;
    return {(w-bw)/2,top,(w+bw)/2,top+48};
}
void PaintMissionHud(HDC dc,const RECT& client) {
    SetBkMode(dc,TRANSPARENT);
    HBRUSH panel=CreateSolidBrush(RGB(10,18,27));
    RECT card{24,24,500,190};
    FillRect(dc,&card,panel);
    HPEN edge=CreatePen(PS_SOLID,1,RGB(68,119,148));
    HGDIOBJ oldPen=SelectObject(dc,edge),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc,card.left,card.top,card.right,card.bottom);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(edge); DeleteObject(panel);
    HFONT heading=CreateFontW(19,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HFONT body=CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HGDIOBJ oldFont=SelectObject(dc,heading);
    SetTextColor(dc,RGB(105,190,224));
    RECT title{44,38,480,68}; DrawTextW(dc,L"FIRST PATROL",-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,body); SetTextColor(dc,RGB(239,244,248));
    RECT objective{44,76,480,106};
    DrawTextW(dc,missionOverlay.objective.c_str(),-1,&objective,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SetTextColor(dc,RGB(157,180,197));
    RECT progress{44,112,270,138}; DrawTextW(dc,missionOverlay.progress.c_str(),-1,&progress,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT distance{280,112,480,138}; DrawTextW(dc,missionOverlay.distance.c_str(),-1,&distance,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    SetTextColor(dc,RGB(113,192,220));
    RECT prompt{44,145,480,174}; DrawTextW(dc,missionOverlay.feedback.empty()?missionOverlay.prompt.c_str():missionOverlay.feedback.c_str(),
        -1,&prompt,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,oldFont); DeleteObject(body); DeleteObject(heading);

    if(missionOverlay.completed) {
        RECT banner{client.right/2-250,client.bottom-142,client.right/2+250,client.bottom-70};
        HBRUSH success=CreateSolidBrush(RGB(10,24,34)); FillRect(dc,&banner,success); DeleteObject(success);
        HFONT complete=CreateFontW(24,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        oldFont=SelectObject(dc,complete); SetTextColor(dc,RGB(125,215,190));
        DrawTextW(dc,L"PATROL COMPLETE",-1,&banner,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc,oldFont); DeleteObject(complete);
    }
    // A restrained centre reticle keeps the first-person view readable without
    // introducing a full-screen opaque HUD layer.
    HPEN reticle=CreatePen(PS_SOLID,1,RGB(226,236,242));
    oldPen=SelectObject(dc,reticle);
    const int cx=(client.left+client.right)/2,cy=(client.top+client.bottom)/2;
    MoveToEx(dc,cx-7,cy,nullptr); LineTo(dc,cx-3,cy);
    MoveToEx(dc,cx+3,cy,nullptr); LineTo(dc,cx+7,cy);
    MoveToEx(dc,cx,cy-7,nullptr); LineTo(dc,cx,cy-3);
    MoveToEx(dc,cx,cy+3,nullptr); LineTo(dc,cx,cy+7);
    SelectObject(dc,oldPen); DeleteObject(reticle);
}
void PaintMenu(HWND hwnd) {
    PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT client{}; GetClientRect(hwnd,&client);
    HBRUSH key=CreateSolidBrush(RGB(255,0,255)); FillRect(dc,&client,key); DeleteObject(key);
    if(frontendState==FrontendState::Playing) {
        PaintMissionHud(dc,client);
        EndPaint(hwnd,&ps);
        return;
    }
    RECT p=MenuPanel(client.right,client.bottom);
    HBRUSH panel=CreateSolidBrush(RGB(10,18,28)); FillRect(dc,&p,panel); DeleteObject(panel);
    HPEN edge=CreatePen(PS_SOLID,2,RGB(76,139,171)); HGDIOBJ op=SelectObject(dc,edge);
    HGDIOBJ ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH)); Rectangle(dc,p.left,p.top,p.right,p.bottom);
    SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(edge); SetBkMode(dc,TRANSPARENT);
    HFONT title=CreateFontW(42,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HFONT body=CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HGDIOBJ old=SelectObject(dc,title); SetTextColor(dc,RGB(238,244,248));
    RECT tr{p.left+24,p.top+28,p.right-24,p.top+88};
    DrawTextW(dc,frontendState==FrontendState::MainMenu?L"TALVISOTA":L"PAUSED",-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,body); SetTextColor(dc,RGB(147,176,196));
    RECT sr{p.left+20,p.top+92,p.right-20,p.top+126};
    DrawTextW(dc,frontendState==FrontendState::MainMenu?
        L"THE WINTER WAR  |  KARELIAN ISTHMUS  |  1939":L"GAME PAUSED  |  YOUR PATROL PROGRESS IS PRESERVED",
        -1,&sr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(frontendState==FrontendState::MainMenu) {
        SetTextColor(dc,RGB(106,190,222));
        RECT missionTitle{p.left+44,p.top+150,p.right-44,p.top+182};
        DrawTextW(dc,missionOverlay.title.c_str(),-1,&missionTitle,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(dc,RGB(208,220,230));
        RECT briefing{p.left+44,p.top+188,p.right-44,p.top+250};
        DrawTextW(dc,missionOverlay.description.c_str(),-1,&briefing,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
    }
    const int count=frontendState==FrontendState::MainMenu?2:3;
    for(int i=0;i<count;++i) {
        RECT b=MenuButton(client.right,client.bottom,i);
        HBRUSH fill=CreateSolidBrush(i==selectedMenuButton?RGB(35,105,145):RGB(23,37,51));
        FillRect(dc,&b,fill); DeleteObject(fill);
        HPEN pen=CreatePen(PS_SOLID,1,i==selectedMenuButton?RGB(110,198,229):RGB(63,91,112));
        op=SelectObject(dc,pen); ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc,b.left,b.top,b.right,b.bottom); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(pen);
        SetTextColor(dc,RGB(240,245,249));
        const wchar_t* label=frontendState==FrontendState::MainMenu?
            (i==0?(missionOverlay.completed?L"REPLAY FIRST PATROL":missionOverlay.hasProgress?L"CONTINUE PATROL":L"START FIRST PATROL"):L"QUIT"):
            (i==0?L"RESUME":i==1?L"RETURN TO MAIN MENU":L"QUIT");
        DrawTextW(dc,label,-1,&b,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    SelectObject(dc,old); SetTextColor(dc,RGB(126,151,169));
    RECT hint{p.left+18,p.bottom-38,p.right-18,p.bottom-12};
    DrawTextW(dc,frontendState==FrontendState::MainMenu?
        L"WASD MOVE  |  MOUSE LOOK  |  E INTERACT  |  ESC PAUSE":
        L"ESC TO RESUME  |  MISSION PROGRESS IS KEPT",
        -1,&hint,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DeleteObject(body); DeleteObject(title); EndPaint(hwnd,&ps);
}
void ActivateFrontendButton(const int index) {
    if(frontendState==FrontendState::MainMenu) {
        if(index==0) {
            if(missionOverlay.completed) restartPatrolRequested=true;
            SetFrontendState(FrontendState::Playing);
        } else if(index==1&&mainWindow) DestroyWindow(mainWindow);
    } else if(frontendState==FrontendState::Paused) {
        if(index==0) SetFrontendState(FrontendState::Playing);
        else if(index==1) SetFrontendState(FrontendState::MainMenu);
        else if(index==2&&mainWindow) DestroyWindow(mainWindow);
    }
}
LRESULT CALLBACK OverlayProcedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) { PaintMenu(hwnd); return 0; }
    if(msg==WM_NCHITTEST&&frontendState==FrontendState::Playing) return HTTRANSPARENT;
    if(msg==WM_SETCURSOR && LOWORD(lp)==HTCLIENT) {
        if(frontendState==FrontendState::Playing) return FALSE;
        SetCursor(LoadCursorW(nullptr,IDC_ARROW)); return TRUE;
    }
    if(msg==WM_LBUTTONUP) {
        if(frontendState==FrontendState::Playing) return 0;
        RECT c{}; GetClientRect(hwnd,&c); const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        const int count=frontendState==FrontendState::MainMenu?2:3;
        for(int i=0;i<count;++i) { RECT b=MenuButton(c.right,c.bottom,i);
            if(x<b.left||x>=b.right||y<b.top||y>=b.bottom) continue;
            selectedMenuButton=i;
            ActivateFrontendButton(i);
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
    if(msg==WM_KEYDOWN && frontendEnabled && frontendState!=FrontendState::Playing &&
        (wp==VK_UP||wp==VK_DOWN) && (lp&(1LL<<30))==0) {
        const int count=frontendState==FrontendState::MainMenu?2:3;
        selectedMenuButton=(selectedMenuButton+(wp==VK_DOWN?1:count-1))%count;
        if(frontendOverlay) InvalidateRect(frontendOverlay,nullptr,FALSE);
        return 0;
    }
    if(msg==WM_KEYDOWN && frontendEnabled && frontendState!=FrontendState::Playing &&
        wp==VK_RETURN && (lp&(1LL<<30))==0) {
        ActivateFrontendButton(selectedMenuButton);
        return 0;
    }
    if(msg==WM_KEYDOWN && wp==VK_ESCAPE && (lp&(1LL<<30))==0) {
        if(frontendEnabled && frontendState==FrontendState::Playing) SetFrontendState(FrontendState::Paused);
        else if(frontendEnabled && frontendState==FrontendState::Paused) SetFrontendState(FrontendState::Playing);
        else if(frontendState==FrontendState::MainMenu||!frontendEnabled) DestroyWindow(hwnd);
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

bool KeepCameraAboveTerrain(const talvisota::world::WorldRegion& region,
    vanta::math::Vec3& position, const float clearance, std::string& error) {
    float groundHeight = 0.0F;
    std::array<float, 3> groundNormal{};
    if (!talvisota::world::SampleTerrainHeight(region, position.x, position.z,
            groundHeight, groundNormal, error))
        return false;
    position.y = std::max(position.y, groundHeight + clearance);
    error.clear();
    return true;
}

bool ResolveContentRoot(const HINSTANCE instance,std::filesystem::path& output,std::string& error) {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    const auto working=std::filesystem::current_path(ec);
    if(!ec) candidates.push_back(working);
    std::array<wchar_t,32768> executable{};
    const DWORD length=GetModuleFileNameW(instance,executable.data(),static_cast<DWORD>(executable.size()));
    if(length>0U&&length<executable.size()) {
        const std::filesystem::path executableDirectory=std::filesystem::path(executable.data()).parent_path();
        candidates.push_back(executableDirectory);
        candidates.push_back(executableDirectory.parent_path());
        candidates.push_back(executableDirectory.parent_path().parent_path());
    }
    for(const auto& candidate:candidates) {
        ec.clear();
        if(std::filesystem::is_regular_file(candidate/L"content"/L"catalog.tmanifest",ec)&&!ec) {
            output=std::filesystem::weakly_canonical(candidate,ec);
            if(!ec) { error.clear(); return true; }
        }
    }
    error="Could not locate the Talvisota content root (content/catalog.tmanifest) beside the game or in the working directory.";
    return false;
}

bool LoadFirstPatrol(const std::filesystem::path& contentRoot,
    talvisota::content::AuthoredDefinition& output,std::string& error) {
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    if(!talvisota::content::LoadContentManifest(contentRoot/"content"/"catalog.tmanifest",
            contentRoot,registry,manifestDiagnostics)) {
        error="First Patrol content manifest validation failed.";
        for(const auto& item:manifestDiagnostics)
            error+="\n"+item.file.string()+":"+std::to_string(item.line)+" ["+item.field+"] "+item.message;
        return false;
    }
    std::vector<talvisota::content::AuthoredDefinitionDiagnostic> diagnostics;
    if(!talvisota::content::LoadAuthoredDefinition(contentRoot/"content"/"missions"/"first-patrol.tmission",
            registry,output,diagnostics)) {
        error="First Patrol authored definition failed validation.";
        for(const auto& item:diagnostics)
            error+="\n"+item.file.string()+":"+std::to_string(item.line)+" ["+item.field+"] "+item.message;
        return false;
    }
    error.clear();
    return true;
}

// Mission targets are approached from outside their authored collision volumes.
// This matters for structures such as the church: a waypoint at the structure's
// centre is physically unreachable even though the mission target is valid.
bool PlaceMissionInteractionAnchors(const talvisota::world::WorldRegion& region,
    talvisota::gameplay::SimulationState& simulation,const std::array<float,3>& playerSpawn,
    const float playerRadius,std::string& error) {
    std::array<float,3> approachFrom=playerSpawn;
    for(const auto& objective:simulation.mission.objectives) {
        auto* target=simulation.entities.Find(objective.interactionTargetId);
        if(target==nullptr) {
            error="Mission interaction anchor is missing its world-object entity: "+objective.interactionTargetId;
            return false;
        }
        const std::string collisionId=objective.interactionTargetId+".collision";
        const auto collision=std::find_if(region.collision.begin(),region.collision.end(),
            [&collisionId](const talvisota::world::CollisionBox& box){return box.id==collisionId;});
        if(collision==region.collision.end()) continue;
        std::array<float,3> interactionPoint=collision->center;
        const float dx=approachFrom[0]-collision->center[0];
        const float dz=approachFrom[2]-collision->center[2];
        if(std::abs(dx)>std::abs(dz))
            interactionPoint[0]+=std::copysign(collision->halfExtents[0]+playerRadius+0.25F,dx);
        else
            interactionPoint[2]+=std::copysign(collision->halfExtents[2]+playerRadius+0.25F,dz==0.0F?-1.0F:dz);
        std::array<float,3> groundNormal{};
        if(!talvisota::world::SampleTerrainHeight(region,interactionPoint[0],interactionPoint[2],
                interactionPoint[1],groundNormal,error)) {
            error="Could not resolve an accessible mission interaction point for "+
                objective.interactionTargetId+": "+error;
            return false;
        }
        if(!simulation.entities.SetPosition(objective.interactionTargetId,interactionPoint,error))
            return false;
        approachFrom=interactionPoint;
    }
    error.clear();
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

std::size_t CullCaptureInstances(SceneSnapshot& scene, const std::vector<float>& meshRadii,
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
    return visibleCount;
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
    // Keep the overview inside the 280 m vegetation visibility envelope.
    {L"01-sakkola-overview.png", {0.0F, 35.0F, 30.0F}, {0.0F, 20.0F, 190.0F}},
    // North approach, with both eye and target above the authored church-site terrain.
    {L"02-church-village.png", {0.0F, 22.0F, 225.0F}, {0.0F, 17.0F, 178.0F}},
    // Stand on the ice and look directly across the north-shore boundary at close range.
    {L"03-suvanto-shoreline.png", {0.0F, 7.0F, -380.0F}, {0.0F, 7.0F, -245.0F}},
    {L"04-karelian-forest.png", {82.0F, 11.0F, -205.0F}, {175.0F, 4.0F, -62.0F}},
    // Keep the +Z entrance in view, but lower the camera to show the facade below
    // the roofline instead of looking down onto the roof from 20 m.
    {L"05-farmstead.png", {-80.0F, 12.0F, 90.0F}, {-45.0F, 9.0F, 123.0F}}
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
    bool missionSmokeRequested = false;
    int argumentCount = 0;
    LPWSTR* parsedArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (parsedArguments == nullptr) {
        if (ownsCom) CoUninitialize();
        return 1;
    }
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring_view token(parsedArguments[index]);
        if (token == L"--mission-smoke") {
            missionSmokeRequested = true;
        } else if (token.rfind(L"--capture-dir=", 0) == 0) {
            captureRequested = true;
            captureDir = token.substr(14);
        } else if (token.rfind(L"--frames-per-view=", 0) == 0) {
            try { framesPerView = std::clamp(std::stoi(std::wstring(token.substr(18))), 2, 120); }
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
    if (missionSmokeRequested && captureRequested) {
        if (ownsCom) CoUninitialize();
        return 1;
    }
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

    std::filesystem::path contentRoot;
    std::string contentRootError;
    if(!ResolveContentRoot(instance,contentRoot,contentRootError)) {
        std::cerr<<contentRootError<<"\n";
        if(captureLog) captureLog<<contentRootError<<"\n";
        if(ownsCom) CoUninitialize();
        return 2;
    }

    SceneSnapshot scene;
    talvisota::world::WorldRegion gameRegion;
    std::size_t placements = 0, draws = 0;
    std::vector<float> instanceMaxDrawDistances;
    std::string error;
    if (!BuildSakkolaScene(contentRoot, scene,
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

    if(missionSmokeRequested) {
        talvisota::game::PlayerConfig playerConfig;
        talvisota::game::PlayerState playerState;
        if(!FindPlayerSpawn(gameRegion,playerConfig,playerState,error)) {
            std::cerr<<"First Patrol smoke could not find a player spawn: "<<error<<"\\n";
            if(ownsCom) CoUninitialize();
            return 10;
        }
        talvisota::content::AuthoredDefinition authoredMission;
        talvisota::gameplay::GameplayRules rules;
        talvisota::gameplay::SimulationState simulation;
        if(!LoadFirstPatrol(contentRoot,authoredMission,error) ||
            !talvisota::gameplay::InitializeSimulationFromAuthoredMission(
                authoredMission,gameRegion,playerState.position,rules,simulation,error) ||
            !PlaceMissionInteractionAnchors(gameRegion,simulation,playerState.position,
                playerConfig.capsuleRadius,error)) {
            std::cerr<<"First Patrol smoke initialization failed: "<<error<<"\\n";
            if(ownsCom) CoUninitialize();
            return 10;
        }
        for(std::size_t index=0U;index<simulation.mission.objectives.size();++index) {
            talvisota::gameplay::ActiveObjectiveTarget waypoint;
            if(!talvisota::gameplay::FindActiveObjectiveTarget(
                    simulation,simulation.playerEntityId,waypoint,error)) {
                std::cerr<<"First Patrol smoke waypoint failed: "<<error<<"\\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            talvisota::game::PlayerState reachablePlayer;
            if(!talvisota::game::InitializePlayer(gameRegion,playerConfig,
                    waypoint.position,reachablePlayer,error) ||
                !simulation.entities.SetPosition(simulation.playerEntityId,
                    reachablePlayer.position,error)) {
                std::cerr<<"First Patrol smoke found an unreachable objective anchor: "
                    <<waypoint.entityId<<": "<<error<<"\\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            talvisota::gameplay::InteractionTargetSelection selection;
            if(!talvisota::gameplay::FindNearestActiveObjectiveTarget(simulation,
                    simulation.playerEntityId,rules.interactionRadiusMeters,selection,error) ||
                selection.entityId!=waypoint.entityId) {
                std::cerr<<"First Patrol smoke could not select the reachable objective: "
                    <<waypoint.entityId<<"\\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            const std::array<talvisota::gameplay::InteractionCommand,1> commands{{
                {"smoke.interact."+std::to_string(index+1U),
                    simulation.playerEntityId,selection.entityId}
            }};
            std::vector<talvisota::gameplay::CommandResult> results;
            if(!talvisota::gameplay::AdvanceSimulation(simulation,rules,1.0/60.0,
                    commands,results,error)) {
                std::cerr<<"First Patrol smoke interaction failed: "<<error<<"\\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            const auto expected=index+1U==simulation.mission.objectives.size()
                ?talvisota::gameplay::CommandOutcome::MissionCompleted
                :talvisota::gameplay::CommandOutcome::ObjectiveCompleted;
            if(results.size()!=1U||results.front().outcome!=expected) {
                std::cerr<<"First Patrol smoke produced an unexpected objective result.\\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            std::cout<<"First Patrol smoke completed objective "<<(index+1U)
                <<"/"<<simulation.mission.objectives.size()<<": "<<waypoint.entityId<<"\\n";
        }
        if(simulation.mission.status!=talvisota::missions::MissionStatus::Completed) {
            std::cerr<<"First Patrol smoke did not complete the mission.\\n";
            if(ownsCom) CoUninitialize();
            return 10;
        }
        std::cout<<"First Patrol runtime smoke passed: all authored targets are reachable and all objectives complete.\\n";
        if(ownsCom) CoUninitialize();
        return 0;
    }

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
        std::cerr << "Raw mouse registration failed; use arrow keys for camera look.\n";
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
            auto eye = view.eye;
            auto target = view.target;
            if (!KeepCameraAboveTerrain(gameRegion, eye, 1.7F, error) ||
                !KeepCameraAboveTerrain(gameRegion, target, 0.35F, error)) {
                error = "Could not place Sakkola capture camera above the terrain: " + error;
                break;
            }
            if (!SetCamera(scene, eye, target, client.right - client.left, client.bottom - client.top)) {
                error = "Could not construct a valid Sakkola capture camera.";
                break;
            }
            const std::size_t visibleInstanceCount = CullCaptureInstances(scene,
                meshCullRadii, instanceMaxDrawDistances, eye, target,
                static_cast<float>(client.right-client.left)/static_cast<float>(client.bottom-client.top));
            std::vector<double> warmFrameTimesMs;
            std::vector<double> warmRenderTimesMs;
            warmFrameTimesMs.reserve(static_cast<std::size_t>(framesPerView - 1));
            warmRenderTimesMs.reserve(static_cast<std::size_t>(framesPerView - 1));
            double firstFrameMs = 0.0;
            double firstRenderMs = 0.0;
            for (int frame = 0; frame < framesPerView; ++frame) {
                const auto frameStart = std::chrono::steady_clock::now();
                scene.camera.elapsedSeconds = static_cast<float>(frameNumber) / 60.0F;
                scene.camera.frameNumber = frameNumber++;
                const auto renderStart = std::chrono::steady_clock::now();
                if (!renderer->Render(scene, error)) break;
                const auto renderEnd = std::chrono::steady_clock::now();
                const double renderMs = std::chrono::duration<double, std::milli>(
                    renderEnd - renderStart).count();
                if (!captureRequested) Sleep(16);
                const double frameMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - frameStart).count();
                if (frame == 0) {
                    firstFrameMs = frameMs;
                    firstRenderMs = renderMs;
                } else {
                    warmFrameTimesMs.push_back(frameMs);
                    warmRenderTimesMs.push_back(renderMs);
                }
            }
            if (!error.empty()) break;
            const std::size_t expectedWarmFrames = static_cast<std::size_t>(framesPerView - 1);
            if (warmFrameTimesMs.size() != expectedWarmFrames ||
                warmRenderTimesMs.size() != expectedWarmFrames) {
                error = "Capture warm timing sample count does not match the requested frame count.";
                break;
            }
            const auto percentile95 = [](std::vector<double> samples) {
                std::sort(samples.begin(), samples.end());
                const auto index = static_cast<std::size_t>(
                    std::ceil(static_cast<double>(samples.size()) * 0.95)) - 1U;
                return samples[index];
            };
            double totalWarmFrameMs = 0.0;
            double totalWarmRenderMs = 0.0;
            for (const double sample : warmFrameTimesMs) totalWarmFrameMs += sample;
            for (const double sample : warmRenderTimesMs) totalWarmRenderMs += sample;
            const double warmAverageFrameMs = totalWarmFrameMs / static_cast<double>(warmFrameTimesMs.size());
            const double warmAverageRenderMs = totalWarmRenderMs / static_cast<double>(warmRenderTimesMs.size());
            const double warmP95FrameMs = percentile95(warmFrameTimesMs);
            const double warmP95RenderMs = percentile95(warmRenderTimesMs);
            const std::string timingSummary = "Performance sample " +
                std::filesystem::path(view.file).string() + ": frames=" +
                std::to_string(framesPerView) + ", visibleInstances=" +
                std::to_string(visibleInstanceCount) + "/" + std::to_string(scene.instances.size()) +
                ", firstFrameMs=" + std::to_string(firstFrameMs) +
                ", firstRenderMs=" + std::to_string(firstRenderMs) +
                ", warmFrames=" + std::to_string(warmFrameTimesMs.size()) +
                ", warmAvgFrameMs=" + std::to_string(warmAverageFrameMs) +
                ", warmP95FrameMs=" + std::to_string(warmP95FrameMs) +
                ", warmAvgRenderMs=" + std::to_string(warmAverageRenderMs) +
                ", warmP95RenderMs=" + std::to_string(warmP95RenderMs);
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
            if (!WriteBgraPng(framebuffer, framebufferWidth, framebufferHeight,
                    output / view.file, error))
                break;
            PresentedSkyRegionStats skyStats;
            if (!ValidatePresentedSkyRegion(framebuffer, framebufferWidth, framebufferHeight,
                    skyStats, error))
                break;
            const std::string skySummary = "Presented sky validation: " +
                std::to_string(skyStats.blueDominantSampleCount) + "/" +
                std::to_string(skyStats.sampledPixels) +
                " top-quarter pixels; mean BGRA=(" +
                std::to_string(static_cast<unsigned int>(skyStats.meanBlue)) + "," +
                std::to_string(static_cast<unsigned int>(skyStats.meanGreen)) + "," +
                std::to_string(static_cast<unsigned int>(skyStats.meanRed)) + ").";
            std::cout << skySummary << "\n";
            if (captureLog) captureLog << skySummary << "\n";
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
            std::cout << "Viewpoint " << std::filesystem::path(view.file).string() << ": eye=(" << eye.x << ","
                << eye.y << "," << eye.z << ") target=(" << target.x << ","
                << target.y << "," << target.z << "), frame=" << frameNumber
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
    talvisota::content::AuthoredDefinition authoredMission;
    if(!LoadFirstPatrol(contentRoot,authoredMission,error)) {
        std::cerr<<error<<"\n";
        renderer.reset(); DestroyWindow(window); if(ownsCom) CoUninitialize(); return 8;
    }
    talvisota::gameplay::GameplayRules gameplayRules;
    talvisota::gameplay::SimulationState gameplayState;
    if(!talvisota::gameplay::InitializeSimulationFromAuthoredMission(
            authoredMission,gameRegion,playerState.position,gameplayRules,gameplayState,error) ||
        !PlaceMissionInteractionAnchors(gameRegion,gameplayState,playerState.position,
            playerConfig.capsuleRadius,error)) {
        std::cerr<<"First Patrol initialization failed: "<<error<<"\n";
        renderer.reset(); DestroyWindow(window); if(ownsCom) CoUninitialize(); return 8;
    }
    missionOverlay.title=Utf8ToWide(authoredMission.name);
    missionOverlay.description=Utf8ToWide(authoredMission.description);
    if(!RefreshMissionOverlay(gameplayState,gameplayRules.interactionRadiusMeters,error)) {
        std::cerr<<"First Patrol HUD initialization failed: "<<error<<"\n";
        renderer.reset(); DestroyWindow(window); if(ownsCom) CoUninitialize(); return 8;
    }

    auto playerCamera = talvisota::game::BuildPlayerCamera(playerState, playerConfig);
    vanta::math::Vec3 cameraPosition{playerCamera.position[0], playerCamera.position[1], playerCamera.position[2]};
    if (!KeepCameraAboveTerrain(gameRegion, cameraPosition, playerConfig.eyeHeight, error)) {
        renderer.reset(); DestroyWindow(window);
        if (ownsCom) CoUninitialize();
        return 8;
    }
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
    bool running=true, jumpWasDown=false, interactWasDown=false, interactionQueued=false;
    bool restartCameraRequested=false;
    constexpr double gameplayFixedStep=1.0/60.0;
    double gameplayAccumulator=0.0;
    FrontendState previousFrontendState=frontendState;
    std::cout << "Sakkola: Start Game; WASD, mouse look, Space jump, Shift sprint, Ctrl crouch, ESC pause.\n";
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;
        if(restartPatrolRequested) {
            if(!talvisota::missions::RestartMission(gameplayState.mission,error) ||
                !gameplayState.entities.SetPosition(gameplayState.playerEntityId,initialPlayerState.position,error)) {
                std::cerr<<"Could not restart First Patrol: "<<error<<"\n";
                break;
            }
            playerState=initialPlayerState;
            restartCameraRequested=true;
            gameplayAccumulator=0.0;
            interactionQueued=false;
            restartPatrolRequested=false;
            SetMissionFeedback(L"PATROL RESTARTED",std::chrono::milliseconds(2200));
            if(!RefreshMissionOverlay(gameplayState,gameplayRules.interactionRadiusMeters,error)) break;
        }
        const auto now=Clock::now();
        bool cameraChanged=restartCameraRequested;
        restartCameraRequested=false;
        if(frontendState!=previousFrontendState) {
            rawMouseDeltaX=0; rawMouseDeltaY=0; jumpWasDown=false;
            interactWasDown=false; interactionQueued=false; gameplayAccumulator=0.0;
            previousFrame=now; previousFrontendState=frontendState;
            InvalidateRect(frontendOverlay,nullptr,FALSE);
        }
        const double frameDeltaMs =
            std::chrono::duration<double, std::milli>(now - previousFrame).count();
        const float deltaSeconds = std::clamp(
            static_cast<float>(frameDeltaMs * 0.001), 0.0F, 0.1F);
        previousFrame = now;
        if(frontendState==FrontendState::Playing &&
            gameplayState.mission.status!=talvisota::missions::MissionStatus::Completed) {
            talvisota::game::PlayerInput playerInput;
            playerInput.forward=((GetAsyncKeyState('W')&0x8000)!=0?1.0F:0.0F)-
                ((GetAsyncKeyState('S')&0x8000)!=0?1.0F:0.0F);
            playerInput.right=((GetAsyncKeyState('D')&0x8000)!=0?1.0F:0.0F)-
                ((GetAsyncKeyState('A')&0x8000)!=0?1.0F:0.0F);
            playerInput.sprint=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
            playerInput.crouch=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
            const bool jumpDown=(GetAsyncKeyState(VK_SPACE)&0x8000)!=0;
            playerInput.jumpPressed=jumpDown&&!jumpWasDown; jumpWasDown=jumpDown;
            const bool interactDown=(GetAsyncKeyState('E')&0x8000)!=0;
            if(interactDown&&!interactWasDown) interactionQueued=true;
            interactWasDown=interactDown;
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
                std::cerr<<"Player simulation failed: "<<error<<"\n"; break;
            }
            if(!gameplayState.entities.SetPosition(gameplayState.playerEntityId,playerState.position,error)) {
                std::cerr<<"Player entity synchronization failed: "<<error<<"\n"; break;
            }
            cameraChanged=playerState.position!=previousPosition ||
                playerInput.lookYawRadians!=0.0F || playerInput.lookPitchRadians!=0.0F ||
                playerState.crouched!=wasCrouched;
            gameplayAccumulator+=static_cast<double>(deltaSeconds);
            while(gameplayAccumulator>=gameplayFixedStep) {
                std::vector<talvisota::gameplay::InteractionCommand> commands;
                if(interactionQueued) {
                    talvisota::gameplay::InteractionTargetSelection selection;
                    if(!talvisota::gameplay::FindNearestActiveObjectiveTarget(gameplayState,
                            gameplayState.playerEntityId,gameplayRules.interactionRadiusMeters,selection,error)) {
                        std::cerr<<"Objective interaction query failed: "<<error<<"\n"; break;
                    }
                    if(selection.entityId.empty()) {
                        SetMissionFeedback(L"MOVE CLOSER TO THE OBJECTIVE",std::chrono::milliseconds(1400));
                    } else {
                        commands.push_back({"interact."+std::to_string(gameplayState.tickIndex+1U),
                            gameplayState.playerEntityId,selection.entityId});
                    }
                    interactionQueued=false;
                }
                std::vector<talvisota::gameplay::CommandResult> commandResults;
                if(!talvisota::gameplay::AdvanceSimulation(gameplayState,gameplayRules,
                        gameplayFixedStep,commands,commandResults,error)) {
                    std::cerr<<"Gameplay simulation failed: "<<error<<"\n"; break;
                }
                for(const auto& result:commandResults) {
                    if(result.outcome==talvisota::gameplay::CommandOutcome::ObjectiveCompleted)
                        SetMissionFeedback(L"OBJECTIVE COMPLETE",std::chrono::milliseconds(2200));
                    else if(result.outcome==talvisota::gameplay::CommandOutcome::MissionCompleted)
                        SetMissionFeedback(L"FIRST PATROL COMPLETE",std::chrono::milliseconds(6000));
                    else if(result.outcome==talvisota::gameplay::CommandOutcome::OutOfRange)
                        SetMissionFeedback(L"MOVE CLOSER TO THE OBJECTIVE",std::chrono::milliseconds(1400));
                }
                gameplayAccumulator-=gameplayFixedStep;
            }
            if(!error.empty()) break;
            if(!RefreshMissionOverlay(gameplayState,gameplayRules.interactionRadiusMeters,error)) {
                std::cerr<<"Mission HUD update failed: "<<error<<"\n"; break;
            }
        } else {
            rawMouseDeltaX=0; rawMouseDeltaY=0; jumpWasDown=false;
            interactWasDown=false; interactionQueued=false;
            if(frontendState==FrontendState::Playing) {
                if(!RefreshMissionOverlay(gameplayState,gameplayRules.interactionRadiusMeters,error)) break;
            }
        }
        if (cameraChanged) {
            playerCamera = talvisota::game::BuildPlayerCamera(playerState, playerConfig);
            cameraPosition = {playerCamera.position[0], playerCamera.position[1], playerCamera.position[2]};
            if (!KeepCameraAboveTerrain(gameRegion, cameraPosition,
                    playerState.crouched ? playerConfig.crouchedEyeHeight : playerConfig.eyeHeight, error)) {
                std::cerr << "Player camera terrain sampling failed: " << error << "\n";
                break;
            }
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
