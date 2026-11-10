#include "talvisota/presentation/SakkolaSceneBuilder.hpp"
#include "talvisota/presentation/MaterialRenderBridge.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "vanta/assets/ObjLoader.hpp"
#include "vanta/assets/GltfLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <memory>
#include <limits>
#include <map>
#include <string>
#include <string_view>
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
    if (ec || (!std::filesystem::create_directories(cacheRoot, ec) && ec)) {
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

bool AreRenderMaterialsEquivalent(const RenderMaterial& lhs, const RenderMaterial& rhs)
{
    return lhs.baseColor == rhs.baseColor &&
        lhs.roughness == rhs.roughness &&
        lhs.metallic == rhs.metallic &&
        lhs.emissive == rhs.emissive &&
        lhs.normalScale == rhs.normalScale &&
        lhs.occlusionStrength == rhs.occlusionStrength &&
        lhs.alphaCutoff == rhs.alphaCutoff &&
        lhs.alphaMode == rhs.alphaMode &&
        lhs.doubleSided == rhs.doubleSided &&
        lhs.baseColorTexture == rhs.baseColorTexture &&
        lhs.normalTexture == rhs.normalTexture &&
        lhs.roughnessTexture == rhs.roughnessTexture &&
        lhs.metallicTexture == rhs.metallicTexture &&
        lhs.ambientOcclusionTexture == rhs.ambientOcclusionTexture &&
        lhs.emissiveTexture == rhs.emissiveTexture &&
        lhs.packedMetallicRoughness == rhs.packedMetallicRoughness &&
        lhs.terrain == rhs.terrain &&
        lhs.snowSurface == rhs.snowSurface;
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
struct ModelLodSource { std::string assetId; float screenCoverageHint=0.0F; bool optional=false; };
struct ModelLodBindings { float minimumScreenCoverage=0.0F; std::map<std::size_t,SubmeshBinding> materials; };
struct ModelBindings { std::vector<ModelLodBindings> lods; std::array<float,3> referenceCenter{}; float referenceRadius=0.0F; std::size_t materialSlotCount=0U; };
std::vector<ModelLodSource> GetModelLodSources(const std::string_view modelAssetId)
{
    if(modelAssetId=="model.environment.scots_pine") return {
        {std::string(modelAssetId),0.35F,false},
        {"model.environment.scots_pine_lod1",0.12F,true},
        {"model.environment.scots_pine_lod2",0.035F,true},
        {"model.environment.scots_pine_lod3",0.0F,true}};
    if(modelAssetId=="model.environment.boulder") return {
        {std::string(modelAssetId),0.30F,false},
        {"model.environment.boulder_lod1",0.10F,true},
        {"model.environment.boulder_lod2",0.025F,true},
        {"model.environment.boulder_lod3",0.0F,true}};
    return {{std::string(modelAssetId),0.0F,false}};
}

} // namespace

bool BuildWorldScene(const std::filesystem::path& root, const std::string_view worldId,
    const std::string_view regionId, SceneSnapshot& scene,
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
    const auto* worldAsset = registry.Find(worldId);
    if (worldAsset == nullptr || worldAsset->kind != talvisota::content::AssetKind::Definition ||
        worldAsset->relativePath.extension() != ".tworld") {
        error = "World scene references an unregistered world definition: " + std::string(worldId);
        return false;
    }
    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    if (!talvisota::content::LoadWorldDefinition(root / worldAsset->relativePath,
            registry, world, worldDiagnostics)) {
        error = "World definition failed validation:\n" + FormatDiagnostics(worldDiagnostics);
        return false;
    }
    const auto* regionAsset = registry.Find(regionId);
    if (regionAsset == nullptr || regionAsset->kind != talvisota::content::AssetKind::Definition ||
        regionAsset->relativePath.extension() != ".tregion") {
        error = "World scene references an unregistered region definition: " + std::string(regionId);
        return false;
    }
    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
    if (!talvisota::world::LoadWorldRegion(root / regionAsset->relativePath,
            registry, world, region, regionDiagnostics)) {
        error = "World region failed validation:\n" + FormatDiagnostics(regionDiagnostics);
        return false;
    }

    std::map<std::string, ModelBindings> importedModels;
    std::map<std::string, talvisota::content::MaterialDefinition> typedMaterials;
    std::size_t importedModelCount=0U, importedSubmeshRunCount=0U, mergedModelMaterialCount=0U;
    for(const auto& placement:region.placements) {
        auto model=importedModels.find(placement.modelAssetId);
        if(model==importedModels.end()) {
            ModelBindings modelBindings;
            for(const auto& lodSource:GetModelLodSources(placement.modelAssetId)) {
                const auto* modelAsset=registry.Find(lodSource.assetId);
                if(modelAsset==nullptr) {
                    if(lodSource.optional)break;
                    error="Placement references a missing model asset: "+lodSource.assetId;return false;
                }
                if(modelAsset->kind!=talvisota::content::AssetKind::Model) {
                    error="Placement LOD references a non-model asset: "+lodSource.assetId;return false;
                }
                const auto modelPath=registry.Root()/modelAsset->relativePath;
                std::error_code fileError;
                bool filePresent=std::filesystem::is_regular_file(modelPath,fileError);
                if(fileError==std::errc::no_such_file_or_directory||fileError==std::errc::not_a_directory) {
                    fileError.clear();filePresent=false;
                }
                if(fileError){error="Could not inspect model LOD file "+lodSource.assetId+": "+fileError.message();return false;}
                if(!filePresent) {
                    if(lodSource.optional)break;
                    error="Required model LOD file is missing: "+lodSource.assetId;return false;
                }
                vanta::assets::CpuModel imported;
                std::string importError;
                const std::string extension=modelPath.extension().string();
                const bool importedOk=extension==".glb"||extension==".gltf"
                    ? vanta::assets::LoadGltf(modelPath,imported,importError)
                    : vanta::assets::LoadWavefrontObj(modelPath,imported,importError);
                if(!importedOk){error="Could not import "+lodSource.assetId+": "+importError;return false;}
                importedSubmeshRunCount+=imported.submeshes.size();
                if(imported.submeshes.empty()||imported.materials.empty()) {
                    error="Model LOD has no materialized submeshes: "+lodSource.assetId;return false;
                }
                if(!modelBindings.lods.empty()&&imported.materials.size()!=modelBindings.materialSlotCount) {
                    error="Model LOD material-slot count differs from LOD0: "+lodSource.assetId;return false;
                }
                if(modelBindings.lods.empty())modelBindings.materialSlotCount=imported.materials.size();
                std::vector<RenderMesh> meshesByMaterial(imported.materials.size());
                std::vector<std::unordered_map<std::uint32_t,std::uint32_t>> remaps(imported.materials.size());
                for(const auto& submesh:imported.submeshes) {
                    if(submesh.materialIndex>=imported.materials.size()||submesh.indexCount==0U||
                        submesh.indexCount%3U!=0U||
                        static_cast<std::size_t>(submesh.firstIndex)+submesh.indexCount>imported.indices.size()) {
                        error="Model LOD has an invalid submesh/material range: "+lodSource.assetId;return false;
                    }
                    auto& mesh=meshesByMaterial[submesh.materialIndex];
                    auto& remap=remaps[submesh.materialIndex];
                    const auto first=imported.indices.begin()+submesh.firstIndex;
                    for(std::uint32_t offset=0U;offset<submesh.indexCount;++offset) {
                        const std::uint32_t sourceIndex=*(first+offset);
                        if(sourceIndex>=imported.vertices.size()) {
                            error="Model LOD submesh references a vertex outside its source buffer: "+lodSource.assetId;return false;
                        }
                        auto found=remap.find(sourceIndex);
                        if(found==remap.end()) {
                            if(mesh.vertices.size()>=std::numeric_limits<std::uint32_t>::max()) {
                                error="Merged model LOD material mesh exceeds the 32-bit vertex index limit: "+lodSource.assetId;return false;
                            }
                            const auto& vertex=imported.vertices[sourceIndex];
                            const auto destination=static_cast<std::uint32_t>(mesh.vertices.size());
                            mesh.vertices.push_back({{vertex.position.x,vertex.position.y,vertex.position.z},
                                {vertex.normal.x,vertex.normal.y,vertex.normal.z},vertex.u,vertex.v});
                            found=remap.emplace(sourceIndex,destination).first;
                        }
                        mesh.indices.push_back(found->second);
                    }
                }
                ModelLodBindings lodBindings;
                lodBindings.minimumScreenCoverage=lodSource.screenCoverageHint;
                for(std::size_t slot=0U;slot<imported.materials.size();++slot) {
                    RenderMesh& mesh=meshesByMaterial[slot];
                    if(mesh.indices.empty())continue;
                    const auto& sourceMaterial=imported.materials[slot];
                    RenderMaterial material;
                    material.baseColor={sourceMaterial.baseColor.x,sourceMaterial.baseColor.y,sourceMaterial.baseColor.z};
                    material.roughness=sourceMaterial.roughness;
                    material.metallic=sourceMaterial.metallic;
                    material.packedMetallicRoughness=sourceMaterial.packedMetallicRoughness;
                    if(!ResolveImportedTexture(sourceMaterial.diffuseTexture,sourceMaterial.embeddedDiffuseTexture,material.baseColorTexture,error)||
                        !ResolveImportedTexture(sourceMaterial.normalTexture,sourceMaterial.embeddedNormalTexture,material.normalTexture,error)||
                        !ResolveImportedTexture(sourceMaterial.roughnessTexture,sourceMaterial.embeddedRoughnessTexture,material.roughnessTexture,error)||
                        !ResolveImportedTexture(sourceMaterial.metallicTexture,sourceMaterial.embeddedMetallicTexture,material.metallicTexture,error)||
                        !ResolveImportedTexture(sourceMaterial.ambientOcclusionTexture,sourceMaterial.embeddedAmbientOcclusionTexture,material.ambientOcclusionTexture,error))return false;
                    if(!placement.materialAssetId.empty()&&placement.materialAssetId!="-"&&sourceMaterial.name=="snow") {
                        auto typed=typedMaterials.find(placement.materialAssetId);
                        if(typed==typedMaterials.end()) {
                            const auto* typedAsset=registry.Find(placement.materialAssetId);
                            if(typedAsset==nullptr||typedAsset->kind!=talvisota::content::AssetKind::Material) {
                                error="Placement "+placement.id+" references a missing typed material: "+placement.materialAssetId;return false;
                            }
                            talvisota::content::MaterialDefinition definition;
                            std::vector<talvisota::content::MaterialDiagnostic> diagnostics;
                            if(!talvisota::content::LoadMaterialDefinition(registry.Root()/typedAsset->relativePath,registry,definition,diagnostics)) {
                                error="Typed material failed validation:\n"+FormatDiagnostics(diagnostics);return false;
                            }
                            typed=typedMaterials.emplace(placement.materialAssetId,std::move(definition)).first;
                        }
                        if(!ApplyMaterialDefinition(typed->second,registry,material,error))return false;
                    }
                    if(placement.modelAssetId==world.terrainModelId&&sourceMaterial.name=="snow") {
                        material.terrain=true;material.snowSurface=true;
                        if(!RefineSnowTerrain(mesh,region,error)){error="Could not refine Sakkola snow terrain: "+error;return false;}
                    }
                    const auto meshIndex=static_cast<std::uint32_t>(scene.meshes.size());
                    std::uint32_t materialIndex=0U;
                    if(!modelBindings.lods.empty()) {
                        const auto baseMaterial=modelBindings.lods.front().materials.find(slot);
                        if(baseMaterial!=modelBindings.lods.front().materials.end()&&
                            AreRenderMaterialsEquivalent(scene.materials[baseMaterial->second.materialIndex],material)) {
                            materialIndex=baseMaterial->second.materialIndex;
                        } else {
                            materialIndex=static_cast<std::uint32_t>(scene.materials.size());
                            scene.materials.push_back(std::move(material));
                        }
                    } else {
                        materialIndex=static_cast<std::uint32_t>(scene.materials.size());
                        scene.materials.push_back(std::move(material));
                    }
                    scene.meshes.push_back(std::move(mesh));
                    if(!lodBindings.materials.emplace(slot,SubmeshBinding{meshIndex,materialIndex}).second) {
                        error="Model LOD contains a duplicate material slot: "+lodSource.assetId+
                            " (slot "+std::to_string(slot)+")";return false;
                    }
                }
                if(lodBindings.materials.empty()){error="Model LOD produced no material-bound geometry: "+lodSource.assetId;return false;}
                if(!modelBindings.lods.empty()) {
                    const auto& base=modelBindings.lods.front().materials;
                    for(const auto& [slot,binding]:lodBindings.materials) {
                        (void)binding;
                        if(!base.contains(slot)) {
                            error="Model LOD introduces a material slot absent from LOD0: "+lodSource.assetId+
                                " (slot "+std::to_string(slot)+")";return false;
                        }
                    }
                }
                mergedModelMaterialCount+=lodBindings.materials.size();
                modelBindings.lods.push_back(std::move(lodBindings));
                ++importedModelCount;
            }
            if(modelBindings.lods.empty()){error="Model import produced no usable LOD geometry: "+placement.modelAssetId;return false;}
            if(modelBindings.lods.size()>1U) {
                modelBindings.lods.back().minimumScreenCoverage=0.0F;
                std::array<float,3> boundsMin{
                    std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
                std::array<float,3> boundsMax{
                    std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()};
                for(const auto& [materialSlot,binding]:modelBindings.lods.front().materials) {
                    (void)materialSlot;
                    for(const auto& vertex:scene.meshes[binding.meshIndex].vertices)
                        for(std::size_t axis=0U;axis<3U;++axis) {
                            boundsMin[axis]=std::min(boundsMin[axis],vertex.position[axis]);
                            boundsMax[axis]=std::max(boundsMax[axis],vertex.position[axis]);
                        }
                }
                modelBindings.referenceCenter={(boundsMin[0]+boundsMax[0])*0.5F,
                    (boundsMin[1]+boundsMax[1])*0.5F,(boundsMin[2]+boundsMax[2])*0.5F};
                const float dx=boundsMax[0]-boundsMin[0],dy=boundsMax[1]-boundsMin[1],dz=boundsMax[2]-boundsMin[2];
                modelBindings.referenceRadius=0.5F*std::sqrt(dx*dx+dy*dy+dz*dz);
                if(!std::isfinite(modelBindings.referenceRadius)||modelBindings.referenceRadius<=1.0e-5F) {
                    error="Model LOD0 has invalid bounds for screen-space LOD selection: "+placement.modelAssetId;return false;
                }
            }
            model=importedModels.emplace(placement.modelAssetId,std::move(modelBindings)).first;
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
            placement.modelAssetId == world.terrainModelId ||
            placement.modelAssetId == "model.environment.suvanto_ice" ||
            placement.modelAssetId == "model.environment.taipale_ice" ||
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
        for(const auto& [materialSlot,baseBinding]:model->second.lods.front().materials) {
            RenderInstance instance;
            instance.meshIndex=baseBinding.meshIndex;instance.materialIndex=baseBinding.materialIndex;
            if(model->second.lods.size()>1U) {
                instance.lodCount=static_cast<std::uint8_t>(model->second.lods.size());
                instance.lodReferenceCenter=model->second.referenceCenter;
                instance.lodReferenceRadius=model->second.referenceRadius;
                for(std::size_t lodIndex=0U;lodIndex<model->second.lods.size();++lodIndex) {
                    const auto& lod=model->second.lods[lodIndex];
                    const auto found=lod.materials.find(materialSlot);
                    const SubmeshBinding& selected=found!=lod.materials.end()?found->second:baseBinding;
                    instance.lodVariants[lodIndex]={selected.meshIndex,selected.materialIndex,lod.minimumScreenCoverage};
                }
            }
            instance.world=transform;instance.placementId=placement.id;scene.instances.push_back(instance);
            // World-space terrain, ice and roads retain their extended draw distance.
            float maxDrawDistance=1200.0F;
            if(placement.modelAssetId==world.terrainModelId||
                placement.modelAssetId=="model.environment.suvanto_ice"||
                placement.modelAssetId=="model.environment.taipale_ice"||
                placement.modelAssetId=="model.environment.sakkola_roads")maxDrawDistance=6000.0F;
            else if(placement.modelAssetId=="model.environment.juniper"||
                placement.modelAssetId=="model.environment.winter_brush")maxDrawDistance=280.0F;
            else switch(placement.category) {
            case talvisota::world::PlacementCategory::Vegetation:maxDrawDistance=280.0F;break;
            case talvisota::world::PlacementCategory::Prop:maxDrawDistance=600.0F;break;
            case talvisota::world::PlacementCategory::Rock:maxDrawDistance=720.0F;break;
            case talvisota::world::PlacementCategory::Structure:maxDrawDistance=1200.0F;break;
            case talvisota::world::PlacementCategory::Terrain:maxDrawDistance=1200.0F;break;
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
        error = "World scene import produced no renderable world content.";
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
        << importedModelCount << " distinct environment model/LOD files, " << importedSubmeshRunCount
        << " imported submesh runs merged into " << mergedModelMaterialCount
        << " model-material meshes, " << drawCount << " material-bound instances; "
        << uniqueVertices << " unique mesh vertices, " << expandedVertices
        << " expanded instance vertices, " << uniqueGeometryBytes
        << " bytes unique mesh data, estimated " << expandedGeometryBytes
        << " bytes expanded draw data.\n";
    outputRegion = std::move(region);
    return true;
}

bool BuildSakkolaScene(const std::filesystem::path& root, SceneSnapshot& scene,
    std::vector<float>& instanceMaxDrawDistances,
    std::size_t& placementCount, std::size_t& drawCount,
    talvisota::world::WorldRegion& outputRegion, std::string& error)
{
    return BuildWorldScene(root, "world.sakkola", "region.sakkola.church_village",
        scene, instanceMaxDrawDistances, placementCount, drawCount, outputRegion, error);
}

} // namespace talvisota::presentation
