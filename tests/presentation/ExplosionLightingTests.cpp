#include "talvisota/presentation/ExplosionLighting.hpp"

#include <cstdlib>
#include <limits>
#include <string>

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
    ExplosionLightingSystem effects;
    SceneSnapshot scene;
    std::string error;
    const std::array<float,3> impact{12.0F,1.5F,-8.0F};

    Require(effects.Trigger("event.test.artillery",impact,0.0,48.0F,850.0F,1.4F,error));
    Require(effects.ActiveEventCount()==1U);
    Require(!effects.Trigger("event.test.artillery",impact,0.0,48.0F,850.0F,1.4F,error));
    Require(!effects.Trigger("event.test.invalid",impact,0.0,0.0F,850.0F,1.4F,error));

    Require(effects.Update(scene,0.0,error));
    Require(scene.pointLights.empty());
    Require(effects.Update(scene,0.02,error));
    Require(scene.pointLights.size()==1U);
    Require(scene.pointLights.front().range>0.0F &&
        scene.pointLights.front().intensity>0.0F);
    const float peak=scene.pointLights.front().intensity;
    Require(ValidateSceneSnapshot(scene,error));

    Require(effects.Update(scene,0.4,error));
    Require(scene.pointLights.size()==1U &&
        scene.pointLights.front().intensity<peak);
    Require(effects.Update(scene,2.0,error));
    Require(scene.pointLights.empty() && effects.ActiveEventCount()==0U);

    Require(effects.Trigger("event.test.second",impact,2.0,32.0F,400.0F,1.0F,error));
    Require(effects.Update(scene,2.02,error));
    const auto preserved=scene.pointLights;
    Require(!effects.Update(scene,std::numeric_limits<double>::quiet_NaN(),error));
    Require(scene.pointLights.size()==preserved.size() &&
        scene.pointLights.front().intensity==preserved.front().intensity);
    effects.Clear();
    Require(effects.ActiveEventCount()==0U);
    return 0;
}
