#include "LightManager.h"

LightManager::LightManager() : isSimulationRunning(false) {}

LightManager::~LightManager() {}

void LightManager::initialize(SConfig& config, double offset_x, double offset_y)
{
    // stepLength = config.stepLength;
    // frameRate = config.frameRate;
    // frameInterval = 1000.0 / config.frameRate;
    stepLength = config.getConfigOrDefault<float>("stepLength",0.03);
    frameRate = config.getConfigOrDefault<int>("frameRate",30);
    frameInterval = 1000.0/ frameRate;
    this->offset_x = offset_x;
    this->offset_y = offset_y;
}

void LightManager::initializeLights()
{
    if (!isSimulationRunning)
        return;
    // init logic
    //------------------------
    {
    }
    //------------------------
    cout << "Successfully initialized " << endl;
}

std::vector<TrafficLight> LightManager::getLights()
{
    vector<TrafficLight> lights;
    if (!isSimulationRunning)
        return lights;

    try {
        vector<string> lightIDs = libsumo::TrafficLight::getIDList();
        
        lights.resize(lightIDs.size());

        auto buildActor = [&](auto item) {
            TrafficLight light;

            light.actorID = item;
            auto pos = libsumo::Junction::getPosition(item,true);
            Vector3 pos3D{pos.x,pos.y,pos.z};
            light.pos = pos3D;
            light.updateState(item);
            light.updatePrograms(item);
            light.updateControlledLanes(item);
            return light;
        };
        transform(execution::par, lightIDs.begin(), lightIDs.end(), lights.begin(), buildActor);
        return lights;
    } catch (const exception& e) {
        cerr << "Get TrafficLight Data Error: " << e.what() << endl;
    }
}
