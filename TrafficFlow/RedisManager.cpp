
#include "RedisManager.h"

RedisManager::RedisManager() : running_(false) {}

RedisManager::~RedisManager()
{
    shutdown();
}

void RedisManager::initialize()
{
    std::lock_guard<std::mutex> lock(poolMutex_);

    config_ = loadRedisConfig();
    
    // 启动清理线程
    running_ = true;
    cleanupThread_ = std::thread(&RedisManager::cleanupThreadFunc, this);
}
RedisConfig RedisManager::loadRedisConfig()
{
    // 获取当前工作路径
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer)) == nullptr) {
        std::cerr << "获取当前路径失败: " << strerror(errno) << std::endl;
        // return false;
        return RedisConfig{};
    }
    std::string currentPath = buffer;
    std::string configPath = currentPath + "/../config/RedisConfig.json";
    std::ifstream infile(configPath);
    if (!infile) {
        throw std::runtime_error("无法打开配置文件: " + configPath);
    }

    json jsonData;
    infile >> jsonData;

    // 每增加一个都需要修改代码，比较麻烦，直接建个map吧
    RedisConfig config;
    for(json::iterator it = jsonData.begin(); it!= jsonData.end();++it){
        config.configMap[it.key()] = it.value();
    }

    // 加载IP映射配置
    std::string ipMapPath = currentPath + "/../config/IPMap.json";
    std::ifstream ipMapFile(ipMapPath);
    if (ipMapFile) {
        json ipMapJson;
        ipMapFile >> ipMapJson;
        
        if (ipMapJson.contains("IPMap")) {
            std::map<std::string, std::string> ipMap;
            for (auto& [key, value] : ipMapJson["IPMap"].items()) {
                ipMap[key] = value.get<std::string>();
            }
            config.setIpMap(ipMap);
        }
    } else {
        std::cerr << "警告: 无法打开IP映射配置文件: " << ipMapPath << std::endl;
    }
    return config;
}

RedisConfig& RedisManager::getRedisConfig()
{
    return config_;
}

void RedisManager::shutdown()
{
    if (running_) {
        running_ = false;
        cleanupCV_.notify_all();
        if (cleanupThread_.joinable()) {
            cleanupThread_.join();
        }
    }

    // 清理所有连接
    std::lock_guard<std::mutex> lock(poolMutex_);
    activeConnections_.clear();
    idleConnections_.clear();
}

RedisConnector* RedisManager::getRedisConnector(const std::string& instanceID)
{
    std::lock_guard<std::mutex> lock(poolMutex_);

    // 检查是否已有活跃连接
    auto activeIt = activeConnections_.find(instanceID);
    if (activeIt != activeConnections_.end()) {
        return activeIt->second.get();
    }

    // 尝试从空闲池获取
    if (!idleConnections_.empty()) {
        auto connector = std::move(idleConnections_.front());
        idleConnections_.pop_front();

        // 检查连接是否有效
        if (connector->isConnected()) {
            activeConnections_[instanceID] = std::move(connector);
            return activeConnections_[instanceID].get();
        }
    }

    // 创建新连接
    auto newConnector = createNewConnector();
    if (newConnector && newConnector->isConnected()) {
        activeConnections_[instanceID] = std::move(newConnector);
        
        return activeConnections_[instanceID].get();
    }

    return nullptr;
}

void RedisManager::releaseConnector(const std::string& instanceID)
{
    std::lock_guard<std::mutex> lock(poolMutex_);

    auto it = activeConnections_.find(instanceID);
    if (it != activeConnections_.end()) {
        // 移动到空闲池
        idleConnections_.push_back(std::move(it->second));
        activeConnections_.erase(it);
    }
}

void RedisManager::writeToRedis(const std::string& instanceID, const std::string& key, const std::string& data)
{
    auto connector = getRedisConnector(instanceID);
    // if (!connector || !connector->isConnected()) 
    if(!connector)
    {
        std::cerr << "Redis connection not available for instance: " << instanceID << std::endl;
        return;
    }

    try {
        std::string prefixedKey;
        // 添加实例ID作为前缀
        if (instanceID.empty()||instanceID=="")
            prefixedKey = key;
        else prefixedKey = instanceID + ":" + key;
        connector->set(prefixedKey, std::move(data));
    } catch (const std::exception& e) {
        std::cerr << "Error writing to Redis for instance " << instanceID << ": " << e.what() << std::endl;
    }
}
std::string RedisManager::readFromRedis(const std::string& instanceID, const std::string& key)
{
    auto connector = getRedisConnector(instanceID);
    // if (!connector || !connector->isConnected())
    if (!connector)
    {
        std::cerr << "Redis connection not available for instance: " << instanceID << std::endl;
        return "";
    }
    try {
        std::string prefixedKey;
        // 添加实例ID作为前缀
        if (instanceID.empty()||instanceID=="")
            prefixedKey = key;
        else prefixedKey = instanceID + ":" + key;

        return connector->trim(connector->get(prefixedKey));
        
    } catch (const std::exception& e) {
        std::cerr << "Error reading from Redis for instance " << instanceID << ": " << e.what() << std::endl;
        return "";
    }
}
std::unique_ptr<RedisConnector> RedisManager::createNewConnector()
{
    try {
        auto connector = std::make_unique<RedisConnector>(config_);

        if (connector->checkConnection()) {
            return connector;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to create Redis connector: " << e.what() << std::endl;
    }
    return nullptr;
}

void RedisManager::cleanupThreadFunc()
{
    while (running_) {
        {
            std::unique_lock<std::mutex> lock(poolMutex_);

            // 等待清理周期或关闭信号
            cleanupCV_.wait_for(lock, 10s, [this] { return !running_; });

            if (!running_)
                break;

            // 清理超时空闲连接
            auto now = std::chrono::steady_clock::now();
            auto it = idleConnections_.begin();
            while (it != idleConnections_.end()) {
                // 检查空闲时间是否超过30秒
                auto idleTime = now - (*it)->getLastUsedTime();
                if (std::chrono::duration_cast<std::chrono::seconds>(idleTime).count() > 30) {
                    it = idleConnections_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // 短暂休眠避免过度占用CPU
        std::this_thread::sleep_for(1000ms);
    }
}
