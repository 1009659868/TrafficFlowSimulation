#pragma once

#include "Utiles.h"
#include "RedisConnector.h"
class RedisManager {
public:
    // 单例模式
    static RedisManager& getInstance() {
        static RedisManager instance;
        return instance;
    }
    // 禁止复制和移动
    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

    // 初始化管理器
    void initialize();
    RedisConfig loadRedisConfig();

    RedisConfig& getRedisConfig();

    void writeToRedis(const std::string& instanceID, const std::string& key, const std::string& data);
    std::string readFromRedis(const std::string& instanceID,const std::string& key);
    // 获取连接器
    RedisConnector* getRedisConnector(const std::string& instanceID);
    // 释放连接器（当实例关闭时）
    void releaseConnector(const std::string& instanceID);
    // 关闭管理器
    void shutdown();
private:
    RedisManager();
    ~RedisManager();
    
    // 连接池清理线程
    void cleanupThreadFunc();

    // 创建新连接器
    std::unique_ptr<RedisConnector> createNewConnector();

    // 配置信息
    RedisConfig config_;
    
    // 活跃连接：实例ID -> 连接器
    std::map<std::string, std::unique_ptr<RedisConnector>> activeConnections_;
    
    // 空闲连接列表
    std::list<std::unique_ptr<RedisConnector>> idleConnections_;

    // 连接池保护锁
    mutable std::mutex poolMutex_;
    // 清理线程控制
    std::thread cleanupThread_;
    std::atomic<bool> running_{false};
    std::condition_variable cleanupCV_;

};