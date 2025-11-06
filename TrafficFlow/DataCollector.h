#pragma once
#include "Utiles.h"
#define CYBERTRON_PI 3.1415926535897932f
#define TO_UE_POS_X(x) ((x) * 100.0f)
#define TO_UE_POS_Y(y) (-(y) * 100.0f)
#define TO_UE_POS_Z(z) ((z) * 100.0f)
#define TO_UE_ROT_X(x) (((x) / CYBERTRON_PI) * 180.0f)
#define TO_UE_ROT_Y(y) (-((y) / CYBERTRON_PI) * 180.0f)
#define TO_UE_ROT_Z(z) ((z) > 0 ? 180.0f - ((z) / CYBERTRON_PI) * 180.0f : -180.0f - ((z) / CYBERTRON_PI) * 180.0f)
#define TO_UE_YAW(z) TO_UE_ROT_Z(z) - 180

class DataCollector
{
    public:
    DataCollector();
    ~DataCollector();
    // 设置前一帧车辆数据
    bool setPreVehicles(const std::vector<TrafficVehicle>& preVeh);
    // 获取前一帧车辆数据
    const std::vector<TrafficVehicle> getPreVehicles() const;

    std::vector<TrafficVehicle*> findDisappearedVehicles(std::vector<TrafficVehicle>& prevVehicles,
                                                         std::vector<TrafficVehicle>& currentVehicles);
    const std::string collectTrafficLightData(std::vector<TrafficLight>& lights, double offset_x, double offset_y,
                                              const ConfigTrafficFlow& configTra);

    const std::string collectTrafficVehicleData(std::vector<TrafficVehicle>& vehicles, double offset_x, double offset_y,
                                                const ConfigTrafficFlow& configTra);
    const std::string collectEgoVehiclesData(const std::vector<TrafficVehicle>& vehicles, double offset_x,
                                             double offset_y, const ConfigTrafficFlow& configTra);
    // 滑动窗口管理
    void updateSlidingWindows(std::vector<TrafficVehicle*>& newDisappearing,
                              std::vector<TrafficVehicle*>& newAppearing);

    std::vector<TrafficVehicle*> getConsolidatedDisappearingVehicles();
    std::deque<TrafficVehicle> tryGetDisappearingVehicles()
    {
        lock_guard<mutex> lock(mtx_r);
        return removedVehQue;
    }
    std::vector<TrafficVehicle*> getConsolidatedAppearingVehicles();
    std::deque<TrafficVehicle> tryGetAppearingVehicles()
    {
        lock_guard<mutex> lock(mtx);
        return addedVehQue;
    };
    void initialize(SConfig& config);
    mutex mtx;
    mutex mtx_r;
    std::atomic<bool> popAddedVehQue = false;

    private:
    std::vector<TrafficVehicle> preVehicles;
    size_t slidingWindowSize;                                      // 滑窗大小可配置
    std::deque<std::vector<TrafficVehicle>> disappearingVehWindow; // 消失车辆滑窗
    std::deque<std::vector<TrafficVehicle>> appearingVehWindow;    // 新增车辆滑窗
    std::deque<TrafficVehicle> addedVehQue;
    std::deque<TrafficVehicle> removedVehQue;
    int collectCount;
};