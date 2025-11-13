#pragma once
#include "Utiles.h"

class TrafficSimulationController {
public:
    TrafficSimulationController();
    ~TrafficSimulationController();
    
    bool startSimulation(const std::vector<std::string>& options);
    void closeSimulation();
    void step();
    bool& getSimRunning() { return isSimulationRunning; }
    
    void initialize(SConfig& config);
    void initializeVehicles(VehicleType& vTypes);
private:
    bool isSimulationRunning = false;
    double simulationTimeAccumulator = 0.0;
    
    double stepLength;
    double frameInterval;
    double frameRate;
};









