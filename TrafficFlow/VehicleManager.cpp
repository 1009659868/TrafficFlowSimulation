#include "VehicleManager.h"
std::vector<std::string> MySplit(const std::string& str, const std::string& delim)
{
    std::vector<std::string> tokens;
    size_t start = 0, end = 0;
    while ((end = str.find(delim, start)) != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + delim.length();
    }
    tokens.push_back(str.substr(start));
    return tokens;
}
VehicleManager::VehicleManager()
    : gen(std::random_device{}()),
      // vehicleTypeMap_{{VehicleType::AcuraRL, "001-AcuraRL"},
      //                                                {VehicleType::MotorCoach01, "048-MotorCoach01"},
      //                                                {VehicleType::MPV01, "050-MPV01"},
      //                                                {VehicleType::Truck04, "053-Truck04"},
      //                                                {VehicleType::Van02, "055-Van02"},
      //                                                {VehicleType::MixerTruck01, "039-MixerTruck01"}},
      nextActorID(1), isSimulationRunning(false)
{
}

VehicleManager::~VehicleManager() {}

void VehicleManager::initialize(SConfig& config, double offset_x, double offset_y, VehicleType vehicleTypes)
{
    // stepLength = config.stepLength;
    // frameRate = config.frameRate;
    // frameInterval = 1000.0 / config.frameRate;
    stepLength = config.getConfigOrDefault<float>("stepLength", 0.03);
    frameRate = config.getConfigOrDefault<int>("frameRate", 30);
    frameInterval = 1000.0 / frameRate;

    this->offset_x = offset_x;
    this->offset_y = offset_y;
    this->vehicleTypes_ = vehicleTypes;
}
// 初始化车辆
void VehicleManager::initializeVehicles(int maxVehicles)
{
    if (!isSimulationRunning)
        return;

    // 获取所有可用路线ID
    vector<string> routeIDs = libsumo::Route::getIDList();
    if (routeIDs.empty()) {
        log_error("Error: No routes available in the network.");
        // cerr << "Error: No routes available in the network." << endl;
        return;
    }
    // long long vehiclesToAdd = static_cast<int>(maxVehicles);
    int targetVehicleCount = maxVehicles;
    int currentVehicleCount = libsumo::Vehicle::getIDCount();
    int vehiclesToAdd = targetVehicleCount - currentVehicleCount;
    if (vehiclesToAdd <= 0) {
        log_info_format("当前已有 %d 辆车，无需添加新车辆", currentVehicleCount);
        return;
    }
    // cout << routeIDs.size() << endl;
    // random_device rd;
    // mt19937 gen(rd());
    uniform_int_distribution<> routeDist(0, routeIDs.size() - 1);
    uniform_real_distribution<> posDist(0.0, 1.0); // 0.0-1.0表示车道位置百分比

    int successCount = 0;
    int attemptCount = 0;
    const int MAX_ATTEMPTS = vehiclesToAdd * 10; // 最大尝试次数
    // 分批添加车辆，避免一次性添加过多导致冲突
    const int BATCH_SIZE = 50;
    // string vehType = "001-AcuraRL";
    string vehType;
    while (successCount < targetVehicleCount && attemptCount < MAX_ATTEMPTS) {
        int batchSize = min(BATCH_SIZE, vehiclesToAdd);
        for (int i = 0; i < batchSize; ++i) {
            attemptCount++;
            // 为每辆车生成唯一ID
            string vehID = "v" + to_string(nextActorID);
            nextActorID++;

            try {
                // 随机选择一个路线
                string routeID = getRandomRouteID(routeIDs, routeDist);
                // string routeID = "r2%0";
                if (routeID.empty()) {
                    log_warning("获取到空路线ID，跳过");
                    continue;
                }

                RoadInfo roadInfo = getRandomRoadInfo(routeID, posDist);
                string pos = roadInfo.pos;
                string laneIndex = roadInfo.laneIndex;
                if (roadInfo.pos.empty()) {
                    log_info_format("路线 %s 无法获取有效位置信息，跳过", routeID.c_str());
                    continue;
                }

                string vCategory = getRandomVehicleType();
                vehType = getRandomDetailVehicleType(vCategory);

                // 使用正确的参数添加车辆
                libsumo::Vehicle::add(vehID, routeID, vehType,
                                      "now",        // depart
                                      "best",       // departLane: 使用计算的车道索引（字符串形式）
                                      roadInfo.pos, // departPos: 纵向位置（字符串形式）
                                      "0",          // departSpeed
                                      "current",    // arrivalLane: 使用默认值"current"
                                      "max",        // arrivalPos: 默认值"max"
                                      "current",    // arrivalSpeed: 默认值"current"
                                      "",           // fromTaz: 默认为空
                                      "",           // toTaz: 默认为空
                                      "",           // line: 默认为空
                                      0,            // personCapacity: 默认为0
                                      0             // personNumber: 默认为0
                );
                libsumo::Simulation::step(0.0);
                successCount++;
            } catch (const std::exception& e) {
                cerr << "Failed to initialize vehicle " << vehID << ": " << e.what() << endl;
            }
        }
        int stabilizationSteps = static_cast<int>(2.0 / stepLength); // 2秒稳定时间
        for (int i = 0; i < stabilizationSteps; i++) {
            try {
                libsumo::Simulation::step(stepLength);

            } catch (const exception& e) {
                log_info_format("仿真步进失败: %s", e.what());
            }
        }
        // 更新还需要添加的车辆数量
        successCount = currentVehicleCount = libsumo::Vehicle::getIDCount();
        vehiclesToAdd = targetVehicleCount - currentVehicleCount;
        // log_info_format("仿真步进后，当前车辆数量: %d , %d, %d , %d", currentVehicleCount, successCount, maxVehicles,
        //                 vehiclesToAdd);
        if (vehiclesToAdd <= 0)
            break;
    }

    // step sim soonly for 30s
    // 获取仿真步长
    // double stepLength = configTra.sConfig.stepLength;
    // if (stepLength <= 0) {
    //     stepLength = 0.01; // 默认步长0.03秒
    // }
    int steps = static_cast<int>(frameRate / stepLength); // 30秒所需的步数

    cout << "Pre-simulation running for " << steps * stepLength << "s (" << steps << " steps)..." << endl;
    // while (true)
    // {
    //     libsumo::Simulation::step(0.0);
    //     if(libsumo::Vehicle::getIDCount()>=maxVehicles-100) break;
    //     cout<<libsumo::Vehicle::getIDCount()<<endl;

    // }

    for (int i = 0; i < steps; i++) {
        libsumo::Simulation::step(stepLength);
    }
    if (attemptCount >= MAX_ATTEMPTS) {
        log_warning("达到最大尝试次数，可能有些车辆无法添加");
    }
    cout << "Pre-simulation completed." << endl;
    cout << "Successfully initialized " << successCount << " of " << targetVehicleCount << " vehicles." << endl;
}

// 添加随机车辆
void VehicleManager::addRandomVehicles(int count)
{
    if (!isSimulationRunning)
        return;

    // 1. 获取路线和车辆类型
    vector<string> routeIDs = libsumo::Route::getIDList();
    if (routeIDs.empty()) {
        cerr << "Error: No routes available." << endl;
        return;
    }

    uniform_int_distribution<> routeDist(0, routeIDs.size() - 1);
    uniform_real_distribution<> posDist(0.0, 1.0);
    string vehType = "001-AcuraRL";
    int successCount = 0;
    for (int i = 0; i < count; ++i) {
        string vehID = "v" + to_string(nextActorID);

        try {
            // 3. 随机选择有效路线
            string routeID = getRandomRouteID(routeIDs, routeDist);
            // string routeID = "r2%0";
            if (routeID == "")
                continue;
            string pos = getRandomRoutePosition(routeID, posDist);
            if (pos == "")
                continue;
            // log_warning("add veh pos=" + pos);
            string vCategory = getRandomVehicleType();
            vehType = getRandomDetailVehicleType(vCategory);
            // 8. 添加车辆
            libsumo::Vehicle::add(vehID,
                                  routeID,  // 完整路线
                                  vehType,  // 车辆类型
                                  "now",    // depart
                                  "random", // 车道ID
                                  "0",      // 位置
                                  "0"       // 初始速度
            );
            // libsumo::Vehicle::setParameter(vehID, "priority", "1");
            // 设置SUMO车辆驾驶行为（安全参数）
            // libsumo::Vehicle::setMinGap(vehID, 5.0);         // 增加最小车距
            // libsumo::Vehicle::setTau(vehID, 2.5);            // 增加反应时间
            // libsumo::Vehicle::setDecel(vehID, 6.0);          // 增加最大减速度
            // libsumo::Vehicle::setSpeedMode(vehID, 0b111111); // 所有安全行为
            // libsumo::Vehicle::setParameter(vehID, "obstacle.avoidance", "true");
            // sumoToActorID[vehID] = nextActorID;
            nextActorID++;
            successCount++;

        } catch (const std::exception& e) {
            string error = e.what();
            if (error.find("null") != string::npos) {
                cerr << "Failed to add vehicle " << vehID << ": Invalid parameter passed (likely empty string)" << endl;
            } else {
                cerr << "Failed to add vehicle " << vehID << ": " << error << endl;
            }
        }
    }
}
// 移除指定数量的车辆
void VehicleManager::removeVehicles(int count)
{
    if (!isSimulationRunning)
        return;

    vector<string> vehIDs = libsumo::Vehicle::getIDList();
    if (vehIDs.empty())
        return;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dist(0, vehIDs.size() - 1);

    for (int i = 0; i < min(count, static_cast<int>(vehIDs.size())); ++i) {
        int index = dist(gen);
        if (index < vehIDs.size()) {
            try {
                
                libsumo::Vehicle::remove(vehIDs[index]);
                // sumoToActorID.erase(vehIDs[index]);

            } catch (const std::exception& e) {
                cerr << "移除车辆失败: " << e.what() << endl;
            }
        }
    }
}

std::vector<TrafficVehicle> VehicleManager::getEgoVehicles()
{
    vector<TrafficVehicle> vehicles;
    if (!isSimulationRunning)
        return vehicles;

    try {
        int start = 0;
        const auto& items = getEgoIDs();
        if (items.size() <= 0)
            return vector<TrafficVehicle>{};
        vehicles.resize(items.size());
        for (const auto& eID : items) {
            auto eData = getEgo(eID);
            TrafficVehicle& vehicle = vehicles[start];
            vehicle.actorID = eID;
            // populateActorData(vehicle, eID);
            auto& pos = eData->pos;
            vehicle.pos.x = pos.x;
            vehicle.pos.y = pos.y;
            vehicle.pos.z = pos.z;
            auto& rot = eData->rot;
            vehicle.rot[0] = rot[0];
            vehicle.rot[1] = rot[1];
            vehicle.rot[2] = rot[2];

            auto& type = eData->vehType;
            vehicle.vehType = type;
            start++;
        }
    } catch (const std::exception& e) {
        cerr << "获取车辆数据错误: " << e.what() << endl;
    }
    return std::move(vehicles);
}

// 获取所有车辆数据
std::vector<TrafficVehicle> VehicleManager::getVehicles()
{
    vector<TrafficVehicle> vehicles;
    if (!isSimulationRunning)
        return vehicles;

    try {

        // 获取当前所有车辆ID
        vector<string> vehIDs = libsumo::Vehicle::getIDList();
        vehicles.resize(vehIDs.size());
        // cout << vehIDs.size() << endl;
        int curSize = vehIDs.size();

        int n = 4;
        int m = curSize % n;
        int size = curSize / n;
        int e, s = 0;

        // 并行处理每个车辆
        vector<future<void>> futures;
        for (int i = 0; i < n; i++) {

            if (i < m) {
                e = s + size + 1;
            } else {
                e = s + size;
            }

            futures.push_back(async(launch::async, [&, s, e] {
                try {
                    int start = s;
                    int end = e;
                    while (start < end) {
                        try {
                            TrafficVehicle& vehicle = vehicles[start];
                            const string& id = vehIDs[start];

                            vehicle.actorID = id;
                            // populateActorData(vehicle, id);

                            auto pos = libsumo::Vehicle::getPosition3D(id);
                            Vector3 pos3D{pos.x, pos.y, pos.z};
                            vehicle.pos = pos3D;
                            // vehicle.pos.x = static_cast<float>(pos.x);
                            // vehicle.pos.y = static_cast<float>(pos.y);
                            // vehicle.pos.z = static_cast<float>(pos.z);

                            // double slope = libsumo::Vehicle::getSlope(id);
                            // double pitch_deg = std::atan(slope / 100.0) * 180.0 / M_PI;
                            // vehicle.rot[0] = static_cast<float>(pitch_deg);
                            vehicle.rot[0] = 0.0f;
                            vehicle.rot[1] = 0.0f;
                            vehicle.rot[2] = static_cast<float>(libsumo::Vehicle::getAngle(id));

                            vehicle.vehType = libsumo::Vehicle::getTypeID(id);
                            if (id.find("p") != std::string::npos) {
                                vehicle.vehCategory = "EGO";
                            } else {
                                vehicle.vehCategory = "FLOW";
                            }
                            vehicle.vehSpeed = libsumo::Vehicle::getSpeed(id);
                            ++start;
                        } catch (const exception& e) {
                            continue;
                        }
                    }
                } catch (exception& e) {
                    cout << "getVeh" << endl;
                    cout << e.what() << endl;
                }
            }));
            s = e;
        }
        int start = s;
        // 等待所有任务完成
        for (auto& f : futures) {
            f.get();
        }
    } catch (const std::exception& e) {
        cerr << "获取车辆数据错误: " << e.what() << endl;
    }

    return vehicles;
}
TrafficVehicle* VehicleManager::getEgo(const std::string& egoID)
{
    auto it = egoMap_.find(egoID);
    if (it != egoMap_.end()) {
        return &it->second;
    }
    return nullptr;
}

const std::vector<std::string>& VehicleManager::getEgoIDs()
{
    return egoIDs;
}
bool VehicleManager::isEgoEmpty()
{
    if (egoIDs.size() > 0)
        return false;
    return true;
}
TrafficVehicle VehicleManager::initializeEgo(const std::string& egoID, const std::string& vType)
{
    TrafficVehicle vehicle;
    vehicle.actorID = egoID;
    // populateActorData(vehicle, egoID);
    vehicle.pos.x = 0;
    vehicle.pos.y = 0;
    vehicle.pos.z = 0;

    vehicle.rot[0] = 0;
    vehicle.rot[1] = 0;
    vehicle.rot[2] = 0;
    vehicle.vehType = vType;
    return vehicle;
}
void VehicleManager::removeEgo(const string& egoID)
{
    try {
        libsumo::Simulation::step(0.0);
        libsumo::Vehicle::remove(egoID);
        log_info("Removed old vehicle: " + egoID);
        // cout << "Removed old vehicle: " << egoID << endl;

        // 同时从内部管理结构中移除
        {
            auto it = find(egoIDs.begin(), egoIDs.end(), egoID);
            if (it != egoIDs.end()) {
                egoIDs.erase(it);
            }
        }

        egoMap_.erase(egoID);
        string actorID = egoID.substr(egoID.find("_") + 1);
        entityIDToEgoIDMap_.erase(actorID);
    } catch (const exception& e) {
        // 车辆可能已经被移除或其他原因不存在，忽略错误
        log_warning(string("Remove old vehicle warning: \n") + e.what());
        // cerr << "Remove old vehicle error: " << e.what() << endl;
    }
}
// 添加主车
bool VehicleManager::addEgoVehicle(const string& actorID, const string& routeID, string& egoID)
{
    try {
        int currentCount = 0;
        bool isRemove = false;
        // 生成唯一的主车ID test
        // std::string egoID = "player_" + std::to_string(nextActorID);
        if (actorIDsSet.find(actorID) != actorIDsSet.end()) {
            // 获取当前计数
            currentCount = actorIDToCounts[actorID];

            // 移除该entityID的所有历史车辆
            for (int i = 0; i <= currentCount; i++) {
                string oldEgoID = "p%" + to_string(i) + "_" + actorID;
                removeEgo(oldEgoID);
            }

            // 递增计数
            currentCount++;
            actorIDToCounts[actorID] = currentCount;
            isRemove = true;
        } else {
            // 首次添加该entityID
            actorIDsSet.insert(actorID);
            actorIDToCounts[actorID] = 0;
            currentCount = 0;
        }

        egoID = "p%" + to_string(currentCount) + "_" + actorID;

        std::string vType = getRandomDetailVehicleType("Military");

        try {

            // 使用正确的参数添加车辆
            libsumo::Vehicle::add(egoID, routeID, vType,
                                  "now",     // depart
                                  "best",    // departLane: 使用计算的车道索引（字符串形式）
                                  "0",       // departPos: 纵向位置（字符串形式）
                                  "10",      // departSpeed
                                  "current", // arrivalLane: 使用默认值"current"
                                  "max",     // arrivalPos: 默认值"max"
                                  "current", // arrivalSpeed: 默认值"current"
                                  "",        // fromTaz: 默认为空
                                  "",        // toTaz: 默认为空
                                  "",        // line: 默认为空
                                  0,         // personCapacity: 默认为0
                                  0          // personNumber: 默认为0
            );

        } catch (const exception& e) {
            cerr << e.what() << endl;
        }

        // 设置主车参数
        libsumo::Vehicle::setParameter(egoID, "priority", "100");

        egoIDs.push_back(egoID);
        entityIDToEgoIDMap_[actorID] = egoID;
        egoMap_[egoID] = initializeEgo(egoID, vType);

        // nextActorID++;
        log_info("成功添加主车: " + egoID + " ->RouteID: " + routeID);
        // std::cout << "成功添加主车: " << egoID << " ->RouteID: " << routeID << std::endl;
        if (isRemove) {
            log_info("已移除entityID=" + actorID + "的所有历史车辆，当前计数: " + to_string(currentCount));
            // std::cout << "已移除entityID=" << actorID << "的所有历史车辆，当前计数: " << currentCount << std::endl;
        }
        return true;
    } catch (const libsumo::TraCIException& e) {
        log_error(string("添加主车失败: ") + e.what());
        // cerr << "添加主车失败: " << e.what() << endl;
        return false;
    }
}

// 获取随机路线ID
std::string VehicleManager::getRandomRouteID(vector<string>& routeIDs, uniform_int_distribution<>& routeDist)
{
    try {
        string routeID = routeIDs[routeDist(gen)];

        if (routeID.empty()) { // 新加：确保路线ID非空
            cerr << "Warning: Selected route ID is empty." << endl;
        }
        return routeID;
    } catch (const std::exception& e) {
        cerr << "Failed to getRandomRouteID " << endl;
    }

    return "";
}
// 获取随机路线位置
std::string VehicleManager::getRandomRoutePosition(string& routeID, uniform_real_distribution<>& posDist)
{
    vector<string> edges = libsumo::Route::getEdges(routeID);
    if (edges.empty()) {
        cerr << "Error: Route " << routeID << " has no edges." << endl;
        return "";
    }

    // 随机选择一条边
    uniform_int_distribution<> edgeDist(0, edges.size() - 1);
    string edgeID = edges[edgeDist(gen)];
    if (edgeID.empty()) { // 新加：确保边ID非空
        cerr << "Warning: Selected edge ID is empty." << endl;
        return "";
    }

    int laneCount = libsumo::Edge::getLaneNumber(edgeID);
    if (laneCount <= 0) {
        cerr << "Error: Edge " << edgeID << " has no lanes." << endl;
        return "";
    }

    // 6. 随机选择一条车道
    uniform_int_distribution<> laneDist(0, laneCount - 1);
    int laneIndex = laneDist(gen);
    string laneID = edgeID + "_" + to_string(laneIndex);

    // 7. 验证车道ID是否有效
    double laneLength = 0;
    try {
        laneLength = libsumo::Lane::getLength(laneID);
    } catch (const std::exception&) {
        cerr << "Warning: Invalid lane ID: " << laneID << ". Using lane index 0." << endl;
        laneID = edgeID + "_0"; // 后备：使用第一条车道
        laneLength = libsumo::Lane::getLength(laneID);
    }

    return to_string(posDist(gen) * laneLength);
}

// 获取随机路线位置
RoadInfo VehicleManager::getRandomRoadInfo(string& routeID, uniform_real_distribution<>& posDist)
{
    vector<string> edges = libsumo::Route::getEdges(routeID);
    if (edges.empty()) {
        cerr << "Error: Route " << routeID << " has no edges." << endl;
        return {};
    }

    // 随机选择一条边
    uniform_int_distribution<> edgeDist(0, edges.size() - 1);
    string edgeID = edges[edgeDist(gen)];
    if (edgeID.empty()) { // 新加：确保边ID非空
        cerr << "Warning: Selected edge ID is empty." << endl;
        return {};
    }

    int laneCount = libsumo::Edge::getLaneNumber(edgeID);
    if (laneCount <= 0) {
        cerr << "Error: Edge " << edgeID << " has no lanes." << endl;
        return {};
    }

    // 6. 随机选择一条车道
    uniform_int_distribution<> laneDist(0, laneCount - 1);
    int laneIndex = laneDist(gen);
    string laneID = edgeID + "_" + to_string(laneIndex);

    // 7. 验证车道ID是否有效
    double laneLength = 0;
    try {
        laneLength = libsumo::Lane::getLength(laneID);
    } catch (const std::exception&) {
        cerr << "Warning: Invalid lane ID: " << laneID << ". Using lane index 0." << endl;
        laneID = edgeID + "_0"; // 后备：使用第一条车道
        laneLength = libsumo::Lane::getLength(laneID);
    }

    RoadInfo roadInfo{routeID, edgeID, laneID, to_string(laneIndex), to_string(posDist(gen) * laneLength)};
    return roadInfo;
}

std::string VehicleManager::getRandomVehicleType()
{
    const auto& typeList = vehicleTypes_.typeList;
    if (typeList.empty()) {
        cerr << "No vehicle types configured" << endl;
        return "";
    }

    float totalTypeDensity = 0;
    for (const auto& type : typeList) {
        // float typeDensity = vehicleTypes_.getTypeParamOrDefault<float>(type,VehicleType::TypeParam::DENSITY, 0.5);
        // cout<<type<<" density:"<<typeDensity<<endl;
        float typeDensity = vehicleTypes_.getTypeDensityOrDefault<float>(type, 0.5);
        totalTypeDensity += typeDensity;
    }
    std::uniform_real_distribution<float> dist(0.0f, totalTypeDensity);
    float randomValue = dist(gen);

    float cumulative = 0.0f;
    for (const auto& type : typeList) {
        float density = vehicleTypes_.getTypeDensityOrDefault<float>(type, 0.5);
        cumulative += density;
        if (randomValue <= cumulative) {
            return type;
        }
    }
    return typeList.back();
}

// 获取随机车辆类型字符串
std::string VehicleManager::getRandomDetailVehicleType(const string& vType)
{
    map<string, vector<string>>& detailTypeListMap = vehicleTypes_.detailTypeListMap;

    if (detailTypeListMap.find(vType) == detailTypeListMap.end()) {
        cerr << "Vehicle type '" << vType << "' not found" << endl;
        return "";
    }

    auto& detailTypes = detailTypeListMap[vType];

    if (detailTypes.empty()) {
        cerr << " No detail types for vehicle type '" << vType << "'" << endl;
    }

    float totalTypeDensity = 0;

    for (const auto& type : detailTypes) {
        float typeDensity = vehicleTypes_.getTypeParamOrDefault<float>(type, VehicleType::TypeParam::DENSITY, 0.5);
        totalTypeDensity += typeDensity;
    }
    std::uniform_real_distribution<float> dist(0.0f, totalTypeDensity);

    float randomValue = static_cast<float>(dist(gen));

    float cumulative = 0.0f;
    for (const auto& type : detailTypes) {

        float density = vehicleTypes_.getTypeParamOrDefault<float>(type, VehicleType::TypeParam::DENSITY, 0.5);
        cumulative += density;
        if (randomValue <= cumulative) {

            return type;
        }
    }

    return detailTypes.back();
}
