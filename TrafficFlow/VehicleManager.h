#pragma once

#include "Utiles.h"
#include "public/MHDMap.h"
#include "public/MLocation.h"

class VehicleManager
{
    public:
    VehicleManager();
    ~VehicleManager();
    void initialize(SConfig& config, double offset_x, double offset_y, VehicleType vehicleTypes);
    void initializeVehicles(int maxVehicles);
    TrafficVehicle initializeEgo(const std::string& egoID, const std::string& vType);
    // 添加随机车辆
    void addRandomVehicles(int count);
    // 移除指定数量的车辆
    void removeVehicles(int count);

    bool isEgoEmpty();
    // 主车管理
    bool addEgoVehicle(const string& actorID, const string& routeID, string& egoID);
    // 销毁主车
    
    void updateSimStatus(bool& status) { isSimulationRunning = status; };

    void setEgoStopPoint(const string& egoID, const std::pair<string, double>& point)
    {
        egoStopPointMap_[egoID] = point;
    };
    const std::pair<string, double> getEgoStopPoint(const string& egoID) { return egoStopPointMap_[egoID]; };
    void removeEgo(const string& egoID);

    private:
    // 获取随机路线ID
    std::string getRandomRouteID(vector<string>& routeIDs, uniform_int_distribution<>& routeDist);
    // 获取随机路线位置
    std::string getRandomRoutePosition(string& routeID, uniform_real_distribution<>& posDist);
    // 获取随机路线位置
    RoadInfo getRandomRoadInfo(string& routeID, uniform_real_distribution<>& posDist);
    // 获取随机车辆类型字符串
    std::string getRandomVehicleType();
    std::string getRandomDetailVehicleType(const string& vType);

    public:
    std::vector<TrafficVehicle> getEgoVehicles();
    std::vector<TrafficVehicle> getVehicles();
    TrafficVehicle* getEgo(const std::string& egoID);
    const std::vector<std::string>& getEgoIDs();
    const string entityIDToEgoID(const string& entityID) { return entityIDToEgoIDMap_[entityID]; };

    private:
    std::map<std::string, std::string> sumoToActorID;
    long long nextActorID = 1;

    std::random_device rd;
    std::mt19937 gen;

    // 枚举到字符串的映射
    VehicleType vehicleTypes_;

    std::vector<std::string> egoIDs;
    std::map<std::string, std::string> entityIDToEgoIDMap_;
    unordered_set<string> actorIDsSet;
    std::map<std::string, int> actorIDToCounts;
    std::map<std::string, TrafficVehicle> egoMap_;
    std::map<std::string, std::pair<string, double>> egoStopPointMap_;

    bool isSimulationRunning;
    double frameRate;
    double stepLength;
    double frameInterval;
    double offset_x;
    double offset_y;
    int riskCount = 0;

    private:
};