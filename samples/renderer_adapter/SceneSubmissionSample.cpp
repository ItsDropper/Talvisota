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


float TerrainHash(const int x, const int z) noexcept {
    std::uint32_t value = 0x9e3779b9U ^
        (static_cast<std::uint32_t>(x) * 0x85ebca6bU) ^
        (static_cast<std::uint32_t>(z) * 0xc2b2ae35U);
    value ^= value >> 16U; value *= 0x7feb352dU;
    value ^= value >> 15U; value *= 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00ffffffU) / 16777215.0F;
}
float TerrainValueNoise(const float x, const float z) noexcept {
    const int x0 = static_cast<int>(std::floor(x)), z0 = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(x0), fz = z - static_cast<float>(z0);
    const float tx = fx * fx * (3.0F - 2.0F * fx), tz = fz * fz * (3.0F - 2.0F * fz);
    const float a = TerrainHash(x0, z0), b = TerrainHash(x0 + 1, z0);
    const float d = TerrainHash(x0, z0 + 1), e = TerrainHash(x0 + 1, z0 + 1);
    return (a + (b-a)*tx) + ((d + (e-d)*tx) - (a + (b-a)*tx))*tz;
}
float TerrainFbm(const float x, const float z) noexcept {
    float amplitude = 0.5F, total = 0.0F, normalization = 0.0F, frequency = 1.0F;
    for (int octave = 0; octave < 4; ++octave) {
        total += TerrainValueNoise(x * frequency, z * frequency) * amplitude;
        normalization += amplitude; amplitude *= 0.5F; frequency *= 2.03F;
    }
    return total / normalization;
}
float SnowDriftDisplacement(const float x, const float z) noexcept {
    const float warpX = TerrainValueNoise(x*0.018F+17.3F,z*0.018F-8.1F)-0.5F;
    const float warpZ = TerrainValueNoise(x*0.018F-31.7F,z*0.018F+22.9F)-0.5F;
    const float broad = TerrainFbm((x+warpX*8.0F)*0.038F,(z+warpZ*8.0F)*0.038F);
    const float drift = TerrainFbm((x+warpX*2.2F)*0.145F+9.1F,(z+warpZ*2.2F)*0.145F-13.7F);
    const float granular = TerrainFbm(x*0.58F+4.7F,z*0.58F+18.2F);
    return (broad-0.5F)*0.22F+(drift-0.5F)*0.075F+(granular-0.5F)*0.018F;
}
bool RefineSnowTerrain(RenderMesh& mesh, std::string& error) {
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size()%3U!=0U) {
        error="Snow terrain refinement requires indexed triangle geometry."; return false;
    }
    constexpr std::size_t MaximumVertices=1800000U;
    constexpr float MaximumEdgeLength=1.35F;
    const std::size_t originalVertexCount=mesh.vertices.size();
    const auto distanceSquared=[](const auto& a,const auto& b) {
        const float x=a.position[0]-b.position[0], y=a.position[1]-b.position[1], z=a.position[2]-b.position[2];
        return x*x+y*y+z*z;
    };
    const auto edgeKey=[](std::uint32_t a,std::uint32_t b) {
        if(a>b) std::swap(a,b);
        return (static_cast<std::uint64_t>(a)<<32U)|b;
    };
    for(int pass=0;pass<4;++pass) {
        float longest=0.0F;
        for(std::size_t i=0;i<mesh.indices.size();i+=3U) {
            const auto a=mesh.indices[i],b=mesh.indices[i+1U],d=mesh.indices[i+2U];
            if(a>=mesh.vertices.size()||b>=mesh.vertices.size()||d>=mesh.vertices.size()) {
                error="Snow terrain contains an out-of-range index before refinement."; return false;
            }
            longest=std::max({longest,distanceSquared(mesh.vertices[a],mesh.vertices[b]),
                distanceSquared(mesh.vertices[b],mesh.vertices[d]),distanceSquared(mesh.vertices[d],mesh.vertices[a])});
        }
        if(longest<=MaximumEdgeLength*MaximumEdgeLength) break;
        if(mesh.vertices.size()+mesh.indices.size()>MaximumVertices) {
            error="Snow terrain refinement exceeds the 1.8-million-vertex safety limit."; return false;
        }
        auto vertices=mesh.vertices;
        std::vector<std::uint32_t> indices;
        indices.reserve(mesh.indices.size()*4U);
        std::unordered_map<std::uint64_t,std::uint32_t> cache;
        cache.reserve(mesh.indices.size());
        const auto midpoint=[&](std::uint32_t a,std::uint32_t b) {
            const auto key=edgeKey(a,b);
            const auto found=cache.find(key);
            if(found!=cache.end()) return found->second;
            const auto& va=vertices[a]; const auto& vb=vertices[b];
            talvisota::presentation::RenderVertex v;
            for(std::size_t axis=0;axis<3U;++axis) {
                v.position[axis]=(va.position[axis]+vb.position[axis])*0.5F;
                v.normal[axis]=(va.normal[axis]+vb.normal[axis])*0.5F;
            }
            const float n=std::sqrt(v.normal[0]*v.normal[0]+v.normal[1]*v.normal[1]+v.normal[2]*v.normal[2]);
            if(n>1.0e-8F) for(float& component:v.normal) component/=n;
            else v.normal={0.0F,1.0F,0.0F};
            v.u=(va.u+vb.u)*0.5F; v.v=(va.v+vb.v)*0.5F;
            const auto index=static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(v); cache.emplace(key,index); return index;
        };
        for(std::size_t i=0;i<mesh.indices.size();i+=3U) {
            const auto a=mesh.indices[i],b=mesh.indices[i+1U],d=mesh.indices[i+2U];
            const auto ab=midpoint(a,b),bd=midpoint(b,d),da=midpoint(d,a);
            indices.insert(indices.end(),{a,ab,da,ab,b,bd,da,bd,d,ab,bd,da});
        }
        mesh.vertices.swap(vertices); mesh.indices.swap(indices);
    }
    for(auto& vertex:mesh.vertices)
        vertex.position[1]+=SnowDriftDisplacement(vertex.position[0],vertex.position[2]);
    std::vector<std::array<float,3>> normals(mesh.vertices.size(),{0.0F,0.0F,0.0F});
    for(std::size_t i=0;i<mesh.indices.size();i+=3U) {
        const auto ia=mesh.indices[i],ib=mesh.indices[i+1U],ic=mesh.indices[i+2U];
        const auto& a=mesh.vertices[ia].position; const auto& b=mesh.vertices[ib].position; const auto& d=mesh.vertices[ic].position;
        const std::array<float,3> ab{b[0]-a[0],b[1]-a[1],b[2]-a[2]},ad{d[0]-a[0],d[1]-a[1],d[2]-a[2]};
        std::array<float,3> face{ab[1]*ad[2]-ab[2]*ad[1],ab[2]*ad[0]-ab[0]*ad[2],ab[0]*ad[1]-ab[1]*ad[0]};
        const auto& reference=mesh.vertices[ia].normal;
        if(face[0]*reference[0]+face[1]*reference[1]+face[2]*reference[2]<0.0F)
            for(float& component:face) component=-component;
        for(const auto index:{ia,ib,ic}) for(std::size_t axis=0;axis<3U;++axis) normals[index][axis]+=face[axis];
    }
    for(std::size_t i=0;i<mesh.vertices.size();++i) {
        const float n=std::sqrt(normals[i][0]*normals[i][0]+normals[i][1]*normals[i][1]+normals[i][2]*normals[i][2]);
        if(!std::isfinite(n)||n<=1.0e-8F) { error="Snow terrain refinement produced a degenerate normal."; return false; }
        mesh.vertices[i].normal={normals[i][0]/n,normals[i][1]/n,normals[i][2]/n};
    }
    std::cout<<"Snow terrain geometry: "<<originalVertexCount<<" -> "<<mesh.vertices.size()
        <<" vertices, "<<mesh.indices.size()/3U<<" triangles after adaptive subdivision and drift displacement.\n";
    error.clear(); return true;
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
                mesh.vertices.reserve(submesh.indexCount);
                mesh.indices.reserve(submesh.indexCount);
                std::unordered_map<std::uint32_t, std::uint32_t> vertexRemap;
                vertexRemap.reserve(submesh.indexCount);
                const auto first = imported.indices.begin() + submesh.firstIndex;
                for (std::uint32_t indexOffset = 0U; indexOffset < submesh.indexCount; ++indexOffset) {
                    const std::uint32_t sourceIndex = *(first + indexOffset);
                    if (sourceIndex >= imported.vertices.size()) {
                        error = "Model submesh references a vertex outside its source vertex buffer: " +
                            placement.modelAssetId;
                        return false;
                    }
                    auto remapped = vertexRemap.find(sourceIndex);
                    if (remapped == vertexRemap.end()) {
                        const auto& vertex = imported.vertices[sourceIndex];
                        const auto destinationIndex = static_cast<std::uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back({{vertex.position.x, vertex.position.y, vertex.position.z},
                            {vertex.normal.x, vertex.normal.y, vertex.normal.z}, vertex.u, vertex.v});
                        remapped = vertexRemap.emplace(sourceIndex, destinationIndex).first;
                    }
                    mesh.indices.push_back(remapped->second);
                }

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

                if (!placement.materialAssetId.empty() && placement.materialAssetId != "-" && sourceMaterial.name == "snow") {
                    auto typed = typedMaterials.find(placement.materialAssetId);
                    if (typed == typedMaterials.end()) {
                        const auto* typedAsset = registry.Find(placement.materialAssetId);
                        if (typedAsset == nullptr || typedAsset->kind != talvisota::content::AssetKind::Material) {
                            error = "Placement " + placement.id + " references a missing typed material: " + placement.materialAssetId;
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

                if (placement.modelAssetId == "model.terrain.sakkola" && sourceMaterial.name == "snow") {
                    if (!RefineSnowTerrain(mesh, error)) {
                        error = "Could not refine Sakkola snow terrain: " + error;
                        return false;
                    }
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
        auto transform = talvisota::world::BuildPlacementTransform(placement);
        if (placement.modelAssetId != "model.terrain.sakkola" &&
            placement.modelAssetId != "model.environment.suvanto_ice") {
            transform.elements[13] += SnowDriftDisplacement(placement.position[0], placement.position[2]);
        }
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

    std::uint64_t uniqueVertices = 0U;
    std::uint64_t uniqueIndices = 0U;
    std::uint64_t expandedVertices = 0U;
    std::uint64_t expandedIndices = 0U;
    for (const auto& mesh : scene.meshes) {
        uniqueVertices += mesh.vertices.size();
        uniqueIndices += mesh.indices.size();
    }
    for (const auto& instance : scene.instances) {
        const auto& mesh = scene.meshes[instance.meshIndex];
        expandedVertices += mesh.vertices.size();
        expandedIndices += mesh.indices.size();
    }
    const std::uint64_t uniqueGeometryBytes =
        uniqueVertices * sizeof(talvisota::presentation::RenderVertex) +
        uniqueIndices * sizeof(std::uint32_t);
    const std::uint64_t expandedGeometryBytes =
        expandedVertices * sizeof(talvisota::presentation::RenderVertex) +
        expandedIndices * sizeof(std::uint32_t);
    std::cout << "Loaded " << region.id << ": " << placementCount << " placements, "
        << importedModelCount << " distinct OBJ models, " << scene.meshes.size()
        << " submeshes, " << drawCount << " material-bound draw instances; "
        << uniqueVertices << " unique mesh vertices, " << expandedVertices
        << " expanded instance vertices, " << uniqueGeometryBytes
        << " bytes unique mesh data, estimated " << expandedGeometryBytes
        << " bytes expanded draw data.\n";
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
    std::size_t placements = 0, draws = 0;
    std::string error;
    if (!BuildSakkolaScene(std::filesystem::current_path(), scene, placements, draws, error)) {
        std::cerr << error << "\n";
        if (captureLog) captureLog << "Scene build failed:\n" << error << "\n";
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
            if (framebuffer[skyPixel] <= framebuffer[skyPixel + 2U] + 16U) {
                error = "Presented BGRA sky sample is not blue-dominant; framebuffer channel order or sky environment is incorrect.";
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
                << ", FNV1a=0x" << std::hex << frameHash << std::dec << "\\n";
            if (!WriteBgraPng(framebuffer, framebufferWidth, framebufferHeight,
                    output / view.file, error)) break;
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
