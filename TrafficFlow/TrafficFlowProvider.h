#pragma once

#include "DataCollector.h"
#include "Utiles.h"
#include "LightManager.h"
#include "RedisConnector.h"
#include "RedisManager.h"
#include "SimulationController.h"
#include "VehicleManager.h"
#include "hdmap.h"

using json = nlohmann::json;

class TrafficFlowProvider
{
    public:
    TrafficFlowProvider();
    ~TrafficFlowProvider();
    std::string getInstanceID() { return instanceID; }

    // 获取模块
    RedisManager& getRedisManager() { return RedisManager::getInstance(); }
    RedisConnector* getRedisConnector() { return RedisManager::getInstance().getRedisConnector(instanceID); }
    VehicleManager* getVehicleManager() { return vehicleManager_.get(); }
    DataCollector* getDataCollector() { return dataCollector_.get(); }
    TrafficSimulationController* getTrafficSimulationController() { return simController_.get(); }

    RedisConfig& getRedisConfig();
    ConfigTrafficFlow& getTrafficFlowConfig();
    // 初始化
    void initialize();
    void initializeVehicles();
    void shutdown();
    // 加载配置数据
    void loadData(const std::string& rootPath, Parameters& parameters);
    // 解析配置文件
    void parseConfigFile(const std::string& rootPath);
    void parseRoute(json routeJson);
    // 获取导航数据

    // 仿真控制
    // 启动仿真
    bool startSimulation();
    // 关闭仿真
    void closeSimulation();
    // 按帧推进仿真
    void step();
    bool getSimRunning();
    bool shouldWriteThisFrame();
    void reverseKey();
    void initStatus();

    bool getBroken(vector<RoadCoord>& brokenList);
    bool checkNavStop();
    bool navigateStep();
    double getLanePosition(Vector3& target,string& laneID);
    bool checkBroken();
    bool brokenStep();
    void brokenStop(vector<string>& vIDs, vector<RoadCoord>& projections);
    void setStopForVehicle( string& vID, vector<RoadCoord>& projections);
    // 获取所有车辆数据
    std::vector<TrafficVehicle> getVehicles();
    std::vector<TrafficLight> getLights();
    // 添加主车
    bool addEgoVehicle(const string& actorID, const string& routeID,string& egoID);
    bool isEgoEmpty();
    
    bool setPreVehicles(const std::vector<TrafficVehicle>& preVeh);

    // 是否面向
    bool isAhead( string& vID,  Vector3& vPos,  Vector3& target);

    // 更新redis数据
    void updateToRedis(vector<TrafficVehicle>& vehicles);
    void updateToRedis(vector<TrafficLight>& lights);

    // 获取最大车辆数
    int getMaxVehicles() { return maxVehicles; }

    // 文件路径
    void setCurrentPath(const string& path) { currentPath_ = path; }
    std::string getCurrentPath() { return currentPath_; }

    // 线程同步
    void lock() { providerMutex_.lock(); }
    void unlock() { providerMutex_.unlock(); }
    std::unique_lock<std::mutex> getUniqueLock() { return std::unique_lock<std::mutex>(providerMutex_); }
    
    // 设置初始化完成标志
    void setInitializationComplete(bool complete);
    // 等待初始化完成
    void waitForInitialization();
    // 设置步进完成标志
    void setStepComplete(bool complete);
    // 等待步进完成
    void waitForStepComplete();

    void setHDMAPComplete(bool complete);
    void waitForHDMAPComplete();

    // 性能监控
    void startTimer(const std::string& name);
    void stopTimer(const std::string& name);
    void printTimers();
    void resetTimers();
    void logFrameTime(long long duration);

    double getFrameRate() const { return frameRate; }
    double getTargetFrameRate() const { return frameRate; }
    double getTargetFrameTime() const { return 1000.0 / frameRate; }
    int getFrameInterval() const { return frameInterval; }
    // 打印车辆类型统计信息
    void printVehicleTypeStats(const std::map<std::string, int>& counts);
    ConfigTrafficFlow configTra;

    // 添加命令到队列（线程安全）
    void pushCommand(std::function<void()> cmd)
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        commandQueue.push(cmd);
    }

    // 执行所有待处理命令（线程安全）
    void executeCommands()
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        while (!commandQueue.empty()) {
            auto cmd = commandQueue.front();
            commandQueue.pop();
            cmd();
        }
    }
    double offset_x;
    double offset_y;
    VehicleType vehicleTypes;

    public:
    string timeStamp_bombEvent;
    string timeStamp_navEvent;
    private:
    std::string instanceID;

    std::unique_ptr<TrafficSimulationController> simController_;
    std::unique_ptr<VehicleManager> vehicleManager_;
    std::unique_ptr<DataCollector> dataCollector_;
    std::unique_ptr<LightManager> lightManager_;
    RedisManager& redisManager_ = RedisManager::getInstance();
    RedisConnector* redisConnector_ = nullptr;

    private:
    // 同步机制
    std::mutex initMutex_;
    std::condition_variable initCondition_;
    std::atomic<bool> initializationComplete_ = false;

    std::mutex stepMutex_;
    std::condition_variable stepCondition_;
    std::atomic<bool> stepComplete_ = false;

    // 命令队列
    std::mutex commandMutex;
    std::queue<std::function<void()>> commandQueue;
    // 每个实例有自己的互斥量
    std::mutex providerMutex_;
    std::mutex connectorMutex_;

    int maxVehicles;

    bool isTrafficSignal;

    // 导航，路径自动计算
    Navigation navigationInfo_;
    string reqIDpre;

    std::string currentPath_;

    void coverConfig(startConfig& config, Parameters& parameters);
    // 打json数据包
    const string CollectTrafficData(vector<TrafficVehicle> vehicles);
    // 写入数据到Redis
    void WriteTrafficDataToRedis(const string& key, const string& trafficData);

    private:
    // 车辆类型计数器
    std::map<std::string, int> currentCounts_;
    std::map<std::string, long> accumulatedTypeCounts_; // 累计数量
    std::map<std::string, int> typeStatsFrames_;        // 统计帧数
    long totalFramesForTypeStats_ = 0;                  // 总统计帧数
    std::mutex typeStatsMutex_;                         // 用于保护车辆类型统计
    // 性能监控
    int collectCount;
    std::mutex timerMutex;
    std::map<std::string, std::chrono::steady_clock::time_point> startTimes;
    std::map<std::string, long long> elapsedTimes;
    std::map<std::string, int> callCounts;

    long long totalFrameTime;
    long frameCount;
    double frameRate;
    double frameTime;
    int frameInterval;
    double simulationTimeAccumulator;
    // 更新车辆类型统计
    void updateTypeStatistics(const std::map<std::string, int>& currentCounts);
    void resetTypeStatistics();

    private:
    //停滞时间
    const float STOP_DURATION = 3600;
    //安全距离
    const float SAFETY_GAP = 2.f;
    //bomb容器
    vector<RoadCoord> bombProjectionList;
    std::shared_mutex bombProjectionMutex_;
    std::unordered_map<string,int> vehicleBrakeCount_;
    float min_length = 0.0f;
    double highCost = 999999.0;
    double originCost = 0.0;
    //批处理
    void processVehicleBatch(const vector<string>& vIDs);
};