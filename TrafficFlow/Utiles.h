
#ifndef UTILES_H
#define UTILES_H
#include "DataStruct.h"
namespace std{
    template<>
    struct hash<Vector3> {
        std::size_t operator()(const Vector3& v) const {
            return Vector3::Hash{}(v);
        }
    };

    template<>
    struct hash<RoadCoord> {
        std::size_t operator()(const RoadCoord& rc) const {
            return RoadCoord::Hash{}(rc);
        }
    };
};

template<typename T>
static void removeDupliates(std::vector<T>& arr){
    std::unordered_set<T> seen;
    std::vector<T> result;
    result.reserve(arr.size());
    for(const auto& item:arr){
        if(seen.insert(item).second){
            result.push_back(item);
        }
    }
    arr=result;
} 
template <typename T> string to_string0(T t, int s = 2)
{
    string r = std::to_string(t);
    if (r.size() >= s) {
        return r;
    }
    return std::string(s - r.size(), '0') + r;
}
static std::string get_time()
{
    time_t nowtime;
    time(&nowtime); // 获取1970年1月1日0点0分0秒到现在经过的秒数
    tm p;
#ifdef _WIN64
    localtime_s(&p, &nowtime); // 将秒数转换为本地时间,年从1900算起,需要+1900,月为0-11,所以要+1
#else
    localtime_r(&nowtime, &p); // 将秒数转换为本地时间,年从1900算起,需要+1900,月为0-11,所以要+1
#endif

    return std::to_string(p.tm_year + 1900) + "-" + to_string0(p.tm_mon + 1, 2) + "-" + to_string0(p.tm_mday, 2) + " " +
           to_string0(p.tm_hour, 2) + ":" + to_string0(p.tm_min, 2) + ":" + to_string0(p.tm_sec, 2);
}
static void log_info(const std::string& message)
{
    std::cout << get_time() << " [INFO] " << message << std::endl;
}

static void log_warning(const std::string& message)
{
    std::cout << get_time() << " [WARNING] " << message << std::endl;
}

static void log_error(const std::string& message)
{
    std::cerr << get_time() << " [ERROR] " << message << std::endl;
}


template<typename... Args>
static void log_info_format(const char* format, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), format, args...);
    log_info(buffer);
}


static string laneToEdge(const string& laneID)
{
    string edgeID;
    if (laneID.find(":") != string::npos) {
        size_t f_ = laneID.find("_");
        size_t s_ = laneID.find("_", f_ + 1);
        edgeID = laneID.substr(0, s_);
        // cout<<edgeID<<endl;
    } else {
        edgeID = laneID.substr(0, laneID.find("_"));
    }
    return edgeID;
}

static int laneToIndex(const string& laneID)
{
    return stoi(laneID.substr(laneID.find_last_of("_") + 1));
}

static std::string simpleIncrement(const std::string& input)
{
    // 检查是否已经包含 %数字_ 格式
    size_t percentPos = input.find('%');
    if (percentPos != std::string::npos) {
        // 提取数字并递增
        size_t underscorePos = input.find('_', percentPos);
        if (underscorePos != std::string::npos) {
            std::string numStr = input.substr(percentPos + 1, underscorePos - percentPos - 1);
            try {
                int num = std::stoi(numStr);
                return input.substr(0, percentPos + 1) + std::to_string(num + 1) + input.substr(underscorePos);
            } catch (...) {
                // 转换失败，返回原字符串
            }
        }
    } else {
        // 在第一个 _ 后插入 %1_
        size_t firstUnderscore = input.find('_');
        if (firstUnderscore != std::string::npos) {
            return input.substr(0, firstUnderscore + 1) + "%1_" + input.substr(firstUnderscore + 1);
        }
    }
    return input;
}
static size_t calRouteHash(const vector<string>& route)
{
    std::hash<std::string> stringHasher;
    size_t combinedHash = 0;

    // 组合所有道路ID的哈希值
    for (const auto& roadID : route) {
        size_t roadHash = stringHasher(roadID);
        // 使用旋转和异或来组合哈希，减少碰撞
        combinedHash ^= roadHash + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
    }

    return combinedHash;
}
// 将哈希值转换为8位字符串
static std::string hashTo8CharString(size_t hash)
{
    const std::string charSet = "0123456789"
                                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                "abcdefghijklmnopqrstuvwxyz";

    std::string result(8, '0');

    // 使用哈希值的不同部分来生成8个字符
    for (int i = 0; i < 8; i++) {
        // 每次取哈希值的6位（64个可能字符）
        int index = (hash >> (i * 6)) & 0x3F; // 0x3F = 63
        result[i] = charSet[index];
    }

    return result;
}
static string generateRouteContent(const vector<string>& route)
{
    if (route.empty()) {
        return "R00000000_";
    }
    size_t hash = calRouteHash(route);

    string hashStr = hashTo8CharString(hash);

    return "R" + hashStr + "_";
}

namespace TimeUtils
{
// 获取当前时间戳
 std::time_t getCurrentTimestamp();

// 创建日期的时间戳
 std::time_t createTimestamp(int year, int month, int day, int hour = 0, int min = 0, int sec = 0);
 bool isTimeAfter(int year, int month, int day);
 bool isTimer(long t);
// 时间格式化
 std::string formatTime(std::time_t timestamp);
} // namespace TimeUtils
class SumoConfigModifier {
private:
    // 行人参数定义
    const std::vector<std::string> pedestrianParams = {
        "    <pedestrian.model value=\"nonInteracting\"/>",
        "    <pedestrian.striping.stretch value=\"0.8\"/>", 
        "    <pedestrian.remote.port value=\"none\"/>"
    };

    // 递归查找所有 .sumocfg 文件
    std::vector<std::filesystem::path> findSumoConfigFiles(const std::string& directoryPath) {
        std::vector<std::filesystem::path> configFiles;
        
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".sumocfg") {
                    configFiles.push_back(entry.path());
                }
            }
        } catch (const std::filesystem::filesystem_error& ex) {
            std::cerr << "遍历目录错误: " << ex.what() << std::endl;
        }
        
        return configFiles;
    }
    
    // 添加行人参数到配置文件
    bool addPedestrianParamsToConfig(const std::filesystem::path& configPath) {
        std::ifstream inputFile(configPath);
        if (!inputFile.is_open()) {
            // std::cerr << "错误：无法打开配置文件 " << configPath << std::endl;
            return false;
        }
        
        std::vector<std::string> lines;
        std::string line;
        bool hasPedestrianParams = false;
        
        // 读取并分析文件内容
        while (std::getline(inputFile, line)) {
            lines.push_back(line);
            if (line.find("<pedestrian.model") != std::string::npos) {
                hasPedestrianParams = true;
            }
        }
        inputFile.close();
        
        // 如果已经包含行人参数，跳过
        if (hasPedestrianParams) {
            // std::cout << "配置文件 " << configPath.filename() << " 已包含行人参数，跳过" << std::endl;
            return true;
        }
        
        // 查找插入位置
        int insertIndex = -1;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find("<max-depart-delay") != std::string::npos) {
                insertIndex = i + 1;
                break;
            }
        }
        
        if (insertIndex == -1) {
            // 如果找不到 max-depart-delay，在 gui-settings-file 之前插入
            for (size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].find("<gui-settings-file") != std::string::npos) {
                    insertIndex = i;
                    break;
                }
            }
        }
        
        if (insertIndex == -1) {
            // 如果还是找不到，在文件末尾插入
            insertIndex = lines.size();
        }
        
        // 插入行人参数
        std::vector<std::string> newLines;
        for (size_t i = 0; i < lines.size(); ++i) {
            newLines.push_back(lines[i]);
            if (i == insertIndex) {
                for (const auto& param : pedestrianParams) {
                    newLines.push_back(param);
                }
            }
        }
        
        // 如果是插入到末尾的情况
        if (insertIndex == lines.size()) {
            for (const auto& param : pedestrianParams) {
                newLines.push_back(param);
            }
        }
        
        // 写回文件
        std::ofstream outputFile(configPath);
        if (!outputFile.is_open()) {
            // std::cerr << "错误：无法写入配置文件 " << configPath << std::endl;
            return false;
        }
        
        for (const auto& l : newLines) {
            outputFile << l << "\n";
        }
        outputFile.close();
        
        // std::cout << "成功添加行人参数到配置文件: " << configPath.filename() << std::endl;
        return true;
    }
    
    // 从配置文件中移除行人参数
    bool removePedestrianParamsFromConfig(const std::filesystem::path& configPath) {
        std::ifstream inputFile(configPath);
        if (!inputFile.is_open()) {
            // std::cerr << "错误：无法打开配置文件 " << configPath << std::endl;
            return false;
        }
        
        std::vector<std::string> lines;
        std::string line;
        bool hasPedestrianParams = false;
        
        // 读取文件内容，过滤掉行人参数
        while (std::getline(inputFile, line)) {
            bool isPedestrianParam = false;
            for (const auto& param : pedestrianParams) {
                // 检查是否包含行人参数（去掉首尾空格比较）
                std::string trimmedLine = line;
                trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t"));
                trimmedLine.erase(trimmedLine.find_last_not_of(" \t") + 1);
                
                std::string trimmedParam = param;
                trimmedParam.erase(0, trimmedParam.find_first_not_of(" \t"));
                trimmedParam.erase(trimmedParam.find_last_not_of(" \t") + 1);
                
                if (trimmedLine == trimmedParam) {
                    isPedestrianParam = true;
                    hasPedestrianParams = true;
                    break;
                }
            }
            
            if (!isPedestrianParam) {
                lines.push_back(line);
            }
        }
        inputFile.close();
        
        // 如果没有行人参数，直接返回
        if (!hasPedestrianParams) {
            // std::cout << "配置文件 " << configPath.filename() << " 没有行人参数，跳过" << std::endl;
            return true;
        }
        
        // 写回文件
        std::ofstream outputFile(configPath);
        if (!outputFile.is_open()) {
            // std::cerr << "错误：无法写入配置文件 " << configPath << std::endl;
            return false;
        }
        
        for (const auto& l : lines) {
            outputFile << l << "\n";
        }
        outputFile.close();
        
        // std::cout << "成功从配置文件移除行人参数: " << configPath.filename() << std::endl;
        return true;
    }

public:
    // 修改指定目录下所有 .sumocfg 文件 - 添加行人参数
    bool addPedestrianParamsToAllConfigs(const std::string& directoryPath) {
        return modifyAllSumoConfigs(directoryPath, true);
    }
    
    // 修改指定目录下所有 .sumocfg 文件 - 移除行人参数
    bool removePedestrianParamsFromAllConfigs(const std::string& directoryPath) {
        return modifyAllSumoConfigs(directoryPath, false);
    }
    
private:
    // 统一的修改方法
    bool modifyAllSumoConfigs(const std::string& directoryPath, bool addParams) {
        // std::cout << "开始在目录中查找 .sumocfg 文件: " << directoryPath << std::endl;
        
        // 检查目录是否存在
        if (!std::filesystem::exists(directoryPath)) {
            // std::cerr << "错误：目录不存在 - " << directoryPath << std::endl;
            return false;
        }
        
        // 查找所有配置文件
        auto configFiles = findSumoConfigFiles(directoryPath);
        
        if (configFiles.empty()) {
            // std::cout << "在指定目录中未找到任何 .sumocfg 文件" << std::endl;
            return false;
        }
        
        // std::cout << "找到 " << configFiles.size() << " 个配置文件，开始" 
        //           << (addParams ? "添加" : "移除") << "行人参数..." << std::endl;
        
        int successCount = 0;
        int totalCount = configFiles.size();
        
        // 修改每个配置文件
        for (const auto& configFile : configFiles) {
            bool result = addParams ? 
                addPedestrianParamsToConfig(configFile) : 
                removePedestrianParamsFromConfig(configFile);
                
            if (result) {
                successCount++;
            }
        }
        
        // std::cout << "操作完成: " << successCount << "/" << totalCount << " 个文件成功处理" << std::endl;
        
        return successCount > 0;
    }
};
#endif // UTILES_H