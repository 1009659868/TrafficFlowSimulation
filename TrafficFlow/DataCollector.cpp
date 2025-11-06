#include "DataCollector.h"
DataCollector::DataCollector() : collectCount(0) {}
DataCollector::~DataCollector() {}
void DataCollector::initialize(SConfig& config)
{
    slidingWindowSize = config.getConfigOrDefault<size_t>("slidingWindowSize", 10);
}

// 设置前一帧车辆数据
bool DataCollector::setPreVehicles(const std::vector<TrafficVehicle>& preVeh)
{
    preVehicles = preVeh;
    if (preVehicles.empty())
        return false;
    return true;
}
// 获取前一帧车辆数据
const std::vector<TrafficVehicle> DataCollector::getPreVehicles() const
{
    return preVehicles;
}

vector<TrafficVehicle*> DataCollector::findDisappearedVehicles(vector<TrafficVehicle>& prevVehicles,
                                                               vector<TrafficVehicle>& currentVehicles)
{

    vector<TrafficVehicle*> disappeared;
    disappeared.reserve(preVehicles.size());

    // 创建当前车辆ID的哈希集用于快速查找
    unordered_set<string> currentIDs;
    mutex mtx;

    for_each(execution::par, currentVehicles.begin(), currentVehicles.end(), [&](const auto& vehicle) {
        lock_guard<mutex> lock(mtx);
        currentIDs.insert(vehicle.actorID);
    });

    // 并行查找消失车辆
    vector<bool> isDisappeared(prevVehicles.size());
    transform(execution::par, prevVehicles.begin(), prevVehicles.end(), isDisappeared.begin(),
              [&](auto& vehicle) { return currentIDs.find(vehicle.actorID) == currentIDs.end(); });

    // 收集消失车辆
    // for (size_t i = 0; i < prevVehicles.size(); ++i) {
    //     if (isDisappeared[i]) {
    //         // disappeared.push_back(prevVehicles[i]);
    //         disappeared.push_back(&(prevVehicles[i]));
    //     }
    // }
    for (auto it = prevVehicles.begin(); it != prevVehicles.end(); ++it) {
        if (isDisappeared[it - prevVehicles.begin()]) {
            // disappeared.push_back(prevVehicles[i]);
            // TrafficVehicle* ptr = &(*it);
            disappeared.push_back(&(*it));
        }
        // std::cout << *it << " ";
    }

    return disappeared;
}
const string DataCollector::collectEgoVehiclesData(const std::vector<TrafficVehicle>& vehicles, double offset_x,
                                                   double offset_y, const ConfigTrafficFlow& configTra)
{
    nlohmann::json TrafficdData;
    int vehicleCount = vehicles.size();
    auto& actors = TrafficdData["Actors"] = nlohmann::json::array();
    for (const auto& item : vehicles) {
        nlohmann::json actorData = {
            {"actorID", item.actorID},
            {"assetName", item.vehType},
            {"position", {{"x", item.pos.x + offset_x}, {"y", item.pos.y + offset_y}, {"z", item.pos.z}}},
            {"rotation", {{"pitch", item.rot[0]}, {"roll", item.rot[1]}, {"yaw", (item.rot[2])}}},
        };
        actors.push_back(std::move(actorData));
    }
    TrafficdData["vehicle_count"] = vehicleCount;
    TrafficdData["frameID"] = collectCount++;
    const string vehicleData = TrafficdData.dump();
    return std::move(vehicleData);
}

const string DataCollector::collectTrafficLightData(std::vector<TrafficLight>& lights, double offset_x, double offset_y,
                                                    const ConfigTrafficFlow& configTra)
{
    nlohmann::json TrafficLightData;
    int lightCount = lights.size();

    // 并行构建JSON数组
    auto buildActorJson = [&](const TrafficLight& light) {
        nlohmann::json lightJson;

        // 基本信息
        lightJson["actorID"] = light.actorID;
        // lightJson["assetID"] = light.assetID;

        // 位置信息（应用偏移）
        lightJson["position"] = {{"x", light.pos.x + offset_x}, {"y", light.pos.y + offset_y}, {"z", light.pos.z}};

        // 旋转和缩放信息
        // lightJson["rotation"] = {light.rot[0], light.rot[1], light.rot[2]};
        // lightJson["scale"] = {light.scale[0], light.scale[1], light.scale[2]};

        // 交通灯状态信息
        lightJson["state"] = {{"currentPhaseState", light.state.currentPhaseState},
                              {"currentPhaseIndex", light.state.currentPhaseIndex},
                              {"currentPhaseElapsed", light.state.currentPhaseElapsed},
                              {"currentPhaseDuration", light.state.currentPhaseDuration},
                              {"nextSwitchTime", light.state.nextSwitchTime},
                              {"programID", light.state.programID},
                              {"isActive", light.state.isActive}};

        // 当前相位详细信息（如果可用）
        if (const TrafficLight::PhaseDefinition* phase = light.getCurrentPhase()) {
            lightJson["currentPhase"] = {{"duration", phase->duration},
                                         {"state", phase->state},
                                         {"name", phase->name},
                                         {"minDur", phase->minDur},
                                         {"maxDur", phase->maxDur}};
        }

        // 受控车道信息
        lightJson["controlledLanes"] = light.controlledLanes;

        // 程序信息（只包含程序ID和类型，避免数据过大）
        nlohmann::json programsJson = nlohmann::json::array();
        for (const auto& program : light.programs) {
            programsJson.push_back(
                {{"programID", program.programID}, {"type", program.type}, {"phaseCount", program.phases.size()}});
        }
        lightJson["programs"] = programsJson;

        return lightJson;
    };

    // 并行转换所有交通灯数据
    std::vector<nlohmann::json> lightActorsTemp(lightCount);
    std::transform(std::execution::par, lights.begin(), lights.end(), lightActorsTemp.begin(), buildActorJson);

    // 组装最终JSON
    TrafficLightData["Actors"] = lightActorsTemp;
    TrafficLightData["light_count"] = lightCount;
    TrafficLightData["timestamp"] = libsumo::Simulation::getTime();
    TrafficLightData["collection_time"] = std::chrono::system_clock::now().time_since_epoch().count();
    const string lightData = TrafficLightData.dump();
    return lightData;
}

// 打json数据包
const string DataCollector::collectTrafficVehicleData(std::vector<TrafficVehicle>& vehicles, double offset_x,
                                                      double offset_y, const ConfigTrafficFlow& configTra)
{
    nlohmann::json TrafficdData;
    int vehicleCount = vehicles.size();

    unordered_set<string> prevIDs;
    map<string, TrafficVehicle> preVehiclesMap;

    for_each(execution::par, preVehicles.begin(), preVehicles.end(), [&](const auto& vehicle) {
        lock_guard<mutex> lock(mtx);
        prevIDs.insert(vehicle.actorID);
        preVehiclesMap[vehicle.actorID] = vehicle;
    });

    // 计算当前帧的三种变化类型
    vector<TrafficVehicle*> currentDisappeared = findDisappearedVehicles(preVehicles, vehicles);

    vector<TrafficVehicle*> newAppearing;
    vector<TrafficVehicle*> updatedVehicles;

    newAppearing.reserve(vehicles.size());
    updatedVehicles.reserve(vehicles.size());
    if (popAddedVehQue) {
        while (!addedVehQue.empty()) {
            addedVehQue.pop_front();
        }
        popAddedVehQue = false;
    }
    for_each(execution::par, vehicles.begin(), vehicles.end(), [&](auto& vehicle) {
        if (prevIDs.find(vehicle.actorID) == prevIDs.end()) {
            lock_guard<mutex> lock(mtx);
            newAppearing.push_back(&vehicle);
            addedVehQue.push_back(vehicle);
        } else {
            lock_guard<mutex> lock(mtx);
            updatedVehicles.push_back(&vehicle);
        }
    });
    // 更新滑动窗口状态
    updateSlidingWindows(currentDisappeared, newAppearing);
    // 获取滑窗整合后的数据（去重、多帧保活）
    auto disappearingToSend = getConsolidatedDisappearingVehicles();
    auto appearingToSend = getConsolidatedAppearingVehicles();

    int debugID = 0;
    // 并行构建三类JSON数组
    auto buildActorJson = [&](auto item) {
        // in update
        auto& id = item->actorID;
        if (preVehiclesMap.find(id) != preVehiclesMap.end()) {
            auto& preVeh = preVehiclesMap[id];
            auto &preX = preVeh.pos.x, preY = preVeh.pos.y, preZ = preVeh.pos.z;
            auto& rotPitch = preVeh.rot[0];
            auto &curX = item->pos.x, curY = item->pos.y, curZ = item->pos.z;
            double l = std::sqrt((preX - curX) * (preX - curX) + (preY - curY) * (preY - curY));

            double gapZ = curZ - preZ;

            if (curZ == 0) {
                item->pos.z = preZ;
                // item->rot[0] = rotPitch;
            }
            double pitch = std::atan2(gapZ, l) * M_PI / 180.0;
            item->rot[0] = pitch;
        }

        return nlohmann::json{
            {"actorID", item->actorID},
            {"category", item->vehCategory},
            {"assetName", item->vehType},
            {"currentSpeed", item->vehSpeed},
            {"position", {{"x", item->pos.x + offset_x}, {"y", item->pos.y + offset_y}, {"z", item->pos.z}}},
            {"rotation", {{"pitch", item->rot[0]}, {"roll", item->rot[1]}, {"yaw", item->rot[2]}}}};
    };
    //-------------------------------------------------
    // 消失车辆JSON
    vector<nlohmann::json> removedActorsTemp(disappearingToSend.size());
    transform(execution::par, disappearingToSend.begin(), disappearingToSend.end(), removedActorsTemp.begin(),
              buildActorJson);

    // 新增车辆JSON
    vector<nlohmann::json> addActorsTemp(appearingToSend.size());
    transform(execution::par, appearingToSend.begin(), appearingToSend.end(), addActorsTemp.begin(), buildActorJson);

    // 更新车辆JSON
    vector<nlohmann::json> updateActorsTemp(updatedVehicles.size());
    transform(execution::par, updatedVehicles.begin(), updatedVehicles.end(), updateActorsTemp.begin(), buildActorJson);
    //-------------------------------------------------
    // 组装最终JSON
    nlohmann::json removedActors = nlohmann::json::array();
    removedActors.get_ref<nlohmann::json::array_t&>().reserve(removedActorsTemp.size());
    for (auto&& actor : removedActorsTemp) {
        removedActors.push_back(move(actor));
    }

    nlohmann::json addActors = nlohmann::json::array();
    addActors.get_ref<nlohmann::json::array_t&>().reserve(addActorsTemp.size());
    for (auto&& actor : addActorsTemp) {
        addActors.push_back(move(actor));
    }

    nlohmann::json updateActors = nlohmann::json::array();
    updateActors.get_ref<nlohmann::json::array_t&>().reserve(updateActorsTemp.size());
    for (auto&& actor : updateActorsTemp) {
        updateActors.push_back(move(actor));
    }
    //-------------------------------------------------
    TrafficdData["removedActors"] = move(removedActors);
    TrafficdData["addActors"] = move(addActors);
    TrafficdData["updateActors"] = move(updateActors);
    TrafficdData["vehicle_count"] = vehicleCount;

    const string vehicleData = TrafficdData.dump();
    // cout<<"vehicleCount:"<<vehicleCount<<endl;

    return vehicleData;
}

// 更新滑动窗口
void DataCollector::updateSlidingWindows(std::vector<TrafficVehicle*>& newDisappearing,
                                         std::vector<TrafficVehicle*>& newAppearing)
{
    // 使用移动语义减少拷贝
    disappearingVehWindow.push_back(vector<TrafficVehicle>());
    disappearingVehWindow.back().reserve(newDisappearing.size());
    for (auto&& vehicle : newDisappearing) {
        // if (vehicle->vehCategory == "EGO") {
        //     cout << "DISAPPEARING EGO: " << vehicle->actorID << endl;
        // }
        disappearingVehWindow.back().push_back(move(*vehicle));
    }

    if (disappearingVehWindow.size() > slidingWindowSize) {
        disappearingVehWindow.pop_front();
    }

    appearingVehWindow.push_back(vector<TrafficVehicle>());
    appearingVehWindow.back().reserve(newAppearing.size());
    for (auto&& vehicle : newAppearing) {
        appearingVehWindow.back().push_back(move(*vehicle));
    }

    if (appearingVehWindow.size() > slidingWindowSize) {
        appearingVehWindow.pop_front();
    }
}
// 获取整合后的消失车辆列表（去重）
std::vector<TrafficVehicle*> DataCollector::getConsolidatedDisappearingVehicles()
{
    std::unordered_set<string> seenIDs;
    std::vector<TrafficVehicle*> consolidated;

    // 预分配足够空间
    size_t totalSize = 0;
    for (auto& window : disappearingVehWindow) {
        totalSize += window.size();
    }
    consolidated.reserve(totalSize);

    // 按时间从新到旧遍历窗口
    for (auto it = disappearingVehWindow.rbegin(); it != disappearingVehWindow.rend(); ++it) {
        for (auto& vehicle : *it) {
            // 只添加首次出现的消失车辆
            if (seenIDs.find(vehicle.actorID) == seenIDs.end()) {
                consolidated.push_back(&vehicle);
                seenIDs.insert(vehicle.actorID);
            }
        }
    }
    return consolidated;
}
// 获取整合后的新增车辆列表（去重）
std::vector<TrafficVehicle*> DataCollector::getConsolidatedAppearingVehicles()
{
    std::unordered_set<string> seenIDs;
    std::vector<TrafficVehicle*> consolidated;

    // 预分配足够空间
    size_t totalSize = 0;
    for (const auto& window : appearingVehWindow) {
        totalSize += window.size();
    }
    consolidated.reserve(totalSize);

    // 按时间从新到旧遍历窗口
    for (auto it = appearingVehWindow.rbegin(); it != appearingVehWindow.rend(); ++it) {
        for (auto& vehicle : *it) {
            // 只添加首次出现的新增车辆
            if (seenIDs.find(vehicle.actorID) == seenIDs.end()) {
                consolidated.push_back(&vehicle);
                seenIDs.insert(vehicle.actorID);
            }
        }
    }
    return consolidated;
}