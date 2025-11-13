#pragma once
#define REDIS_PLUS_PLUS_DO_NOT_USE_STRING_VIEW
#define REDIS_PLUS_PLUS_DO_NOT_USE_OPTIONAL

#include <atomic>
#include <hiredis/hiredis.h>
#include <sw/redis++/redis++.h>
#include <memory>
#include <mutex>
// #include <filesystem>
#include "Utiles.h"
#include "json.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
// namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace sw::redis;

template <typename outT, typename inT> outT json_get_by_default(json& j, inT p, outT def)
{
    try {
        return j.at(p).template get<outT>();
    } catch (...) {
        return def;
    }
}



string get_cfg(map<string, string>& cfgmap, char* key, char* def);
// 获取当前时间
std::string get_time();

class RedisConnector
{
private:
    atomic<bool> heartbeatRunning_{false};
    thread heartbeatThread_;
    chrono::seconds heartbeatInterval_{30};//30s
    void startHeartbeat();
    void stopHeartbeat();
public:
    // 使用配置文件构造
    RedisConnector(RedisConfig cfg);
    RedisConnector();
    ~RedisConnector();
    
    // 连接判断
    bool isConnected() const;
    // 写入
    bool set(const std::string& key, const std::string& value);
    // 获取
    std::string get(const std::string& key);
    std::string trim(const std::string& s);

    // 推送
    void publish(const std::string& channel, const std::string& message);
    // 重新连接
    bool reconnect();
    // 关闭连接
    void disconnect();

    RedisConfig config_;

    // 设置最后使用时间
    void setLastUsedTime() { lastUsed_ = std::chrono::steady_clock::now(); }
    // 获取最后使用时间
    std::chrono::steady_clock::time_point getLastUsedTime() const { return lastUsed_; }
    // 检查连接是否有效
    bool checkConnection();

    private:
    std::chrono::steady_clock::time_point lastUsed_;
    bool connect();
    void tryRedirect();
    std::atomic<bool> running_{false};

    redisContext* context_;
    // 使用智能指针管理 hiredis 连接
    std::shared_ptr<redisContext> hiredisContext_;
    // redis++ 连接 
    std::shared_ptr<sw::redis::Redis> redisPlusPlusStandalone_; // 单机连接对象
    std::shared_ptr<sw::redis::RedisCluster> redisPlusPlusCluster_;

    RedisType redisType_;
    ConnectionTypes connectionType_ = ConnectionTypes::UNKNOWN;
    // 连接配置
    std::string host_;
    int port_;
    std::string password_;
    int db_;
    size_t pool_size_;
    int timeout_;
    int retryCount;
    
    int maxTryCount =3;
    int tryCount=0;

    int maxSetTryCount=1;
    int setTryCount=0;
    mutable std::recursive_mutex mutex_;   // 递归锁允许同一线程重入
    std::atomic_bool reconnecting_{false}; // 重连状态标志
    std::atomic<bool> connected_{false};

    private:
   
};
