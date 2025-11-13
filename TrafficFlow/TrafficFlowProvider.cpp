#include "TrafficFlowProvider.h"

TrafficFlowProvider::TrafficFlowProvider()
    : instanceID(""), maxVehicles(0), isTrafficSignal(false), collectCount(0), totalFrameTime(0), frameCount(0),
      frameRate(30.0), frameTime(1000.0 / frameRate), frameInterval(static_cast<int>(frameTime))
{
}
TrafficFlowProvider::~TrafficFlowProvider()
{
    closeSimulation();
}

RedisConfig& TrafficFlowProvider::getRedisConfig()
{
    return redisManager_.getRedisConfig();
}

ConfigTrafficFlow& TrafficFlowProvider::getTrafficFlowConfig()
{
    return configTra;
}

void TrafficFlowProvider::initialize()
{

    redisConnector_ = redisManager_.getRedisConnector(instanceID);

    redisConnector_->config_.configMap["redis_NavigationReq"] = instanceID + ":NavigationReqList";
    redisConnector_->config_.configMap["redis_NavigationRsp_points"] = instanceID + ":NavigationRsp_points";
    simController_ = std::make_unique<TrafficSimulationController>();
    vehicleManager_ = std::make_unique<VehicleManager>();
    dataCollector_ = std::make_unique<DataCollector>();
    lightManager_ = std::make_unique<LightManager>();

    simController_->initialize(configTra.sConfig);
    dataCollector_->initialize(configTra.sConfig);
    vehicleManager_->initialize(configTra.sConfig, offset_x, offset_y, vehicleTypes);
    lightManager_->initialize(configTra.sConfig, offset_x, offset_y);
    initStatus();
}

// 初始化车辆
void TrafficFlowProvider::initializeVehicles()
{
    vehicleManager_->initializeVehicles(maxVehicles);
}

void TrafficFlowProvider::shutdown()
{
    closeSimulation();
    redisManager_.releaseConnector(instanceID);
}

bool TrafficFlowProvider::isEgoEmpty()
{
    return vehicleManager_->isEgoEmpty();
}

// 启动仿真
bool TrafficFlowProvider::startSimulation()
{
    bool success = simController_->startSimulation(configTra.options);
    simController_->initializeVehicles(vehicleTypes);
    // update status
    vehicleManager_->updateSimStatus(simController_->getSimRunning());
    lightManager_->updateSimStatus(simController_->getSimRunning());
    return success;
}
// 关闭仿真
void TrafficFlowProvider::closeSimulation()
{
    // 清理逻辑
    std::cout << "关闭模拟实例: " << instanceID << std::endl;

    // 释放资源
    if (simController_) {
        simController_.reset();
        simController_ = nullptr;
    }
}

bool TrafficFlowProvider::getSimRunning()
{
    return simController_->getSimRunning();
}

// 按帧推进仿真
void TrafficFlowProvider::step()
{
    simController_->step();
    // 动态调整车辆数量
    int currentCount = libsumo::Vehicle::getIDCount();
    if (currentCount < maxVehicles) {
        // 添加车辆
        int toAdd = static_cast<int>(maxVehicles) - currentCount;
        vehicleManager_->addRandomVehicles(min(
            toAdd, configTra.sConfig.getConfigOrDefault<int>("addMax", 1))); // 每次最多添加n辆,可以在配置文件中进行配置
    }
}
// 判断是否能够写入当前帧
bool TrafficFlowProvider::shouldWriteThisFrame()
{
    try {
        std::string status = redisManager_.readFromRedis(instanceID, "status");

        return status == "true";
    } catch (const std::exception& e) {
        std::cerr << "获取状态失败: " << e.what() << std::endl;
        return false;
    }
}
void TrafficFlowProvider::reverseKey()
{
    try {

        redisManager_.writeToRedis(instanceID, "status", "false");
    } catch (const std::exception& e) {
        std::cerr << "反转状态失败: " << e.what() << std::endl;
    }
}
void TrafficFlowProvider::initStatus()
{
    try {
        redisManager_.writeToRedis(instanceID, "status", "true");
    } catch (const std::exception& e) {
        std::cerr << "初始化状态失败: " << e.what() << std::endl;
    }
}
bool TrafficFlowProvider::checkNavStop()
{
    constexpr auto CHECK_INTERVAL = std::chrono::milliseconds(10000);
    while (getSimRunning()) {
        if (!redisConnector_->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            continue;
        }
        log_info("checkNavStop ...");

        std::deque<TrafficVehicle> disappearVehQue = dataCollector_->tryGetDisappearingVehicles();
        while (!disappearVehQue.empty()) {
            try {
                TrafficVehicle item = disappearVehQue.front();
                disappearVehQue.pop_front();

                vehicleManager_->removeEgo(item.actorID);
            } catch (const exception& e) {
                log_error(string("remove disappearing error: ") + e.what());
            }
        }

        auto actorIDs = vehicleManager_->getEgoIDs();
        vector<string> toRemove;
        for (const auto& item : actorIDs) {
            auto [laneID, lanePosition] = vehicleManager_->getEgoStopPoint(item);
            string curLaneID = libsumo::Vehicle::getLaneID(item);
            double curLanePosition = libsumo::Vehicle::getLanePosition(item);
            if (curLaneID != laneID)
                continue;
            if (std::abs(curLanePosition - lanePosition) <= 3.5) {
                toRemove.push_back(item);
            }
        }
        for (const auto& item : toRemove) {
            vehicleManager_->removeEgo(item);
        }
        string event;
        try {
            event = redisManager_.readFromRedis(instanceID, "ControllEvent");
            if (event.empty()) {
                std::this_thread::sleep_for(CHECK_INTERVAL);
                continue;
            }
        } catch (const exception& e) {
            cerr << e.what() << endl;
        }
        try {
            json eventJson = json::parse(event);
            string timeStamp = eventJson["TimeStamp"].get<string>();
            if (timeStamp_navEvent != timeStamp) {
                timeStamp_navEvent = timeStamp;

                vector<string> actors = eventJson["Actors"];

                for (auto& entityID : actors) {
                    const string egoID = vehicleManager_->entityIDToEgoID(entityID);
                    if (egoID.empty())
                        continue;
                    vehicleManager_->removeEgo(egoID);
                }
            }
        } catch (const exception& e) {
            log_error(e.what());
        }

        std::this_thread::sleep_for(CHECK_INTERVAL);
    }
    return true;
}
bool TrafficFlowProvider::navigateStep()
{
    vector<string> routeIDs = libsumo::Route::getIDList();
    unordered_set<string> routeIDsSet;

    for (const auto& item : routeIDs) {
        if (routeIDsSet.find(item) == routeIDsSet.end()) {
            routeIDsSet.insert(item);
        }
    }
    while (getSimRunning()) {
        if (!redisConnector_->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            continue;
        }
        log_info("navStep ...");
        vector<Route> routeList;
        get_route(routeList);

        if (routeList.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            continue;
        }

        unordered_set<string> routeStart;
        bool isSameStartRoute = false;
        for (auto it = routeList.begin(); it != routeList.end(); ++it) {
            try {

                Route navActor = *it;

                if (navActor.route.size() < 1)
                    continue;
                const string& actorID = navActor.actorID;
                string content = generateRouteContent(navActor.route);
                string routeID = content;
                if (routeStart.find(navActor.route[0]) == routeStart.end()) {
                    routeStart.insert(navActor.route[0]);
                    isSameStartRoute = false;
                } else {
                    // routeStart.erase(navActor.route[0]);
                    isSameStartRoute = true;
                }
                if (routeIDsSet.find(routeID) != routeIDsSet.end()) {

                } else {
                    routeIDsSet.insert(routeID);
                    try {
                        libsumo::Route::add(routeID, navActor.route);
                    } catch (const exception& e2) {
                        cerr << e2.what() << endl;
                    }
                }
                std::string egoID;

                waitForStepComplete();

                const bool success = addEgoVehicle(actorID, routeID, egoID);
                if (!success) {
                    log_error(string("addEgo error"));
                    // cerr << "addEgo error" << endl;
                }
                waitForStepComplete();
                vector<string> curRoute = libsumo::Vehicle::getRoute(egoID);

                Vector3 target{navActor.src[0], navActor.src[1], navActor.src[2]};
                target -= Vector3{offset_x, offset_y, 0};

                RoadCoord rc_Start = convertToRoad(target);

                try {

                    libsumo::Vehicle::moveTo(egoID, rc_Start.laneID, rc_Start.lanePos);
                    waitForStepComplete();
                } catch (const exception& e) {
                    log_error(string("moveV error:") + e.what());
                    // cerr << "moveV error:" << e.what() << endl;
                }

                Vector3 targetEnd = Vector3{navActor.dist[0], navActor.dist[1], navActor.dist[2]};
                targetEnd -= Vector3{offset_x, offset_y, 0};

                RoadCoord rc_End = convertToRoad(targetEnd);
                vehicleManager_->setEgoStopPoint(egoID, std::pair<string, double>(rc_End.laneID, rc_End.lanePos));

                try {

                    libsumo::Vehicle::insertStop(egoID, 0, rc_End.edgeID, rc_End.lanePos, rc_End.laneIndex,
                                                 STOP_DURATION, libsumo::STOP_DEFAULT);
                } catch (const exception& e) {
                    log_error(std::string("nav insertStop err:") + e.what());
                    // cerr << "nav insertStop err:" << e.what() << endl;
                }

                std::ostringstream routeStream;
                routeStream << "routes:\n";
                for (const auto& route : curRoute) {
                    routeStream << route << " -> ";
                }
                std::string routeInfo = routeStream.str();

                // for_each(execution::par, curRoute.begin(), curRoute.end(), [&](const auto& route) {
                //     // lock_guard<mutex> lock(mtx);
                //     routeInfo += route + " -> ";
                //     // cout << route << " -> ";
                // });

                log_info_format("当前车辆路由信息\n%s", routeInfo.c_str());
                if (isSameStartRoute)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

            } catch (const exception& e) {
                log_error(string("addEgo error:") + e.what());
                // cerr << "addEgo error:" << e.what() << endl;
            }
        }
        routeStart.clear();

        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    }
    return true;
}

bool TrafficFlowProvider::getBroken(vector<RoadCoord>& brokenList)
{
    string bomb = redisManager_.readFromRedis(instanceID, "BombEvent");
    if (bomb.empty())
        return false;
    json bombJson = json::parse(bomb);

    string timeStamp = bombJson["TimeStamp"].get<string>();

    if (timeStamp_bombEvent == timeStamp) {
        return false;
    }
    timeStamp_bombEvent = timeStamp;

    for (auto& item : bombJson["DataArray"]) {
        string type = item["EntityType"].get<string>();
        if (string::npos == type.find("Road")) {
            continue;
        }
        string bombLocation = item["BombLocation"].get<string>();
        float radius = item["MaxDamageRadius"].get<float>();
        // cout<<bombLocation<<endl;
        RoadCoord broken;
        broken.position = RoadCoord::parseCoordinateString(bombLocation);
        broken.position = broken.position - Vector3{offset_x, offset_y, 0};

        broken.radius = radius;
        brokenList.push_back(broken);
    }
    return true;
}
void TrafficFlowProvider::processVehicleBatch(const vector<string>& vIDs)
{
    // 获取当前爆炸点快照，避免锁竞争
    vector<RoadCoord> bombSnapshots;
    {
        std::shared_lock lock(bombProjectionMutex_);
        bombSnapshots = bombProjectionList;
    }

    if (bombSnapshots.empty())
        return;
    for (const auto& vID : vIDs) {
        if (!getSimRunning())
            break;
        try {
            auto vehPos = libsumo::Vehicle::getPosition3D(vID);
            Vector3 vPos{vehPos.x, vehPos.y, vehPos.z};
            auto vSpeed = libsumo::Vehicle::getSpeed(vID);
            for (const RoadCoord& item : bombSnapshots) {
                Vector3 bTarget = item.position;
                double distance = Vector3::Distance(vPos, bTarget);

                if (distance <= (item.radius + SAFETY_GAP) && vSpeed > 0) {
                    if (distance < item.radius) {
                        pushCommand([vID]() {
                            try {
                                libsumo::Vehicle::remove(vID);
                            } catch (...) {
                            }
                        });
                        log_info(string("紧急 移除：") + vID);
                        // cout << "紧急 移除：" << vID << endl;
                        break;
                    } else if (distance <= (item.radius + SAFETY_GAP / 2)) {
                        if ((vehicleBrakeCount_[vID]++) > 5) {
                            pushCommand([vID]() {
                                try {
                                    libsumo::Vehicle::remove(vID);
                                } catch (...) {
                                }
                            });
                            vehicleBrakeCount_.erase(vID);
                        } else {
                            pushCommand([vID]() {
                                try {
                                    libsumo::Vehicle::setSpeed(vID, 0.0);
                                    libsumo::Vehicle::setAcceleration(vID, 0.0, 3600.0);
                                } catch (...) {
                                }
                            });
                            log_info(string("紧急 制动：") + vID);
                        }

                        // cout << "紧急 制动：" << vID << endl;
                        break;
                    }
                } else {
                }
            }
        } catch (const exception& e) {
            log_error("处理车辆 " + vID + " 失败: " + e.what());
        }
    }
}

bool TrafficFlowProvider::checkBroken()
{
    constexpr auto CHECK_INTERVAL = std::chrono::milliseconds(5000); // 减少检查频率

    auto lastFullCheck = std::chrono::steady_clock::now();
    while (getSimRunning()) {
        if (!redisConnector_->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            continue;
        }
        log_info("checkBroken ...");
        //  cout << "checkBroken ..." << endl;
        auto now = std::chrono::steady_clock::now();
        // 检查是否有爆炸点
        {
            std::shared_lock lock(bombProjectionMutex_);
            if (bombProjectionList.empty()) {
                std::this_thread::sleep_for(CHECK_INTERVAL);
                continue;
            }
        }

        // 分批处理车辆，避免全量遍历
        vector<string> vIDs;
        try {
            vIDs = libsumo::Vehicle::getIDList();
        } catch (const exception& e) {
            log_error(string("获取车辆列表失败: ") + e.what());
            // cerr << "获取车辆列表失败: " << e.what() << endl;
            std::this_thread::sleep_for(CHECK_INTERVAL);
            continue;
        }

        // 分批处理，每批500辆车
        const size_t BATCH_SIZE = 500;
        for (size_t i = 0; i < vIDs.size(); i += BATCH_SIZE) {
            if (!getSimRunning())
                break;

            auto batchStart = vIDs.begin() + i;
            auto batchEnd = (i + BATCH_SIZE < vIDs.size()) ? (vIDs.begin() + i + BATCH_SIZE) : (vIDs.end());
            vector<string> batch(batchStart, batchEnd);
            processVehicleBatch(batch);
            // 小延迟避免过度占用CPU
            if (i + BATCH_SIZE < vIDs.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        {
            std::shared_lock lock(bombProjectionMutex_);
            removeDupliates(bombProjectionList);
        }

        std::this_thread::sleep_for(CHECK_INTERVAL);
    }
    return true;
}

bool TrafficFlowProvider::brokenStep()
{
    constexpr auto PROCESS_INTERVAL = std::chrono::milliseconds(10000);
    while (getSimRunning()) {
        if (!redisConnector_->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            continue;
        }
        log_info("brokenStep ...");
        // cout << "brokenStep ..." << endl;
        vector<RoadCoord> brokenList;
        getBroken(brokenList);

        {
            std::shared_lock lock(bombProjectionMutex_);
            // 处理新增车辆的路由和停车点
            if (!bombProjectionList.empty()) {
                std::deque<TrafficVehicle> appearVehQue = dataCollector_->tryGetAppearingVehicles();

                while (!appearVehQue.empty()) {

                    TrafficVehicle veh = appearVehQue.front();
                    appearVehQue.pop_front();
                    try {
                        libsumo::Vehicle::rerouteTraveltime(veh.actorID);
                        // log_info("为新增车辆 " + veh.actorID + " 重新路由");
                        // cout << "为新增车辆 " << veh.actorID << " 重新路由" << endl;

                        // 为新车辆设置停车点
                        setStopForVehicle(veh.actorID, bombProjectionList);
                    } catch (const exception& e) {
                        // log_error(string("处理新增车辆 时出错: ") + e.what());
                        // cerr << "处理新增车辆 " << " 时出错: " << e.what() << endl;
                    }
                }
                // lock_guard<mutex> lock(dataCollector_->mtx);
                dataCollector_->popAddedVehQue = true;
            }
        }

        if (brokenList.empty()) {
            std::this_thread::sleep_for(PROCESS_INTERVAL);
            continue;
        }

        // 获取范围内的车道
        vector<string> laneInRange;
        vector<string> edgeInRange;
        vector<RoadCoord> projections;
        projections.reserve(brokenList.size());

        for (const auto& item : brokenList) {
            try {
                RoadCoord brokenInfo = convertToRoad(item.position);
                brokenInfo.radius = item.radius;
                laneInRange.push_back(brokenInfo.laneID);
                edgeInRange.push_back(brokenInfo.edgeID);
                projections.push_back(brokenInfo);
                log_info_format("在爆炸位置pos:(%.3f,%.3f,%.3f) 半径:%.1f 范围内找到车道 %s, 投影点位置: %.2f",
                                brokenInfo.position.x, brokenInfo.position.y, brokenInfo.position.z, brokenInfo.radius,
                                brokenInfo.laneID.c_str(), brokenInfo.lanePos);
            } catch (const exception& e) {
                log_error(string("处理爆炸点失败: ") + e.what());
            }
        }
        // 原子性地更新共享容器
        {
            std::unique_lock lock(bombProjectionMutex_);
            bombProjectionList.insert(bombProjectionList.end(), projections.begin(), projections.end());
        }

        if (laneInRange.empty()) {
            log_warning("所有爆炸范围内无车道\n");
            // printf("所有爆炸范围内无车道\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(PROCESS_INTERVAL));
            continue;
        }

        // 设置车道成本
        for (const auto& item : edgeInRange) {
            try {
                libsumo::Edge::setEffort(item, highCost);
            } catch (const exception& e) {
                log_warning("设置边 " + item + string(" 成本时出错 ") + e.what());
            }
        }
        libsumo::Simulation::step(0.0);

        // 处理所有车辆
        vector<string> vehIDs = libsumo::Vehicle::getIDList();

        for_each(execution::par, vehIDs.begin(), vehIDs.end(),
                 [&](string& vID) { setStopForVehicle(vID, projections); });

        std::this_thread::sleep_for(std::chrono::milliseconds(PROCESS_INTERVAL));
    }
    return true;
}

// 改进的 setStopForVehicle 函数
void TrafficFlowProvider::setStopForVehicle(string& vID, vector<RoadCoord>& projections)
{
    // TODO: 参数配置
    const float STOP_DURATION = 3600;
    const float BASE_SAFETY_GAP = 80.0f;       // 增加基础安全距离
    const double MIN_STOP_DISTANCE = 20.0f;    // 最小停车距离
    const float LANE_CHANGE_DISTANCE = 100.0f; // 变道所需距离

    try {
        // 获取车辆当前状态
        auto vehPos = libsumo::Vehicle::getPosition3D(vID);
        Vector3 vPos{vehPos.x, vehPos.y, vehPos.z};

        string currentLane = libsumo::Vehicle::getLaneID(vID);
        int currentLaneIndex = libsumo::Vehicle::getLaneIndex(vID);
        double currentPos = libsumo::Vehicle::getLanePosition(vID);
        double currentSpeed = libsumo::Vehicle::getSpeed(vID);

        // 动态计算安全距离：考虑反应时间 + 制动距离 + 变道距离
        float reactionTime = 2.0f; // 增加反应时间到2秒
        float reactionDistance = currentSpeed * reactionTime;
        float brakingDistance = currentSpeed * currentSpeed / (2 * 2.0f); // 减速度2.0m/s²
        float safetyGap = max(BASE_SAFETY_GAP, reactionDistance + brakingDistance + LANE_CHANGE_DISTANCE);

        // A. 检查车辆是否在爆炸范围内
        for (RoadCoord& item : projections) {
            double distance = Vector3::Distance(vPos, item.position);
            if (distance <= item.radius) {
                try {
                    pushCommand([vID]() { libsumo::Vehicle::remove(vID); });
                    log_info("车辆 " + vID + " 在爆炸范围内, 已移除");
                    return;
                } catch (const exception& e) {
                    log_error("移除车辆 " + vID + string(" 时出错: ") + e.what());
                    pushCommand([vID]() { libsumo::Vehicle::setSpeed(vID, 0); });
                    return;
                }
            }
        }

        // B. 获取车辆完整路由路径和未来路径
        vector<string> route = libsumo::Vehicle::getRoute(vID);
        int currentRouteIndex = libsumo::Vehicle::getRouteIndex(vID);
        string currentEdge = route[currentRouteIndex];

        if (route.empty()) {
            return;
        }

        // C. 为每个爆炸点寻找合适的停车位置
        vector<tuple<string, double, int, double>> potentialStops; // 收集所有可能的停车点

        for (RoadCoord& bomb : projections) {
            // 查找爆炸点所在edge在车辆路由中的位置
            int bombRouteIndex = -1;
            for (int i = currentRouteIndex; i < route.size(); i++) {

                if (route[i] == bomb.edgeID) {
                    bombRouteIndex = i;
                    log_info_format("veh:%s 将会到达爆炸点:route[%d]=%s  当前route:%s\n", vID.c_str(), i,
                                    route[i].c_str(), route[currentRouteIndex].c_str());
                    break;
                }
            }

            if (bombRouteIndex == -1) {
                // cout<<"GGB-7"<<endl;
                continue; // 车辆不会经过这个爆炸点
            }

            // 计算停车参数
            string stopEdge;
            double stopPos = 0.0;
            int stopLaneIndex = 0;
            bool needToStop = false;

            if (bombRouteIndex == currentRouteIndex) {
                // 车辆在爆炸点所在的edge上
                if (!isAhead(vID, vPos, bomb.position)) {
                    log_info("爆炸点在车辆" + vID + "后方");
                    return; // 爆炸点在车辆后方
                }

                double laneLength = libsumo::Lane::getLength(bomb.laneID);
                stopPos = bomb.lanePos - bomb.radius - safetyGap;

                // 确保停车点在合法范围内且大于当前位置
                if (stopPos <= currentPos + MIN_STOP_DISTANCE) {
                    // 停车点太近，尝试紧急制动
                    stopPos = currentPos + MIN_STOP_DISTANCE;
                    if (stopPos >= laneLength - 5.0) { // 保留5米缓冲
                        // 无法在当前edge安全停车，尝试在下一个edge停车
                        if (currentRouteIndex + 1 < route.size()) {
                            stopEdge = route[currentRouteIndex + 1];
                            double nextEdgeLength = libsumo::Lane::getLength(stopEdge + "_0");
                            stopPos = nextEdgeLength - safetyGap; // 在下一个edge起点附近停车
                            stopPos = max(0.0, stopPos);
                        } else {
                            cout << "Faq-5" << endl;
                            continue; // 无法设置合适的停车点
                        }
                    } else {
                        stopEdge = currentEdge;
                    }
                } else {
                    stopEdge = currentEdge;
                }

                needToStop = true;

                // 选择可用的车道索引（尝试所有车道）
                int maxLaneIndex = libsumo::Edge::getLaneNumber(stopEdge) - 1;
                for (int laneIdx = 0; laneIdx <= maxLaneIndex; laneIdx++) {
                    potentialStops.push_back({stopEdge, stopPos, laneIdx, bomb.radius + safetyGap});
                }

            } else {
                // 车辆在爆炸点之前的edge上
                stopEdge = bomb.edgeID;

                // 计算爆炸点所在edge的长度
                double edgeLength = libsumo::Lane::getLength(stopEdge + "_0");
                stopPos = bomb.lanePos - bomb.radius - safetyGap;

                // 处理停车位置可能为负数的情况
                if (stopPos < 0) {
                    // 需要在前一个edge设置停车点
                    if (bombRouteIndex > 0) {
                        string prevEdge = route[bombRouteIndex - 1];
                        double prevEdgeLength = libsumo::Lane::getLength(prevEdge + "_0");
                        stopEdge = prevEdge;
                        stopPos = prevEdgeLength + stopPos;        // stopPos为负，相加后得到正确位置
                        stopPos = max(MIN_STOP_DISTANCE, stopPos); // 确保有最小距离
                    } else {
                        cout << "Faq-3" << endl;
                        continue; // 无法设置合适的停车点
                    }
                } else {
                    stopPos = min(stopPos, edgeLength - 5.0); // 保留5米缓冲
                    if (stopPos < MIN_STOP_DISTANCE) {
                        cout << "Faq-2" << endl;
                        continue; // 停车点太靠近起点，不安全
                    }
                }

                needToStop = true;

                // 在目标edge上选择所有可用的车道
                int maxLaneIndex = libsumo::Edge::getLaneNumber(stopEdge) - 1;
                for (int laneIdx = 0; laneIdx <= maxLaneIndex; laneIdx++) {
                    potentialStops.push_back({stopEdge, stopPos, laneIdx, bomb.radius + safetyGap});
                }
            }
        }

        // D. 尝试所有可能的停车点配置
        for (const auto& [stopEdge, stopPos, stopLaneIndex, bombRadius] : potentialStops) {
            // 验证停车点所在的edge是否在车辆路由中
            bool edgeInRoute = false;
            int stopEdgeIndex = -1;

            for (int i = currentRouteIndex; i < route.size(); i++) {
                if (route[i] == stopEdge) {
                    edgeInRoute = true;
                    stopEdgeIndex = i;
                    log_info_format("veh:%s route[%d]=%s 将经过爆炸点:route[%d]=%s\n", vID.c_str(), currentRouteIndex,
                                    route[currentRouteIndex].c_str(), i, stopEdge.c_str());
                    break;
                }
            }

            if (!edgeInRoute) {
                log_info_format("veh:%s route[%d]=%s 已经过爆炸点:%s\n", vID.c_str(), currentRouteIndex,
                                route[currentRouteIndex].c_str(), stopEdge.c_str());
                continue;
            }

            // 检查停车点是否在车辆前方
            if (stopEdgeIndex == currentRouteIndex && stopPos <= currentPos + 1.0) {
                log_info("在爆炸点附近，移除车辆");
                pushCommand([vID]() {
                    try {
                        libsumo::Vehicle::remove(vID);
                    } catch (...) {
                    }
                });
                return; // 停车点在车辆当前位置或后方
            }

            // 检查是否已存在相似停车点
            bool stopExists = false;
            auto existingStops = libsumo::Vehicle::getNextStops(vID);
            for (const auto& stop : existingStops) {
                if (stop.lane == stopEdge && abs(stop.endPos - stopPos) < bombRadius) {
                    stopExists = true;
                    break;
                }
            }

            if (!stopExists) {
                cout << "执行停车事件" << endl;
                try {
                    log_info_format("currentEdge: %s ,currentEdge[%d] : %s \n", currentEdge.c_str(), currentRouteIndex,
                                    route[currentRouteIndex].c_str());
                    log_info_format("\n为车辆 %s 设置停车点: 边=%s 边长=%.2f, 位置=%.2f, 车道索引=%d, 安全距离=%.2f, "
                                    "\n当前速度=%.2fm/s, 当前边=%s 边长=%.2f, 当前车道索引=%d, 当前车道位置=%.2f",
                                    vID.c_str(), stopEdge.c_str(),
                                    libsumo::Lane::getLength(stopEdge + "_" + to_string(stopLaneIndex)), stopPos,
                                    stopLaneIndex, safetyGap, currentSpeed, currentEdge.c_str(),
                                    libsumo::Lane::getLength(currentEdge + "_" + to_string(currentLaneIndex)),
                                    currentLaneIndex, currentPos);

                    // 清除现有停车点
                    if (!existingStops.empty()) {
                        // 检查车辆是否真的处于停止状态
                        int stopState = libsumo::Vehicle::getStopState(vID);
                        // 如果车辆确实在停车状态（对应停止标志位被设置），才执行resume
                        if ((stopState & 0x1) != 0) {
                            libsumo::Vehicle::resume(vID);
                            libsumo::Simulation::step(0.0);
                        }
                    }
                    currentSpeed *= 0.5;
                    // 先让车辆减速
                    libsumo::Vehicle::setSpeed(vID, currentSpeed);

                    // 插入新停车点
                    libsumo::Vehicle::insertStop(vID, 0, stopEdge, stopPos, stopLaneIndex, STOP_DURATION,
                                                 libsumo::STOP_DEFAULT);
                    log_info("设置停车点成功\n");

                    return; // 成功设置一个停车点

                } catch (const exception& e) {
                    // 继续尝试下一个配置
                    log_warning(e.what() + string("\ntry setStop"));

                    try {
                        libsumo::Vehicle::setStop(vID, stopEdge, stopPos, stopLaneIndex, STOP_DURATION,
                                                  libsumo::STOP_DEFAULT);
                        log_info("设置停车点成功\n");

                        return;
                    } catch (const exception& e1) {
                        log_warning(string("set 失败，尝试下一车道\n") + e1.what());

                        continue;
                    }

                    continue;
                }
            } else {
                // 停车点已存在
                return;
            }
        }

        // E. 如果所有尝试都失败，设置减速
        if (!potentialStops.empty()) {
            libsumo::Vehicle::setSpeed(vID, 0);
            log_warning("车辆 " + vID + " 设置停车点失败，已减速\n");
        }

    } catch (const exception& e) {
        log_error("处理车辆 " + vID + string(" 时出错: ") + e.what());
    }
}
// 是否面向
bool TrafficFlowProvider::isAhead(string& vID, Vector3& vPos, Vector3& target)
{
    try {
        // 方法1: 使用车道位置判断（更可靠）
        string currentLane = libsumo::Vehicle::getLaneID(vID);
        double currentPos = libsumo::Vehicle::getLanePosition(vID);

        // 计算目标点在当前车道上的投影位置
        auto laneShape = libsumo::Lane::getShape(currentLane);

        if (laneShape.value.size() > 0) {

            auto [distance, projection, lanePos] = minDistance_PointToShape(target, laneShape.value);

            // 如果目标点在车辆前方，lanePos应该大于currentPos
            return lanePos > currentPos;
        }

        // 方法2: 备用方法，使用车辆角度
        double angle = libsumo::Vehicle::getAngle(vID);
        double radAngle = angle * M_PI / 180.0;
        Vector3 dir = target - vPos;
        Vector3 forward{std::cos(radAngle), std::sin(radAngle), 0.0};
        double dot = dir.Dot(forward);
        return dot > 0.0;

    } catch (const exception& e) {
        log_warning(string("判断车辆位置出错：") + e.what());
        return false;
    }
}

std::vector<TrafficVehicle> TrafficFlowProvider::getVehicles()
{
    return vehicleManager_->getVehicles();
}
// 获取信号灯
std::vector<TrafficLight> TrafficFlowProvider::getLights()
{
    return lightManager_->getLights();
}
// 添加主车
bool TrafficFlowProvider::addEgoVehicle(const string& actorID, const string& routeID, string& egoID)
{
    return vehicleManager_->addEgoVehicle(actorID, routeID, egoID);
}
// 设置历史帧
bool TrafficFlowProvider::setPreVehicles(const std::vector<TrafficVehicle>& preVeh)
{
    return dataCollector_->setPreVehicles(preVeh);
}

// 更新redis数据
void TrafficFlowProvider::updateToRedis(vector<TrafficVehicle>& vehicles)
{
    // 更新Redis数据
    if (redisConnector_) {
        // 创建JSON对象用于存储交通数据
        const string trafficVehicleData =
            dataCollector_->collectTrafficVehicleData(vehicles, offset_x, offset_y, configTra);

        // 更新车辆计数
        currentCounts_.clear();
        for (auto& item : vehicles) {
            currentCounts_[item.vehType]++;
        }
        // log_info("write to redis");
        // 更新累计统计
        updateTypeStatistics(currentCounts_);

        WriteTrafficDataToRedis("TrafficFlow", std::move(trafficVehicleData));
    }
}
void TrafficFlowProvider::updateToRedis(vector<TrafficLight>& lights)
{
    if (redisConnector_) {
        const string trafficLightData = dataCollector_->collectTrafficLightData(lights, offset_x, offset_y, configTra);

        WriteTrafficDataToRedis("TrafficLight", std::move(trafficLightData));
    }
}

void TrafficFlowProvider::WriteTrafficDataToRedis(const string& key, const string& trafficData)
{

    // if (!redisConnector_ || !redisConnector_->isConnected())
    if (!redisConnector_) {
        std::cerr << "Redis connection is not available!" << std::endl;
        return;
    }

    try {
        redisManager_.writeToRedis(instanceID, key, std::move(trafficData));

        // redisConnector_->set("sumo", trafficData);
        // 调试输出

        // cout << trafficData << endl;

    } catch (const std::exception& e) {
        std::cerr << "Error writing to Redis: " << e.what() << std::endl;
    }
}

void TrafficFlowProvider::parseConfigFile(const std::string& rootPath)
{
    {
        const string configPath = rootPath + "/../config/StartConfig.json";
        std::ifstream infile(configPath);
        if (!infile) {
            cout << "Error occurred " << configPath << endl;
            // std::runtime_error("Error occurred " + configPath);
            return;
        }
        json config;
        infile >> config;
        for (json::iterator it = config.begin(); it != config.end(); ++it) {

            configTra.sConfig.configMap[it.key()] = it.value();
        }
        infile.close();
    }

    try {
        const string vTypePath = rootPath + "/../config/vehicleTypes.json";
        std::ifstream infile(vTypePath);
        if (!infile) {
            cout << "Error occurred " << vTypePath << endl;
            return;
        }

        json config;
        try {
            infile >> config;
        } catch (const json::parse_error& e) {
            cerr << "JSON parse error :" << e.what() << endl;
        }

        if (config.contains("maxVehicles")) {
            configTra.sConfig.configMap["maxVehicles"] = config["maxVehicles"].get<int>();
        }

        // for (const auto& item : config["TypeDensity"]) {
        //     for (auto it = item.begin(); it != item.end(); ++it) {
        //         string type = it.key();
        //         float density = it.value();

        //         vehicleTypes.typeList.push_back(type);
        //         vehicleTypes.densityMap[type] = density;
        //     }
        // }
        auto& typeDensity = config["TypeDensity"];
        for (auto it = typeDensity.begin(); it != typeDensity.end(); ++it) {
            string type = it.key();
            float density = it.value();
            vehicleTypes.typeList.push_back(type);
            vehicleTypes.densityMap[type] = density;
        }
        // for (auto it = config["Type"].begin(); it != config["Type"].end(); ++it) {
        //     string type = it.key();
        //     vector<string> detailTypes;
        //     for (const auto& detailItem : it.value()) {
        //         for (auto detailIt = detailItem.begin(); detailIt != detailItem.end(); ++detailIt) {
        //             string detailType = detailIt.key();
        //             double density = detailIt.value();
        //             detailTypes.push_back(detailType);
        //             vehicleTypes.detailDensityMap[detailType] = density;
        //         }
        //     }
        //     vehicleTypes.detailTypeListMap[type] = detailTypes;
        // }
        for (auto& typeItem : config["Type"].items()) {
            string type = typeItem.key();
            vector<string> detailTypes;
            for (auto& obj : typeItem.value()) {
                for (auto& item : obj.items()) {
                    string detailType = item.key();

                    detailTypes.push_back(detailType);

                    vehicleTypes.detailParamMap[detailType] = item.value();
                }
            }

            vehicleTypes.detailTypeListMap[type] = detailTypes;
        }
        // for(json::iterator it = config.begin();it!=config.end();++it){
        //     vehicleDensityMap_.densityMap[it.key()] = it.value();
        //     vehicleTypeList_.push_back(it.key());
        // }
        infile.close();
    } catch (const json::exception& e) {
        cerr << "Error parsing JSON structure :" << e.what() << endl;
    }
}

void TrafficFlowProvider::coverConfig(startConfig& config, Parameters& parameters)
{
    // 使用传入参数覆盖配置（如果有效）
    // 处理实例ID
    if (!parameters.instanceID.empty()) {
        instanceID = parameters.instanceID;
    } else if (config.configMap.find("instanceID") != config.configMap.end()) {
        // instanceID = config.instanceID;
        instanceID = config.getConfigOrDefault<string>("instanceID", "XD_DEFALT");
    } else {
        instanceID = "default_instance";
    }
    parameters.instanceID = instanceID; // 确保一致性

    // 处理交通信号标志
    if (parameters.paramMap.find("isTrafficSignal") != parameters.paramMap.end()) { // 检查是否设置了有效值
        isTrafficSignal = parameters.getParameterOrDefault<int>("isTrafficSignal", 0);
    } else if (config.configMap.find("isTrafficSignal") != config.configMap.end()) {
        // isTrafficSignal = config.isTrafficSignal;
        isTrafficSignal = config.getConfigOrDefault<int>("isTrafficSignal", 0);
    } else {
        isTrafficSignal = 0; // 默认值
    }
    parameters.paramMap["isTrafficSignal"] = isTrafficSignal; // 确保一致性

    // 处理仿真空间ID
    if (parameters.paramMap.find("simSpaceID") != parameters.paramMap.end()) {
        config.configMap["simSpaceID"] = parameters.getParameterOrDefault<string>("simSpaceID", "950KMXODRFIANL");
        // config.simSpaceID = parameters.simSpaceID;
    } else if (config.configMap.find("simSpaceID") != config.configMap.end()) {
        // 使用config中的值
    } else {
        // config.simSpaceID = "default_space";
        config.configMap["simSpaceID"] = "default_space";
    }
    // parameters.simSpaceID = config.simSpaceID; // 确保一致性
    parameters.paramMap["simSpaceID"] = config.getConfigOrDefault<string>("simSpaceID", "L4"); // 确保一致性
    // 处理最大车辆数
    if (parameters.paramMap.find("maxVehicles") != parameters.paramMap.end()) {
        config.configMap["maxVehicles"] = parameters.getParameterOrDefault<int>("maxVehicles", 0);
        // config.maxVehicles = parameters.maxVehicles;
    } else if (config.configMap.find("maxVehicles") != config.configMap.end())
    // if (config.maxVehicles > 0)
    {
        // 使用config中的值
    } else {
        config.configMap["maxVehicles"] = 1000;
        // config.maxVehicles = 100; // 默认值
    }

    // parameters.maxVehicles = config.maxVehicles; // 确保一致性
    parameters.paramMap["maxVehicles"] = config.getConfigOrDefault<int>("maxVehicles", 1000); // 确保一致性
    // // 处理密度
    // if (parameters.density > 0.0 && parameters.density <= 1.0) {
    //     config.density = parameters.density;
    // } else if (config.density > 0.0 && config.density <= 1.0) {
    //     // 使用config中的值
    // } else {
    //     config.density = 0.3; // 默认值
    // }
    // parameters.density = config.density; // 确保一致性

    // 处理步长
    if (parameters.paramMap.find("stepLength") != parameters.paramMap.end()) {
        // config.getConfigOrDefault<float>("stepLength",0.03);
        config.configMap["stepLength"] = parameters.getParameterOrDefault<float>("stepLength", 0.03);
        // config.stepLength = parameters.stepLength;
    } else if (config.configMap.find("stepLength") != config.configMap.end()) {
        // 使用config中的值
    } else {
        config.configMap["stepLength"] = 0.03;
        // config.stepLength = 0.03; // 默认值
    }
    // parameters.stepLength = config.stepLength; // 确保一致性
    parameters.paramMap["stepLength"] = config.getConfigOrDefault<float>("stepLength", 0.03); // 确保一致性
}
// 加载配置数据
void TrafficFlowProvider::loadData(const std::string& rootPath, Parameters& parameters)
{

    // 启动参数通过json进行配置:
    // 启动参数(是否启用gui) --sumoD 或 --sumo-guiD ,
    // 路网文件名: --name "CityScape4"
    // 仿真步长: --stepLength 0.03
    // 最大车辆数: --maxVehicleNumber 100
    // 车辆密度: --density 0.3
    // ......
    // 是否启用:启动后自动开始仿真 --            默认启用
    // 是否启用:仿真结束时自动退出 --            默认启用
    parseConfigFile(rootPath);
    if (parameters.paramMap.find("Type") != parameters.paramMap.end())
        vehicleTypes = std::move(parameters.paramVType);
    coverConfig(configTra.sConfig, parameters);

    const string configPath = rootPath + "/../OpenDrive/";
#ifdef _WIN64
    const string sumoPath = rootPath + "/../dependencies/sumo/win64/sumoInstall-Debug";
    configTra.sumoBinary = sumoPath + "/bin/" + configTra.sConfig.startType + ".exe";
#else
    // const string sumoPath = rootPath + "/../dependencies/sumo/linux/sumo" + configTra.sConfig.version;
    const string sumoPath = rootPath + "/../dependencies/sumo/linux/sumo" +
                            configTra.sConfig.getConfigOrDefault<string>("version", "1.18.0");
    // configTra.sumoBinary = sumoPath + "/bin/" + configTra.sConfig.startType;
    configTra.sumoBinary = sumoPath + "/bin/" + configTra.sConfig.getConfigOrDefault<string>("startType", "sumo");
#endif
    // const string simSpaceID = configTra.sConfig.simSpaceID;
    const string simSpaceID = configTra.sConfig.getConfigOrDefault<string>("simSpaceID", "L4");
    const string filePath = simSpaceID + "/" + simSpaceID;
    const string basePath = configPath + filePath;

    configTra.sumocfg = basePath + ".sumocfg";
    configTra.route = basePath + ".rou.xml";
    configTra.net = basePath + ".net.xml";
    configTra.xodr = basePath + ".xodr";
    configTra.setting = basePath + ".settings.xml";
    configTra.configVehicles = basePath + ".vehicles.xml";
    // <location netOffset="-192255.52,-2664704.27"
    // convBoundary="0.00,0.00,32796.78,32850.69"
    // origBoundary="192255.52,2664704.27,225052.30,2697554.96"
    // projParameter="!"/> 记录xxx.net.xml中的netOffset
    offset_x = 0;
    offset_y = 0;

    std::ifstream infile(configTra.net);

    if (infile.is_open()) {
        std::string line;
        std::regex pattern("<location netOffset=\"([0-9.+-]+),([0-9.+-]+)\"");
        std::smatch matches;

        while (std::getline(infile, line)) {
            if (std::regex_search(line, matches, pattern)) {
                // std::cout << matches.str(0) << std::endl;

                try {
                    offset_x = -std::stod(matches.str(1));
                    offset_y = -std::stod(matches.str(2));
                } catch (...) {
                }
                break;
            }
        }
        infile.close();
    }
    std::cout << "offset_x,offset_y=" << std::fixed << std::setprecision(2) << offset_x << "," << offset_y << std::endl;

    // maxVehicles = configTra.sConfig.maxVehicles;

    maxVehicles = configTra.sConfig.getConfigOrDefault<int>("maxVehicles", 1000);
    // density = configTra.sConfig.getConfigOrDefault<float>("density",0.6);

    // density = configTra.sConfig.density;
    // 读取帧率参数
    // frameRate = configTra.sConfig.frameRate;
    frameRate = configTra.sConfig.getConfigOrDefault<int>("frameRate", 30);
    frameInterval = 1000.0 / frameRate; // 毫秒

    // 设置选项
    configTra.options = {
        "--configuration",
        configTra.sumocfg,
        "--gui-settings-file",
        configTra.setting,
        "--step-length",
        // to_string(configTra.sConfig.stepLength), // 设置仿真步长为0.1秒
        to_string(configTra.sConfig.getConfigOrDefault<float>("stepLength", 0.03)), // 设置仿真步长为0.1秒
        "--no-warnings",                                                            // 忽略警告信息
        "--time-to-teleport",
        "-1", // 禁用车辆传送
    };
}

// 设置初始化完成标志
void TrafficFlowProvider::setInitializationComplete(bool complete)
{
    std::lock_guard<std::mutex> lock(initMutex_);
    initializationComplete_ = complete;
    initCondition_.notify_all();
}

// 等待初始化完成
void TrafficFlowProvider::waitForInitialization()
{
    std::unique_lock<std::mutex> lock(initMutex_);
    initCondition_.wait(lock, [this] { return initializationComplete_.load(); });
}

// 设置步进完成标志
void TrafficFlowProvider::setStepComplete(bool complete)
{
    std::lock_guard<std::mutex> lock(stepMutex_);
    stepComplete_ = complete;
    stepCondition_.notify_all();
}
// 等待步进完成
void TrafficFlowProvider::waitForStepComplete()
{
    std::unique_lock<std::mutex> lock(stepMutex_);
    stepCondition_.wait(lock, [this] { return stepComplete_.load(); });
}
// void TrafficFlowProvider::setHDMAPComplete(bool complete) {
//     std::lock_guard<std::mutex> lock(navMutex_);
//     navComplete_ = complete;
//     navCondition_.notify_all();
// }
// void TrafficFlowProvider::waitForHDMAPComplete() {
//     std::unique_lock<std::mutex> lock(navMutex_);
//     navCondition_.wait(lock, [this] { return isMapLoaded_.load(); });
// }
void TrafficFlowProvider::startTimer(const std::string& name)
{
    std::lock_guard<std::mutex> lock(timerMutex);
    startTimes[name] = std::chrono::steady_clock::now();
}

void TrafficFlowProvider::stopTimer(const std::string& name)
{
    auto end = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(timerMutex);
    if (startTimes.find(name) == startTimes.end()) {
        return; // 没有对应的开始时间
    }
    auto start = startTimes[name];
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    elapsedTimes[name] += duration;
    callCounts[name]++;
}
void TrafficFlowProvider::printTimers()
{
    std::lock_guard<std::mutex> lock(timerMutex);

    std::cout << "===== Method Execution Time Statistics =====" << std::endl;
    for (const auto& [name, time] : elapsedTimes) {
        long double avg = static_cast<long double>(time) / callCounts[name];
        std::cout << name << ": " << time / 1000.0 << " ms (total), " << avg / 1000.0 << " ms (avg), "
                  << callCounts[name] << " calls" << std::endl;
    }

    if (frameCount > 0) {
        std::cout << "===== Frame Rate Statistics =====" << std::endl;
        std::cout << "Total frames: " << frameCount << std::endl;
        std::cout << "Total time: " << totalFrameTime / 1000.0 << " ms" << std::endl;
        std::cout << "Average frame time: " << static_cast<double>(totalFrameTime) / frameCount / 1000.0 << " ms"
                  << std::endl;
        std::cout << "Average frame rate: " << 1000000.0 / (static_cast<double>(totalFrameTime) / frameCount) << " FPS"
                  << std::endl;
        std::cout << "Target frame time: " << getTargetFrameTime() << " ms" << std::endl;
        std::cout << "Target frame rate: " << getTargetFrameRate() << " FPS" << std::endl;
    }
    std::cout << "============================================" << std::endl;

    printVehicleTypeStats(currentCounts_);
}
void TrafficFlowProvider::printVehicleTypeStats(const std::map<std::string, int>& counts)
{
    std::lock_guard<std::mutex> lock(typeStatsMutex_);

    std::cout << "=============== 车辆类型统计 ===============" << std::endl;

    // 当前帧统计
    int totalCurrent = 0;
    for (const auto& [type, count] : counts) {
        // std::cout << type << ": " << count << " 辆";
        totalCurrent += count;

        // // 计算并显示平均值
        // if (typeStatsFrames_.find(type) != typeStatsFrames_.end() && typeStatsFrames_[type] > 0) {
        //     double avg = static_cast<double>(accumulatedTypeCounts_[type]) / typeStatsFrames_[type];
        //     std::cout << " (平均: " << std::fixed << std::setprecision(2) << avg << ")";
        // }
        // std::cout << std::endl;
    }

    // 总计
    std::cout << "总计: " << totalCurrent << " 辆";
    if (totalFramesForTypeStats_ > 0) {
        double totalAvg =
            static_cast<double>(std::accumulate(accumulatedTypeCounts_.begin(), accumulatedTypeCounts_.end(), 0L,
                                                [](long sum, const auto& pair) { return sum + pair.second; })) /
            totalFramesForTypeStats_;
        std::cout << " (平均总数: " << std::fixed << std::setprecision(2) << totalAvg << ")";
    }
    std::cout << std::endl;

    std::cout << "============================================" << std::endl;
}

// 更新车辆类型统计
void TrafficFlowProvider::updateTypeStatistics(const std::map<std::string, int>& currentCounts)
{
    std::lock_guard<std::mutex> lock(typeStatsMutex_);

    // 更新累计数量
    for (const auto& [type, count] : currentCounts) {
        accumulatedTypeCounts_[type] += count;
    }

    // 更新帧数统计
    for (const auto& [type, _] : accumulatedTypeCounts_) {
        typeStatsFrames_[type]++;
    }

    totalFramesForTypeStats_++;
}
// 重置车辆类型统计
void TrafficFlowProvider::resetTypeStatistics()
{
    std::lock_guard<std::mutex> lock(typeStatsMutex_);
    accumulatedTypeCounts_.clear();
    typeStatsFrames_.clear();
    totalFramesForTypeStats_ = 0;
}
void TrafficFlowProvider::resetTimers()
{
    std::lock_guard<std::mutex> lock(timerMutex);
    startTimes.clear();
    elapsedTimes.clear();
    callCounts.clear();
    totalFrameTime = 0;
    frameCount = 0;
    resetTypeStatistics();
}

void TrafficFlowProvider::logFrameTime(long long duration)
{
    std::lock_guard<std::mutex> lock(timerMutex);
    totalFrameTime += duration;
    frameCount++;
}
