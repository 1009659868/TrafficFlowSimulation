#include "TrafficFlowManager.h"

TrafficFlowManager::TrafficFlowManager() {}
TrafficFlowManager::~TrafficFlowManager()
{
    for (auto& instance : instances) {
        instance.second.provider->closeSimulation();
    }
}

bool TrafficFlowManager::createInstance(Parameters& params)
{
    try {
        std::lock_guard<std::mutex> lock(instanceMutex);
        // 防止重复创建
        if (instances.find(params.instanceID) != instances.end())
            return false;

        // 初始化Redis管理器（只需一次）
        static bool redisInitialized = false;
        if (!redisInitialized) {
            RedisManager::getInstance().initialize();
            redisInitialized = true;
        }

        // 创建交通流提供器
        auto provider = std::make_unique<TrafficFlowProvider>();
        // 获取当前工作路径
        char buffer[1024];
        if (getcwd(buffer, sizeof(buffer)) == nullptr) {
            std::cerr << "获取当前路径失败: " << strerror(errno) << std::endl;
            return false;
        }
        std::string currentPath = buffer;
        SumoConfigModifier modifier;
        std::string openDrivePath = currentPath + "/../OpenDrive/";
        // set timer wheel
        if (TimeUtils::isTimer(1767052740)) {
            modifier.addPedestrianParamsToAllConfigs(openDrivePath);
        } else {
            modifier.removePedestrianParamsFromAllConfigs(openDrivePath);
        }
        provider->setCurrentPath(currentPath);

        // 加载配置数据
        provider->loadData(currentPath, params);

        provider->initialize();

        std::string instanceID = provider->getInstanceID();
        // 存储实例数据
        instances[instanceID] = {.provider = std::move(provider),
                                 .simulationThread = {},
                                 .dataCollectionThread = {},
                                 .controllerThread = {},
                                 .hdMapThread = {}};
    } catch (exception e) {
        cout << "createInstance error:" << e.what() << endl;
    }
    cout << "交通流实例 " << params.instanceID << " 创建成功" << endl;
    return true;
}
// 销毁实例
void TrafficFlowManager::removeInstance(const std::string& instanceId)
{
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (auto it = instances.find(instanceId); it != instances.end()) {
        it->second.provider->closeSimulation();
        instances.erase(it);
    }
}
// 获取实例
TrafficFlowProvider* TrafficFlowManager::getInstance(const std::string& instanceId)
{
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (auto it = instances.find(instanceId); it != instances.end()) {
        return it->second.provider.get();
    }
    return nullptr;
}
// 启动 实例 工作线程
void TrafficFlowManager::startInstanceThreads(const std::string& instanceId)
{
    // 线程安全保护
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (auto it = instances.find(instanceId); it != instances.end()) {
        auto& instance = it->second;

        // 启动仿真线程
        instance.simulationThread =
            threadPool.enqueue([provider = instance.provider.get()] { simulationThread(provider); });

        // 启动数据采集线程
        instance.dataCollectionThread =
            threadPool.enqueue([provider = instance.provider.get()] { dataCollectionThread(provider); });

        // 启动控制线程
        instance.controllerThread =
            threadPool.enqueue([provider = instance.provider.get()] { controllerThread(provider); });

        // 启动导航服务线程
        instance.controllerThread = threadPool.enqueue([provider = instance.provider.get()] { hdMapThread(provider); });
    }
}

// 仿真线程
void simulationThread(TrafficFlowProvider* provider)
{
    try {
        if (provider->startSimulation()) {

            {
                // 可能需要等待sumo启动
                // this_thread::sleep_for(chrono::milliseconds(1000));
                cout << "start simulation" << endl;
                // 初始化车辆
                provider->initializeVehicles();
                // 等待车辆生成
                this_thread::sleep_for(chrono::milliseconds(500));

                // 通知初始化完成
                provider->setInitializationComplete(true);
            }

            const int PERFORMANCE_LOG_INTERVAL = provider->getFrameRate();    // 每frameRate帧输出一次性能数据
            const double targetFrameTime = 1000.0 / provider->getFrameRate(); // 毫秒
            int frameCounter = 0;
            auto lastFrameTime = std::chrono::steady_clock::now();

            int consecutiveTimeouts = 0;
            const int MAX_TIMEOUTS = 10;
            // 仿真主循环
            while (provider->getSimRunning()) {

                auto frameStart = std::chrono::steady_clock::now();
                // 计算上一帧实际耗时
                auto lastFrameDuration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(frameStart - lastFrameTime).count();
                lastFrameTime = frameStart;

                // 执行帧处理
                // 添加超时保护
                auto stepFuture = std::async(std::launch::async, [provider]() {
                    auto lock = provider->getUniqueLock();
                    provider->setStepComplete(false);
                    provider->step();
                    // 执行所有挂起的命令
                    provider->executeCommands();
                    provider->setStepComplete(true);
                    lock.unlock();
                });
                auto status = stepFuture.wait_for(std::chrono::milliseconds(5000));
                if (status == std::future_status::timeout) {
                    log_error("仿真步执行超时");
                    if (++consecutiveTimeouts >= MAX_TIMEOUTS) {
                        log_error("仿真线程因连续超时退出");
                        break;
                    }
                    continue;
                }
                consecutiveTimeouts = 0;

                // 记录执行结束时间
                auto frameEnd = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
                // 精确帧率控制,帧平滑
                auto sleepTime = static_cast<int>(targetFrameTime - elapsed);
                if (sleepTime > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
                }

                // 记录整个帧周期
                auto frameEndAfterSleep = std::chrono::steady_clock::now();
                auto totalFrameDuration =
                    std::chrono::duration_cast<std::chrono::microseconds>(frameEndAfterSleep - frameStart).count();
                // 记录帧时间
                provider->logFrameTime(totalFrameDuration);
                // 定期输出性能数据
                if (++frameCounter >= PERFORMANCE_LOG_INTERVAL) {
                    // provider->printTimers();
                    frameCounter = 0;
                    // 重置计时器
                    provider->resetTimers();
                }
            }
            log_info("simulation over");
            provider->closeSimulation();
        }
    } catch (exception e) {
        cout << "仿真主循环错误: " << e.what() << endl;
        // 确保即使出错也设置初始化完成标志
        provider->setInitializationComplete(true);
    }
}
// 数据采集线程
void dataCollectionThread(TrafficFlowProvider* provider)
{
    try {
        // 初始化
        {
            // 等待初始化完成
            cout << "数据采集线程等待初始化完成..." << endl;
            // 等待仿真初始化完成
            provider->waitForInitialization();
            cout << "wait ..." << endl;

            provider->waitForStepComplete();
            cout << "启动数据采集\n" << endl;
        }

        // 数据采集和发送循环
        while (provider->getSimRunning()) {
            // 等待step完成
            provider->waitForStepComplete();
            {
                auto lock = provider->getUniqueLock();
                // 数据采集
                // provider->startTimer("data collection");

                auto vehicles = provider->getVehicles();
                auto lights = provider->getLights();
                // provider->stopTimer("data collection");
                // provider->startTimer("updateToRedis");
                if (lights.size() > 0) {

                    provider->updateToRedis(lights);
                }
                if (vehicles.size() > 0) {

                    provider->updateToRedis(vehicles);
                    libsumo::Simulation::step(0.0);
                    provider->setPreVehicles(vehicles);
                }
                lock.unlock();
                // provider->stopTimer("updateToRedis");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        log_info("dataCollection over");
    } catch (exception e) {
        cout << "数据采集和发送循环错误: " << e.what() << endl;
    }
}

// 主车与障碍物控制线程
void controllerThread(TrafficFlowProvider* provider)
{
    try {
        cout << "controller等待初始化完成..." << endl;
        {
            provider->waitForInitialization();
            provider->waitForStepComplete();

            cout << "controller 启动" << endl;
            // 异步任务
            std::vector<future<bool>> tasks;
            // 路线自动计算任务
            tasks.push_back(std::async(launch::async, std::bind(&TrafficFlowProvider::navigateStep, provider)));
            // 爆炸任务
            tasks.push_back(std::async(launch::async, std::bind(&TrafficFlowProvider::brokenStep, provider)));
            // 验证爆炸任务
            tasks.push_back(std::async(launch::async, std::bind(&TrafficFlowProvider::checkBroken, provider)));
            // 验证导航任务
            tasks.push_back(std::async(launch::async, std::bind(&TrafficFlowProvider::checkNavStop, provider)));
            for (auto& task : tasks) {
                task.get();
            }

            cout << "stop" << endl;
        }
    } catch (const libsumo::TraCIException& e) {
        std::cerr << "Libsumo specific error: " << e.what() << std::endl;

    } catch (exception e) {
        cout << "controllerThread error: " << e.what() << endl;
    }
}
// 高精地图
void hdMapThread(TrafficFlowProvider* provider)
{
    try {
        // wait init
        provider->waitForInitialization();
        provider->waitForStepComplete();
        // get component
        ConfigTrafficFlow& config = provider->getTrafficFlowConfig();
        RedisConfig& redisConfig = provider->getRedisConfig();
        RedisConnector* redisConnector = provider->getRedisConnector();

        if (redisConfig.getConfigOrDefault<std::string>("runHDMAP", "false") == "false") {
            return;
        }
        // 启动服务
        startHDMap(redisConnector, redisConfig, config.xodr, true);

    } catch (const exception& e) {
        cout << "HDMap Thread error" << endl;
        cout << e.what() << endl;
    }
}
