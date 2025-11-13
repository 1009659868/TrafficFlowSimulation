#include "Utiles.h"

class LightManager
{
    public:
    LightManager();
    ~LightManager();
    void initialize(SConfig& config, double offset_x, double offset_y);
    void initializeLights();
    void updateSimStatus(bool& status) { isSimulationRunning = status; };
    std::vector<TrafficLight> getLights();

    private:
    bool isSimulationRunning;
    double frameRate;
    double stepLength;
    double frameInterval;
    double offset_x;
    double offset_y;
};