#include "talvisota/presentation/WeatherParticles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace talvisota::presentation;

namespace
{
void Require(const bool condition)
{
    if (!condition) std::abort();
}
}

int main()
{
    SceneSnapshot scene;
    std::vector<float> maximumDrawDistances;
    SnowfallSystem snowfall;
    std::string error;

    Require(snowfall.Initialize(scene,maximumDrawDistances,768U,error));
    Require(snowfall.IsInitialized() && snowfall.FlakeCount()==768U);
    Require(scene.meshes.size()==1U && scene.materials.size()==1U);
    Require(scene.instances.size()==768U && maximumDrawDistances.size()==768U);
    Require(std::all_of(maximumDrawDistances.begin(),maximumDrawDistances.end(),
        [](const float distance) { return distance==60.0F; }));
    Require(ValidateSceneSnapshot(scene,error));

    const std::array<float,3> camera{12.0F,5.0F,-30.0F};
    Require(snowfall.Update(scene,camera,12.5,{0.914F,0.406F},0.14F,true,error));
    Require(std::all_of(scene.instances.begin(),scene.instances.end(),
        [](const RenderInstance& instance) {
            return instance.visible && std::isfinite(instance.world[12]) &&
                std::isfinite(instance.world[13]) && std::isfinite(instance.world[14]);
        }));
    for (const RenderInstance& instance : scene.instances)
    {
        Require(std::abs(instance.world[12]-camera[0])<=20.0F);
        Require(instance.world[13]>=camera[1]-12.0F &&
            instance.world[13]<=camera[1]+12.0F);
        Require(std::abs(instance.world[14]-camera[2])<=26.0F);
    }

    // In calm air flakes fall without artificial horizontal drift. A wind field
    // must then transport the same deterministic flakes across the camera volume.
    Require(snowfall.Update(scene,camera,13.0,{0.914F,0.406F},0.0F,true,error));
    const auto calmTransform=scene.instances.front().world;
    Require(snowfall.Update(scene,camera,14.0,{0.914F,0.406F},0.0F,true,error));
    Require(scene.instances.front().world[12]==calmTransform[12] &&
        scene.instances.front().world[14]==calmTransform[14]);
    Require(scene.instances.front().world[13]!=calmTransform[13]);
    const auto calmX=scene.instances.front().world[12];
    const auto calmZ=scene.instances.front().world[14];
    Require(snowfall.Update(scene,camera,14.0,{0.914F,0.406F},0.14F,true,error));
    Require(scene.instances.front().world[12]!=calmX ||
        scene.instances.front().world[14]!=calmZ);

    Require(snowfall.Update(scene,camera,13.0,{0.914F,0.406F},0.14F,false,error));
    Require(std::none_of(scene.instances.begin(),scene.instances.end(),
        [](const RenderInstance& instance) { return instance.visible; }));

    const auto preserved=scene.instances.front().world;
    const auto varied=scene.instances[1].world;
    Require(scene.instances.front().world[0]!=scene.instances[1].world[0] ||
        scene.instances.front().world[5]!=scene.instances[1].world[5]);
    Require(!snowfall.Update(scene,camera,
        std::numeric_limits<double>::quiet_NaN(),{0.914F,0.406F},0.14F,true,error));
    Require(scene.instances.front().world==preserved && scene.instances[1].world==varied &&
        std::none_of(scene.instances.begin(),scene.instances.end(),
            [](const RenderInstance& instance) { return instance.visible; }));

    SceneSnapshot invalidScene;
    std::vector<float> invalidDistances;
    SnowfallSystem invalidSnowfall;
    Require(!invalidSnowfall.Initialize(invalidScene,invalidDistances,0U,error));
    Require(invalidScene.meshes.empty() && invalidScene.materials.empty() &&
        invalidScene.instances.empty() && invalidDistances.empty());
    return 0;
}
