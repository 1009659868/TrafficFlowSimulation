#pragma once
#include "json.hpp"
#include <libsumo/Simulation.h>
#include <libsumo/TraCIConstants.h>
#include <libsumo/TraCIDefs.h>
#include <libsumo/Vehicle.h>
#include <libsumo/libtraci.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <execution>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

#include "public/MRouting.h"

using namespace HDMapStandalone;
using namespace std;
using json = nlohmann::json;

#define VEHICLE_ANIM_FLOAT_MAX 12
#define VEHICLE_SIGNAL_BOOL_MAX 8
#define VEHICLE_TRAJ_MAX 20
#define ACTORNAME_LENGT 256
#define LIGHT_TYPE_MAX 16
#define USERDEFINED_LIGHT_LENGT 256
#define ANIMATION_TYPE_MAX 16

struct TrafficLightState;
struct PhaseDefinition;
struct ProgramLogic;
struct ControlledLink;
struct RoadCoord;
struct Vector3;

struct Vector3
{

    double x, y, z;
    Vector3 operator+(const Vector3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vector3 operator-(const Vector3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vector3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vector3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vector3 operator/(const Vector3& v) const { return {x / v.x, y / v.y, z / v.z}; }

    // 复合赋值运算符
    Vector3& operator+=(const Vector3& v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }
    Vector3& operator-=(const Vector3& v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }
    Vector3& operator*=(double s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    Vector3& operator/=(double s)
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }
    // 相等比较运算符（使用容差值处理浮点数精度问题）
    bool operator==(const Vector3& v) const
    {
        const double epsilon = 1e-9;
        return std::abs(x - v.x) < epsilon && std::abs(y - v.y) < epsilon && std::abs(z - v.z) < epsilon;
    }

    bool operator!=(const Vector3& v) const { return !(*this == v); }

    // 小于比较运算符（用于有序容器）
    bool operator<(const Vector3& v) const
    {
        const double epsilon = 1e-9;
        if (std::abs(x - v.x) > epsilon)
            return x < v.x;
        if (std::abs(y - v.y) > epsilon)
            return y < v.y;
        return z < v.z;
    }

    bool operator>(const Vector3& v) const { return v < *this; }
    bool operator<=(const Vector3& v) const { return !(v < *this); }
    bool operator>=(const Vector3& v) const { return !(*this < v); }
    // 向量运算
    double Length() const { return std::sqrt(x * x + y * y + z * z); }
    Vector3 Normalized() const
    {
        double l = Length();
        return l > 0 ? *this / l : *this;
    }
    double Dot(const Vector3& v) const { return x * v.x + y * v.y + z * v.z; }
    Vector3 Cross(const Vector3& v) const {
        return {
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        };
    }
    string toString() const
    {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "(X=%.3lf,Y=%.3lf,Z=%.3lf)", x, y, z);
        return string(buffer);
    }
    static double Distance(Vector3& A, Vector3& B)
    {
        Vector3 tmp = A - B;
        return tmp.Length();
    }
    static Vector3 Lerp(const Vector3& A, const Vector3& B, double t) {
        return A + (B - A) * t;
    }
    // 哈希函数结构
    struct Hash {
        std::size_t operator()(const Vector3& v) const {
            // 将浮点数转换为整数进行哈希，避免精度问题
            auto toFixed = [](double d) -> int64_t {
                const double precision = 1e6; // 6位小数精度
                return static_cast<int64_t>(std::round(d * precision));
            };
            
            std::size_t h1 = std::hash<int64_t>{}(toFixed(v.x));
            std::size_t h2 = std::hash<int64_t>{}(toFixed(v.y));
            std::size_t h3 = std::hash<int64_t>{}(toFixed(v.z));
            
            // 使用boost::hash_combine类似的组合方式
            std::size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            
            return seed;
        }
    };
};

struct Quaternion
{
    double w, x, y, z;
    static Quaternion Identity() { return {1, 0, 0, 0}; }
    Quaternion operator*(const Quaternion& q) const
    {
        return {w * q.w - x * q.x - y * q.y - z * q.z, w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x, w * q.z + x * q.y - y * q.x + z * q.w};
    }
};
struct VehicleType
{
    // Civilian,Military
    std::vector<std::string> typeList;
    // std::map<std::string, nlohmann::json> typeMap; // 存储任意类型的JSON值
    // Civilian:0.2,Military:0.8 大类密度
    std::map<std::string, nlohmann::json> densityMap; // 存储任意类型的JSON值
    std::map<std::string, std::vector<string>> detailTypeListMap;
    //"001-AcuraRL":0.6,"048-MotorCoach01":0.5,"050-MPV01":0.3
    // Civilian 和 Military 的细节类型密度都放这儿

    std::map<std::string, nlohmann::json> detailParamMap;

    enum class TypeParam { DENSITY, MAXSPEED, SPEEDFACTOR, ACCEL };
    template <typename T> std::optional<T> getTypeParam(const std::string& key, const TypeParam& param)
    {
        auto it = detailParamMap.find(key);
        if (it == densityMap.end()) {
            return std::nullopt; // 键不存在
        }
        try {
            json j = it->second;
            if (TypeParam::DENSITY == param)
                return j["density"].get<T>();
            if (TypeParam::MAXSPEED == param)
                return j["maxSpeed"].get<T>();
            if (TypeParam::SPEEDFACTOR == param)
                return j["speedFactor"].get<T>();
            if (TypeParam::ACCEL == param)
                return j["accel"].get<T>();

        } catch (const json::type_error& e) {
            std::cerr << "类型转换错误 - 键: " << key << ", 期望类型: " << typeid(T).name()
                      << ", 错误信息: " << e.what() << std::endl;
            return std::nullopt;
        } catch (const exception& e) {
            cerr << e.what() << endl;
            return std::nullopt;
        }
    }
    template <typename T> T getTypeParamOrDefault(const std::string& key, const TypeParam& param, const T& defaultValue)
    {
        auto value = getTypeParam<T>(key, param);
        return value.has_value() ? value.value() : defaultValue;
    }

    template <typename T> std::optional<T> getTypeDensity(const std::string& key)
    {
        auto it = densityMap.find(key);
        if (it == densityMap.end()) {
            return std::nullopt; // 键不存在
        }

        try {
            return it->second.get<T>(); // 尝试转换为指定类型
        } catch (const json::type_error& e) {
            std::cerr << "类型转换错误 - 键: " << key << ", 期望类型: " << typeid(T).name()
                      << ", 错误信息: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    template <typename T> T getTypeDensityOrDefault(const std::string& key, const T& defaultValue)
    {
        auto value = getTypeDensity<T>(key);
        return value.has_value() ? value.value() : defaultValue;
    }
};
typedef struct _parameters
{
    string instanceID = ""; // 实例ID,唯一标识
    // string simSpaceID="";   // 仿真区域ID
    // int maxVehicles=-1;     // 车辆总数
    // double density=-1.0;      // 车辆密度
    // double stepLength=-1.0;   // 仿真步长
    // int isTrafficSignal=-1; // 是否开启信号灯

    std::map<string, json> paramMap;
    VehicleType paramVType;
    // 从JSON文件加载参数
    bool loadFromJSON(const std::string& configPath)
    {
        try {
            std::ifstream infile(configPath);
            if (!infile.is_open()) {
                std::cerr << "Warning: Config file not found: " << configPath << " " << std::endl;
                return false;
            }

            json config;
            infile >> config;
            for (json::iterator it = config.begin(); it != config.end(); ++it) {
                paramMap[it.key()] = it.value();
            }

            // for (const auto& item : paramMap["TypeDensity"]) {
            //     for (auto it = item.begin(); it != item.end(); ++it) {
            //         string type = it.key();
            //         float density = it.value();

            //         paramVType.typeList.push_back(type);
            //         paramVType.densityMap[type] = density;
            //     }
            // }

            for (auto& item : paramMap["TypeDensity"].items()) {
                paramVType.typeList.push_back(item.key());
                paramVType.densityMap[item.key()] = item.value().get<float>();
            }

            // for (auto it = paramMap["Type"].begin(); it != paramMap["Type"].end(); ++it) {
            //     string type = it.key();
            //     vector<string> detailTypes;
            //     for (const auto& detailItem : it.value()) {
            //         for (auto detailIt = detailItem.begin(); detailIt != detailItem.end(); ++detailIt) {
            //             string detailType = detailIt.key();
            //             double density = detailIt.value();
            //             detailTypes.push_back(detailType);
            //             paramVType.detailDensityMap[detailType] = density;
            //         }
            //     }
            //     paramVType.detailTypeListMap[type] = detailTypes;
            // }
            for (auto& typeItem : paramMap["Type"].items()) {
                string type = typeItem.key();
                vector<string> detailTypes;
                for (auto& obj : typeItem.value()) {
                    for (auto& item : obj.items()) {
                        string detailType = item.key();

                        detailTypes.push_back(detailType);
                        paramVType.detailParamMap[detailType] = item.value();
                    }
                }

                paramVType.detailTypeListMap[type] = detailTypes;
            }
            infile.close();
            cout << "parse successful " << paramVType.typeList[0] << endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error reading config file: " << e.what() << ", using default values" << std::endl;
            return false;
        }
    }

    template <typename T> std::optional<T> getParameter(const std::string& key)
    {
        auto it = paramMap.find(key);
        if (it == paramMap.end()) {
            return std::nullopt; // 键不存在
        }

        try {
            return it->second.get<T>(); // 尝试转换为指定类型
        } catch (const json::type_error& e) {
            std::cerr << "类型转换错误 - 键: " << key << ", 期望类型: " << typeid(T).name()
                      << ", 错误信息: " << e.what() << std::endl;
            return std::nullopt;
        }
    }
    template <typename T> T getParameterOrDefault(const std::string& key, const T& defaultValue)
    {
        auto value = getParameter<T>(key);
        return value.has_value() ? value.value() : defaultValue;
    }
} Parameters;

typedef struct startConfig
{
    // std::string instanceID;
    // std::string simSpaceID;
    // std::string startType;
    // std::string version;
    // int maxVehicles;
    // double density;
    // double stepLength;
    // int isTrafficSignal;
    // int frameRate;
    // double offset_x;
    // double offset_y;

    std::map<std::string, nlohmann::json> configMap; // 存储任意类型的JSON值
    template <typename T> std::optional<T> getConfig(const std::string& key)
    {
        auto it = configMap.find(key);
        if (it == configMap.end()) {
            return std::nullopt; // 键不存在
        }

        try {
            return it->second.get<T>(); // 尝试转换为指定类型
        } catch (const json::type_error& e) {
            std::cerr << "类型转换错误 - 键: " << key << ", 期望类型: " << typeid(T).name()
                      << ", 错误信息: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    // 带默认值的访问函数
    // std::string host = config.getConfigOrDefault<std::string>("host", "127.0.0.1");
    template <typename T> T getConfigOrDefault(const std::string& key, const T& defaultValue)
    {
        auto value = getConfig<T>(key);
        return value.has_value() ? value.value() : defaultValue;
    }
} SConfig;

class ConfigTrafficFlow
{
    public:
    startConfig sConfig;
    string sumoBinary;
    string route;
    string sumocfg;
    string net;
    string xodr;
    string setting;
    string configVehicles;
    vector<string> options;
};

// enum class VehicleType {
//     AcuraRL ,
//     MotorCoach01,
//     MPV01,
//     Truck04,
//     Van02,
//     MixerTruck01,
//     COUNT // 用于获取枚举项数量
// };
typedef struct Navigation_
{
    /* data */
    vector<string> idList;                                // 存储自动路线计算车辆ID
    std::map<string, std::pair<Vector3, Vector3>> navPos; // 存储车辆对应起点和终点
    std::map<string, vector<string>> navRoutes;           // 存储对应车辆的路由信息

} Navigation;

typedef struct RedisConfig_
{
    std::map<std::string, std::string> ipMap;
    std::string getMappedIp(const std::string& originalIp)
    {
        auto it = ipMap.find(originalIp);
        if (it != ipMap.end()) {
            return it->second;
        }
        return "";
    }

    // 设置IP映射
    void setIpMap(const std::map<std::string, std::string>& map) { ipMap = map; }
    std::map<std::string, nlohmann::json> configMap; // 存储任意类型的JSON值
    // 安全访问模板函数
    // auto portOpt = config.getConfig<int>("port");
    // if (portOpt) {
    //     int port = *portOpt; // 解引用获取值
    //     // 使用port...
    // } else {
    //     // 处理缺失值的情况
    // }
    template <typename T> std::optional<T> getConfig(const std::string& key)
    {
        auto it = configMap.find(key);
        if (it == configMap.end()) {
            return std::nullopt; // 键不存在
        }

        try {
            return it->second.get<T>(); // 尝试转换为指定类型
        } catch (const json::type_error& e) {
            std::cerr << "类型转换错误 - 键: " << key << ", 期望类型: " << typeid(T).name()
                      << ", 错误信息: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    // 带默认值的访问函数
    // std::string host = config.getConfigOrDefault<std::string>("host", "127.0.0.1");
    template <typename T> T getConfigOrDefault(const std::string& key, const T& defaultValue)
    {
        auto value = getConfig<T>(key);
        return value.has_value() ? value.value() : defaultValue;
    }

} RedisConfig;

// 车辆控制参数结构体
struct VehicleControlParams
{
    double maxSpeed = 55.5;             // 最大速度 (m/s)
    double maxAcceleration = 4.0;       // 加速度 (m/s²)
    double maxDeceleration = 8.0;       // 刹车减速度 (m/s²)
    double safeFollowingDistance = 5.0; // 安全距离 (m)
    double brakingDistance = 15.0;      // 减速距离 (m)
    double maxAngularSpeed = 90.0;      // 最大转向速度 (度/秒)
    double routeFollowWeight = 0.7;     // 路径跟随权重
    double collisionAvoidWeight = 1.5;  // 避让权重
};
// 定义车辆订阅数据结构
struct VehicleSubscriptionData
{
    std::string type;                // 车辆类型
    libsumo::TraCIPosition position; // 位置
    double speed;                    // 速度
    double angle;                    // 旋转角度
    double accel;                    // 加速度
    double decel;                    // 减速度
    double emergencyDecel;           // 紧急减速度
    double waitingTime;              // 等待时间
    int signals;                     // 信号状态
    std::string toString() const
    {
        std::ostringstream oss;
        oss << "VehicleSubscriptionData:\n";
        oss << "  Type: " << type << "\n";
        oss << "  Position: (" << std::fixed << std::setprecision(2) << position.x << ", " << position.y << ", "
            << position.z << ")\n";
        oss << "  Speed: " << speed << " m/s\n";
        oss << "  Angle: " << angle << " degrees\n";
        oss << "  Acceleration: " << accel << " m/s²\n";
        oss << "  Deceleration: " << decel << " m/s²\n";
        oss << "  Emergency Deceleration: " << emergencyDecel << " m/s²\n";
        oss << "  Waiting Time: " << waitingTime << " s\n";
        oss << "  Signals: " << signals;
        return oss.str();
    }
};

// 定义道路订阅数据结构
struct RoadSubscriptionData
{
    std::string laneID;                    // 车道ID
    double lanePosition;                   // 车道位置
    std::string roadID;                    // 道路ID
    std::vector<std::string> edges;        // 边ID
    double maxSpeed;                       // 最大速度
    std::vector<std::string> allowed;      // 允许的车辆类型
    std::vector<std::string> disallowed;   // 禁止的车辆类型
    libsumo::TraCINextTLSData nextTLS;     // 下一个交通灯信息
    libsumo::TraCIBestLanesData bestLanes; // 最佳车道信息
    std::string toString() const
    {
        std::ostringstream oss;
        oss << "RoadSubscriptionData:\n";
        oss << "  Lane ID: " << laneID << "\n";
        oss << "  Lane Position: " << lanePosition << " m\n";
        oss << "  Road ID: " << roadID << "\n";

        oss << "  Edges: [";
        for (size_t i = 0; i < edges.size(); ++i) {
            oss << edges[i];
            if (i < edges.size() - 1)
                oss << ", ";
        }
        oss << "]\n";

        oss << "  Max Speed: " << maxSpeed << " m/s\n";

        oss << "  Allowed Vehicle Types: [";
        for (size_t i = 0; i < allowed.size(); ++i) {
            oss << allowed[i];
            if (i < allowed.size() - 1)
                oss << ", ";
        }
        oss << "]\n";

        oss << "  Disallowed Vehicle Types: [";
        for (size_t i = 0; i < disallowed.size(); ++i) {
            oss << disallowed[i];
            if (i < disallowed.size() - 1)
                oss << ", ";
        }
        oss << "]\n";

        oss << "  Next TLS: " << nextTLS.getString() << "\n";
        oss << "  Best Lanes: " << bestLanes.getString();
        return oss.str();
    }
};

// 定义完整的订阅数据结构
struct VehicleSubscriptions
{
    // 主车数据
    VehicleSubscriptionData egoData;

    // 影子车数据
    RoadSubscriptionData shadowData;

    // 主车周围车辆数据
    std::map<std::string, VehicleSubscriptionData> surroundingVehicles;
    std::map<std::string, RoadSubscriptionData> surroundingRoads;
    std::string toString() const
    {
        std::ostringstream oss;
        oss << "===== VehicleSubscriptions =====\n";
        oss << "== Ego Vehicle Data ==\n";
        oss << egoData.toString() << "\n\n";

        oss << "== Shadow Vehicle Data ==\n";
        oss << shadowData.toString() << "\n\n";

        oss << "== Surrounding Vehicles ==\n";
        oss << "Count: " << surroundingVehicles.size() << "\n";
        for (const auto& [vehID, data] : surroundingVehicles) {
            oss << "Vehicle ID: " << vehID << "\n";
            oss << data.toString() << "\n";
        }

        return oss.str();
    }
};

class Actor
{
    public:
    virtual ~Actor() {}
    std::string actorID = "0";
    int assetID = 0;
    bool deleteFlag = false;
    Vector3 pos = {0, 0, 0};
    double rot[3] = {0, 0, 0};
    double size[3] = {0, 0, 0};
    double scale[3] = {1.0, 1.0, 1.0};
};

class TrafficLight : public Actor
{
    public:
    // 交通灯状态结构体
    struct TrafficLightState
    {
        std::string currentPhaseState;     // 当前相位状态字符串 (SUMO风格, 如 "GrGr")[2](@ref)
        int currentPhaseIndex = 0;         // 当前相位索引[2](@ref)
        double currentPhaseElapsed = 0.0;  // 当前相位已运行时间 (秒)
        double currentPhaseDuration = 0.0; // 当前相位的总持续时间 (秒)[2](@ref)
        double nextSwitchTime = 0.0;       // 计划切换到下一相位的绝对仿真时间 (秒)[2](@ref)
        std::string programID;             // 当前信号程序ID[2](@ref)
        bool isActive = true;              // 交通灯是否处于活动状态
    };

    // 相位定义
    struct PhaseDefinition
    {
        double duration;   // 相位持续时间 (秒)[2](@ref)
        std::string state; // 相位状态字符串[2](@ref)
        std::string name;  // 相位名称 (可选)[2](@ref)
        double minDur;     // 最小持续时间 (用于自适应信号控制)
        double maxDur;     // 最大持续时间 (用于自适应信号控制)
    };

    // 信号逻辑程序
    struct ProgramLogic
    {
        std::string programID;                                   // 程序ID[2](@ref)
        int type;                                                // 程序类型(静态/自适应等)
        int currentPhaseIndex;                                   // 当前相位索引
        std::vector<PhaseDefinition> phases;                     // 相位列表[2](@ref)
        std::unordered_map<std::string, std::string> parameters; // 额外参数
    };

    // 受控车道信息
    struct ControlledLink
    {
        std::string incomingLane; // 进口车道[2](@ref)
        std::string outgoingLane; // 出口车道[2](@ref)
        std::string viaLane;      // 通过车道[2](@ref)
    };

    // 成员变量
    TrafficLightState state;
    std::vector<PhaseDefinition> phases;
    std::vector<ProgramLogic> programs;
    std::vector<std::string> controlledLanes;                 // 受控车道列表[2](@ref)
    std::vector<std::vector<ControlledLink>> controlledLinks; // 受控链接列表[2](@ref)
    // 成员函数
    TrafficLight() {}

    virtual ~TrafficLight() {}

    // 更新交通灯状态（从SUMO获取最新状态）
    void updateState(const string& id)
    {
        try {
            // 获取当前相位索引[2](@ref)
            state.currentPhaseIndex = libsumo::TrafficLight::getPhase(id);

            // 获取当前相位持续时间[2](@ref)
            state.currentPhaseDuration = libsumo::TrafficLight::getPhaseDuration(id);

            // 获取下一个切换时间[2](@ref)
            state.nextSwitchTime = libsumo::TrafficLight::getNextSwitch(id);

            // 获取当前相位状态[2](@ref)
            state.currentPhaseState = libsumo::TrafficLight::getRedYellowGreenState(id);

            // 获取当前程序ID[2](@ref)
            state.programID = libsumo::TrafficLight::getProgram(id);

            // 计算已运行时间（需要记录上次更新时间）
            static std::unordered_map<std::string, double> lastUpdateTime;
            double currentTime = libsumo::Simulation::getTime();
            if (lastUpdateTime.find(id) != lastUpdateTime.end()) {
                double elapsed = currentTime - lastUpdateTime[id];
                state.currentPhaseElapsed += elapsed;
            }
            lastUpdateTime[id] = currentTime;

        } catch (const std::exception& e) {
            std::cerr << "Error updating traffic light state from SUMO: " << e.what() << std::endl;
        }
    }
    // 更新受控车道信息
    void updateControlledLanes(const std::string& id)
    {
        try {
            controlledLanes = libsumo::TrafficLight::getControlledLanes(id);
        } catch (const std::exception& e) {
            std::cerr << "Error getting controlled lanes for traffic light " << id << ": " << e.what() << std::endl;
        }
    }
    // 更新程序信息
    void updatePrograms(const std::string& id)
    {
        try {
            programs.clear(); // 清空现有程序

            auto logics = libsumo::TrafficLight::getAllProgramLogics(id);
            for (const auto& logicItem : logics) {
                ProgramLogic logic;
                logic.programID = logicItem.programID;
                logic.type = logicItem.type;
                logic.currentPhaseIndex = logicItem.currentPhaseIndex;

                for (const auto& phaseItem : logicItem.phases) {
                    PhaseDefinition phase;
                    phase.duration = phaseItem->duration;
                    phase.state = phaseItem->state;
                    phase.name = phaseItem->name;
                    phase.minDur = phaseItem->minDur;
                    phase.maxDur = phaseItem->maxDur;
                    logic.phases.push_back(phase);
                }
                programs.push_back(logic);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error getting program logics for traffic light " << id << ": " << e.what() << std::endl;
        }
    }

    const PhaseDefinition* getCurrentPhase() const
    {
        if (programs.empty())
            return nullptr;

        // 查找当前程序
        for (const auto& program : programs) {
            if (program.programID == state.programID) {
                if (program.phases.size() > state.currentPhaseIndex) {
                    return &program.phases[state.currentPhaseIndex];
                }
                break;
            }
        }
        return nullptr;
    }

    // 设置交通灯相位[2](@ref)
    void setPhase(int phaseIndex)
    {
        try {
            libsumo::TrafficLight::setPhase(actorID, phaseIndex);
            state.currentPhaseIndex = phaseIndex;
            state.currentPhaseElapsed = 0.0;
        } catch (const std::exception& e) {
            std::cerr << "Error setting traffic light phase: " << e.what() << std::endl;
        }
    }
    // 设置相位持续时间[2](@ref)
    void setPhaseDuration(double duration)
    {
        try {
            libsumo::TrafficLight::setPhaseDuration(actorID, duration);
            state.currentPhaseDuration = duration;
        } catch (const std::exception& e) {
            std::cerr << "Error setting phase duration: " << e.what() << std::endl;
        }
    }

    // 设置程序逻辑[2](@ref)
    void setProgram(const std::string& programID)
    {
        try {
            libsumo::TrafficLight::setProgram(actorID, programID);
            state.programID = programID;
        } catch (const std::exception& e) {
            std::cerr << "Error setting traffic light program: " << e.what() << std::endl;
        }
    }
};

class TrafficVehicle : public Actor
{
    public:
    std::string vehType;
    std::string vehCategory;
    float vehSpeed;
    struct VehicleState
    {
        Vector3 position;           // 当前位置
        Vector3 velocity;           // 当前速度
        Vector3 acceleration;       // 当前加速度
        Vector3 targetAcceleration; // 目标加速度
        Quaternion rotation;        // 当前旋转（四元数）
        Quaternion targetRotation;  // 目标旋转
        double currentSpeed = 0.0f;
        double currentYaw = 0.0f; // 当前偏航角（度）
        double targetSpeed = 0.0f;
        double targetYaw = 0.0f;        // 目标偏航角（度）
        double maxSpeed = 33.30f;       // 最大速度（m/s）
        double maxAcceleration = 10.0;  // 最大加速度（m/s²）
        double maxAngularSpeed = 180.0; // 最大旋转速度（度/秒）
        double arrivalThreshold = 0.7;  // 到达路径点的阈值距离（米）
        double minDistance = 0.5;
        double brakingDistance = 10.0; // 开始减速的距离（米）
        bool avoidCollision = true;
        double avoidanceDistance = 500.0;  // 避让距离（米）
        double minFollowingDistance = 5.0; // 最小跟车距离（米）
        double avoidanceSpeed = 0;
        double emergencyDeceleration = 10.0; // 紧急减速度（m/s²）
        double followingDeceleration = 4.0;  // 正常跟车减速度（m/s²）
        double minTTC = 10.0;                // 最小时间到碰撞（秒）
    };

    VehicleState state;

    static Vector3 toVector3(const SSD::SimPoint3D& point)
    {
        return Vector3{static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)};
    }
    static Vector3 calculateDirection(const Vector3& current, const Vector3& target)
    {
        Vector3 dir = target - current;
        // {target.x - current.x, target.y - current.y, target.z - current.z};
        // double length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        // if (length > 0.0001f) {
        //     Vector3 tmp{dir.x / length, dir.y / length, dir.z / length};
        //     return tmp.Normalized();
        // }
        return dir.Normalized();
        // return Vector3{0, 0, 0};
    }
    // 计算两点间距离
    static double calculateDistance(const Vector3& a, const Vector3& b)
    {
        double dx = a.x - b.x;
        double dy = a.y - b.y;
        double dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    // 计算旋转角度（偏航角）
    static double calculateYawAngle(const Vector3& direction)
    {
        // 在水平面上计算偏航角（绕Y轴）
        double yaw = 90 - std::atan2(direction.y, direction.x) * 180.0f / M_PI;
        if (yaw < 0) {
            yaw = 360 + yaw;
        }
        return yaw;
    }
};

typedef struct _ThreadParams
{
    void* provider;
    void* redisConnector;
} ThreadParams;

// typedef struct RedisConfig
// {
//     std::string host;
//     int port;
//     std::string password;
//     int db;
//     size_t pool_size;
//     int timeout;
//     int sleep_time_tf;
//     int sleep_time_hm;
//     std::string type;
//     int reTryCount;
//     map<string, string> cfgmap;
// } RedisConfig;
enum class RedisType { HIREDIS, REDIS_PLUS_PLUS };
enum class ConnectionTypes { UNKNOWN, STANDALONE, CLUSTER };

struct Waypoint
{
    double x;
    double y;
    double z;
};
struct RoadInfo
{
    std::string routeID;
    std::string edgeID;
    std::string laneID;
    std::string laneIndex;
    std::string pos;
};

struct RoadCoord
{
    Vector3 position;
    float radius;
    std::string laneID;
    std::string edgeID;
    int laneIndex;
    double lanePos;

    static Vector3 parseCoordinateString(const string& coordStr)
    {
        Vector3 coord;

        sscanf(coordStr.c_str(), "X=%lf Y=%lf Z=%lf", &coord.x, &coord.y, &coord.z);
        coord = coord / Vector3{100, -100, 100};

        return coord;
    }
    // 相等比较运算符
    bool operator==(const RoadCoord& other) const
    {
        const double epsilon = 1e-6;
        return position == other.position && std::abs(radius - other.radius) < epsilon && laneID == other.laneID &&
               edgeID == other.edgeID && laneIndex == other.laneIndex && std::abs(lanePos - other.lanePos) < epsilon;
    }

    bool operator!=(const RoadCoord& other) const { return !(*this == other); }

    // 哈希函数
    struct Hash
    {
        std::size_t operator()(const RoadCoord& rc) const
        {
            std::size_t h1 = Vector3::Hash{}(rc.position);
            std::size_t h2 = std::hash<float>{}(rc.radius);
            std::size_t h3 = std::hash<std::string>{}(rc.laneID);
            std::size_t h4 = std::hash<std::string>{}(rc.edgeID);
            std::size_t h5 = std::hash<int>{}(rc.laneIndex);
            std::size_t h6 = std::hash<double>{}(rc.lanePos);

            // 更好的哈希组合方式（避免冲突）
            std::size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h4 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h5 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h6 + 0x9e3779b9 + (seed << 6) + (seed >> 2);

            return seed;
        }
    };

    // 小于比较运算符（用于有序容器）
    bool operator<(const RoadCoord& other) const
    {
        if (laneID != other.laneID)
            return laneID < other.laneID;
        if (std::abs(lanePos - other.lanePos) > 1e-6)
            return lanePos < other.lanePos;
        if (edgeID != other.edgeID)
            return edgeID < other.edgeID;
        if (laneIndex != other.laneIndex)
            return laneIndex < other.laneIndex;
        if (std::abs(radius - other.radius) > 1e-6)
            return radius < other.radius;
        return position < other.position;
    }

    // 转换为字符串表示（用于调试）
    std::string toString() const
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "RoadCoord{pos=(%.3f,%.3f,%.3f), radius=%.1f, lane=%s, edge=%s, index=%d, pos=%.2f}", position.x,
                 position.y, position.z, radius, laneID.c_str(), edgeID.c_str(), laneIndex, lanePos);
        return std::string(buffer);
    }
};
static tuple<float, Vector3, double> minDistance_PointToShape(Vector3& item,
                                                              std::vector<libsumo::TraCIPosition> lanePoints);
static tuple<float, Vector3, double> minDistance_PointToLane(Vector3& point, libsumo::TraCIPosition& start,
                                                             libsumo::TraCIPosition& end);

/**
    计算点到多边形形状的最小距离
    **/
static tuple<float, Vector3, double> minDistance_PointToShape(Vector3& item,
                                                              std::vector<libsumo::TraCIPosition> lanePoints)
{
    float min_distance = INFINITY;
    Vector3 min_projection;
    double LanePos = 0;
    double dis = 0;
    double accumulatedLength = 0;
    // point to lane ,get the minDistance
    for (int i = 0; i < lanePoints.size() - 1; i++) {
        auto start = lanePoints[i];
        auto end = lanePoints[i + 1];
        Vector3 vStart{start.x, start.y, start.z};
        Vector3 vEnd{end.x, end.y, end.z};
        double segmentLength = Vector3::Distance(vStart, vEnd);
        auto [distance, projection, t] = minDistance_PointToLane(item, start, end);

        if (min_distance > distance) {
            min_distance = distance;
            min_projection = projection;

            // LanePos+=Vector3::Distance(vStart, projection);
            LanePos = accumulatedLength + t * segmentLength;
        }
        accumulatedLength += segmentLength;
    }
    return tuple<float, Vector3, double>(min_distance, min_projection, LanePos);
};
/**
 * 计算点到线段的最小距离
 **/
static std::tuple<float, Vector3, double> minDistance_PointToLane(Vector3& point, libsumo::TraCIPosition& start,
                                                                  libsumo::TraCIPosition& end)
{

    Vector3 vStart{start.x, start.y, start.z};
    Vector3 vEnd{end.x, end.y, end.z};

    Vector3 SE = vEnd - vStart;
    Vector3 SP = point - vStart;

    float laneLength = SE.Length();

    if (laneLength == 0) {
        return std::make_tuple(SP.Length(), vStart, 0.0);
    }

    // 投影比例
    double projectionLength = SP.Dot(SE) / SE.Dot(SE);

    double t = max(0.0, min(1.0, projectionLength));
    // cout<<"t:"<<t<<endl;
    // 投影点
    Vector3 projection = vStart + SE * t;
    // 点到投影点的距离
    Vector3 PP = point - projection;

    return std::make_tuple(PP.Length(), projection, t);
}

static RoadCoord convertToRoad(const Vector3& targetPosition)
{
    try {
        libsumo::TraCIRoadPosition TCI_RoadPosition =
            libsumo::Simulation::convertRoad(targetPosition.x, targetPosition.y);
        string edgeID = TCI_RoadPosition.edgeID;
        float pos = TCI_RoadPosition.pos;
        int laneIndex = TCI_RoadPosition.laneIndex;
        string laneID = edgeID + "_" + to_string(laneIndex);
        libsumo::TraCIPosition projection = libsumo::Simulation::convert3D(edgeID, pos, laneIndex);

        RoadCoord rc{Vector3{projection.x, projection.y, projection.z}, 0, laneID, edgeID, laneIndex, pos};
        return rc;
    } catch (const exception& e) {
        cerr << e.what() << endl;
        return RoadCoord{};
    }
}

static std::pair<string, double> calLanePos(const string& routeID, Vector3& target)
{
    double lanePosition = 0;
    int laneCount = libsumo::Edge::getLaneNumber(routeID);
    double minDistance = INFINITY;
    Vector3 min_projection;
    string minLaneID = routeID + "_" + to_string(0);

    for (int i = 0; i < laneCount; i++) {
        string laneID = routeID + "_" + to_string(i);
        auto laneShape = libsumo::Lane::getShape(laneID);

        auto [distance, projection, lanePos] = minDistance_PointToShape(target, laneShape.value);

        if (minDistance > distance) {

            minDistance = distance;
            minLaneID = laneID;
            lanePosition = lanePos;
            min_projection = projection;
        }
    }
    // printf("Traget(%lf,%lf,%lf)\n",target.x,target.y,target.z);
    // printf("Projection(%lf,%lf,%lf)\n",min_projection.x,min_projection.y,min_projection.z);
    // printf("Distance=%lf , lanePosition=%lf\n",(Vector3::Distance(target,min_projection)),lanePosition);
    // printf("lane: %s \n",&minLaneID);
    return pair<string, double>(minLaneID, lanePosition);
}