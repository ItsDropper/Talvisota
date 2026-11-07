#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <unordered_map>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace talvisota::world
{
namespace
{
constexpr std::string_view Header = "talvisota-region\t1";
constexpr std::uintmax_t MaximumFileBytes = 16U * 1024U * 1024U;
constexpr std::size_t MaximumRecords = 200000U;
constexpr std::size_t MaximumLineBytes = 4096U;

void Add(std::vector<WorldRegionDiagnostic>& diagnostics, const std::filesystem::path& path,
         std::size_t line, std::string field, std::string message)
{
    diagnostics.push_back({path, line, std::move(field), std::move(message)});
}

std::string_view Trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

std::vector<std::string_view> Fields(std::string_view line)
{
    std::vector<std::string_view> fields;
    while (true)
    {
        const auto separator = line.find('\t');
        fields.push_back(Trim(separator == std::string_view::npos ? line : line.substr(0, separator)));
        if (separator == std::string_view::npos)
            return fields;
        line.remove_prefix(separator + 1U);
    }
}

bool ParseFloat(std::string_view value, float& output)
{
    value = Trim(value);
    if (value.empty())
        return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output,
                                        std::chars_format::general);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
           std::isfinite(output);
}

bool ParseVector(std::string_view value, std::array<float, 3>& output)
{
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        const auto comma = value.find(',');
        const auto component = comma == std::string_view::npos ? value : value.substr(0, comma);
        if (!ParseFloat(component, output[i]))
            return false;
        if (i == 2U)
            return comma == std::string_view::npos;
        if (comma == std::string_view::npos)
            return false;
        value.remove_prefix(comma + 1U);
    }
    return false;
}

bool Inside(const std::array<float, 3>& point, const std::array<float, 3>& minimum,
            const std::array<float, 3>& maximum) noexcept
{
    for (std::size_t axis = 0; axis < 3U; ++axis)
        if (point[axis] < minimum[axis] || point[axis] > maximum[axis])
            return false;
    return true;
}

bool IsRecordId(std::string_view id) noexcept
{
    if (id.empty())
        return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char value)
    {
        return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
               value == '.' || value == '_' || value == '-' || value == ':';
    });
}

bool HasDependency(const content::AssetRecord* owner, std::string_view dependency)
{
    return owner != nullptr &&
        std::find(owner->dependencies.begin(), owner->dependencies.end(), dependency) != owner->dependencies.end();
}

bool ResolveAsset(const content::ContentRegistry& registry, const content::AssetRecord* owner,
                  std::string_view id, content::AssetKind kind,
                  const std::filesystem::path& path, std::size_t line, std::string_view field,
                  std::vector<WorldRegionDiagnostic>& diagnostics)
{
    const auto* asset = registry.Find(id);
    if (asset == nullptr || asset->kind != kind)
    {
        Add(diagnostics, path, line, std::string(field), "Referenced asset is missing or has the wrong asset kind: " + std::string(id));
        return false;
    }
    if (!HasDependency(owner, id))
        Add(diagnostics, path, line, std::string(field), "Referenced asset must be an explicit region manifest dependency: " + std::string(id));
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(registry.Root() / asset->relativePath, error);
    const auto relative = resolved.lexically_relative(registry.Root());
    if (error || relative.empty() || relative.is_absolute() ||
        std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; }) ||
        !std::filesystem::is_regular_file(resolved, error) || error)
    {
        Add(diagnostics, path, line, std::string(field), "Referenced asset is absent or is not a regular file: " + std::string(id));
        return false;
    }
    return true;
}
} // namespace

std::array<float, 16> BuildPlacementTransform(const WorldPlacement& placement) noexcept
{
    constexpr float DegreesToRadians = 0.01745329251994329577F;
    const float angle = placement.yawDegrees * DegreesToRadians;
    const float cosine = std::cos(angle) * placement.uniformScale;
    const float sine = std::sin(angle) * placement.uniformScale;
    return {
        cosine, 0.0F, -sine, 0.0F,
        0.0F, placement.uniformScale, 0.0F, 0.0F,
        sine, 0.0F, cosine, 0.0F,
        placement.position[0], placement.position[1], placement.position[2], 1.0F
    };
}

bool QueryCollision(const WorldRegion& region, const std::array<float, 3>& queryMin,
                    const std::array<float, 3>& queryMax, bool& intersects,
                    std::string& error) noexcept
{
    for (std::size_t axis = 0; axis < 3U; ++axis)
    {
        if (!std::isfinite(queryMin[axis]) || !std::isfinite(queryMax[axis]) ||
            queryMin[axis] > queryMax[axis])
        {
            error = "Collision query bounds must be finite and ordered.";
            return false;
        }
    }
    bool candidate = false;
    for (const auto& box : region.collision)
    {
        bool overlap = true;
        for (std::size_t axis = 0; axis < 3U; ++axis)
        {
            const float boxMin = box.center[axis] - box.halfExtents[axis];
            const float boxMax = box.center[axis] + box.halfExtents[axis];
            overlap = overlap && queryMin[axis] <= boxMax && queryMax[axis] >= boxMin;
        }
        if (overlap)
        {
            candidate = true;
            break;
        }
    }
    intersects = candidate;
    error.clear();
    return true;
}

bool FindNavigationPath(const WorldRegion& region, std::string_view startNode,
                        std::string_view goalNode, std::vector<std::string>& output,
                        std::string& error)
{
    std::unordered_map<std::string, std::size_t> indices;
    indices.reserve(region.navigationNodes.size());
    for (std::size_t i = 0; i < region.navigationNodes.size(); ++i)
        indices.emplace(region.navigationNodes[i].id, i);
    const auto start = indices.find(std::string(startNode));
    const auto goal = indices.find(std::string(goalNode));
    if (start == indices.end() || goal == indices.end())
    {
        error = "Navigation path endpoint does not exist.";
        return false;
    }
    const std::size_t count = region.navigationNodes.size();
    std::vector<std::vector<std::size_t>> adjacency(count);
    for (const auto& edge : region.navigationEdges)
    {
        const auto from = indices.find(edge.from);
        const auto to = indices.find(edge.to);
        if (from == indices.end() || to == indices.end())
        {
            error = "Navigation graph contains an unresolved edge.";
            return false;
        }
        adjacency[from->second].push_back(to->second);
    }

    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> distances(count, infinity);
    std::vector<std::size_t> parents(count, count);
    using QueueEntry = std::pair<double, std::size_t>;
    const auto later = [](const QueueEntry& left, const QueueEntry& right)
    {
        if (left.first != right.first) return left.first > right.first;
        return left.second > right.second;
    };
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, decltype(later)> queue(later);
    distances[start->second] = 0.0;
    queue.emplace(0.0, start->second);
    while (!queue.empty())
    {
        const auto [distance, current] = queue.top();
        queue.pop();
        if (distance > distances[current])
            continue;
        if (current == goal->second)
            break;
        const auto& from = region.navigationNodes[current].position;
        for (const std::size_t next : adjacency[current])
        {
            const auto& to = region.navigationNodes[next].position;
            const double dx = static_cast<double>(to[0]) - from[0];
            const double dy = static_cast<double>(to[1]) - from[1];
            const double dz = static_cast<double>(to[2]) - from[2];
            const double candidate = distance + std::sqrt(dx * dx + dy * dy + dz * dz);
            const bool shorter = candidate < distances[next];
            const bool stableTie = candidate == distances[next] &&
                (parents[next] == count ||
                 region.navigationNodes[current].id < region.navigationNodes[parents[next]].id);
            if (shorter || stableTie)
            {
                distances[next] = candidate;
                parents[next] = current;
                queue.emplace(candidate, next);
            }
        }
    }
    if (!std::isfinite(distances[goal->second]))
    {
        error = "No directed navigation path connects the requested nodes.";
        return false;
    }

    std::vector<std::string> candidate;
    for (std::size_t current = goal->second; current != count; current = parents[current])
    {
        candidate.push_back(region.navigationNodes[current].id);
        if (current == start->second)
            break;
    }
    if (candidate.empty() || candidate.back() != region.navigationNodes[start->second].id)
    {
        error = "Navigation path reconstruction failed because the graph is inconsistent.";
        return false;
    }
    std::reverse(candidate.begin(), candidate.end());
    output = std::move(candidate);
    error.clear();
    return true;
}

bool LoadWorldRegion(const std::filesystem::path& path, const content::ContentRegistry& registry,
                     const content::WorldDefinition& world, WorldRegion& output,
                     std::vector<WorldRegionDiagnostic>& diagnostics)
{
    diagnostics.clear();
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_regular_file(canonical, error) || error)
    {
        Add(diagnostics, path, 0, "file", "Region file does not resolve to a regular file.");
        return false;
    }
    const auto relative = canonical.lexically_relative(registry.Root());
    if (relative.empty() || relative.is_absolute() ||
        std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; }))
    {
        Add(diagnostics, path, 0, "file", "Region file is outside the content root.");
        return false;
    }
    const auto bytes = std::filesystem::file_size(canonical, error);
    if (error || bytes > MaximumFileBytes)
    {
        Add(diagnostics, path, 0, "file", "Region file exceeds the 16 MiB limit or cannot be inspected.");
        return false;
    }
    std::ifstream input(canonical, std::ios::binary);
    if (!input)
    {
        Add(diagnostics, path, 0, "file", "Region file could not be opened.");
        return false;
    }

    WorldRegion candidate;
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> navNodes;
    std::vector<std::pair<std::size_t, std::string>> pendingEdges;
    std::size_t lineNumber = 0;
    std::size_t records = 0;
    bool headerSeen = false;
    bool haveId = false, haveWorld = false, haveMin = false, haveMax = false;
    std::string line;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (line.size() > MaximumLineBytes)
        {
            Add(diagnostics, path, lineNumber, "line", "Region record exceeds the 4096-byte line limit.");
            break;
        }
        if (lineNumber == 1U && line.size() >= 3U &&
            static_cast<unsigned char>(line[0]) == 0xEFU &&
            static_cast<unsigned char>(line[1]) == 0xBBU &&
            static_cast<unsigned char>(line[2]) == 0xBFU)
            line.erase(0, 3U);
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        if (!headerSeen)
        {
            if (trimmed != Header)
                Add(diagnostics, path, lineNumber, "header", "Expected 'talvisota-region<TAB>1'.");
            headerSeen = true;
            continue;
        }
        if (++records > MaximumRecords)
        {
            Add(diagnostics, path, lineNumber, "records", "Region exceeds the 200000-record limit.");
            break;
        }
        const auto fields = Fields(trimmed);
        const auto badCount = [&](std::size_t expected)
        {
            if (fields.size() != expected)
            {
                Add(diagnostics, path, lineNumber, fields.empty() ? "record" : std::string(fields[0]),
                    "Record has an invalid number of tab-separated fields.");
                return true;
            }
            return false;
        };
        if (fields[0] == "id" || fields[0] == "world")
        {
            if (badCount(2U)) continue;
            if (fields[1].empty()) { Add(diagnostics, path, lineNumber, std::string(fields[0]), "Value must not be empty."); continue; }
            if (fields[0] == "id") { if (haveId) Add(diagnostics, path, lineNumber, "id", "Duplicate region ID."); candidate.id = fields[1]; haveId = true; }
            else { if (haveWorld) Add(diagnostics, path, lineNumber, "world", "Duplicate world reference."); candidate.worldId = fields[1]; haveWorld = true; }
        }
        else if (fields[0] == "bounds_min" || fields[0] == "bounds_max")
        {
            if (badCount(2U)) continue;
            auto& target = fields[0] == "bounds_min" ? candidate.boundsMin : candidate.boundsMax;
            bool& seen = fields[0] == "bounds_min" ? haveMin : haveMax;
            if (seen) Add(diagnostics, path, lineNumber, std::string(fields[0]), "Duplicate bounds record.");
            if (!ParseVector(fields[1], target)) Add(diagnostics, path, lineNumber, std::string(fields[0]), "Expected three finite comma-separated metre values.");
            seen = true;
        }
        else if (fields[0] == "placement")
        {
            if (badCount(10U)) continue;
            WorldPlacement item;
            item.id = fields[1];
            if (!IsRecordId(item.id) || !ids.insert(item.id).second) Add(diagnostics, path, lineNumber, "placement", "Placement ID is empty or duplicated.");
            if (fields[2] == "terrain") item.category = PlacementCategory::Terrain;
            else if (fields[2] == "vegetation") item.category = PlacementCategory::Vegetation;
            else if (fields[2] == "rock") item.category = PlacementCategory::Rock;
            else if (fields[2] == "structure") item.category = PlacementCategory::Structure;
            else if (fields[2] == "prop") item.category = PlacementCategory::Prop;
            else Add(diagnostics, path, lineNumber, "placement.category", "Expected terrain, vegetation, rock, structure, or prop.");
            item.modelAssetId = fields[3];
            if (fields[4] != "-") item.materialAssetId = fields[4];
            if (!ParseFloat(fields[5], item.position[0]) || !ParseFloat(fields[6], item.position[1]) ||
                !ParseFloat(fields[7], item.position[2]) || !ParseFloat(fields[8], item.yawDegrees) ||
                !ParseFloat(fields[9], item.uniformScale) || item.uniformScale <= 0.0F)
                Add(diagnostics, path, lineNumber, "placement.transform", "Position and yaw must be finite; uniform scale must be finite and greater than zero.");
            candidate.placements.push_back(std::move(item));
        }
        else if (fields[0] == "collision")
        {
            if (badCount(8U)) continue;
            CollisionBox box;
            box.id = fields[1];
            if (!IsRecordId(box.id) || !ids.insert(box.id).second) Add(diagnostics, path, lineNumber, "collision.id", "Collision ID is empty or duplicated.");
            bool valid = true;
            for (std::size_t i = 0; i < 3U; ++i) valid = ParseFloat(fields[2U + i], box.center[i]) && valid;
            for (std::size_t i = 0; i < 3U; ++i) valid = ParseFloat(fields[5U + i], box.halfExtents[i]) && valid;
            if (!valid || std::any_of(box.halfExtents.begin(), box.halfExtents.end(), [](float v) { return v <= 0.0F; }))
                Add(diagnostics, path, lineNumber, "collision.extents", "Collision center must be finite and each half-extent must be finite and positive.");
            candidate.collision.push_back(std::move(box));
        }
        else if (fields[0] == "nav_node")
        {
            if (badCount(5U)) continue;
            NavigationNode node;
            node.id = fields[1];
            if (!IsRecordId(node.id) || !navNodes.insert(node.id).second || !ids.insert(node.id).second)
                Add(diagnostics, path, lineNumber, "nav_node.id", "Navigation node ID is empty or duplicated.");
            bool valid = true;
            for (std::size_t i = 0; i < 3U; ++i) valid = ParseFloat(fields[2U + i], node.position[i]) && valid;
            if (!valid) Add(diagnostics, path, lineNumber, "nav_node.position", "Navigation position must contain finite coordinates.");
            else if (!Inside(node.position, candidate.boundsMin, candidate.boundsMax))
                Add(diagnostics, path, lineNumber, "nav_node.position", "Navigation node must lie inside region bounds.");
            candidate.navigationNodes.push_back(std::move(node));
        }
        else if (fields[0] == "nav_edge")
        {
            if (badCount(3U)) continue;
            pendingEdges.emplace_back(lineNumber, std::string(fields[1]) + "\n" + std::string(fields[2]));
        }
        else
            Add(diagnostics, path, lineNumber, "record", "Unknown region record type.");
    }
    if (!input.eof() && input.fail()) Add(diagnostics, path, lineNumber, "file", "I/O error while reading region file.");
    if (!headerSeen) Add(diagnostics, path, 0, "header", "Region file is empty or has no version header.");
    if (!haveId) Add(diagnostics, path, 0, "id", "Required region ID is missing.");
    if (!haveWorld) Add(diagnostics, path, 0, "world", "Required world reference is missing.");
    if (!haveMin || !haveMax) Add(diagnostics, path, 0, "bounds", "Both region bounds are required.");
    for (std::size_t axis = 0; axis < 3U; ++axis)
        if (candidate.boundsMin[axis] >= candidate.boundsMax[axis])
            Add(diagnostics, path, 0, "bounds", "Each region minimum must be less than its maximum.");
    for (const auto& placement : candidate.placements)
        if (!Inside(placement.position, candidate.boundsMin, candidate.boundsMax))
            Add(diagnostics, path, 0, "placement.position", "Placement origin must lie inside the region bounds.");
    for (const auto& box : candidate.collision)
        for (std::size_t axis = 0; axis < 3U; ++axis)
            if (box.center[axis] - box.halfExtents[axis] < candidate.boundsMin[axis] ||
                box.center[axis] + box.halfExtents[axis] > candidate.boundsMax[axis])
                Add(diagnostics, path, 0, "collision.bounds", "Collision box must fit entirely inside region bounds.");
    if (candidate.worldId != world.id)
        Add(diagnostics, path, 0, "world", "Region references a different world definition.");
    for (std::size_t axis = 0; axis < 3U; ++axis)
        if (candidate.boundsMin[axis] < world.boundsMin[axis] || candidate.boundsMax[axis] > world.boundsMax[axis])
            Add(diagnostics, path, 0, "bounds", "Region bounds must be contained by the parent world bounds.");

    const auto* regionAsset = registry.Find(candidate.id);
    if (regionAsset == nullptr || regionAsset->kind != content::AssetKind::Definition)
        Add(diagnostics, path, 0, "id", "Region ID must resolve to a registered definition asset.");
    else
    {
        const auto registered = std::filesystem::weakly_canonical(registry.Root() / regionAsset->relativePath, error);
        if (error || registered != canonical)
            Add(diagnostics, path, 0, "id", "Region ID is registered to a different source path.");
        const auto* worldAsset = registry.Find(world.id);
        if (!HasDependency(regionAsset, world.id) || worldAsset == nullptr)
            Add(diagnostics, path, 0, "world", "Parent world must be an explicit region manifest dependency.");
        for (const auto& placement : candidate.placements)
        {
            ResolveAsset(registry, regionAsset, placement.modelAssetId, content::AssetKind::Model,
                         path, 0, "placement.model", diagnostics);
            if (!placement.materialAssetId.empty())
                ResolveAsset(registry, regionAsset, placement.materialAssetId, content::AssetKind::Material,
                             path, 0, "placement.material", diagnostics);
        }
    }

    std::unordered_set<std::string> edges;
    for (const auto& [edgeLine, encoded] : pendingEdges)
    {
        const auto separator = encoded.find('\n');
        const std::string from = encoded.substr(0, separator);
        const std::string to = encoded.substr(separator + 1U);
        if (!navNodes.contains(from) || !navNodes.contains(to) || from == to)
            Add(diagnostics, path, edgeLine, "nav_edge", "Navigation edge must connect two distinct declared nodes.");
        const std::string key = from + "\n" + to;
        if (!edges.insert(key).second)
            Add(diagnostics, path, edgeLine, "nav_edge", "Duplicate directed navigation edge.");
        candidate.navigationEdges.push_back({from, to});
    }

    if (!diagnostics.empty())
        return false;
    output = std::move(candidate);
    return true;
}

bool WorldRegionSession::Load(const std::filesystem::path& path, const content::ContentRegistry& registry,
                              const content::WorldDefinition& world,
                              std::vector<WorldRegionDiagnostic>& diagnostics)
{
    WorldRegion candidate;
    if (!LoadWorldRegion(path, registry, world, candidate, diagnostics))
        return false;
    active_ = std::move(candidate);
    loaded_ = true;
    return true;
}

bool WorldRegionSession::Reload(const std::filesystem::path& path, const content::ContentRegistry& registry,
                                const content::WorldDefinition& world,
                                std::vector<WorldRegionDiagnostic>& diagnostics)
{
    return Load(path, registry, world, diagnostics);
}

void WorldRegionSession::Unload() noexcept
{
    active_ = {};
    loaded_ = false;
}

bool WorldRegionSession::IsLoaded() const noexcept { return loaded_; }
const WorldRegion* WorldRegionSession::Active() const noexcept { return loaded_ ? &active_ : nullptr; }
} // namespace talvisota::world
