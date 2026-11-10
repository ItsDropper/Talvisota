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
#include "talvisota/presentation/WeatherParticles.hpp"
#include "talvisota/presentation/ExplosionLighting.hpp"
#include "talvisota/presentation/DynamicWorld.hpp"
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
bool advanceCampaignRequested=false;
bool campaignHasNextMission=false;
float currentCameraFovDegrees=60.0F;
int selectedMenuButton=0;
enum class FrontendState { MainMenu, Playing, Paused };
FrontendState frontendState=FrontendState::MainMenu;

struct MissionOverlayData final {
    std::wstring title=L"FIRST PATROL";
    std::wstring description=L"Check the farmstead well and woodpile before reporting back to the Sakkola church.";
    std::wstring objective=L"INSPECT THE FARMSTEAD WELL";
    std::wstring distance;
    std::wstring progress=L"0 / 3 OBJECTIVES";
    std::wstring ammo;
    std::wstring contact;
    std::wstring prompt=L"FOLLOW THE OBJECTIVE";
    std::wstring feedback;
    bool completed=false;
    bool failed=false;
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
    if(id=="objective.check-in-corporal") return L"CHECK IN WITH THE FARMSTEAD CORPORAL";
    if(id=="objective.inspect-well") return L"INSPECT THE FARMSTEAD WELL";
    if(id=="objective.inspect-woodpile") return L"CHECK THE WESTERN WOODPILE";
    if(id=="objective.report-church") return L"REPORT BACK TO THE CHURCH";
    if(id=="objective.reach-northern-barn") return L"REACH THE NORTHERN BARN";
    if(id=="objective.check-eastern-boundary") return L"CHECK THE EASTERN BOUNDARY";
    if(id=="objective.report-watch") return L"REPORT TO THE WATCH POST";
    if(id=="objective.stop-first-advance") return L"HOLD THE NORTHERN APPROACH";
    if(id=="objective.stop-second-advance") return L"SECURE THE VILLAGE ROAD";
    if(id=="objective.check-section-leader") return L"CHECK IN WITH THE SECTION LEADER";
    if(id=="objective.report-to-post") return L"REPORT TO THE CHURCH POST";
    if(id=="objective.check-damaged-fence") return L"CHECK THE DAMAGED FENCE";
    if(id=="objective.check-northern-barn") return L"CHECK THE NORTHERN BARN";
    if(id=="objective.report-aftermath") return L"REPORT THE ROAD CONDITION";
    if(id=="objective.stop-third-advance") return L"SECURE THE EASTERN APPROACH";
    if(id=="objective.stop-fourth-advance") return L"CHECK THE ROAD BEYOND THE BARN";
    return Utf8ToWide(id);
}


struct BattlefieldRenderAssets final {
    std::uint32_t bodyMesh=0U, armsMesh=0U, legMesh=0U, gearMesh=0U, headMesh=0U;
    std::uint32_t rifleMesh=0U, submachineGunMesh=0U, lightMachineGunMesh=0U;
    std::uint32_t finnishMaterial=0U, sovietMaterial=0U;
    std::uint32_t finnishGearMaterial=0U, sovietGearMaterial=0U, skinMaterial=0U, weaponMaterial=0U;
};
struct BattlefieldActorRenderBinding final {
    std::string entityId;
    std::size_t bodyInstance=0U, armsInstance=0U, gearInstance=0U, headInstance=0U;
    std::size_t leftLegInstance=0U, rightLegInstance=0U, weaponInstance=0U;
    std::array<float,3> previousPosition{};
    float stridePhase=0.0F;
    float lastUpdateSeconds=0.0F;
    float fallStartSeconds=-1.0F;
    bool hasPreviousPosition=false;
};
void AppendBox(talvisota::presentation::RenderMesh& mesh,
    const std::array<float,3>& center,const std::array<float,3>& half) {
    const std::array<std::array<float,3>,8> corners{{
        {{center[0]-half[0],center[1]-half[1],center[2]-half[2]}},
        {{center[0]+half[0],center[1]-half[1],center[2]-half[2]}},
        {{center[0]+half[0],center[1]+half[1],center[2]-half[2]}},
        {{center[0]-half[0],center[1]+half[1],center[2]-half[2]}},
        {{center[0]-half[0],center[1]-half[1],center[2]+half[2]}},
        {{center[0]+half[0],center[1]-half[1],center[2]+half[2]}},
        {{center[0]+half[0],center[1]+half[1],center[2]+half[2]}},
        {{center[0]-half[0],center[1]+half[1],center[2]+half[2]}}
    }};
    constexpr std::array<std::array<std::uint32_t,4>,6> faces{{
        {{0U,3U,2U,1U}},{{4U,5U,6U,7U}},{{0U,4U,7U,3U}},
        {{1U,2U,6U,5U}},{{0U,1U,5U,4U}},{{3U,7U,6U,2U}}
    }};
    constexpr std::array<std::array<float,3>,6> normals{{
        {{0.0F,0.0F,-1.0F}},{{0.0F,0.0F,1.0F}},{{-1.0F,0.0F,0.0F}},
        {{1.0F,0.0F,0.0F}},{{0.0F,-1.0F,0.0F}},{{0.0F,1.0F,0.0F}}
    }};
    constexpr std::array<std::array<float,2>,4> uv{{
        {{0.0F,1.0F}},{{1.0F,1.0F}},{{1.0F,0.0F}},{{0.0F,0.0F}}
    }};
    for(std::size_t face=0U;face<faces.size();++face) {
        const auto base=static_cast<std::uint32_t>(mesh.vertices.size());
        for(std::size_t corner=0U;corner<4U;++corner) {
            talvisota::presentation::RenderVertex vertex;
            vertex.position=corners[faces[face][corner]];
            vertex.normal=normals[face]; vertex.u=uv[corner][0]; vertex.v=uv[corner][1];
            mesh.vertices.push_back(vertex);
        }
        mesh.indices.insert(mesh.indices.end(),{base,base+1U,base+2U,base,base+2U,base+3U});
    }
}
void AppendEllipsoid(talvisota::presentation::RenderMesh& mesh,
    const std::array<float,3>& center,const std::array<float,3>& radii,
    const float rotationX=0.0F) {
    // Smoother shared geometry avoids faceted silhouettes without multiplying draw calls.
    constexpr std::uint32_t rings=12U, segments=24U;
    const float cosine=std::cos(rotationX),sine=std::sin(rotationX);
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for(std::uint32_t ring=0U;ring<=rings;++ring) {
        const float v=static_cast<float>(ring)/static_cast<float>(rings);
        const float theta=v*3.14159265358979323846F;
        for(std::uint32_t segment=0U;segment<=segments;++segment) {
            const float u=static_cast<float>(segment)/static_cast<float>(segments);
            const float phi=u*6.28318530717958647692F;
            const float sx=std::sin(theta)*std::cos(phi);
            const float sy=std::cos(theta);
            const float sz=std::sin(theta)*std::sin(phi);
            const float nx=sx/radii[0],ny=sy/radii[1],nz=sz/radii[2];
            const float normalLength=std::sqrt(nx*nx+ny*ny+nz*nz);
            talvisota::presentation::RenderVertex vertex;
            const float localY=sy*radii[1],localZ=sz*radii[2];
            const float normalY=ny/normalLength,normalZ=nz/normalLength;
            vertex.position={center[0]+sx*radii[0],
                center[1]+localY*cosine-localZ*sine,
                center[2]+localY*sine+localZ*cosine};
            vertex.normal={nx/normalLength,
                normalY*cosine-normalZ*sine,normalY*sine+normalZ*cosine};
            vertex.u=u; vertex.v=v;
            mesh.vertices.push_back(vertex);
        }
    }
    for(std::uint32_t ring=0U;ring<rings;++ring) {
        for(std::uint32_t segment=0U;segment<segments;++segment) {
            const std::uint32_t a=first+ring*(segments+1U)+segment;
            const std::uint32_t b=a+segments+1U;
            mesh.indices.insert(mesh.indices.end(),{a,a+1U,b,a+1U,b+1U,b});
        }
    }
}
void AppendCylinderAlongZ(talvisota::presentation::RenderMesh& mesh,
    const float centerX,const float centerY,const float startZ,const float endZ,
    const float radius,const std::uint32_t segments=16U) {
    if(segments<6U||!(endZ>startZ)||!(radius>0.0F)) return;
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr float tau=6.28318530717958647692F;
    for(std::uint32_t ring=0U;ring<2U;++ring) {
        const float z=ring==0U?startZ:endZ;
        for(std::uint32_t segment=0U;segment<=segments;++segment) {
            const float angle=tau*static_cast<float>(segment)/static_cast<float>(segments);
            const float x=std::cos(angle),y=std::sin(angle);
            talvisota::presentation::RenderVertex vertex;
            vertex.position={centerX+x*radius,centerY+y*radius,z};
            vertex.normal={x,y,0.0F};
            vertex.u=static_cast<float>(segment)/static_cast<float>(segments);
            vertex.v=static_cast<float>(ring);
            mesh.vertices.push_back(vertex);
        }
    }
    for(std::uint32_t segment=0U;segment<segments;++segment) {
        const auto a=first+segment,b=a+segments+1U;
        mesh.indices.insert(mesh.indices.end(),{a,a+1U,b,a+1U,b+1U,b});
    }
    for(std::uint32_t cap=0U;cap<2U;++cap) {
        const float z=cap==0U?startZ:endZ;
        const auto center=static_cast<std::uint32_t>(mesh.vertices.size());
        talvisota::presentation::RenderVertex vertex;
        vertex.position={centerX,centerY,z};
        vertex.normal={0.0F,0.0F,cap==0U?-1.0F:1.0F};
        mesh.vertices.push_back(vertex);
        const auto ring=first+cap*(segments+1U);
        for(std::uint32_t segment=0U;segment<segments;++segment) {
            const auto a=ring+segment,b=a+1U;
            if(cap==0U) mesh.indices.insert(mesh.indices.end(),{center,b,a});
            else mesh.indices.insert(mesh.indices.end(),{center,a,b});
        }
    }
}
talvisota::presentation::RenderMesh BuildWeaponMesh(
    const talvisota::combat::WeaponArchetype archetype) {
    talvisota::presentation::RenderMesh mesh;
    if(archetype==talvisota::combat::WeaponArchetype::BoltActionRifle) {
        AppendBox(mesh,{0.0F,0.0F,-0.04F},{0.035F,0.042F,0.22F});
        AppendBox(mesh,{0.0F,0.012F,0.22F},{0.043F,0.045F,0.14F});
        AppendBox(mesh,{0.0F,0.018F,0.54F},{0.014F,0.014F,0.24F});
        AppendBox(mesh,{0.0F,0.062F,0.34F},{0.014F,0.016F,0.025F});
        AppendCylinderAlongZ(mesh,0.0F,0.018F,0.43F,0.91F,0.012F);
        AppendCylinderAlongZ(mesh,0.0F,0.018F,0.89F,0.925F,0.016F);
        AppendBox(mesh,{0.0F,0.047F,0.46F},{0.012F,0.024F,0.018F});
        AppendBox(mesh,{0.0F,0.065F,0.12F},{0.012F,0.014F,0.045F});
        AppendBox(mesh,{0.047F,0.014F,0.23F},{0.012F,0.022F,0.075F});
    } else if(archetype==talvisota::combat::WeaponArchetype::SubmachineGun) {
        AppendBox(mesh,{0.0F,0.0F,-0.025F},{0.04F,0.045F,0.16F});
        AppendBox(mesh,{0.0F,0.012F,0.20F},{0.052F,0.055F,0.15F});
        AppendBox(mesh,{0.0F,0.018F,0.48F},{0.016F,0.016F,0.14F});
        AppendBox(mesh,{0.0F,-0.085F,0.23F},{0.067F,0.078F,0.07F});
        AppendCylinderAlongZ(mesh,0.0F,0.018F,0.37F,0.69F,0.014F);
        AppendCylinderAlongZ(mesh,0.0F,0.018F,0.67F,0.705F,0.018F);
        AppendBox(mesh,{0.0F,0.065F,0.30F},{0.012F,0.016F,0.025F});
        AppendBox(mesh,{0.0F,0.057F,0.48F},{0.012F,0.014F,0.02F});
    } else {
        AppendBox(mesh,{0.0F,0.0F,-0.04F},{0.045F,0.05F,0.18F});
        AppendBox(mesh,{0.0F,0.018F,0.22F},{0.06F,0.07F,0.17F});
        AppendBox(mesh,{0.0F,0.024F,0.57F},{0.018F,0.018F,0.26F});
        AppendBox(mesh,{0.0F,0.115F,0.23F},{0.06F,0.09F,0.075F});
        AppendBox(mesh,{-0.035F,-0.095F,0.42F},{0.012F,0.11F,0.018F});
        AppendBox(mesh,{0.035F,-0.095F,0.42F},{0.012F,0.11F,0.018F});
        AppendCylinderAlongZ(mesh,0.0F,0.024F,0.48F,0.91F,0.014F);
        AppendCylinderAlongZ(mesh,0.0F,0.024F,0.89F,0.925F,0.018F);
        AppendBox(mesh,{0.0F,0.11F,0.48F},{0.018F,0.025F,0.035F});
        AppendBox(mesh,{0.0F,0.105F,0.15F},{0.045F,0.012F,0.055F});
    }
    return mesh;
}
bool BuildBattlefieldRenderAssets(talvisota::presentation::SceneSnapshot& scene,
    BattlefieldRenderAssets& output,std::string& error) {
    if(scene.meshes.size()>static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()-8U) ||
        scene.materials.size()>static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()-6U)) {
        error="Battlefield render assets exceed the scene index capacity."; return false;
    }
    talvisota::presentation::RenderMesh body;
    // Build a shared winter infantry rig from separately materialed torso, head,
    // field equipment, articulated arms and articulated legs.
    AppendEllipsoid(body,{0.0F,1.08F,0.0F},{0.245F,0.39F,0.155F});
    // Arms and legs are separate articulated meshes, not baked into the torso.
    AppendEllipsoid(body,{0.0F,1.465F,0.015F},{0.075F,0.075F,0.075F});
    AppendEllipsoid(body,{0.0F,1.68F,0.0F},{0.165F,0.075F,0.16F});
    AppendEllipsoid(body,{0.0F,1.65F,0.0F},{0.176F,0.025F,0.17F});
    AppendBox(body,{0.0F,1.635F,0.0F},{0.18F,0.012F,0.175F});
    output.bodyMesh=static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(body));

    talvisota::presentation::RenderMesh head;
    AppendEllipsoid(head,{0.0F,1.535F,0.015F},{0.105F,0.135F,0.105F});
    AppendEllipsoid(head,{0.0F,1.525F,0.112F},{0.025F,0.035F,0.025F});
    AppendEllipsoid(head,{-0.102F,1.55F,0.015F},{0.018F,0.045F,0.04F});
    AppendEllipsoid(head,{0.102F,1.55F,0.015F},{0.018F,0.045F,0.04F});
    output.headMesh=static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(head));

    talvisota::presentation::RenderMesh gear;
    AppendEllipsoid(gear,{0.0F,1.115F,-0.205F},{0.18F,0.235F,0.09F});
    AppendBox(gear,{0.0F,0.83F,0.0F},{0.245F,0.045F,0.16F});
    AppendBox(gear,{0.0F,0.85F,0.164F},{0.055F,0.038F,0.012F});
    AppendBox(gear,{-0.075F,1.18F,0.153F},{0.025F,0.24F,0.012F});
    AppendBox(gear,{0.075F,1.18F,0.153F},{0.025F,0.24F,0.012F});
    AppendBox(gear,{-0.17F,0.91F,0.175F},{0.065F,0.075F,0.055F});
    AppendBox(gear,{0.17F,0.91F,0.175F},{0.065F,0.075F,0.055F});
    AppendBox(gear,{-0.235F,0.86F,0.10F},{0.065F,0.075F,0.045F});
    AppendBox(gear,{0.235F,0.86F,0.10F},{0.065F,0.075F,0.045F});
    output.gearMesh=static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(gear));

    talvisota::presentation::RenderMesh arms;
    AppendEllipsoid(arms,{-0.245F,1.13F,0.075F},{0.085F,0.225F,0.09F},0.82F);
    AppendEllipsoid(arms,{0.245F,1.13F,0.075F},{0.085F,0.225F,0.09F},0.82F);
    AppendEllipsoid(arms,{-0.205F,0.94F,0.30F},{0.065F,0.075F,0.065F});
    AppendEllipsoid(arms,{0.205F,0.94F,0.30F},{0.065F,0.075F,0.065F});
    AppendEllipsoid(arms,{-0.245F,1.00F,0.235F},{0.087F,0.045F,0.055F});
    AppendEllipsoid(arms,{0.245F,1.00F,0.235F},{0.087F,0.045F,0.055F});
    output.armsMesh=static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(arms));

    talvisota::presentation::RenderMesh leg;
    // Each leg is authored around its hip pivot so stride animation rotates the
    // whole boot, knee and trouser silhouette coherently.
    AppendEllipsoid(leg,{0.0F,-0.20F,0.0F},{0.095F,0.235F,0.105F});
    AppendEllipsoid(leg,{0.0F,-0.48F,0.015F},{0.078F,0.215F,0.09F});
    AppendEllipsoid(leg,{0.0F,-0.70F,0.065F},{0.13F,0.085F,0.19F});
    AppendBox(leg,{0.0F,-0.52F,0.095F},{0.082F,0.07F,0.018F});
    AppendBox(leg,{0.0F,-0.665F,0.215F},{0.075F,0.025F,0.018F});
    AppendEllipsoid(leg,{0.0F,-0.29F,0.095F},{0.09F,0.075F,0.018F});
    AppendBox(leg,{0.0F,-0.055F,0.075F},{0.09F,0.035F,0.015F});
    output.legMesh=static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(leg));
    const std::array archetypes{
        talvisota::combat::WeaponArchetype::BoltActionRifle,
        talvisota::combat::WeaponArchetype::SubmachineGun,
        talvisota::combat::WeaponArchetype::LightMachineGun};
    std::array<std::uint32_t,3> weaponMeshes{};
    for(std::size_t i=0U;i<archetypes.size();++i) {
        weaponMeshes[i]=static_cast<std::uint32_t>(scene.meshes.size());
        scene.meshes.push_back(BuildWeaponMesh(archetypes[i]));
    }
    output.rifleMesh=weaponMeshes[0];
    output.submachineGunMesh=weaponMeshes[1];
    output.lightMachineGunMesh=weaponMeshes[2];
    talvisota::presentation::RenderMaterial finnish;
    finnish.baseColor={0.72F,0.76F,0.77F}; finnish.roughness=0.96F;
    output.finnishMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back(finnish);
    talvisota::presentation::RenderMaterial soviet;
    soviet.baseColor={0.39F,0.38F,0.29F}; soviet.roughness=0.94F;
    output.sovietMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back(soviet);
    talvisota::presentation::RenderMaterial finnishGear;
    finnishGear.baseColor={0.31F,0.36F,0.31F}; finnishGear.roughness=0.96F;
    output.finnishGearMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back(finnishGear);
    talvisota::presentation::RenderMaterial sovietGear;
    sovietGear.baseColor={0.29F,0.31F,0.23F}; sovietGear.roughness=0.96F;
    output.sovietGearMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back(sovietGear);
    talvisota::presentation::RenderMaterial skin;
    skin.baseColor={0.48F,0.34F,0.26F}; skin.roughness=0.92F;
    output.skinMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back(skin);
    talvisota::presentation::RenderMaterial weapon;
    weapon.baseColor={0.13F,0.145F,0.13F}; weapon.roughness=0.72F; weapon.metallic=0.12F;
    output.weaponMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back(weapon);
    error.clear(); return true;
}
std::uint32_t ResolveWeaponMesh(const BattlefieldRenderAssets& assets,std::string_view weaponId) {
    const auto* weapon=talvisota::combat::FindWeaponDefinition(weaponId);
    if(weapon==nullptr) return assets.rifleMesh;
    switch(weapon->archetype) {
    case talvisota::combat::WeaponArchetype::BoltActionRifle: return assets.rifleMesh;
    case talvisota::combat::WeaponArchetype::SubmachineGun: return assets.submachineGunMesh;
    case talvisota::combat::WeaponArchetype::LightMachineGun: return assets.lightMachineGunMesh;
    }
    return assets.rifleMesh;
}
std::array<float,16> ActorWorldMatrix(const std::array<float,3>& position,float yaw,
    float bob,float pitch,float roll) {
    const float cy=std::cos(yaw),sy=std::sin(yaw);
    const float cp=std::cos(pitch),sp=std::sin(pitch);
    const float cr=std::cos(roll),sr=std::sin(roll);
    return {
        cy*cr+sy*sp*sr,cp*sr,-sy*cr+cy*sp*sr,0.0F,
        -cy*sr+sy*sp*cr,cp*cr,sy*sr+cy*sp*cr,0.0F,
        sy*cp,-sp,cy*cp,0.0F,
        position[0],position[1]+bob,position[2],1.0F
    };
}
std::array<float,16> ActorLegWorldMatrix(const std::array<float,3>& position,
    float yaw,float pivotX,float swing,float bodyPitch,float bodyRoll) {
    const float angle=swing+bodyPitch;
    const float cy=std::cos(yaw),sy=std::sin(yaw);
    const float cp=std::cos(angle),sp=std::sin(angle);
    const float bodyCp=std::cos(bodyPitch),bodySp=std::sin(bodyPitch);
    const float cr=std::cos(bodyRoll),sr=std::sin(bodyRoll);
    // Rotate the hip pivot by the torso pose, but add stride only to the leg's
    // orientation. This keeps the leg attached while it swings.
    const float pivotLocalX=cr*pivotX-sr*0.83F;
    const float pivotLocalY=sr*pivotX+cr*0.83F;
    const float pivotY=bodyCp*pivotLocalY;
    const float pivotZ=bodySp*pivotLocalY;
    return {
        cy*cr+sy*sp*sr,cp*sr,-sy*cr+cy*sp*sr,0.0F,
        -cy*sr+sy*sp*cr,cp*cr,sy*sr+cy*sp*cr,0.0F,
        sy*cp,-sp,cy*cp,0.0F,
        position[0]+cy*pivotLocalX+sy*pivotZ,
        position[1]+pivotY,
        position[2]-sy*pivotLocalX+cy*pivotZ,1.0F
    };
}
std::array<float,16> ActorWeaponWorldMatrix(const std::array<float,3>& position,
    float yaw,float bob,float stridePhase,float movementAmount,float pitch,float roll) {
    auto transform=ActorWorldMatrix(position,yaw,bob,pitch,roll);
    const float sway=std::sin(stridePhase)*0.012F*movementAmount;
    constexpr float localY=1.02F,localZ=0.15F;
    const float localX=0.27F+sway;
    transform[12]+=transform[0]*localX+transform[4]*localY+transform[8]*localZ;
    transform[13]+=transform[1]*localX+transform[5]*localY+transform[9]*localZ;
    transform[14]+=transform[2]*localX+transform[6]*localY+transform[10]*localZ;
    return transform;
}
std::array<float,16> PlayerWeaponWorldMatrix(
    const talvisota::game::PlayerCamera& camera,float elapsedSeconds) {
    const auto& f=camera.forward;
    std::array<float,3> right{f[2],0.0F,-f[0]};
    float length=std::sqrt(right[0]*right[0]+right[2]*right[2]);
    if(length<1.0e-5F) { right={1.0F,0.0F,0.0F}; length=1.0F; }
    for(float& value:right) value/=length;
    const std::array<float,3> up{
        f[1]*right[2]-f[2]*right[1],f[2]*right[0]-f[0]*right[2],
        f[0]*right[1]-f[1]*right[0]};
    const float sway=std::sin(elapsedSeconds*2.1F)*0.004F;
    const std::array<float,3> p{
        camera.position[0]+f[0]*0.68F+right[0]*0.25F-up[0]*0.23F+right[0]*sway,
        camera.position[1]+f[1]*0.68F+right[1]*0.25F-up[1]*0.23F+0.004F*std::sin(elapsedSeconds*4.2F),
        camera.position[2]+f[2]*0.68F+right[2]*0.25F-up[2]*0.23F+right[2]*sway};
    return {right[0],right[1],right[2],0.0F,up[0],up[1],up[2],0.0F,
        f[0],f[1],f[2],0.0F,p[0],p[1],p[2],1.0F};
}
bool EnsureBattlefieldActorInstances(talvisota::presentation::SceneSnapshot& scene,
    std::vector<float>& maxDrawDistances,const talvisota::gameplay::SimulationState& simulation,
    const BattlefieldRenderAssets& assets,std::vector<BattlefieldActorRenderBinding>& bindings,
    std::string& error) {
    for(auto& binding:bindings) {
        for(const std::size_t index:{binding.bodyInstance,binding.armsInstance,binding.gearInstance,
                binding.headInstance,binding.leftLegInstance,binding.rightLegInstance,binding.weaponInstance})
            if(index<scene.instances.size()) scene.instances[index].visible=false;
    }
    if(simulation.entities.Find(simulation.playerEntityId)==nullptr) {
        error="Battlefield actor presentation requires the active player entity."; return false;
    }
    for(const auto& [actorId,combatant]:simulation.combat.combatants) {
        if(actorId==simulation.playerEntityId) continue;
        const auto* actor=simulation.entities.Find(actorId);
        if(actor==nullptr||!actor->active||actor->kind!=talvisota::entities::EntityKind::Infantry) {
            error="A mission combatant has no matching active infantry entity: "+actorId; return false;
        }
        auto found=std::find_if(bindings.begin(),bindings.end(),
            [&](const BattlefieldActorRenderBinding& b){return b.entityId==actorId;});
        if(found==bindings.end()) {
            BattlefieldActorRenderBinding binding;
            binding.entityId=actorId;
            const auto appendActorPart=[&](const std::uint32_t meshIndex,const std::uint32_t materialIndex) {
                talvisota::presentation::RenderInstance instance;
                instance.meshIndex=meshIndex; instance.materialIndex=materialIndex;
                const std::size_t index=scene.instances.size();
                scene.instances.push_back(std::move(instance));
                maxDrawDistances.push_back(520.0F);
                return index;
            };
            binding.bodyInstance=appendActorPart(assets.bodyMesh,assets.finnishMaterial);
            binding.armsInstance=appendActorPart(assets.armsMesh,assets.finnishMaterial);
            binding.gearInstance=appendActorPart(assets.gearMesh,assets.finnishGearMaterial);
            binding.headInstance=appendActorPart(assets.headMesh,assets.skinMaterial);
            binding.leftLegInstance=appendActorPart(assets.legMesh,assets.finnishMaterial);
            binding.rightLegInstance=appendActorPart(assets.legMesh,assets.finnishMaterial);
            binding.weaponInstance=appendActorPart(
                ResolveWeaponMesh(assets,combatant.weaponId),assets.weaponMaterial);
            bindings.push_back(std::move(binding)); found=std::prev(bindings.end());
        }
        const auto uniformMaterial=actor->factionId=="faction.finnish"?
            assets.finnishMaterial:assets.sovietMaterial;
        auto& body=scene.instances[found->bodyInstance];
        body.meshIndex=assets.bodyMesh; body.materialIndex=uniformMaterial;
        body.visible=combatant.health>0.0F;
        auto& arms=scene.instances[found->armsInstance];
        arms.meshIndex=assets.armsMesh; arms.materialIndex=uniformMaterial;
        arms.visible=combatant.health>0.0F;
        auto& gear=scene.instances[found->gearInstance];
        gear.meshIndex=assets.gearMesh;
        gear.materialIndex=actor->factionId=="faction.finnish"?
            assets.finnishGearMaterial:assets.sovietGearMaterial;
        gear.visible=combatant.health>0.0F;
        auto& head=scene.instances[found->headInstance];
        head.meshIndex=assets.headMesh; head.materialIndex=assets.skinMaterial;
        head.visible=combatant.health>0.0F;
        for(const std::size_t index:{found->leftLegInstance,found->rightLegInstance}) {
            auto& part=scene.instances[index];
            part.meshIndex=assets.legMesh; part.materialIndex=uniformMaterial;
            part.visible=combatant.health>0.0F;
        }
        auto& weapon=scene.instances[found->weaponInstance];
        weapon.meshIndex=ResolveWeaponMesh(assets,combatant.weaponId);
        weapon.materialIndex=assets.weaponMaterial; weapon.visible=combatant.health>0.0F;
    }
    error.clear(); return true;
}
void UpdateBattlefieldActorInstances(talvisota::presentation::SceneSnapshot& scene,
    const talvisota::gameplay::SimulationState& simulation,
    std::vector<BattlefieldActorRenderBinding>& bindings,float elapsedSeconds) {
    const auto* player=simulation.entities.Find(simulation.playerEntityId);
    for(std::size_t i=0U;i<bindings.size();++i) {
        auto& binding=bindings[i];
        if(binding.bodyInstance>=scene.instances.size()||binding.weaponInstance>=scene.instances.size()) continue;
        const auto* actor=simulation.entities.Find(binding.entityId);
        const auto combatant=simulation.combat.combatants.find(binding.entityId);
        const bool registered=player!=nullptr&&actor!=nullptr&&actor->active&&
            combatant!=simulation.combat.combatants.end();
        const bool defeated=registered&&combatant->second.health<=0.0F;
        const bool visible=registered;
        auto& body=scene.instances[binding.bodyInstance];
        auto& arms=scene.instances[binding.armsInstance];
        auto& gear=scene.instances[binding.gearInstance];
        auto& head=scene.instances[binding.headInstance];
        auto& leftLeg=scene.instances[binding.leftLegInstance];
        auto& rightLeg=scene.instances[binding.rightLegInstance];
        auto& weapon=scene.instances[binding.weaponInstance];
        body.visible=visible; arms.visible=visible; gear.visible=visible; head.visible=visible;
        leftLeg.visible=visible; rightLeg.visible=visible; weapon.visible=visible;
        if(!visible) continue;
        const float yaw=std::atan2(player->position[0]-actor->position[0],
            player->position[2]-actor->position[2]);
        if(defeated) {
            // Defeated infantry remain as non-graphic scene characters and settle
            // onto the terrain instead of vanishing on the first defeated frame.
            if(binding.fallStartSeconds<0.0F) binding.fallStartSeconds=elapsedSeconds;
            const float progress=std::clamp((elapsedSeconds-binding.fallStartSeconds)/0.48F,0.0F,1.0F);
            const float eased=progress*progress*(3.0F-2.0F*progress);
            const float direction=(i%2U)==0U?1.0F:-1.0F;
            constexpr float pivotHeight=0.84F;
            auto fallen=ActorWorldMatrix(actor->position,yaw,0.0F,0.0F,direction*1.42F*eased);
            // Reposition the transform around the character centre so the body
            // rests on the snow rather than rotating below the terrain.
            fallen[12]-=fallen[4]*pivotHeight;
            fallen[13]+=pivotHeight-fallen[5]*pivotHeight;
            fallen[14]-=fallen[6]*pivotHeight;
            body.world=fallen; arms.world=fallen; gear.world=fallen; head.world=fallen;
            leftLeg.world=fallen; rightLeg.world=fallen; weapon.world=fallen;
            binding.hasPreviousPosition=false;
            continue;
        }
        binding.fallStartSeconds=-1.0F;
        float movementAmount=0.0F;
        if(binding.hasPreviousPosition) {
            const float dt=elapsedSeconds-binding.lastUpdateSeconds;
            if(dt>1.0e-4F&&dt<0.5F) {
                const float dx=actor->position[0]-binding.previousPosition[0];
                const float dz=actor->position[2]-binding.previousPosition[2];
                const float speed=std::sqrt(dx*dx+dz*dz)/dt;
                movementAmount=std::clamp(speed/3.2F,0.0F,1.0F);
                binding.stridePhase=std::fmod(binding.stridePhase+speed*dt*2.6F,6.28318530718F);
            }
        }
        binding.previousPosition=actor->position;
        binding.lastUpdateSeconds=elapsedSeconds;
        binding.hasPreviousPosition=true;
        const float gait=std::sin(binding.stridePhase+static_cast<float>(i)*0.37F);
        const float bob=movementAmount*(0.012F+0.018F*std::abs(gait))+
            std::sin(elapsedSeconds*1.8F+static_cast<float>(i)*0.73F)*0.0025F;
        const float bodyPitch=0.12F*movementAmount;
        const float bodyRoll=gait*0.035F*movementAmount;
        body.world=ActorWorldMatrix(actor->position,yaw,bob,bodyPitch,bodyRoll);
        arms.world=body.world;
        gear.world=body.world;
        head.world=body.world;
        leftLeg.world=ActorLegWorldMatrix(actor->position,yaw,-0.125F,
            gait*0.34F*movementAmount,bodyPitch,bodyRoll);
        rightLeg.world=ActorLegWorldMatrix(actor->position,yaw,0.125F,
            -gait*0.34F*movementAmount,bodyPitch,bodyRoll);
        weapon.world=ActorWeaponWorldMatrix(actor->position,yaw,bob,
            binding.stridePhase,movementAmount,bodyPitch,bodyRoll);
    }
}
bool AppendPlayerWeaponInstance(talvisota::presentation::SceneSnapshot& scene,
    std::vector<float>& maxDrawDistances,const BattlefieldRenderAssets& assets,
    std::size_t& outputIndex,std::string& error) {
    outputIndex=scene.instances.size();
    talvisota::presentation::RenderInstance weapon;
    weapon.meshIndex=assets.rifleMesh; weapon.materialIndex=assets.weaponMaterial;
    scene.instances.push_back(weapon); maxDrawDistances.push_back(100.0F);
    error.clear(); return true;
}
void UpdatePlayerWeaponInstance(talvisota::presentation::SceneSnapshot& scene,
    std::size_t instanceIndex,const talvisota::gameplay::SimulationState& simulation,
    const BattlefieldRenderAssets& assets,const talvisota::game::PlayerCamera& camera,
    float elapsedSeconds) {
    if(instanceIndex>=scene.instances.size()) return;
    const auto player=simulation.combat.combatants.find(simulation.playerEntityId);
    auto& instance=scene.instances[instanceIndex];
    if(player==simulation.combat.combatants.end()||player->second.health<=0.0F) {
        instance.visible=false; return;
    }
    instance.meshIndex=ResolveWeaponMesh(assets,player->second.weaponId);
    instance.materialIndex=assets.weaponMaterial;
    instance.world=PlayerWeaponWorldMatrix(camera,elapsedSeconds);
    instance.visible=true;
}


void SetMissionFeedback(std::wstring message,const std::chrono::milliseconds duration) {
    missionOverlay.feedback=std::move(message);
    missionFeedbackUntil=std::chrono::steady_clock::now()+duration;
    if(frontendOverlay&&IsWindow(frontendOverlay)) InvalidateRect(frontendOverlay,nullptr,FALSE);
}

bool RefreshMissionOverlay(const talvisota::gameplay::SimulationState& simulation,
    const talvisota::game::PlayerState& player,const float interactionRadius,std::string& error) {
    MissionOverlayData updated=missionOverlay;
    updated.completed=simulation.mission.status==talvisota::missions::MissionStatus::Completed;
    updated.failed=simulation.mission.status==talvisota::missions::MissionStatus::Failed;
    updated.hasProgress=simulation.mission.completedObjectives>0U;
    updated.progress=std::to_wstring(simulation.mission.completedObjectives)+L" / "+
        std::to_wstring(simulation.mission.objectives.size())+L" OBJECTIVES";
    if(updated.completed) {
        updated.objective=L"MISSION COMPLETE";
        updated.distance.clear();
        updated.prompt=campaignHasNextMission?L"PRESS E TO RECEIVE YOUR NEXT ORDERS":L"PRESS ESC TO RETURN TO THE MENU";
    } else if(updated.failed) {
        updated.objective=L"MISSION FAILED";
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
        const auto* objectiveEntity=simulation.entities.Find(target.entityId);
        const bool combatObjective=objectiveEntity!=nullptr && objectiveEntity->kind!=talvisota::entities::EntityKind::WorldObject;
        const long distance=std::max(0L,std::lround(target.distanceMeters));
        const double bearing=std::atan2(
            static_cast<double>(target.position[0])-player.position[0],
            static_cast<double>(target.position[2])-player.position[2]);
        constexpr double fullTurn=6.28318530717958647692;
        const double relativeBearing=std::remainder(
            bearing-static_cast<double>(player.yawRadians),fullTurn);
        const std::wstring direction=std::abs(relativeBearing)>2.35?L"BEHIND":
            relativeBearing>0.35?L"RIGHT":relativeBearing< -0.35?L"LEFT":L"AHEAD";
        updated.distance=std::to_wstring(distance)+L" m  "+direction;
        updated.prompt=combatObjective?L"RMB AIM  |  LMB FIRE / HOLD AUTO  |  R RELOAD":
            (target.distanceMeters<=interactionRadius?L"[E]  INTERACT":L"FOLLOW THE OBJECTIVE");
    }
    if(std::chrono::steady_clock::now()>=missionFeedbackUntil) updated.feedback.clear();
    const auto playerCombatant=simulation.combat.combatants.find(simulation.playerEntityId);
    std::size_t friendlyContacts=0U, hostileContacts=0U;
    const auto* playerEntity=simulation.entities.Find(simulation.playerEntityId);
    for(const auto& [entityId,combatant]:simulation.combat.combatants) {
        if(entityId==simulation.playerEntityId || combatant.health<=0.0F) continue;
        const auto* actor=simulation.entities.Find(entityId);
        if(actor==nullptr || !actor->active || playerEntity==nullptr) continue;
        if(actor->factionId==playerEntity->factionId) ++friendlyContacts;
        else ++hostileContacts;
    }
    updated.contact=hostileContacts>0U
        ?L"SECTION  "+std::to_wstring(friendlyContacts)+L" ACTIVE   |   HOSTILE CONTACTS  "+
            std::to_wstring(hostileContacts)
        :L"WASD MOVE   |   MOUSE LOOK   |   E INTERACT";
    if(playerCombatant!=simulation.combat.combatants.end()) {
        const auto* weapon=talvisota::combat::FindWeaponDefinition(playerCombatant->second.weaponId);
        if(weapon!=nullptr) updated.ammo=L"HEALTH  "+
            std::to_wstring(static_cast<int>(std::lround(playerCombatant->second.health)))+L"   |   "+
            Utf8ToWide(weapon->displayName)+L"  "+
            std::to_wstring(playerCombatant->second.magazineRounds)+L" / "+
            std::to_wstring(playerCombatant->second.reserveRounds);
    }
    const bool changed=updated.title!=missionOverlay.title||updated.description!=missionOverlay.description||
        updated.objective!=missionOverlay.objective||updated.distance!=missionOverlay.distance||
        updated.progress!=missionOverlay.progress||updated.prompt!=missionOverlay.prompt||
        updated.feedback!=missionOverlay.feedback||updated.ammo!=missionOverlay.ammo||updated.contact!=missionOverlay.contact||updated.completed!=missionOverlay.completed||
        updated.failed!=missionOverlay.failed||updated.hasProgress!=missionOverlay.hasProgress;
    missionOverlay=std::move(updated);
    if(changed&&frontendOverlay&&IsWindow(frontendOverlay)) InvalidateRect(frontendOverlay,nullptr,FALSE);
    error.clear();
    return true;
}

void SetFrontendState(const FrontendState state) {
    frontendState=state;
    if(state!=FrontendState::Playing) { selectedMenuButton=0; currentCameraFovDegrees=60.0F; }
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
    RECT card{24,24,500,250};
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
    RECT title{44,38,480,68}; DrawTextW(dc,missionOverlay.title.c_str(),-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,body); SetTextColor(dc,RGB(239,244,248));
    RECT objective{44,76,480,106};
    DrawTextW(dc,missionOverlay.objective.c_str(),-1,&objective,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SetTextColor(dc,RGB(157,180,197));
    RECT progress{44,112,270,138}; DrawTextW(dc,missionOverlay.progress.c_str(),-1,&progress,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT distance{280,112,480,138}; DrawTextW(dc,missionOverlay.distance.c_str(),-1,&distance,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    SetTextColor(dc,RGB(113,192,220));
    RECT prompt{44,145,480,174}; DrawTextW(dc,missionOverlay.feedback.empty()?missionOverlay.prompt.c_str():missionOverlay.feedback.c_str(),
        -1,&prompt,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SetTextColor(dc,RGB(177,197,209));
    RECT ammo{44,180,480,207}; DrawTextW(dc,missionOverlay.ammo.c_str(),-1,&ammo,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SetTextColor(dc,RGB(222,184,118));
    RECT contact{44,210,480,238};
    DrawTextW(dc,missionOverlay.contact.c_str(),-1,&contact,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,oldFont); DeleteObject(body); DeleteObject(heading);

    if(missionOverlay.completed || missionOverlay.failed) {
        RECT banner{client.right/2-250,client.bottom-142,client.right/2+250,client.bottom-70};
        HBRUSH success=CreateSolidBrush(RGB(10,24,34)); FillRect(dc,&banner,success); DeleteObject(success);
        HFONT complete=CreateFontW(24,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        oldFont=SelectObject(dc,complete); SetTextColor(dc,missionOverlay.failed?RGB(220,155,130):RGB(125,215,190));
        DrawTextW(dc,missionOverlay.failed?L"MISSION FAILED":L"MISSION COMPLETE",-1,&banner,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
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
            (i==0?(missionOverlay.failed?L"RESTART MISSION":missionOverlay.completed?(campaignHasNextMission?L"CONTINUE CAMPAIGN":L"REPLAY MISSION"):missionOverlay.hasProgress?L"CONTINUE MISSION":L"START MISSION"):L"QUIT"):
            (i==0?(missionOverlay.failed?L"RESTART MISSION":L"RESUME"):i==1?L"RETURN TO MAIN MENU":L"QUIT");
        DrawTextW(dc,label,-1,&b,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    SelectObject(dc,old); SetTextColor(dc,RGB(126,151,169));
    RECT hint{p.left+18,p.bottom-38,p.right-18,p.bottom-12};
    DrawTextW(dc,frontendState==FrontendState::MainMenu?
        L"WASD MOVE  |  MOUSE LOOK  |  RMB AIM  |  E INTERACT  |  F PUSH PROPS  |  1/2/3 WEAPONS  |  LMB FIRE / HOLD AUTO  |  R RELOAD  |  ESC PAUSE":
        L"ESC TO RESUME  |  MISSION PROGRESS IS KEPT",
        -1,&hint,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DeleteObject(body); DeleteObject(title); EndPaint(hwnd,&ps);
}
void ActivateFrontendButton(const int index) {
    if(frontendState==FrontendState::MainMenu) {
        if(index==0) {
            if(missionOverlay.failed) restartPatrolRequested=true;
            else if(missionOverlay.completed) {
                if(campaignHasNextMission) advanceCampaignRequested=true;
                else restartPatrolRequested=true;
            }
            SetFrontendState(FrontendState::Playing);
        } else if(index==1&&mainWindow) DestroyWindow(mainWindow);
    } else if(frontendState==FrontendState::Paused) {
        if(index==0) {
            if(missionOverlay.failed) restartPatrolRequested=true;
            SetFrontendState(FrontendState::Playing);
        }
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
    const vanta::math::Vec3& target, int width, int height,const float fovDegrees=60.0F) {
    const auto view = vanta::math::Mat4::LookAtLH(eye, target, {0.0F, 1.0F, 0.0F});
    if (!view || width <= 0 || height <= 0) return false;
    const float fov = std::clamp(fovDegrees,20.0F,100.0F) * 3.14159265358979323846F / 180.0F;
    const auto projection = vanta::math::Mat4::Perspective(
        fov, static_cast<float>(width) / static_cast<float>(height), 0.1F, 6000.0F);
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

bool LoadMission(const std::filesystem::path& contentRoot,const std::string_view missionId,
    talvisota::content::AuthoredDefinition& output,std::string& error) {
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    if(!talvisota::content::LoadContentManifest(contentRoot/"content"/"catalog.tmanifest",
            contentRoot,registry,manifestDiagnostics)) {
        error="Campaign content manifest validation failed.";
        for(const auto& item:manifestDiagnostics)
            error+="\n"+item.file.string()+":"+std::to_string(item.line)+" ["+item.field+"] "+item.message;
        return false;
    }
    std::vector<talvisota::content::AuthoredDefinitionDiagnostic> diagnostics;
    const auto missionRecord=registry.Find(missionId);
    if(missionRecord==nullptr || missionRecord->kind!=talvisota::content::AssetKind::Definition ||
        missionRecord->relativePath.extension()!=".tmission") {
        error="Campaign mission ID is not registered as an authored mission: "+std::string(missionId);
        return false;
    }
    if(!talvisota::content::LoadAuthoredDefinition(contentRoot/missionRecord->relativePath,
            registry,output,diagnostics)) {
        error="Campaign authored mission failed validation: "+std::string(missionId);
        for(const auto& item:diagnostics)
            error+="\n"+item.file.string()+":"+std::to_string(item.line)+" ["+item.field+"] "+item.message;
        return false;
    }
    error.clear();
    return true;
}

bool TriggerOpeningArtilleryFlash(
    talvisota::presentation::ExplosionLightingSystem& effects,
    const talvisota::world::WorldRegion& region,
    const talvisota::game::PlayerState& player,
    const double elapsedSeconds, const std::uint64_t sequence,
    std::string& error)
{
    // The opening barrage lands beyond the section's position, not beside the
    // player. Staggered, offset impacts read as distant artillery through fog.
    const float forwardX=std::sin(player.yawRadians),forwardZ=std::cos(player.yawRadians);
    const float rightX=std::cos(player.yawRadians),rightZ=-std::sin(player.yawRadians);
    constexpr std::array<std::array<float,2>,3> offsets{{
        {{66.0F,-17.0F}},{{73.0F,15.0F}},{{82.0F,-3.0F}}
    }};
    constexpr std::array<float,3> intensities{{380.0F,460.0F,410.0F}};
    auto candidate=effects;
    for(std::size_t index=0U;index<offsets.size();++index) {
        const float x=player.position[0]+forwardX*offsets[index][0]+rightX*offsets[index][1];
        const float z=player.position[2]+forwardZ*offsets[index][0]+rightZ*offsets[index][1];
        float y=0.0F;
        std::array<float,3> normal{};
        if(!talvisota::world::SampleTerrainHeight(region,x,z,y,normal,error))
            return false;
        if(!candidate.Trigger("event.sakkola.defensive-line.artillery-flash."+
                std::to_string(sequence)+"."+std::to_string(index),
                {x,y+0.15F,z},elapsedSeconds+static_cast<double>(index)*0.72,
                92.0F,intensities[index],1.15F,error))
            return false;
    }
    effects=std::move(candidate);
    error.clear();
    return true;
}


struct SoldierRenderAssets final {
    std::uint32_t meshIndex=0U;
    std::uint32_t finnishMaterialIndex=0U;
    std::uint32_t sovietMaterialIndex=0U;
};

void AppendSoldierBox(talvisota::presentation::RenderMesh& mesh,
    const std::array<float,3>& center,const std::array<float,3>& half) {
    using Vertex=talvisota::presentation::RenderVertex;
    const float x0=center[0]-half[0],x1=center[0]+half[0];
    const float y0=center[1]-half[1],y1=center[1]+half[1];
    const float z0=center[2]-half[2],z1=center[2]+half[2];
    const std::array<std::array<float,3>,8> corners{{
        {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
        {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}
    }};
    struct Face final { std::array<std::uint32_t,4> corners; std::array<float,3> normal; };
    constexpr std::array<Face,6> faces{{
        {{{4U,5U,6U,7U}},{0.0F,0.0F,1.0F}},
        {{{1U,0U,3U,2U}},{0.0F,0.0F,-1.0F}},
        {{{5U,1U,2U,6U}},{1.0F,0.0F,0.0F}},
        {{{0U,4U,7U,3U}},{-1.0F,0.0F,0.0F}},
        {{{3U,7U,6U,2U}},{0.0F,1.0F,0.0F}},
        {{{0U,1U,5U,4U}},{0.0F,-1.0F,0.0F}}
    }};
    for(const Face& face:faces) {
        const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const std::uint32_t index:face.corners)
            mesh.vertices.push_back(Vertex{corners[index],face.normal,0.0F,0.0F});
        mesh.indices.insert(mesh.indices.end(),{first,first+1U,first+2U,first,first+2U,first+3U});
    }
}

bool CreateSoldierRenderAssets(SceneSnapshot& scene,SoldierRenderAssets& output,std::string& error) {
    if(scene.meshes.size()>=static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        scene.materials.size()+2U>static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        error="Scene has no remaining material or mesh indices for battlefield infantry.";
        return false;
    }
    talvisota::presentation::RenderMesh soldier;
    // A compact, deliberately readable low-poly winter infantry silhouette.
    // The model is game presentation only; it does not encode real weapon handling.
    AppendSoldierBox(soldier,{0.0F,1.08F,0.0F},{0.25F,0.34F,0.15F});       // coat
    AppendSoldierBox(soldier,{0.0F,1.53F,0.0F},{0.13F,0.16F,0.13F});       // head
    AppendSoldierBox(soldier,{0.0F,1.70F,0.0F},{0.17F,0.075F,0.17F});     // winter helmet
    AppendSoldierBox(soldier,{-0.13F,0.46F,0.0F},{0.105F,0.34F,0.12F});   // left leg
    AppendSoldierBox(soldier,{0.13F,0.46F,0.0F},{0.105F,0.34F,0.12F});    // right leg
    AppendSoldierBox(soldier,{-0.13F,0.105F,0.035F},{0.13F,0.095F,0.19F}); // left boot
    AppendSoldierBox(soldier,{0.13F,0.105F,0.035F},{0.13F,0.095F,0.19F}); // right boot
    AppendSoldierBox(soldier,{-0.31F,1.12F,0.015F},{0.095F,0.29F,0.11F}); // left arm
    AppendSoldierBox(soldier,{0.31F,1.12F,0.015F},{0.095F,0.29F,0.11F});  // right arm
    AppendSoldierBox(soldier,{0.0F,1.10F,-0.205F},{0.19F,0.27F,0.095F}); // field pack
    output.meshIndex=static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(soldier));
    output.finnishMaterialIndex=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back({{0.30F,0.35F,0.29F},0.96F,0.0F});
    output.sovietMaterialIndex=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back({{0.34F,0.31F,0.23F},0.96F,0.0F});
    error.clear();
    return true;
}

bool SyncMissionActorsToScene(SceneSnapshot& scene,
    std::vector<float>& instanceMaxDrawDistances,const SoldierRenderAssets& assets,
    const talvisota::gameplay::SimulationState& simulation,
    const talvisota::game::PlayerState& player,
    std::map<std::string,std::size_t,std::less<>>& actorInstances,std::string& error) {
    for(const auto& [actorId,index]:actorInstances) {
        (void)actorId;
        if(index<scene.instances.size()) scene.instances[index].visible=false;
    }
    for(const auto& [entityId,entity]:simulation.entities.All()) {
        if(!entity.active || entity.kind!=talvisota::entities::EntityKind::Infantry) continue;
        const auto combatant=simulation.combat.combatants.find(entityId);
        if(combatant==simulation.combat.combatants.end()) continue;
        auto found=actorInstances.find(entityId);
        if(found==actorInstances.end()) {
            if(scene.instances.size()>=static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                error="Scene instance capacity was exceeded while adding battlefield infantry.";
                return false;
            }
            talvisota::presentation::RenderInstance instance;
            instance.meshIndex=assets.meshIndex;
            instance.materialIndex=entity.factionId=="faction.soviet"
                ?assets.sovietMaterialIndex:assets.finnishMaterialIndex;
            const std::size_t index=scene.instances.size();
            scene.instances.push_back(instance);
            instanceMaxDrawDistances.push_back(420.0F);
            found=actorInstances.emplace(entityId,index).first;
        }
        auto& instance=scene.instances[found->second];
        instance.visible=combatant->second.health>0.0F;
        const double yaw=std::atan2(static_cast<double>(player.position[0])-entity.position[0],
            static_cast<double>(player.position[2])-entity.position[2]);
        const float cosine=static_cast<float>(std::cos(yaw));
        const float sine=static_cast<float>(std::sin(yaw));
        instance.world={
            cosine,0.0F,-sine,0.0F,
            0.0F,1.0F,0.0F,0.0F,
            sine,0.0F,cosine,0.0F,
            entity.position[0],entity.position[1],entity.position[2],1.0F
        };
    }
    error.clear();
    return true;
}

void ApplyMissionEnvironment(SceneSnapshot& scene,
    const talvisota::content::AuthoredDefinition& mission) {
    if(mission.environmentProfile=="overcast") {
        scene.environment.enabled=true;
        scene.environment.sunIntensity=std::min(scene.environment.sunIntensity,1.45F);
        scene.environment.ambientIntensity=std::min(scene.environment.ambientIntensity,0.82F);
        scene.environment.fogColor={0.48F,0.57F,0.68F};
        scene.environment.fogDensity=std::max(scene.environment.fogDensity,0.0020F);
        scene.environment.fogStart=std::min(scene.environment.fogStart,65.0F);
        scene.environment.windStrength=std::max(scene.environment.windStrength,0.10F);
    } else if(mission.environmentProfile=="low_visibility") {
        scene.environment.enabled=true;
        scene.environment.sunIntensity=std::min(scene.environment.sunIntensity,1.10F);
        scene.environment.ambientIntensity=std::min(scene.environment.ambientIntensity,0.65F);
        scene.environment.fogColor={0.45F,0.54F,0.66F};
        scene.environment.fogDensity=std::max(scene.environment.fogDensity,0.0040F);
        scene.environment.fogStart=std::min(scene.environment.fogStart,32.0F);
        scene.environment.windStrength=std::max(scene.environment.windStrength,0.14F);
    }
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
        if(target!=nullptr && target->kind!=talvisota::entities::EntityKind::WorldObject)
            continue;
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
    constexpr float originX = -90.0F, originZ = 70.0F, step = 8.0F;
    constexpr int directions = 16;
    for (int ring = 0; ring <= 12; ++ring) {
        const float radius = static_cast<float>(ring) * step;
        for (int direction = 0; direction < directions; ++direction) {
            const float angle = static_cast<float>(direction) *
                (2.0F * 3.14159265358979323846F / static_cast<float>(directions));
            const std::array<float, 3> spawn{originX + std::sin(angle) * radius,
                0.0F, originZ + std::cos(angle) * radius};
            if (talvisota::game::InitializePlayer(region, config, spawn, player, error)) {
                player.yawRadians=std::atan2(-57.0F-spawn[0],129.0F-spawn[2]);
                return true;
            }
        }
    }
    error = "Could not find a collision-free first-person spawn near the authored region start area: " + error;
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
        instance.visible=depth+radius>=0.1F && depth-radius<=6000.0F &&
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

struct CaptureView { const wchar_t* file; vanta::math::Vec3 eye; vanta::math::Vec3 target; bool expectSky = true; };
constexpr std::array<CaptureView, 7> Views = {{
    // Exterior composition checks.
    {L"01-sakkola-overview.png", {0.0F, 35.0F, 30.0F}, {0.0F, 20.0F, 190.0F}},
    {L"02-church-village.png", {0.0F, 22.0F, 225.0F}, {0.0F, 17.0F, 178.0F}},
    {L"03-suvanto-shoreline.png", {0.0F, 4.0F, -1100.0F}, {0.0F, 1.82F, -270.0F}},
    {L"04-karelian-forest.png", {82.0F, 11.0F, -205.0F}, {175.0F, 4.0F, -62.0F}},
    {L"05-farmstead.png", {-80.0F, 12.0F, 90.0F}, {-45.0F, 9.0F, 123.0F}},
    // Inside the church nave, facing the altar and the rows of pews.
    {L"06-church-interior.png", {0.0F, 10.9F, 180.8F}, {0.0F, 10.9F, 175.5F}, false},
    // Inside the west farmhouse, facing from the entrance toward the stove and table.
    {L"07-farmhouse-interior.png", {-42.46F, 9.7F, 125.54F}, {-42.67F, 9.7F, 124.54F}, false}
}};
constexpr std::array<CaptureView, 7> TaipaleViews = {{
    // Elevated landscape compositions replace the previous near-horizontal sightlines,
    // which placed the river at the vanishing line and let winter fog flatten the scene.
    {L"01-taipale-overview.png", {0.0F, 10.0F, 820.0F}, {0.0F, 3.8F, 590.0F}},
    {L"02-taipale-river-bend.png", {-430.0F, 12.0F, 790.0F}, {-40.0F, 3.8F, 620.0F}},
    {L"03-taipale-frozen-river.png", {30.0F, 7.5F, 790.0F}, {0.0F, 3.8F, 590.0F}},
    {L"04-taipale-forest.png", {1400.0F, 16.0F, 1400.0F}, {1500.0F, 6.0F, 1150.0F}, false},
    {L"05-taipale-farmstead.png", {950.0F, 14.0F, -300.0F}, {720.0F, 5.0F, -560.0F}, false},
    {L"06-taipale-watch-post.png", {-1350.0F, 14.0F, 1250.0F}, {-1120.0F, 5.0F, 1040.0F}, false},
    {L"07-taipale-command-hut.png", {1500.0F, 14.0F, -1400.0F}, {1360.0F, 5.0F, -1160.0F}, false}
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
    std::string selectedMissionId = "mission.sakkola.first-patrol";
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
        } else if (token.rfind(L"--mission=", 0) == 0) {
            selectedMissionId.clear();
            for (const wchar_t character : token.substr(10)) {
                if (character < 0x21 || character > 0x7e) {
                    LocalFree(parsedArguments);
                    if (ownsCom) CoUninitialize();
                    return 1;
                }
                selectedMissionId.push_back(static_cast<char>(character));
            }
            if (selectedMissionId.empty()) {
                LocalFree(parsedArguments);
                if (ownsCom) CoUninitialize();
                return 1;
            }
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
    if ((missionSmokeRequested && captureRequested) ||
        (missionSmokeRequested && selectedMissionId != "mission.sakkola.first-patrol")) {
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
        captureLog << "Talvisota renderer backend: " << (useD3D11 ? "D3D11" : "D3D12")
            << "; capture mode enabled; output=" << std::filesystem::path(captureDir).string() << "\n";
    }
    std::cout << "Talvisota renderer backend: " << (useD3D11 ? "D3D11" : "D3D12")
        << "; capture mode: " << (captureRequested ? "enabled" : "disabled") << ".\n";

    std::filesystem::path contentRoot;
    std::string contentRootError;
    if(!ResolveContentRoot(instance,contentRoot,contentRootError)) {
        std::cerr<<contentRootError<<"\n";
        if(captureLog) captureLog<<contentRootError<<"\n";
        if(ownsCom) CoUninitialize();
        return 2;
    }

    std::string error;
    talvisota::content::AuthoredDefinition authoredMission;
    std::string selectedWorldId = "world.sakkola";
    std::string selectedRegionId = "region.sakkola.church_village";
    if (!captureRequested || selectedMissionId != "mission.sakkola.first-patrol") {
        if (!LoadMission(contentRoot, selectedMissionId, authoredMission, error)) {
            std::cerr << "Could not load selected mission: " << error << "\\n";
            if (ownsCom) CoUninitialize();
            return 2;
        }
        selectedWorldId = authoredMission.worldId;
        selectedRegionId = authoredMission.regionId;
    }

    SceneSnapshot scene;
    talvisota::world::WorldRegion gameRegion;
    std::size_t placements = 0, draws = 0;
    std::vector<float> instanceMaxDrawDistances;
    if (!talvisota::presentation::BuildWorldScene(contentRoot, selectedWorldId, selectedRegionId,
            scene, instanceMaxDrawDistances, placements, draws, gameRegion, error)) {
        std::cerr << error << "\n";
        if (captureLog) captureLog << "Scene build failed:\n" << error << "\n";
        if (ownsCom) CoUninitialize();
        return 2;
    }

    SoldierRenderAssets soldierRenderAssets;
    if(!CreateSoldierRenderAssets(scene,soldierRenderAssets,error)) {
        std::cerr<<"Could not create battlefield infantry presentation: "<<error<<"\\n";
        if(ownsCom) CoUninitialize();
        return 2;
    }

    const auto worldEnvironment=scene.environment;

    std::cout << gameRegion.id << " scene composition: placements=" << placements
        << ", material-bound instances=" << draws << ", unique meshes=" << scene.meshes.size()
        << ", materials=" << scene.materials.size() << ".\n";
    if (captureLog) captureLog << gameRegion.id << " scene composition: placements=" << placements
        << ", material-bound instances=" << draws << ", unique meshes=" << scene.meshes.size()
        << ", materials=" << scene.materials.size() << ".\n";

    if(missionSmokeRequested) {
        talvisota::game::PlayerConfig playerConfig;
        talvisota::game::PlayerState playerState;
        if(!FindPlayerSpawn(gameRegion,playerConfig,playerState,error)) {
            std::cerr<<"Campaign smoke could not find a player spawn: "<<error<<"\n";
            if(ownsCom) CoUninitialize();
            return 10;
        }
        talvisota::content::AuthoredDefinition campaignMission;
        if(!LoadMission(contentRoot,"mission.sakkola.first-patrol",campaignMission,error)) {
            std::cerr<<"Campaign smoke could not load First Patrol: "<<error<<"\n";
            if(ownsCom) CoUninitialize();
            return 10;
        }
        talvisota::gameplay::GameplayRules rules;
        std::size_t completedMissions=0U;
        while(true) {
            talvisota::gameplay::SimulationState simulation;
            if(!talvisota::gameplay::InitializeSimulationFromAuthoredMission(
                    campaignMission,gameRegion,playerState.position,rules,simulation,error) ||
                !PlaceMissionInteractionAnchors(gameRegion,simulation,playerState.position,
                    playerConfig.capsuleRadius,error)) {
                std::cerr<<"Campaign smoke initialization failed for "<<campaignMission.id<<": "<<error<<"\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            for(std::size_t index=0U;index<simulation.mission.objectives.size();++index) {
                talvisota::gameplay::ActiveObjectiveTarget waypoint;
                if(!talvisota::gameplay::FindActiveObjectiveTarget(
                        simulation,simulation.playerEntityId,waypoint,error)) {
                    std::cerr<<"Campaign smoke waypoint failed: "<<error<<"\n";
                    if(ownsCom) CoUninitialize();
                    return 10;
                }
                const auto* waypointEntity=simulation.entities.Find(waypoint.entityId);
                if(waypointEntity!=nullptr &&
                    waypointEntity->kind!=talvisota::entities::EntityKind::WorldObject) {
                    if(!simulation.entities.SetPosition(simulation.playerEntityId,waypoint.position,error)) {
                        std::cerr<<"Campaign smoke could not stage the authored combat objective: "<<error<<"\n";
                        if(ownsCom) CoUninitialize();
                        return 10;
                    }
                    bool defeated=false;
                    for(std::size_t shot=0U;shot<4U&&!defeated;++shot) {
                        const std::array<talvisota::combat::CombatCommand,1> fire{{
                            {"smoke.combat."+campaignMission.id+"."+std::to_string(index+1U)+"."+std::to_string(shot+1U),
                                simulation.playerEntityId,waypoint.entityId,talvisota::combat::CombatAction::Fire}
                        }};
                        std::vector<talvisota::combat::CombatResult> combatResults;
                        if(!talvisota::combat::AdvanceCombat(simulation.combat,simulation.entities,
                                0.05,fire,combatResults,error)) {
                            std::cerr<<"Campaign smoke combat step failed: "<<error<<"\n";
                            if(ownsCom) CoUninitialize();
                            return 10;
                        }
                        std::vector<talvisota::gameplay::CommandResult> missionResults;
                        if(!talvisota::gameplay::ApplyCombatMissionEvents(simulation,simulation.combat,
                                fire,combatResults,missionResults,error)) {
                            std::cerr<<"Campaign smoke combat objective failed: "<<error<<"\n";
                            if(ownsCom) CoUninitialize();
                            return 10;
                        }
                        defeated=combatResults.size()==1U &&
                            combatResults.front().outcome==talvisota::combat::CombatOutcome::TargetDefeated;
                        if(defeated) {
                            const auto expected=index+1U==simulation.mission.objectives.size()
                                ?talvisota::gameplay::CommandOutcome::MissionCompleted
                                :talvisota::gameplay::CommandOutcome::ObjectiveCompleted;
                            if(missionResults.size()!=1U||missionResults.front().outcome!=expected) {
                                std::cerr<<"Campaign smoke combat defeat produced an unexpected mission result.\n";
                                if(ownsCom) CoUninitialize();
                                return 10;
                            }
                        } else {
                            for(int cooldown=0;cooldown<5;++cooldown)
                                if(!talvisota::combat::AdvanceCombat(simulation.combat,simulation.entities,
                                        0.25,std::span<const talvisota::combat::CombatCommand>{},combatResults,error)) {
                                    std::cerr<<"Campaign smoke weapon cooldown failed: "<<error<<"\n";
                                    if(ownsCom) CoUninitialize();
                                    return 10;
                                }
                        }
                    }
                    if(!defeated) {
                        std::cerr<<"Campaign smoke could not defeat authored combat objective: "<<waypoint.entityId<<"\n";
                        if(ownsCom) CoUninitialize();
                        return 10;
                    }
                    continue;
                }
                talvisota::game::PlayerState reachablePlayer;
                if(!talvisota::game::InitializePlayer(gameRegion,playerConfig,
                        waypoint.position,reachablePlayer,error) ||
                    !simulation.entities.SetPosition(simulation.playerEntityId,
                        reachablePlayer.position,error)) {
                    std::cerr<<"Campaign smoke found an unreachable objective anchor: "
                        <<waypoint.entityId<<": "<<error<<"\n";
                    if(ownsCom) CoUninitialize();
                    return 10;
                }
                talvisota::gameplay::InteractionTargetSelection selection;
                if(!talvisota::gameplay::FindNearestActiveObjectiveTarget(simulation,
                        simulation.playerEntityId,rules.interactionRadiusMeters,selection,error) ||
                    selection.entityId!=waypoint.entityId) {
                    std::cerr<<"Campaign smoke could not select the reachable objective: "
                        <<waypoint.entityId<<"\n";
                    if(ownsCom) CoUninitialize();
                    return 10;
                }
                const std::array<talvisota::gameplay::InteractionCommand,1> commands{{
                    {"smoke."+campaignMission.id+"."+std::to_string(index+1U),
                        simulation.playerEntityId,selection.entityId}
                }};
                std::vector<talvisota::gameplay::CommandResult> results;
                if(!talvisota::gameplay::AdvanceSimulation(simulation,rules,1.0/60.0,
                        commands,results,error)) {
                    std::cerr<<"Campaign smoke interaction failed: "<<error<<"\n";
                    if(ownsCom) CoUninitialize();
                    return 10;
                }
                const auto expected=index+1U==simulation.mission.objectives.size()
                    ?talvisota::gameplay::CommandOutcome::MissionCompleted
                    :talvisota::gameplay::CommandOutcome::ObjectiveCompleted;
                if(results.size()!=1U||results.front().outcome!=expected) {
                    std::cerr<<"Campaign smoke produced an unexpected objective result.\n";
                    if(ownsCom) CoUninitialize();
                    return 10;
                }
            }
            if(simulation.mission.status!=talvisota::missions::MissionStatus::Completed) {
                std::cerr<<"Campaign smoke did not complete mission "<<campaignMission.id<<".\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            ++completedMissions;
            std::cout<<"Campaign smoke completed "<<campaignMission.id<<" ("
                <<simulation.mission.objectives.size()<<" objectives).\n";
            if(campaignMission.nextMissionId.empty())
                break;
            talvisota::content::AuthoredDefinition nextMission;
            if(!LoadMission(contentRoot,campaignMission.nextMissionId,nextMission,error)) {
                std::cerr<<"Campaign smoke could not resolve successor: "<<error<<"\n";
                if(ownsCom) CoUninitialize();
                return 10;
            }
            campaignMission=std::move(nextMission);
        }
        std::cout<<"Campaign runtime smoke passed: "<<completedMissions
            <<" linked missions completed with every authored target reachable.\n";
        if(ownsCom) CoUninitialize();
        return 0;
    }

    BattlefieldRenderAssets battlefieldRenderAssets;
    if(!BuildBattlefieldRenderAssets(scene,battlefieldRenderAssets,error) ||
        !talvisota::presentation::ValidateSceneSnapshot(scene,error)) {
        std::cerr<<"Could not build battlefield character and weapon presentation: "<<error<<"\\n";
        if(ownsCom) CoUninitialize();
        return 2;
    }
    talvisota::presentation::ExplosionLightingSystem explosionLighting;
    std::uint64_t explosionSequence=0U;
    talvisota::presentation::SnowfallSystem snowfall;
    if(captureDir.empty() && !snowfall.Initialize(scene,instanceMaxDrawDistances,768U,error)) {
        std::cerr<<"Could not initialize Sakkola snowfall: "<<error<<"\\n";
        if(ownsCom) CoUninitialize();
        return 2;
    }

    // Persistent runtime state is bound to stable authored placement IDs, not
    // to screenshot-only scene objects or material draw-call ordering.
    talvisota::presentation::DynamicWorldSystem dynamicWorld;
    if(!dynamicWorld.Initialize(gameRegion,scene,error)) {
        std::cerr<<"Could not initialize Sakkola dynamic world: "<<error<<"\\n";
        if(ownsCom) CoUninitialize();
        return 2;
    }
    talvisota::platform::UserPaths worldUserPaths;
    std::filesystem::path dynamicWorldSavePath;
    bool dynamicWorldPersistenceEnabled=false;
    auto lastDynamicWorldSave=std::chrono::steady_clock::now();
    if(captureDir.empty() && talvisota::platform::ResolveUserPaths(worldUserPaths,error)) {
        dynamicWorldSavePath=worldUserPaths.dataDirectory/"worlds"/(selectedWorldId+".state");
        if(dynamicWorld.Load(dynamicWorldSavePath,error)) {
            dynamicWorldPersistenceEnabled=true;
            std::cout<<"Sakkola dynamic world: "<<dynamicWorld.Objects().size()
                <<" placements, persistent state "<<dynamicWorldSavePath.string()<<".\\n";
        } else {
            std::cerr<<"Sakkola world-state restore skipped: "<<error<<"\\n";
        }
    } else if(captureDir.empty()) {
        std::cerr<<"Sakkola world-state persistence unavailable: "<<error<<"\\n";
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
        // Screenshot review needs readable mid-distance terrain; the gameplay fog
        // profiles are intentionally atmospheric but wash out the entire snowfield
        // at these fixed landscape-review distances.
        scene.environment.fogDensity = std::min(scene.environment.fogDensity, 0.00045F);
        scene.environment.fogStart = std::max(scene.environment.fogStart, 100.0F);
        const auto& activeViews = gameRegion.id == "region.taipale.riverfront"
            ? TaipaleViews : Views;
        for (const auto& view : activeViews) {
            if (!error.empty()) break;
            GetClientRect(window, &client);
            auto eye = view.eye;
            auto target = view.target;
            if (!KeepCameraAboveTerrain(gameRegion, eye, 1.7F, error) ||
                !KeepCameraAboveTerrain(gameRegion, target, 0.35F, error)) {
                error = "Could not place world capture camera above the terrain: " + error;
                break;
            }
            if (!SetCamera(scene, eye, target, client.right - client.left, client.bottom - client.top)) {
                error = "Could not construct a valid world capture camera.";
                break;
            }
            std::cout << "Capture camera " << (view.file ? "view" : "unnamed")
                << ": eye=(" << eye.x << "," << eye.y << "," << eye.z
                << "), target=(" << target.x << "," << target.y << "," << target.z << ").\\n";
            const std::size_t visibleInstanceCount = CullCaptureInstances(scene,
                meshCullRadii, instanceMaxDrawDistances, eye, target,
                static_cast<float>(client.right-client.left)/static_cast<float>(client.bottom-client.top));
            if(visibleInstanceCount==0U) {
                error = "World capture camera sees no scene instances after culling.";
                break;
            }
            // Capture mode is a visual regression harness, not a frame-rate benchmark.
            // Render a few settling frames, then read back exactly one presented image.
            for (int frame = 0; frame < framesPerView; ++frame) {
                scene.camera.elapsedSeconds = static_cast<float>(frameNumber) / 60.0F;
                scene.camera.frameNumber = frameNumber++;
                if (!renderer->Render(scene, error)) break;
            }
            if (!error.empty()) break;
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
            if (view.expectSky) {
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
        if (captureLog) captureLog << "Capture completed: seven PNG viewpoints rendered; five exterior and two interior views.\n";
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
    talvisota::game::PlayerState missionStartPlayerState=playerState;
    talvisota::gameplay::GameplayRules gameplayRules;
    talvisota::gameplay::SimulationState gameplayState;
    std::map<std::string,std::size_t,std::less<>> actorRenderInstances;
    if(!talvisota::gameplay::InitializeSimulationFromAuthoredMission(
            authoredMission,gameRegion,playerState.position,gameplayRules,gameplayState,error) ||
        !PlaceMissionInteractionAnchors(gameRegion,gameplayState,playerState.position,
            playerConfig.capsuleRadius,error)) {
        std::cerr<<"First Patrol initialization failed: "<<error<<"\n";
        renderer.reset(); DestroyWindow(window); if(ownsCom) CoUninitialize(); return 8;
    }
    missionOverlay.title=Utf8ToWide(authoredMission.name);
    missionOverlay.description=Utf8ToWide(authoredMission.description);
    campaignHasNextMission=!authoredMission.nextMissionId.empty();
    scene.environment=worldEnvironment;
    ApplyMissionEnvironment(scene,authoredMission);
    if(authoredMission.id=="mission.sakkola.defensive-line" &&
        !TriggerOpeningArtilleryFlash(explosionLighting,gameRegion,playerState,
            scene.camera.elapsedSeconds,explosionSequence++,error)) {
        std::cerr<<"Could not schedule the opening artillery flash: "<<error<<"\\n";
        renderer.reset(); DestroyWindow(window); if(ownsCom) CoUninitialize(); return 8;
    }
    if(!RefreshMissionOverlay(gameplayState,playerState,gameplayRules.interactionRadiusMeters,error)) {
        std::cerr<<"First Patrol HUD initialization failed: "<<error<<"\n";
        renderer.reset(); DestroyWindow(window); if(ownsCom) CoUninitialize(); return 8;
    }
    if(authoredMission.id=="mission.sakkola.first-patrol")
        SetMissionFeedback(L"CORPORAL: CHECK IN, THEN INSPECT THE WELL AND WOODPILE.",
            std::chrono::milliseconds(5200));
    std::vector<BattlefieldActorRenderBinding> battlefieldActorBindings;
    std::size_t playerWeaponInstance=0U;
    if(!EnsureBattlefieldActorInstances(scene,instanceMaxDrawDistances,gameplayState,
            battlefieldRenderAssets,battlefieldActorBindings,error) ||
        !AppendPlayerWeaponInstance(scene,instanceMaxDrawDistances,battlefieldRenderAssets,
            playerWeaponInstance,error) ||
        !talvisota::presentation::ValidateSceneSnapshot(scene,error)) {
        std::cerr<<"Battlefield actor scene initialization failed: "<<error<<"\n";
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
    if (!SetCamera(scene, cameraPosition, cameraTarget, width, height,currentCameraFovDegrees)) {
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
    bool fireWasDown=false, reloadWasDown=false, fireQueued=false, reloadQueued=false;
    bool weapon1WasDown=false, weapon2WasDown=false, weapon3WasDown=false;
    bool pushObjectWasDown=false;
    int weaponSwitchQueued=0;
    bool restartCameraRequested=false;
    constexpr double gameplayFixedStep=1.0/60.0;
    double gameplayAccumulator=0.0;
    FrontendState previousFrontendState=frontendState;
    std::cout << "Sakkola: WASD, mouse look, Space jump, Shift sprint, Ctrl crouch, E interact, F push nearby props, 1/2/3 weapons, LMB fire, R reload, ESC pause.\n";
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;
        if(advanceCampaignRequested) {
            advanceCampaignRequested=false;
            if(!authoredMission.nextMissionId.empty()) {
                talvisota::content::AuthoredDefinition nextMission;
                talvisota::gameplay::SimulationState nextGameplay;
                if(!LoadMission(contentRoot,authoredMission.nextMissionId,nextMission,error) ||
                    !talvisota::gameplay::InitializeSimulationFromAuthoredMission(
                        nextMission,gameRegion,playerState.position,gameplayRules,nextGameplay,error) ||
                    !PlaceMissionInteractionAnchors(gameRegion,nextGameplay,playerState.position,
                        playerConfig.capsuleRadius,error)) {
                    std::cerr<<"Campaign transition failed: "<<error<<"\\n";
                    break;
                }
                if(authoredMission.id=="mission.sakkola.defensive-line" &&
                    nextMission.id=="mission.sakkola.aftermath") {
                    if(!dynamicWorld.ApplyDamage("prop.fence.05",1.0F,error)) {
                        std::cerr<<"Could not apply the localized Sakkola aftermath change: "<<error<<"\\n";
                        break;
                    }
                    if(dynamicWorldPersistenceEnabled&&!dynamicWorld.Save(dynamicWorldSavePath,error)) {
                        std::cerr<<"Could not save the Sakkola aftermath state: "<<error<<"\\n";
                        break;
                    }
                }
                authoredMission=std::move(nextMission);
                missionStartPlayerState=playerState;
                scene.environment=worldEnvironment;
                ApplyMissionEnvironment(scene,authoredMission);
                if(authoredMission.id=="mission.sakkola.defensive-line" &&
                    !TriggerOpeningArtilleryFlash(explosionLighting,gameRegion,playerState,
                        scene.camera.elapsedSeconds,explosionSequence++,error)) {
                    std::cerr<<"Could not schedule the opening artillery flash: "<<error<<"\\n"; break;
                }
                gameplayState=std::move(nextGameplay);
                if(!EnsureBattlefieldActorInstances(scene,instanceMaxDrawDistances,gameplayState,
                        battlefieldRenderAssets,battlefieldActorBindings,error)) {
                    std::cerr<<"Campaign actor presentation update failed: "<<error<<"\\n"; break;
                }
                missionOverlay.contact.clear();
                campaignHasNextMission=!authoredMission.nextMissionId.empty();
                missionOverlay.title=Utf8ToWide(authoredMission.name);
                missionOverlay.description=Utf8ToWide(authoredMission.description);
                gameplayAccumulator=0.0;
                interactionQueued=false;
                if(authoredMission.id=="mission.sakkola.defensive-line")
                    SetMissionFeedback(L"SECTION LEADER: HOLD THE ROAD. WAIT FOR THE SIGNAL.",
                        std::chrono::milliseconds(5200));
                else
                    SetMissionFeedback(L"NEW ORDERS RECEIVED",std::chrono::milliseconds(3200));
                if(!RefreshMissionOverlay(gameplayState,playerState,gameplayRules.interactionRadiusMeters,error)) break;
            }
        }
        if(restartPatrolRequested) {
            talvisota::gameplay::SimulationState restartedGameplay;
            if(!talvisota::gameplay::InitializeSimulationFromAuthoredMission(
                    authoredMission,gameRegion,missionStartPlayerState.position,
                    gameplayRules,restartedGameplay,error) ||
                !PlaceMissionInteractionAnchors(gameRegion,restartedGameplay,
                    missionStartPlayerState.position,playerConfig.capsuleRadius,error)) {
                std::cerr<<"Could not restart mission: "<<error<<"\n";
                break;
            }
            gameplayState=std::move(restartedGameplay);
            if(!EnsureBattlefieldActorInstances(scene,instanceMaxDrawDistances,gameplayState,
                    battlefieldRenderAssets,battlefieldActorBindings,error)) {
                std::cerr<<"Restart actor presentation update failed: "<<error<<"\\n"; break;
            }
            missionOverlay.contact.clear();
            playerState=missionStartPlayerState;
            if(authoredMission.id=="mission.sakkola.defensive-line" &&
                !TriggerOpeningArtilleryFlash(explosionLighting,gameRegion,playerState,
                    scene.camera.elapsedSeconds,explosionSequence++,error)) {
                std::cerr<<"Could not schedule the opening artillery flash: "<<error<<"\\n"; break;
            }
            restartCameraRequested=true;
            gameplayAccumulator=0.0;
            interactionQueued=false;
            restartPatrolRequested=false;
            if(authoredMission.id=="mission.sakkola.defensive-line")
                SetMissionFeedback(L"SECTION LEADER: HOLD THE ROAD. WAIT FOR THE SIGNAL.",
                    std::chrono::milliseconds(5200));
            else
                SetMissionFeedback(L"MISSION RESTARTED",std::chrono::milliseconds(2200));
            if(!RefreshMissionOverlay(gameplayState,playerState,gameplayRules.interactionRadiusMeters,error)) break;
        }
        const auto now=Clock::now();
        bool cameraChanged=restartCameraRequested;
        restartCameraRequested=false;
        if(frontendState!=previousFrontendState) {
            rawMouseDeltaX=0; rawMouseDeltaY=0; jumpWasDown=false;
            interactWasDown=false; interactionQueued=false;
            fireWasDown=false; reloadWasDown=false; fireQueued=false; reloadQueued=false;
            weapon1WasDown=false; weapon2WasDown=false; weapon3WasDown=false; weaponSwitchQueued=0;
            pushObjectWasDown=false;
            gameplayAccumulator=0.0;
            previousFrame=now; previousFrontendState=frontendState;
            InvalidateRect(frontendOverlay,nullptr,FALSE);
        }
        const double frameDeltaMs =
            std::chrono::duration<double, std::milli>(now - previousFrame).count();
        const float deltaSeconds = std::clamp(
            static_cast<float>(frameDeltaMs * 0.001), 0.0F, 0.1F);
        previousFrame = now;
        if(frontendState==FrontendState::Playing &&
            gameplayState.mission.status==talvisota::missions::MissionStatus::Active) {
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
            const bool fireDown=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
            if(fireDown&&!fireWasDown) fireQueued=true;
            if(fireDown) {
                const auto heldWeaponState=gameplayState.combat.combatants.find(gameplayState.playerEntityId);
                const auto* heldWeapon=heldWeaponState==gameplayState.combat.combatants.end()?nullptr:
                    talvisota::combat::FindWeaponDefinition(heldWeaponState->second.weaponId);
                if(heldWeapon!=nullptr&&heldWeapon->archetype!=talvisota::combat::WeaponArchetype::BoltActionRifle&&
                    gameplayState.combat.elapsedSeconds>=heldWeaponState->second.nextShotAtSeconds)
                    fireQueued=true;
            }
            fireWasDown=fireDown;
            const bool reloadDown=(GetAsyncKeyState('R')&0x8000)!=0;
            if(reloadDown&&!reloadWasDown) reloadQueued=true;
            reloadWasDown=reloadDown;
            const bool weapon1Down=(GetAsyncKeyState('1')&0x8000)!=0;
            const bool weapon2Down=(GetAsyncKeyState('2')&0x8000)!=0;
            const bool weapon3Down=(GetAsyncKeyState('3')&0x8000)!=0;
            if(weapon1Down&&!weapon1WasDown) weaponSwitchQueued=1;
            if(weapon2Down&&!weapon2WasDown) weaponSwitchQueued=2;
            if(weapon3Down&&!weapon3WasDown) weaponSwitchQueued=3;
            weapon1WasDown=weapon1Down; weapon2WasDown=weapon2Down; weapon3WasDown=weapon3Down;
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
            // F applies a physical shove to a nearby movable world prop.
            const bool pushObjectDown=(GetAsyncKeyState('F')&0x8000)!=0;
            if(pushObjectDown&&!pushObjectWasDown) {
                const auto pushCamera=talvisota::game::BuildPlayerCamera(playerState,playerConfig);
                const talvisota::presentation::DynamicWorldObject* nearest=nullptr;
                float nearestDistanceSquared=2.5F*2.5F;
                for(const auto& object:dynamicWorld.Objects()) {
                    if(object.category==talvisota::world::PlacementCategory::Terrain ||
                        object.category==talvisota::world::PlacementCategory::Structure ||
                        object.integrity<=0.0F) continue;
                    const float dx=object.position[0]-playerState.position[0];
                    const float dz=object.position[2]-playerState.position[2];
                    const float distanceSquared=dx*dx+dz*dz;
                    if(distanceSquared<0.64F || distanceSquared>=nearestDistanceSquared) continue;
                    const float inverseDistance=1.0F/std::sqrt(distanceSquared);
                    const float facing=(dx*pushCamera.forward[0]+dz*pushCamera.forward[2])*inverseDistance;
                    if(facing<0.55F) continue;
                    nearest=&object;
                    nearestDistanceSquared=distanceSquared;
                }
                if(nearest!=nullptr) {
                    const float dx=nearest->position[0]-playerState.position[0];
                    const float dz=nearest->position[2]-playerState.position[2];
                    const float inverseDistance=1.0F/std::sqrt(dx*dx+dz*dz);
                    const std::array<float,3> impulse{
                        dx*inverseDistance*nearest->mass*1.25F,0.0F,
                        dz*inverseDistance*nearest->mass*1.25F};
                    const std::array<float,3> torque{
                        0.0F,(dx*pushCamera.forward[2]-dz*pushCamera.forward[0])*nearest->mass*0.12F,0.0F};
                    if(!dynamicWorld.ApplyImpulse(nearest->id,impulse,torque,error)) {
                        std::cerr<<"World prop interaction failed: "<<error<<"\\n"; break;
                    }
                    SetMissionFeedback(L"OBJECT MOVED",std::chrono::milliseconds(1000));
                } else {
                    SetMissionFeedback(L"NO MOVABLE OBJECT IN REACH",std::chrono::milliseconds(900));
                }
            }
            pushObjectWasDown=pushObjectDown;
            cameraChanged=playerState.position!=previousPosition ||
                playerInput.lookYawRadians!=0.0F || playerInput.lookPitchRadians!=0.0F ||
                playerState.crouched!=wasCrouched;
            const bool aiming=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
            const float desiredFov=aiming?38.0F:60.0F;
            const float aimBlend=1.0F-std::exp(-12.0F*deltaSeconds);
            const float nextFov=currentCameraFovDegrees+(desiredFov-currentCameraFovDegrees)*aimBlend;
            if(std::abs(nextFov-currentCameraFovDegrees)>0.02F) cameraChanged=true;
            currentCameraFovDegrees=nextFov;
            const auto contactCamera=talvisota::game::BuildPlayerCamera(playerState,playerConfig);
            const auto playerCombatantForContact=gameplayState.combat.combatants.find(gameplayState.playerEntityId);
            const auto* contactWeapon=playerCombatantForContact==gameplayState.combat.combatants.end()?nullptr:
                talvisota::combat::FindWeaponDefinition(playerCombatantForContact->second.weaponId);
            std::wstring contactText;
            if(contactWeapon!=nullptr) {
                std::string contactId;
                if(!talvisota::gameplay::FindNearestHostileCombatant(gameplayState,gameRegion,
                        gameplayState.playerEntityId,contactCamera.position,contactCamera.forward,
                        contactWeapon->effectiveRangeMeters,0.95F,contactId,error)) {
                    std::cerr<<"Battlefield contact query failed: "<<error<<"\\n"; break;
                }
                if(!contactId.empty()) {
                    const auto* contactEntity=gameplayState.entities.Find(contactId);
                    if(contactEntity!=nullptr) {
                        const double dx=static_cast<double>(contactEntity->position[0])-playerState.position[0];
                        const double dy=static_cast<double>(contactEntity->position[1])-playerState.position[1];
                        const double dz=static_cast<double>(contactEntity->position[2])-playerState.position[2];
                        const auto range=static_cast<long>(std::lround(std::sqrt(dx*dx+dy*dy+dz*dz)));
                        contactText=L"CONTACT  |  "+std::to_wstring(range)+L" M";
                    }
                }
            }
            if(contactText!=missionOverlay.contact) {
                missionOverlay.contact=std::move(contactText);
                if(frontendOverlay&&IsWindow(frontendOverlay)) InvalidateRect(frontendOverlay,nullptr,FALSE);
            }
            gameplayAccumulator+=static_cast<double>(deltaSeconds);
            while(gameplayAccumulator>=gameplayFixedStep) {
                if(weaponSwitchQueued>0) {
                    constexpr std::array<std::string_view,3> weaponIds{
                        "weapon.mosin_m27","weapon.suomi_kp31","weapon.lahti_saloranta_m26"};
                    const auto weaponIndex=static_cast<std::size_t>(weaponSwitchQueued-1);
                    if(!talvisota::combat::EquipCombatantWeapon(gameplayState.combat,
                            gameplayState.entities,gameplayState.playerEntityId,
                            weaponIds[weaponIndex],error)) {
                        std::cerr<<"Weapon switch failed: "<<error<<"\\n"; break;
                    }
                    const auto* equipped=talvisota::combat::FindWeaponDefinition(weaponIds[weaponIndex]);
                    if(equipped!=nullptr)
                        SetMissionFeedback(L"EQUIPPED  "+Utf8ToWide(equipped->displayName),
                            std::chrono::milliseconds(1000));
                    weaponSwitchQueued=0;
                }
                std::vector<talvisota::gameplay::CombatCommand> combatCommands;
                if(fireQueued) {
                    const auto playerCombatant=gameplayState.combat.combatants.find(gameplayState.playerEntityId);
                    const auto* weapon=playerCombatant==gameplayState.combat.combatants.end()?nullptr:
                        talvisota::combat::FindWeaponDefinition(playerCombatant->second.weaponId);
                    const auto aimCamera=talvisota::game::BuildPlayerCamera(playerState,playerConfig);
                    std::string targetEntityId;
                    if(weapon==nullptr || !talvisota::gameplay::FindNearestHostileCombatant(
                            gameplayState,gameRegion,gameplayState.playerEntityId,aimCamera.position,
                            aimCamera.forward,weapon->effectiveRangeMeters,0.95F,targetEntityId,error)) {
                        std::cerr<<"Combat target selection failed: "<<error<<"\\n"; break;
                    }
                    if(targetEntityId.empty())
                        SetMissionFeedback(L"NO HOSTILE TARGET IN SIGHT",std::chrono::milliseconds(900));
                    else
                        combatCommands.push_back({"combat.player.fire."+std::to_string(gameplayState.tickIndex+1U),
                            gameplayState.playerEntityId,targetEntityId,talvisota::combat::CombatAction::Fire});
                    fireQueued=false;
                }
                if(reloadQueued) {
                    combatCommands.push_back({"combat.player.reload."+std::to_string(gameplayState.tickIndex+1U),
                        gameplayState.playerEntityId,{},talvisota::combat::CombatAction::Reload});
                    reloadQueued=false;
                }
                std::vector<talvisota::combat::CombatCommand> aiCommands;
                if(!talvisota::gameplay::BuildBattlefieldAiCommands(gameplayState,aiCommands,error)) {
                    std::cerr<<"Battlefield AI failed: "<<error<<"\\n"; break;
                }
                combatCommands.insert(combatCommands.end(),aiCommands.begin(),aiCommands.end());
                const auto playerHealthBefore=gameplayState.combat.combatants.at(gameplayState.playerEntityId).health;
                std::vector<talvisota::combat::CombatResult> combatResults;
                if(!talvisota::combat::AdvanceCombat(gameplayState.combat,gameplayState.entities,
                        gameplayFixedStep,combatCommands,combatResults,error)) {
                    std::cerr<<"Combat simulation failed: "<<error<<"\\n"; break;
                }
                if(!combatCommands.empty()) {
                    std::vector<talvisota::gameplay::CommandResult> combatMissionResults;
                    if(!talvisota::gameplay::ApplyCombatMissionEvents(gameplayState,
                            gameplayState.combat,combatCommands,combatResults,combatMissionResults,error)) {
                        std::cerr<<"Combat mission integration failed: "<<error<<"\\n"; break;
                    }
                    for(std::size_t combatIndex=0U;combatIndex<combatResults.size();++combatIndex) {
                        const auto& result=combatResults[combatIndex];
                        const bool playerIssued=combatCommands[combatIndex].actorEntityId==gameplayState.playerEntityId;
                        if(playerIssued && result.outcome==talvisota::combat::CombatOutcome::TargetDefeated)
                            SetMissionFeedback(L"HOSTILE COMBATANT DEFEATED",std::chrono::milliseconds(1800));
                        else if(playerIssued && result.outcome==talvisota::combat::CombatOutcome::Fired)
                            SetMissionFeedback(L"SHOT FIRED",std::chrono::milliseconds(650));
                        else if(playerIssued && result.outcome==talvisota::combat::CombatOutcome::ReloadStarted)
                            SetMissionFeedback(L"RELOADING",std::chrono::milliseconds(1000));
                        else if(playerIssued && result.outcome==talvisota::combat::CombatOutcome::EmptyMagazine)
                            SetMissionFeedback(L"EMPTY MAGAZINE  |  PRESS R",std::chrono::milliseconds(1200));
                        else if(playerIssued && result.outcome==talvisota::combat::CombatOutcome::OutOfRange)
                            SetMissionFeedback(L"TARGET OUT OF RANGE",std::chrono::milliseconds(900));
                        if(combatMissionResults[combatIndex].outcome==talvisota::gameplay::CommandOutcome::ObjectiveCompleted ||
                            combatMissionResults[combatIndex].outcome==talvisota::gameplay::CommandOutcome::MissionCompleted)
                            SetMissionFeedback(L"MISSION OBJECTIVE COMPLETE",std::chrono::milliseconds(2200));
                    }
                }
                const auto playerHealthAfter=gameplayState.combat.combatants.at(gameplayState.playerEntityId).health;
                if(playerHealthAfter<playerHealthBefore)
                    SetMissionFeedback(L"INCOMING FIRE",std::chrono::milliseconds(850));
                if(playerHealthAfter<=0.0F) {
                    if(!talvisota::missions::FailMission(gameplayState.mission,error)) {
                        std::cerr<<"Could not fail defeated player mission: "<<error<<"\\n"; break;
                    }
                    SetMissionFeedback(L"SECTION OVERWHELMED",std::chrono::milliseconds(5000));
                }
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
                    if(result.outcome==talvisota::gameplay::CommandOutcome::ObjectiveCompleted) {
                        SetMissionFeedback(L"OBJECTIVE COMPLETE",std::chrono::milliseconds(2200));
                        const auto nextObjective=std::find_if(gameplayState.mission.objectives.begin(),
                            gameplayState.mission.objectives.end(),[](const auto& objective) {
                                return objective.status==talvisota::missions::ObjectiveStatus::Active;
                            });
                        if(nextObjective!=gameplayState.mission.objectives.end() &&
                            authoredMission.id=="mission.sakkola.first-patrol") {
                            if(nextObjective->id=="objective.inspect-well")
                                SetMissionFeedback(L"CORPORAL: CHECK THE WELL, THEN THE WOODPILE.",
                                    std::chrono::milliseconds(4200));
                            else if(nextObjective->id=="objective.inspect-woodpile")
                                SetMissionFeedback(L"CORPORAL: CHECK THE WOODPILE. REPORT TO THE CHURCH.",
                                    std::chrono::milliseconds(4200));
                            else if(nextObjective->id=="objective.report-church")
                                SetMissionFeedback(L"CORPORAL: GOOD. REPORT TO THE CHURCH POST.",
                                    std::chrono::milliseconds(4200));
                        } else if(nextObjective!=gameplayState.mission.objectives.end() &&
                            authoredMission.id=="mission.sakkola.defensive-line") {
                            if(nextObjective->id=="objective.check-section-leader")
                                SetMissionFeedback(L"SECTION LEADER: CHECK IN WITH ME BEFORE WE MOVE.",
                                    std::chrono::milliseconds(4200));
                            else if(nextObjective->id=="objective.stop-third-advance")
                                SetMissionFeedback(L"SECTION LEADER: EASTERN APPROACH. STAY WITH THE SECTION.",
                                    std::chrono::milliseconds(4200));
                            else if(nextObjective->id=="objective.stop-fourth-advance")
                                SetMissionFeedback(L"SECTION LEADER: THE ROAD BEYOND THE BARN. KEEP WATCH.",
                                    std::chrono::milliseconds(4200));
                        }
                    } else if(result.outcome==talvisota::gameplay::CommandOutcome::MissionCompleted)
                        SetMissionFeedback(L"MISSION COMPLETE",std::chrono::milliseconds(6000));
                    else if(result.outcome==talvisota::gameplay::CommandOutcome::OutOfRange)
                        SetMissionFeedback(L"MOVE CLOSER TO THE OBJECTIVE",std::chrono::milliseconds(1400));
                }
                gameplayAccumulator-=gameplayFixedStep;
            }
            if(!error.empty()) break;
            if(!RefreshMissionOverlay(gameplayState,playerState,gameplayRules.interactionRadiusMeters,error)) {
                std::cerr<<"Mission HUD update failed: "<<error<<"\n"; break;
            }
        } else {
            rawMouseDeltaX=0; rawMouseDeltaY=0; jumpWasDown=false;
            const bool interactDown=(GetAsyncKeyState('E')&0x8000)!=0;
            interactionQueued=false;
            fireWasDown=false; reloadWasDown=false; fireQueued=false; reloadQueued=false;
            weapon1WasDown=false; weapon2WasDown=false; weapon3WasDown=false; weaponSwitchQueued=0;
            if(frontendState==FrontendState::Playing &&
                gameplayState.mission.status==talvisota::missions::MissionStatus::Completed &&
                campaignHasNextMission && interactDown && !interactWasDown) {
                advanceCampaignRequested=true;
            }
            interactWasDown=interactDown;
            if(frontendState==FrontendState::Playing) {
                if(!RefreshMissionOverlay(gameplayState,playerState,gameplayRules.interactionRadiusMeters,error)) break;
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
            if (!SetCamera(scene, cameraPosition, cameraTarget, w, h,currentCameraFovDegrees)) {
                error = "First-person camera view/projection update failed.";
                break;
            }
            CullCaptureInstances(scene, meshCullRadii, instanceMaxDrawDistances,
                cameraPosition, cameraTarget, static_cast<float>(w) / static_cast<float>(h));
        }
        if(!SyncMissionActorsToScene(scene,instanceMaxDrawDistances,soldierRenderAssets,
                gameplayState,playerState,actorRenderInstances,error)) {
            std::cerr<<"Battlefield actor presentation failed: "<<error<<"\\n";
            break;
        }
        scene.camera.elapsedSeconds = std::chrono::duration<float>(now - start).count();
        scene.camera.frameNumber = frameNumber++;
        if(!dynamicWorld.Update(gameRegion,scene,deltaSeconds,scene.camera.elapsedSeconds,
                scene.environment.windDirection,scene.environment.windStrength,error)) {
            std::cerr<<"Sakkola dynamic world update failed: "<<error<<"\\n";
            break;
        }
        if(dynamicWorldPersistenceEnabled) {
            if(now-lastDynamicWorldSave>=std::chrono::seconds(15)) {
                if(!dynamicWorld.Save(dynamicWorldSavePath,error)) {
                    std::cerr<<"World-state save failed: "<<error<<"\\n";
                    break;
                }
                lastDynamicWorldSave=now;
            }
        }
        const std::array<float,3> snowfallCamera{
            cameraPosition.x,cameraPosition.y,cameraPosition.z};
        if(!snowfall.Update(scene,snowfallCamera,scene.camera.elapsedSeconds,
                scene.environment.windDirection,scene.environment.windStrength,
                authoredMission.environmentProfile!="default",error)) {
            std::cerr<<"Sakkola snowfall update failed: "<<error<<"\\n";
            break;
        }
        if(!explosionLighting.Update(scene,scene.camera.elapsedSeconds,error)) {
            std::cerr<<"Sakkola explosion lighting update failed: "<<error<<"\\n";
            break;
        }
        const auto presentationCamera=talvisota::game::BuildPlayerCamera(playerState,playerConfig);
        UpdateBattlefieldActorInstances(scene,gameplayState,battlefieldActorBindings,
            scene.camera.elapsedSeconds);
        UpdatePlayerWeaponInstance(scene,playerWeaponInstance,gameplayState,battlefieldRenderAssets,
            presentationCamera,scene.camera.elapsedSeconds);
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
    if(dynamicWorldPersistenceEnabled) {
        std::string saveError;
        if(!dynamicWorld.Save(dynamicWorldSavePath,saveError))
            std::cerr<<"Final world-state save failed: "<<saveError<<"\n";
    }
    renderer.reset(); frontendOverlay=nullptr; mainWindow=nullptr;
    if (IsWindow(window)) DestroyWindow(window);
    if (ownsCom) CoUninitialize();
    return error.empty() ? 0 : 9;
}
