#pragma once
#include "TrafficFlowProvider.h"
#include "ThreadPool.h"
#include <map>
#include <memory>
#include <mutex>
#include <unistd.h>
#include <future>
#include <iostream>
class TrafficFlowManager {
public:
    static TrafficFlowManager& getInstance() {
        static TrafficFlowManager instance;
        return instance;
    }
    
    // 禁止拷贝
    TrafficFlowManager(const TrafficFlowManager&) = delete;
    void operator=(const TrafficFlowManager&) = delete;
    
    // 实例管理
    bool createInstance(Parameters& params);

    void removeInstance(const std::string& instanceId);

    TrafficFlowProvider* getInstance(const std::string& instanceId);
    
    // 线程管理
    void startInstanceThreads(const std::string& instanceId);

private:
    TrafficFlowManager();
    ~TrafficFlowManager();
    
    ThreadPool threadPool{8}; // 4个线程的线程池
    struct InstanceData {
        std::unique_ptr<TrafficFlowProvider> provider; 
        std::future<void> simulationThread;
        std::future<void> dataCollectionThread;
        std::future<void> controllerThread;
        std::future<void> hdMapThread;
    };
    std::map<std::string, InstanceData> instances;
    std::mutex instanceMutex;
    // std::chrono::time_point<std::chrono::steady_clock> PreFrameTime;
    // std::chrono::time_point<std::chrono::steady_clock> CurrentFrameTime;
};

void hdMapThread(TrafficFlowProvider* provider);

// 仿真线程函数
void simulationThread(TrafficFlowProvider* provider);

// 数据采集线程函数
void dataCollectionThread(TrafficFlowProvider* provider);

// 主车与障碍物控制线程
void controllerThread(TrafficFlowProvider* provider);