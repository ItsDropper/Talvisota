#include "talvisota/presentation/DynamicWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace talvisota::presentation
{
namespace
{
constexpr float Gravity = 9.81F;
constexpr float SleepLinearSpeed = 0.075F;
constexpr float SleepAngularSpeed = 0.12F;
constexpr float SleepDelay = 0.8F;

bool Finite3(const std::array<float, 3>& value) noexcept
{
    return std::all_of(value.begin(), value.end(), [](float v) { return std::isfinite(v); });
}

float LengthSquared(const std::array<float, 3>& value) noexcept
{
    return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
}

void ApplyRotation(std::array<float, 16>& matrix, const float pitch,
    const float yaw, const float roll) noexcept
{
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cr = std::cos(roll), sr = std::sin(roll);
    for (std::size_t column = 0; column < 3U; ++column)
    {
        const std::size_t offset = column * 4U;
        const float x = matrix[offset], y = matrix[offset + 1U], z = matrix[offset + 2U];
        const float pitchedY = cp * y - sp * z;
        const float pitchedZ = sp * y + cp * z;
        const float yawedX = cy * x + sy * pitchedZ;
        const float yawedZ = -sy * x + cy * pitchedZ;
        matrix[offset] = cr * yawedX - sr * pitchedY;
        matrix[offset + 1U] = sr * yawedX + cr * pitchedY;
        matrix[offset + 2U] = yawedZ;
    }
}

std::uint32_t StableHash(const std::string_view value) noexcept
{
    std::uint32_t hash = 2166136261U;
    for (const char rawCharacter : value)
    {
        hash ^= static_cast<unsigned char>(rawCharacter);
        hash *= 16777619U;
    }
    return hash;
}

float ObjectMass(const talvisota::world::WorldPlacement& placement) noexcept
{
    switch (placement.category)
    {
    case talvisota::world::PlacementCategory::Vegetation: return std::max(12.0F, 45.0F * placement.uniformScale);
    case talvisota::world::PlacementCategory::Rock: return std::max(80.0F, 240.0F * placement.uniformScale);
    case talvisota::world::PlacementCategory::Structure: return std::max(500.0F, 1800.0F * placement.uniformScale);
    case talvisota::world::PlacementCategory::Terrain: return std::numeric_limits<float>::max();
    case talvisota::world::PlacementCategory::Prop: return std::max(3.0F, 18.0F * placement.uniformScale);
    }
    return 1.0F;
}
} // namespace

bool DynamicWorldSystem::Initialize(const talvisota::world::WorldRegion& region,
    SceneSnapshot& scene, std::string& error)
{
    if (!objects_.empty())
    {
        error = "Dynamic world system is already initialized.";
        return false;
    }
    if (region.placements.empty() || scene.instances.empty())
    {
        error = "Dynamic world initialization requires authored placements and rendered instances.";
        return false;
    }

    std::vector<DynamicWorldObject> objects;
    objects.reserve(region.placements.size());
    std::unordered_map<std::string, bool> placementIds;
    placementIds.reserve(region.placements.size());
    for (const auto& placement : region.placements)
    {
        if (placement.id.empty() || !placementIds.emplace(placement.id, true).second)
        {
            error = "Dynamic world placements require unique, non-empty authored IDs.";
            return false;
        }
    }
    for (const auto& placement : region.placements)
    {
        DynamicWorldObject object;
        object.id = placement.id;
        object.category = placement.category;
        object.authoredTransform = talvisota::world::BuildPlacementTransform(placement);
        object.position = placement.position;
        object.mass = ObjectMass(placement);
        object.anchored = placement.category == talvisota::world::PlacementCategory::Terrain ||
            placement.category == talvisota::world::PlacementCategory::Structure;
        object.sleeping = object.anchored;
        if (placement.category == talvisota::world::PlacementCategory::Terrain)
            object.integrity = std::numeric_limits<float>::max();

        // Scene construction writes the authored placement ID onto every
        // material-bound draw instance. Never infer identity from transforms:
        // coincident placements and world-space assets can legitimately overlap.
        bool capturedInitialTransform = false;
        for (std::size_t index = 0; index < scene.instances.size(); ++index)
        {
            const auto& instance = scene.instances[index];
            if (instance.placementId != placement.id)
                continue;
            if (!capturedInitialTransform)
            {
                object.authoredTransform = instance.world;
                object.position = {instance.world[12], instance.world[13], instance.world[14]};
                capturedInitialTransform = true;
            }
            object.instanceIndices.push_back(index);
        }
        if (object.instanceIndices.empty())
        {
            error = "Dynamic world could not bind placement to rendered instances: " + placement.id;
            return false;
        }
        objects.push_back(std::move(object));
    }
    objects_.swap(objects);
    error.clear();
    return true;
}

bool DynamicWorldSystem::ApplyImpulse(const std::string_view placementId,
    const std::array<float, 3>& impulse, const std::array<float, 3>& torque,
    std::string& error)
{
    if (!Finite3(impulse) || !Finite3(torque))
    {
        error = "Dynamic world impulse and torque must be finite.";
        return false;
    }
    const auto found = std::find_if(objects_.begin(), objects_.end(),
        [placementId](const DynamicWorldObject& object) { return object.id == placementId; });
    if (found == objects_.end())
    {
        error = "Dynamic world placement does not exist: " + std::string(placementId);
        return false;
    }
    if (found->category == talvisota::world::PlacementCategory::Terrain ||
        found->category == talvisota::world::PlacementCategory::Structure)
    {
        error = "This placement is anchored by its authored structural constraints.";
        return false;
    }
    if (found->integrity <= 0.0F)
    {
        error = "A fully damaged placement cannot receive another impulse.";
        return false;
    }
    auto nextVelocity = found->velocity;
    auto nextAngularVelocity = found->angularVelocity;
    for (std::size_t axis = 0; axis < 3U; ++axis)
    {
        nextVelocity[axis] += impulse[axis] / found->mass;
        nextAngularVelocity[axis] += torque[axis] / found->mass;
    }
    if (!Finite3(nextVelocity) || !Finite3(nextAngularVelocity))
    {
        error = "Dynamic world impulse would produce a non-finite object velocity.";
        return false;
    }
    found->velocity = nextVelocity;
    found->angularVelocity = nextAngularVelocity;
    found->anchored = false;
    found->sleeping = false;
    found->sleepSeconds = 0.0F;
    error.clear();
    return true;
}

bool DynamicWorldSystem::ApplyDamage(const std::string_view placementId,
    const float damage, std::string& error)
{
    if (!std::isfinite(damage) || damage < 0.0F)
    {
        error = "Dynamic world damage must be finite and non-negative.";
        return false;
    }
    const auto found = std::find_if(objects_.begin(), objects_.end(),
        [placementId](const DynamicWorldObject& object) { return object.id == placementId; });
    if (found == objects_.end())
    {
        error = "Dynamic world placement does not exist: " + std::string(placementId);
        return false;
    }
    if (found->category == talvisota::world::PlacementCategory::Terrain ||
        found->category == talvisota::world::PlacementCategory::Structure)
    {
        error = "Terrain and load-bearing structures require structural damage evaluation.";
        return false;
    }
    found->integrity = std::max(0.0F, found->integrity - damage);
    if (found->integrity <= 0.0F)
    {
        found->anchored = false;
        found->sleeping = false;
    }
    error.clear();
    return true;
}

bool DynamicWorldSystem::Update(const talvisota::world::WorldRegion& region,
    SceneSnapshot& scene, const float deltaSeconds, const double elapsedSeconds,
    const std::array<float, 2>& windDirection, const float windStrength,
    std::string& error)
{
    if (objects_.empty() || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F ||
        deltaSeconds > 0.1F || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
        !std::isfinite(windDirection[0]) || !std::isfinite(windDirection[1]) ||
        !std::isfinite(windStrength) || windStrength < 0.0F || windStrength > 1.0F)
    {
        error = "Dynamic world update received invalid timestep or weather parameters.";
        return false;
    }

    for (auto& object : objects_)
    {
        const float time = static_cast<float>(std::fmod(elapsedSeconds, 100000.0));
        float swayPitch = 0.0F, swayRoll = 0.0F;
        if (object.anchored && object.category == talvisota::world::PlacementCategory::Vegetation)
        {
            const float phase = static_cast<float>(StableHash(object.id) % 6283U) * 0.001F;
            const float amplitude = windStrength * 0.035F;
            swayPitch = std::sin(time * 1.7F + phase) * amplitude * windDirection[1];
            swayRoll = std::sin(time * 1.13F + phase * 1.7F) * amplitude * windDirection[0] * 0.65F;
        }
        else if (!object.anchored && !object.sleeping)
        {
            object.velocity[1] -= Gravity * deltaSeconds;
            for (std::size_t axis = 0; axis < 3U; ++axis)
            {
                object.position[axis] += object.velocity[axis] * deltaSeconds;
                object.rotation[axis] += object.angularVelocity[axis] * deltaSeconds;
                object.angularVelocity[axis] *= std::max(0.0F, 1.0F - 1.8F * deltaSeconds);
            }
            object.position[0] = std::clamp(object.position[0], region.boundsMin[0], region.boundsMax[0]);
            object.position[2] = std::clamp(object.position[2], region.boundsMin[2], region.boundsMax[2]);
            float ground = 0.0F;
            std::array<float, 3> normal{};
            std::string terrainError;
            if (talvisota::world::SampleTerrainHeight(region, object.position[0],
                    object.position[2], ground, normal, terrainError) &&
                object.position[1] <= ground + 0.08F)
            {
                object.position[1] = ground + 0.08F;
                if (object.velocity[1] < 0.0F) object.velocity[1] = -object.velocity[1] * 0.12F;
                object.velocity[0] *= 0.72F;
                object.velocity[2] *= 0.72F;
                object.angularVelocity[0] *= 0.55F;
                object.angularVelocity[2] *= 0.55F;
            }
            const float linearSpeed = LengthSquared(object.velocity);
            const float angularSpeed = LengthSquared(object.angularVelocity);
            if (linearSpeed < SleepLinearSpeed * SleepLinearSpeed &&
                angularSpeed < SleepAngularSpeed * SleepAngularSpeed)
                object.sleepSeconds += deltaSeconds;
            else
                object.sleepSeconds = 0.0F;
            if (object.sleepSeconds >= SleepDelay)
            {
                object.sleeping = true;
                object.velocity = {};
                object.angularVelocity = {};
            }
            swayPitch = std::clamp(object.angularVelocity[0] * 0.12F, -0.3F, 0.3F);
            swayRoll = std::clamp(object.angularVelocity[2] * 0.12F, -0.3F, 0.3F);
        }

        auto transform = object.authoredTransform;
        transform[12] = object.position[0];
        transform[13] = object.position[1];
        transform[14] = object.position[2];
        ApplyRotation(transform, object.rotation[0] + swayPitch,
            object.rotation[1], object.rotation[2] + swayRoll);
        for (const auto index : object.instanceIndices)
        {
            if (index >= scene.instances.size())
            {
                error = "Dynamic world instance binding became invalid for placement: " + object.id;
                return false;
            }
            scene.instances[index].world = transform;
        }
    }
    error.clear();
    return true;
}

bool DynamicWorldSystem::Save(const std::filesystem::path& path, std::string& error) const
{
    if (objects_.empty() || path.empty())
    {
        error = "Dynamic world save requires initialized state and a destination path.";
        return false;
    }
    std::error_code ec;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "Could not create dynamic world save directory: " + ec.message();
        return false;
    }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::out | std::ios::trunc);
        if (!output)
        {
            error = "Could not open temporary dynamic world save.";
            return false;
        }
        output << "TALVISOTA_WORLD_STATE 2\n" << objects_.size() << '\n';
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        for (const auto& object : objects_)
        {
            output << std::quoted(object.id) << ' ' << object.integrity << ' ' << object.mass << ' '
                << object.anchored << ' ' << object.sleeping << ' ' << object.sleepSeconds;
            for (const float value : object.position) output << ' ' << value;
            for (const float value : object.velocity) output << ' ' << value;
            for (const float value : object.angularVelocity) output << ' ' << value;
            for (const float value : object.rotation) output << ' ' << value;
            output << '\n';
        }
        output.flush();
        if (!output)
        {
            error = "Could not write complete dynamic world save.";
            return false;
        }
        output.close();
        if (!output)
        {
            error = "Could not close the completed dynamic world save.";
            return false;
        }
    }

    // POSIX rename replaces atomically. Windows does not replace an existing
    // destination, so retain a rollback copy instead of deleting the last good save.
    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        std::error_code existsError;
        const bool destinationExists = std::filesystem::exists(path, existsError);
        if (existsError)
        {
            error = "Could not inspect the previous dynamic world save: " + existsError.message();
            return false;
        }
        if (destinationExists)
        {
            ec.clear();
            const std::filesystem::path backup = path.string() + ".bak";
            std::error_code backupError;
            std::filesystem::remove(backup, backupError);
            backupError.clear();
            std::filesystem::rename(path, backup, backupError);
            if (backupError)
            {
                error = "Could not protect the previous dynamic world save: " + backupError.message();
                return false;
            }
            std::filesystem::rename(temporary, path, ec);
            if (ec)
            {
                std::error_code rollbackError;
                std::filesystem::rename(backup, path, rollbackError);
                error = "Could not publish the new dynamic world save: " + ec.message();
                if (rollbackError)
                    error += "; restoring the previous save also failed: " + rollbackError.message();
                return false;
            }
            std::error_code cleanupError;
            std::filesystem::remove(backup, cleanupError);
        }
    }
    if (ec)
    {
        error = "Could not publish dynamic world save: " + ec.message();
        return false;
    }
    error.clear();
    return true;
}

bool DynamicWorldSystem::Load(const std::filesystem::path& path, std::string& error)
{
    if (objects_.empty())
    {
        error = "Dynamic world must be initialized before loading saved state.";
        return false;
    }
    std::ifstream input(path);
    if (!input)
    {
        if (!std::filesystem::exists(path))
        {
            error.clear();
            return true;
        }
        error = "Could not open dynamic world save.";
        return false;
    }
    std::string magic;
    unsigned version = 0U;
    std::size_t count = 0U;
    if (!(input >> magic >> version >> count) || magic != "TALVISOTA_WORLD_STATE" ||
        (version != 1U && version != 2U) || count != objects_.size())
    {
        error = "Dynamic world save has an unsupported version or placement inventory.";
        return false;
    }
    input.ignore(std::numeric_limits<std::streamsize>::max(), '\\n');
    auto restored = objects_;
    std::unordered_map<std::string, std::size_t> indices;
    indices.reserve(restored.size());
    for (std::size_t index = 0; index < restored.size(); ++index)
        indices.emplace(restored[index].id, index);
    std::vector<bool> seen(restored.size(), false);
    for (std::size_t row = 0; row < count; ++row)
    {
        std::string recordLine;
        if (!std::getline(input, recordLine))
        {
            error = "Dynamic world save contains a truncated object record.";
            return false;
        }
        std::istringstream record(recordLine);
        std::string id;
        DynamicWorldObject state;
        if (!(record >> std::quoted(id) >> state.integrity >> state.mass >>
                state.anchored >> state.sleeping >> state.sleepSeconds))
        {
            error = "Dynamic world save contains an invalid object record.";
            return false;
        }
        for (float& value : state.position) if (!(record >> value)) { error = "Truncated position."; return false; }
        for (float& value : state.velocity) if (!(record >> value)) { error = "Truncated velocity."; return false; }
        for (float& value : state.angularVelocity) if (!(record >> value)) { error = "Truncated angular velocity."; return false; }
        record >> std::ws;
        if (version >= 2U)
        {
            for (float& value : state.rotation) if (!(record >> value)) { error = "Truncated rotation."; return false; }
        }
        else if (!record.eof())
        {
            // Version 1 was briefly written both before and after rotation was
            // added. Accept either row shape and default old rotations to zero.
            for (float& value : state.rotation) if (!(record >> value)) { error = "Invalid legacy rotation."; return false; }
        }
        record >> std::ws;
        if (!record.eof())
        {
            error = "Dynamic world save object record contains trailing data.";
            return false;
        }
        const auto found = indices.find(id);
        if (found == indices.end() || seen[found->second] ||
            !std::isfinite(state.integrity) || state.integrity < 0.0F ||
            !std::isfinite(state.mass) || state.mass <= 0.0F ||
            !std::isfinite(state.sleepSeconds) || state.sleepSeconds < 0.0F || state.sleepSeconds > 60.0F ||
            !Finite3(state.position) || !Finite3(state.velocity) || !Finite3(state.angularVelocity) || !Finite3(state.rotation))
        {
            error = "Dynamic world save contains an unknown, duplicate, or invalid object state.";
            return false;
        }
        auto& target = restored[found->second];
        if (std::abs(state.mass - target.mass) > std::max(0.001F, target.mass * 0.0001F) ||
            (target.category != talvisota::world::PlacementCategory::Terrain && state.integrity > 1.0F) ||
            (state.anchored && !state.sleeping) || (state.integrity <= 0.0F && state.anchored))
        {
            error = "Dynamic world save violates an object mass, integrity, or sleep invariant.";
            return false;
        }
        if (target.category == talvisota::world::PlacementCategory::Terrain ||
            target.category == talvisota::world::PlacementCategory::Structure)
        {
            if (!state.anchored || state.integrity != target.integrity)
            {
                error = "Dynamic world save attempts to alter an authored structural constraint.";
                return false;
            }
        }
        target.integrity = state.integrity;
        target.mass = state.mass;
        target.anchored = state.anchored;
        target.sleeping = state.sleeping;
        target.sleepSeconds = state.sleepSeconds;
        target.position = state.position;
        target.velocity = state.velocity;
        target.angularVelocity = state.angularVelocity;
        target.rotation = state.rotation;
        seen[found->second] = true;
    }
    std::string trailing;
    if (input >> trailing)
    {
        error = "Dynamic world save contains unexpected trailing records.";
        return false;
    }
    objects_.swap(restored);
    error.clear();
    return true;
}
} // namespace talvisota::presentation
