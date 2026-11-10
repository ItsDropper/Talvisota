#include "talvisota/presentation/SakkolaSceneBuilder.hpp"
#include "talvisota/presentation/MaterialRenderBridge.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "vanta/assets/ObjLoader.hpp"
#include "vanta/assets/GltfLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <memory>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace talvisota::presentation
{
namespace
{
template<class Diagnostics>
std::string FormatDiagnostics(const Diagnostics& diagnostics) {
    std::string result;
    for (const auto& item : diagnostics)
        result += item.file.string() + ":" + std::to_string(item.line) + " [" +
            item.field + "] " + item.message + "\n";
    return result;
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

bool ResolveImportedTexture(const std::filesystem::path& source,
    const std::shared_ptr<const vanta::assets::CpuEmbeddedImage>& embedded,
    std::string& output, std::string& error) {
    if (!source.empty())
        return ResolveImportedTexture(source, output, error);
    if (!embedded) {
        output.clear();
        return true;
    }
    if (embedded->encodedBytes.empty()) {
        error = "Imported embedded material texture has no encoded image bytes.";
        return false;
    }

    std::string extension;
    if (embedded->mimeType == "image/png") extension = ".png";
    else if (embedded->mimeType == "image/jpeg") extension = ".jpg";
    else {
        error = "Unsupported embedded material texture MIME type: " + embedded->mimeType;
        return false;
    }

    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : embedded->encodedBytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    std::error_code ec;
    const auto cacheRoot = std::filesystem::temp_directory_path(ec) /
        "talvisota-embedded-model-textures";
    if (ec || !std::filesystem::create_directories(cacheRoot, ec) && ec) {
        error = "Could not create the embedded model texture cache: " + ec.message();
        return false;
    }
    const auto cachedPath = cacheRoot / (std::to_string(hash) + extension);
    const bool cachedFileMatches = std::filesystem::is_regular_file(cachedPath, ec) && !ec &&
        std::filesystem::file_size(cachedPath, ec) == embedded->encodedBytes.size() && !ec;
    if (!cachedFileMatches) {
        std::ofstream outputFile(cachedPath, std::ios::binary | std::ios::trunc);
        if (!outputFile || !outputFile.write(
                reinterpret_cast<const char*>(embedded->encodedBytes.data()),
                static_cast<std::streamsize>(embedded->encodedBytes.size()))) {
            error = "Could not write an embedded model texture to the runtime cache: " +
                cachedPath.string();
            return false;
        }
        outputFile.close();
        if (!outputFile) {
            error = "Could not finish writing an embedded model texture to the runtime cache: " +
                cachedPath.string();
            return false;
        }
    }

    const auto canonical = std::filesystem::weakly_canonical(cachedPath, ec);
    if (ec || !std::filesystem::is_regular_file(canonical, ec) || ec) {
        error = "Cached embedded model texture is missing or unreadable: " + cachedPath.string();
        return false;
    }
    output = canonical.string();
    error.clear();
    return true;
}

bool RefineSnowTerrain(RenderMesh& mesh, const talvisota::world::WorldRegion& region, std::string& error) {
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size()%3U!=0U) {
        error="Snow terrain refinement requires indexed triangle geometry."; return false;
    }
    constexpr std::size_t MaximumVertices=1000000U;
    const std::size_t originalVertexCount=mesh.vertices.size();
    // Keep the same one-million-vertex budget for both the original close-range
    // village mesh and the expanded regional mesh. Larger source grids use a
    // coarser final edge target so refinement stays bounded instead of failing
    // solely because the playable area is larger.
    const float maximumEdgeLength=originalVertexCount>20000U?17.0F:2.2F;
    const auto distanceSquared=[](const auto& a,const auto& b) {
        const float x=a.position[0]-b.position[0], y=a.position[1]-b.position[1], z=a.position[2]-b.position[2];
        return x*x+y*y+z*z;
    };
    const auto edgeKey=[](std::uint32_t a,std::uint32_t b) {
        if(a>b) std::swap(a,b);
        return (static_cast<std::uint64_t>(a)<<32U)|b;
    };
    for(int pass=0;pass<3;++pass) {
        float longest=0.0F;
        for(std::size_t i=0;i<mesh.indices.size();i+=3U) {
            const auto a=mesh.indices[i],b=mesh.indices[i+1U],d=mesh.indices[i+2U];
            if(a>=mesh.vertices.size()||b>=mesh.vertices.size()||d>=mesh.vertices.size()) {
                error="Snow terrain contains an out-of-range index before refinement."; return false;
            }
            longest=std::max({longest,distanceSquared(mesh.vertices[a],mesh.vertices[b]),
                distanceSquared(mesh.vertices[b],mesh.vertices[d]),distanceSquared(mesh.vertices[d],mesh.vertices[a])});
        }
        if(longest<=maximumEdgeLength*maximumEdgeLength) break;
        if(mesh.vertices.size()+mesh.indices.size()>MaximumVertices) {
            error="Snow terrain refinement exceeds the 1-million-vertex safety limit."; return false;
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
        vertex.position[1]+=talvisota::world::SampleTerrainDisplacement(region,vertex.position[0],vertex.position[2]);
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

} // namespace

bool BuildSakkolaScene(const std::filesystem::path& root, SceneSnapshot& scene,
    std::vector<float>& instanceMaxDrawDistances,
    std::size_t& placementCount, std::size_t& drawCount,
    talvisota::world::WorldRegion& outputRegion, std::string& error) {
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
    std::size_t importedSubmeshRunCount = 0;
    std::size_t mergedModelMaterialCount = 0;
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
            const std::string extension = modelPath.extension().string();
            const bool importedSuccessfully = extension == ".glb" || extension == ".gltf"
                ? vanta::assets::LoadGltf(modelPath, imported, importError)
                : vanta::assets::LoadWavefrontObj(modelPath, imported, importError);
            if (!importedSuccessfully) {
                error = "Could not import " + placement.modelAssetId + ": " + importError;
                return false;
            }
            importedSubmeshRunCount += imported.submeshes.size();
            if (imported.submeshes.empty() || imported.materials.empty()) {
                error = "Model has no materialized submeshes: " + placement.modelAssetId;
                return false;
            }
            // OBJ importers may emit one submesh for every contiguous material run.
            // Detailed authored trees/buildings contain hundreds of repeated runs per
            // material; turning each run into an instance multiplies CPU and GPU work.
            // Merge every run that uses the same material into one indexed mesh before
            // creating the model's reusable material-bound bindings.
            std::vector<RenderMesh> meshesByMaterial(imported.materials.size());
            std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> vertexRemaps(
                imported.materials.size());
            for (const auto& submesh : imported.submeshes) {
                if (submesh.materialIndex >= imported.materials.size() || submesh.indexCount == 0U ||
                    submesh.indexCount % 3U != 0U ||
                    static_cast<std::size_t>(submesh.firstIndex) + submesh.indexCount > imported.indices.size()) {
                    error = "Model has an invalid submesh/material range: " + placement.modelAssetId;
                    return false;
                }
                auto& mesh = meshesByMaterial[submesh.materialIndex];
                auto& vertexRemap = vertexRemaps[submesh.materialIndex];
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
                        if (mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
                            error = "Merged model material mesh exceeds the 32-bit vertex index limit: " +
                                placement.modelAssetId;
                            return false;
                        }
                        const auto& vertex = imported.vertices[sourceIndex];
                        const auto destinationIndex = static_cast<std::uint32_t>(mesh.vertices.size());
                        // Preserve asset-authored UVs. Terrain texture scale is owned by
                        // the OBJ/material content contract, not a renderer-side override.
                        mesh.vertices.push_back({{vertex.position.x, vertex.position.y, vertex.position.z},
                            {vertex.normal.x, vertex.normal.y, vertex.normal.z}, vertex.u, vertex.v});
                        remapped = vertexRemap.emplace(sourceIndex, destinationIndex).first;
                    }
                    mesh.indices.push_back(remapped->second);
                }
            }

            std::vector<SubmeshBinding> bindings;
            bindings.reserve(imported.materials.size());
            for (std::size_t materialSlot = 0; materialSlot < imported.materials.size(); ++materialSlot) {
                RenderMesh& mesh = meshesByMaterial[materialSlot];
                if (mesh.indices.empty())
                    continue;
                const auto& sourceMaterial = imported.materials[materialSlot];
                RenderMaterial material;
                material.baseColor = {sourceMaterial.baseColor.x, sourceMaterial.baseColor.y,
                    sourceMaterial.baseColor.z};
                material.roughness = sourceMaterial.roughness;
                material.metallic = sourceMaterial.metallic;
                if (!ResolveImportedTexture(sourceMaterial.diffuseTexture, sourceMaterial.embeddedDiffuseTexture, material.baseColorTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.normalTexture, sourceMaterial.embeddedNormalTexture, material.normalTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.roughnessTexture, sourceMaterial.embeddedRoughnessTexture, material.roughnessTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.metallicTexture, sourceMaterial.embeddedMetallicTexture, material.metallicTexture, error) ||
                    !ResolveImportedTexture(sourceMaterial.ambientOcclusionTexture, sourceMaterial.embeddedAmbientOcclusionTexture, material.ambientOcclusionTexture, error))
                    return false;

                if (!placement.materialAssetId.empty() && placement.materialAssetId != "-" &&
                    sourceMaterial.name == "snow") {
                    auto typed = typedMaterials.find(placement.materialAssetId);
                    if (typed == typedMaterials.end()) {
                        const auto* typedAsset = registry.Find(placement.materialAssetId);
                        if (typedAsset == nullptr || typedAsset->kind != talvisota::content::AssetKind::Material) {
                            error = "Placement " + placement.id + " references a missing typed material: " +
                                placement.materialAssetId;
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
                    if (!ApplyMaterialDefinition(typed->second, registry, material, error))
                        return false;
                }

                if (placement.modelAssetId == "model.terrain.sakkola" && sourceMaterial.name == "snow") {
                    material.terrain = true;
                    material.snowSurface = true;
                    if (!RefineSnowTerrain(mesh, region, error)) {
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
            if (bindings.empty()) {
                error = "Model import produced no material-bound geometry: " + placement.modelAssetId;
                return false;
            }
            mergedModelMaterialCount += bindings.size();
            model = importedModels.emplace(placement.modelAssetId, std::move(bindings)).first;
            ++importedModelCount;
        }
        auto transform = talvisota::world::BuildPlacementTransform(placement);
        if (placement.modelAssetId == "model.environment.scots_pine") {
            // Stable FNV-1a variation keeps the forest identical across machines
            // while giving every authored tree a distinct mature-height profile.
            std::uint64_t hash = 14695981039346656037ULL;
            for (const unsigned char byte : placement.id) {
                hash ^= byte;
                hash *= 1099511628211ULL;
            }
            const float heightVariation = 0.78F +
                static_cast<float>(hash % 10001ULL) / 10000.0F * 0.50F;
            transform[5] *= heightVariation;
        }
        // These meshes are authored in world coordinates, not placement-local
        // coordinates. Applying the placement's sampled ground elevation would
        // translate the entire road network vertically (the roads placement is
        // intentionally at the world origin).
        const bool worldSpaceGeometry =
            placement.modelAssetId == "model.terrain.sakkola" ||
            placement.modelAssetId == "model.environment.suvanto_ice" ||
            placement.modelAssetId == "model.environment.sakkola_roads";
        if (!worldSpaceGeometry) {
            float groundedY = placement.position[1];
            if (!talvisota::world::ResolveGroundedPlacementHeight(region,
                    placement.position[0], placement.position[2], placement.position[1],
                    groundedY, error)) {
                error = "Could not ground placement " + placement.id + ": " + error;
                return false;
            }
            transform[13] = groundedY;
        }
        for (const auto& binding : model->second) {
            RenderInstance instance;
            instance.meshIndex = binding.meshIndex;
            instance.materialIndex = binding.materialIndex;
            instance.world = transform;
            instance.placementId = placement.id;
            scene.instances.push_back(instance);
            float maxDrawDistance = 1200.0F;
            if (placement.modelAssetId == "model.environment.juniper" ||
                placement.modelAssetId == "model.environment.winter_brush") {
                maxDrawDistance = 280.0F;
            } else switch (placement.category) {
            case talvisota::world::PlacementCategory::Vegetation:
                maxDrawDistance = 280.0F;
                break;
            case talvisota::world::PlacementCategory::Prop:
                maxDrawDistance = 600.0F;
                break;
            case talvisota::world::PlacementCategory::Rock:
                maxDrawDistance = 720.0F;
                break;
            case talvisota::world::PlacementCategory::Structure:
                maxDrawDistance = 1200.0F;
                break;
            case talvisota::world::PlacementCategory::Terrain:
                maxDrawDistance = 1200.0F;
                break;
            }
            instanceMaxDrawDistances.push_back(maxDrawDistance);
        }
    }
    placementCount = region.placements.size();
    drawCount = scene.instances.size();
    if (instanceMaxDrawDistances.size() != scene.instances.size()) {
        error = "Per-instance draw-distance metadata does not match the submitted scene.";
        return false;
    }
    for (const float distance : instanceMaxDrawDistances) {
        if (!std::isfinite(distance) || distance <= 0.0F) {
            error = "Scene contains an invalid per-instance draw distance.";
            return false;
        }
    }
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
        << importedModelCount << " distinct OBJ models, " << importedSubmeshRunCount
        << " contiguous OBJ material runs merged into " << mergedModelMaterialCount
        << " model-material meshes, " << drawCount << " material-bound instances; "
        << uniqueVertices << " unique mesh vertices, " << expandedVertices
        << " expanded instance vertices, " << uniqueGeometryBytes
        << " bytes unique mesh data, estimated " << expandedGeometryBytes
        << " bytes expanded draw data.\n";
    outputRegion = std::move(region);
    return true;
}


} // namespace talvisota::presentation
