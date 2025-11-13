#include "RedisConnector.h"
#include <fstream>
#include <hiredis/hiredis.h>
#include <set>
#include <sstream>
#include <stdexcept>
static std::set<std::string> redirectedIps;
static std::mutex redirectedIpsMutex;

void log_redirect(const std::string& from_host, int from_port, const std::string& to_host, int to_port)
{
    std::string redirectKey = to_host + ":" + std::to_string(to_port);

    std::lock_guard<std::mutex> lock(redirectedIpsMutex);
    if (redirectedIps.find(redirectKey) == redirectedIps.end()) {
        // 这是一个新的重定向IP，输出日志并记录
        std::cout << get_time() << " [REDIRECT] Redis重定向: " << from_host << ":" << from_port << " -> " << to_host
                  << ":" << to_port << std::endl;
        redirectedIps.insert(redirectKey);

        // 同时输出当前所有已知的重定向节点（可选，便于调试）
        if (redirectedIps.size() > 1) {
            std::cout << get_time() << " [REDIRECT] 当前已知重定向节点(" << redirectedIps.size() << "个): ";
            for (const auto& ip : redirectedIps) {
                std::cout << ip << " ";
            }
            std::cout << std::endl;
        }
    }
    // 如果已经记录过这个IP，就不重复输出
}

// 添加一个函数来查看所有重定向过的IP（用于调试）
void log_all_redirected_ips()
{
    std::lock_guard<std::mutex> lock(redirectedIpsMutex);
    if (!redirectedIps.empty()) {
        std::cout << get_time() << " [REDIRECT] 所有重定向过的节点(" << redirectedIps.size() << "个): ";
        for (const auto& ip : redirectedIps) {
            std::cout << ip << " ";
        }
        std::cout << std::endl;
    }
}

void RedisConnector::startHeartbeat() {
    heartbeatRunning_=true;
    heartbeatThread_=thread([this](){
        while (heartbeatRunning_)
        {
            this_thread::sleep_for(heartbeatInterval_);
            log_warning("心跳检测...");
            // 增加更严格的状态检查
            if(!heartbeatRunning_) break;
            if(!connected_||reconnecting_) continue;
            bool need_check = false;
            {
                lock_guard<recursive_mutex> lock(mutex_);
                // 只有在上次使用后经过一定时间才检查
                auto now = chrono::steady_clock::now();
                auto elapsed = chrono::duration_cast<chrono::seconds>(now - lastUsed_);
                if (elapsed < chrono::seconds(10)) { // 10秒内有活动则不检查
                    continue;
                }
                need_check = (context_ != nullptr);
            }
            
            if (need_check && !reconnecting_) {
                try {
                    lock_guard<recursive_mutex> lock(mutex_);
                    if (!context_) continue;
                    
                    redisReply* reply = (redisReply*)redisCommand(context_, "PING");
                    if (!reply || reply->type == REDIS_REPLY_ERROR || 
                        (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) != "PONG")) {
                        log_error("心跳检测失败，连接已断开");
                        connected_ = false;
                        // 异步重连，不阻塞心跳线程
                        std::thread([this]() { 
                            if (!reconnecting_) reconnect(); 
                        }).detach();
                    }
                    if (reply) freeReplyObject(reply);
                } catch (...) {
                    log_error("心跳检测异常");
                }
            }
        }
        
    });
}
void RedisConnector::stopHeartbeat() {
    heartbeatRunning_ = false;
    if(heartbeatThread_.joinable()){
        heartbeatThread_.join();
    }
}

RedisConnector::~RedisConnector()
{

    stopHeartbeat();
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    disconnect();
}
RedisConnector::RedisConnector()
{
    // 获取当前工作路径
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer)) == nullptr) {
        log_error("获取当前路径失败: " + std::string(strerror(errno)));
        // return false;
        return;
    }
    std::string currentPath = buffer;
    std::string configPath = currentPath + "/../config/RedisConfig.json";
    std::ifstream infile(configPath);
    if (!infile) {
        log_error("无法打开配置文件: " + configPath);
        throw std::runtime_error("无法打开配置文件: " + configPath);
    }

    json jsonData;
    infile >> jsonData;

    // 每增加一个都需要修改代码，比较麻烦，直接建个map吧
    for (json::iterator it = jsonData.begin(); it != jsonData.end(); ++it) {
        config_.configMap[it.key()] = it.value();
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
            config_.setIpMap(ipMap);
        }
    } else {
        log_warning("无法打开IP映射配置文件: " + ipMapPath);
    }

    std::string type = config_.getConfigOrDefault<std::string>("type", "hiredis");
    if (type == "redis++") {
        redisType_ = RedisType::REDIS_PLUS_PLUS;
    } else {
        redisType_ = RedisType::HIREDIS;
    }
    host_ = config_.getConfigOrDefault<std::string>("host", "127.0.0.1");
    port_ = config_.getConfigOrDefault<int>("port", 6379);
    password_ = config_.getConfigOrDefault<std::string>("password", "");
    db_ = config_.getConfigOrDefault<int>("database", 0);
    pool_size_ = config_.getConfigOrDefault<int>("pool_size", 1);
    retryCount = config_.getConfigOrDefault<int>("retryCount", 5);
    timeout_ = config_.getConfigOrDefault<int>("time_out", 100);

    context_ = nullptr;
    log_info("Redis连接器初始化完成 - 类型: " + type + ", 主机: " + host_ + ":" + std::to_string(port_));
    connect();
    startHeartbeat();
}
RedisConnector::RedisConnector(RedisConfig cfg) : config_(cfg)
{
    std::string type = config_.getConfigOrDefault<std::string>("type", "hiredis");
    if (type == "redis++") {
        redisType_ = RedisType::REDIS_PLUS_PLUS;
    } else {
        redisType_ = RedisType::HIREDIS;
    }
    host_ = config_.getConfigOrDefault<std::string>("host", "127.0.0.1");
    port_ = config_.getConfigOrDefault<int>("port", 6379);
    password_ = config_.getConfigOrDefault<std::string>("password", "");
    db_ = config_.getConfigOrDefault<int>("database", 0);
    pool_size_ = config_.getConfigOrDefault<int>("pool_size", 1);
    retryCount = config_.getConfigOrDefault<int>("retryCount", 5);
    timeout_ = config_.getConfigOrDefault<int>("time_out", 100);

    context_ = nullptr;
    connect();
    startHeartbeat();
}

void RedisConnector::disconnect()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (reconnecting_) {
        // log_warning("正在重连过程中，跳过断开连接操作");
        return;
    }
    if (context_) {
        switch (redisType_) {
        case RedisType::HIREDIS:
            redisFree(context_);
            context_ = nullptr;
            hiredisContext_.reset();
            break;
        case RedisType::REDIS_PLUS_PLUS:
            redisPlusPlusStandalone_.reset();
            redisPlusPlusCluster_.reset();
            break;
        }
    }

    connected_ = false;
    connectionType_ = ConnectionTypes::UNKNOWN;
}
void RedisConnector::tryRedirect()
{
    redisContext* context = nullptr;
    if (host_.empty()) {
        return;
    }
    log_info("尝试重定向检测，当前主机: " + host_ + ":" + std::to_string(port_));
    context = redisConnect(host_.c_str(), port_);

    if (context == nullptr) {
        log_error("重定向检测连接失败");
        // redisFree(context);
        return;
    }

    if (context->err) {
        log_error("重定向检测连接错误: " + std::string(context->errstr));
        redisFree(context);
        context = nullptr;
        return;
    }

    std::string redisOper = "GET keys";
    redisReply* reply = (redisReply*)redisCommand(context, redisOper.c_str());

    if (reply == nullptr) {
        log_error("重定向检测命令执行失败");
        freeReplyObject(reply);
        redisFree(context);
        return;
    }
    if ((reply->type == REDIS_REPLY_ERROR) && (std::string(reply->str).find("MOVED") != std::string::npos)) {

        char* movedInfo = reply->str;
        int slot, newport;
        char newhost[256];
        if (movedInfo != NULL) {
            sscanf(movedInfo, "MOVED %d %[^:]:%d", &slot, newhost, &newport);
        } else {
            freeReplyObject(reply);
            redisFree(context);
            return;
        }

        if (newhost == nullptr) {
            freeReplyObject(reply);
            redisFree(context);
            return;
        }

        if (context == nullptr) {
            freeReplyObject(reply);
            // redisFree(context);
            return;
        }
        if (context->err) {
            freeReplyObject(reply);
            redisFree(context);
            return;
        }
        host_ = config_.getMappedIp(string(newhost));
        port_ = newport;
        log_redirect(host_, port_, string(newhost), newport);
        log_info("重定向到新节点 - 主机: " + host_ + ", 端口: " + std::to_string(port_) +
                 ", 槽: " + std::to_string(slot));
    }

    freeReplyObject(reply);
    redisFree(context);
}
bool RedisConnector::connect()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::cout << "开始连接 Redis..." << std::endl;

    if (reconnecting_) {
        log_warning("正在重连过程中，跳过新连接");
        return false;
    }

    try {
        if (redisType_ == RedisType::HIREDIS) {
            // Hiredis 连接逻辑
            tryRedirect();

            context_ = nullptr;
            context_ = redisConnect(host_.c_str(), port_);
            if (context_ == nullptr || context_->err) {
                std::cout << " 连接失败! 主机: " << host_ << ":" << port_ << std::endl;
                // redisFree(context_);
                context_ = nullptr;
                return false;
            }
        }
        connected_ = true;
        setLastUsedTime();
        std::string typeStr = (redisType_ == RedisType::HIREDIS ? "HIREDIS" : "REDIS_PLUS_PLUS");
        std::string connTypeStr = (connectionType_ == ConnectionTypes::STANDALONE ? "STANDALONE" : "CLUSTER");
        std::cout << typeStr << " " << connTypeStr << " 连接成功! 主机: " << host_ << ":" << port_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << " 连接异常: " << e.what() << std::endl;
        disconnect();
        return false;
    }
}

bool RedisConnector::reconnect()
{
    bool expected = false;
    if(!reconnecting_.compare_exchange_strong(expected,true)){
        log_warning("重连操作正在进行中，跳过本次重连");
        return false;
    }
    // std::lock_guard<std::recursive_mutex> lock(mutex_);
    // std::lock_guard<std::mutex> lock(connectMutex_);
    // 防止递归重连
    // if (reconnecting_)
    //     return false;
    // reconnecting_ = true;

    // RAII保护，确保标志位正确重置
    struct ReconnectGuard {
        std::atomic<bool>& flag_;
        ReconnectGuard(std::atomic<bool>& flag) : flag_(flag) {}
        ~ReconnectGuard() { flag_ = false; }
    } guard(reconnecting_);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    log_warning("开始Redis重连过程...");
    disconnect();

    // cout<<host_<<endl;
    // 在重连时应用IP映射
    std::string originalHost = host_;
    auto mappedHost = config_.getMappedIp(host_);
    if (mappedHost != "") {
        host_ = mappedHost;
        log_info("重连时应用IP映射: " + originalHost + " -> " + host_);
        // std::cout << "重连时应用IP映射: " << originalHost << " -> " << host_ << std::endl;
    }

    // try reconnect
    for (int i = 0; i < retryCount; i++) {
        int waitSeconds = std::min(1 << i, 10); // 指数退避，最大10秒
        if (i > 0) {
            log_info("等待 " + std::to_string(waitSeconds) + " 秒后重试...");
            std::this_thread::sleep_for(std::chrono::seconds(waitSeconds));
        }
        log_info("尝试重新连接Redis (" + std::to_string(i + 1) + "/" + 
                std::to_string(retryCount) + ") 到 " + host_ + ":" + std::to_string(port_));
        // std::cout << "尝试重新连接Redis (" << (i + 1) << "/" << (retryCount) << ")..." << std::endl;

        redisContext* newContext = redisConnect(host_.c_str(), port_);

        if (newContext && !newContext->err) {
            // 释放旧连接
            if (context_) {
                redisFree(context_);
            }
            context_ = newContext;
            connected_ = true;
            
            // 验证连接有效性
            redisReply* reply = (redisReply*)redisCommand(context_, "PING");
            if (reply && reply->type == REDIS_REPLY_STATUS && 
                std::string(reply->str) == "PONG") {
                log_info("Redis重连成功并验证通过");
                freeReplyObject(reply);
                // 重连成功后重新启动心跳
                stopHeartbeat();
                startHeartbeat();
                return true;
            }
            if (reply) freeReplyObject(reply);
        }
        if (newContext) {
            if (newContext->err) {
                log_error("重连失败: " + std::string(newContext->errstr));
            }
            redisFree(newContext);
        } else {
            log_error("重连失败: 无法创建Redis连接");
        }
        // if (context_ == nullptr || context_->err) {
        //     std::cout << " 连接失败! 主机: " << host_ << ":" << port_ << std::endl;
        //     // redisFree(context_);
        //     reconnecting_ = false;
        //     return false;
        // } else {
        //     connected_ = true;
        // }

        // if (connected_) {

        //     reconnecting_ = false;
        //     // cout<<"Redis重连成功"<<endl;
        //     return true;
        // }

        // // 等待一段时间再重试
        // std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cerr << "Redis重连失败" << std::endl;
    host_ = originalHost;
    return false;
}

bool RedisConnector::checkConnection()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (reconnecting_) {
        log_warning("重连进行中，跳过连接检查");
        return false;
    }
    // if (!connected_ || !context_) {
    //     log_warning("连接不可用，尝试重连...");
    //     return reconnect();
    // }

    // 使用PING命令验证连接有效性
    try {
        redisReply* reply = (redisReply*)redisCommand(context_, "PING");
        if (!reply) {
            log_error("PING命令无响应，连接可能已断开");
            return reconnect();
        }
        
        bool success = (reply->type == REDIS_REPLY_STATUS && 
                       std::string(reply->str) == "PONG");
        freeReplyObject(reply);
        
        if (!success) {
            log_error("PING响应异常，连接可能已断开");
            return reconnect();
        }
        
        setLastUsedTime();
        return true;
        
    } catch (const std::exception& e) {
        log_error(std::string("连接检查异常: ") + e.what());
        return reconnect();
    }

    // if (!connected_ || !context_)
    // if (!connected_) {
    //     std::string originalHost = host_;
    //     auto mappedHost = config_.getMappedIp(host_);
    //     if (mappedHost != "") {
    //         host_ = mappedHost;
    //         std::cout << "连接检测时应用IP映射: " << originalHost << " -> " << host_ << std::endl;
    //     }
    //     bool result = reconnect();
    //     connected_ = result;
    //     // 恢复原始主机地址
    //     host_ = originalHost;

    //     return result;
    // }

    // try {
    //     if (redisType_ == RedisType::HIREDIS) {
    //         // 发送PING命令检查连接
    //         redisReply* reply = (redisReply*)redisCommand(context_, "GET keys");
    //         // redisReply* reply = executeCommand("PING");
    //         if (reply == nullptr) {
    //             std::cerr << "PING命令失败: 无回复" << std::endl;
    //             return reconnect();
    //         }

    //         bool success = false;
    //         if (reply->type == REDIS_REPLY_STATUS && std::string(reply->str, reply->len) == "PONG") {
    //             success = true;
    //         } else if (reply->type == REDIS_REPLY_ERROR) {
    //             std::cerr << "PING错误: " << std::string(reply->str) << std::endl;
    //         }

    //         freeReplyObject(reply);

    //         if (!success) {
    //             return reconnect();
    //         }

    //         setLastUsedTime();
    //         return true;
    //     }

    // } catch (...) {
    //     return reconnect();
    // }
}

bool RedisConnector::isConnected() const
{
    return !(context_ == NULL);
}

bool RedisConnector::set(const std::string& key, const std::string& value)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // if (!checkConnection()) {
    //     std::cerr << "Redis未连接，无法设置键值" << std::endl;
    //     return false;
    // }
    if (reconnecting_) {
        log_warning("正在重连，跳过SET操作: " + key);
        return "";
    }
    // 只在长时间未使用时检查连接
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUsed_);
    if (elapsed > std::chrono::seconds(30) && !checkConnection()) {
        log_error("Redis连接检查失败: " + key);
        return false;
    }
    // static int checkCount=0;
    // if(checkCount>10){
    //     checkCount=0;
    //     // log_warning("checkConnectioning ...");
    //     return !checkConnection();
    // }
    // checkCount++;
    
    bool success = false;
    if (redisType_ == RedisType::HIREDIS) {

        redisReply* reply = (redisReply*)redisCommand(context_, "SET %s %s ", key.c_str(), value.c_str());
        if (reply == NULL) {
            // cout << "reply is null :" << context_->errstr << endl;
            // // freeReplyObject(reply);
            // success = connect();
            log_error("SET命令执行失败: " + std::string(context_->errstr));
            connected_ = false;
            return false;
        }

        if (reply->type == REDIS_REPLY_ERROR) {
            // cout << "reply type is ERROR :" << reply->str << endl;
            log_error("SET命令错误: " + std::string(reply->str));
            char* movedInfo = reply->str;
            int slot, newport;
            char newhost[256];
            if (movedInfo != NULL) {
                sscanf(movedInfo, "MOVED %d %[^:]:%d", &slot, newhost, &newport);
            } else {

                freeReplyObject(reply);
                redisFree(context_);
                return false;
            }

            if (newhost == nullptr) {
                freeReplyObject(reply);
                redisFree(context_);
                return false;
            }

            if (context_ == nullptr) {
                freeReplyObject(reply);
                redisFree(context_);
                return false;
            }

            if (context_->err) {
                freeReplyObject(reply);
                redisFree(context_);
                return false;
            }
            // cout << string(newhost) << endl;
            host_ = string(newhost);
            port_ = newport;

            success = reconnect();
            if (success && setTryCount < maxSetTryCount) {
                setTryCount++;
                success = set(key, value);
            }
        } else {
            success = true;
        }
    }

    setLastUsedTime();
    connected_ = success;
    setTryCount = 0;
    // if(success){
    //     string str = get(key);
    //     cout<<"GET "<<key<<" : ("<<str<<")"<<endl;
    // }
    return success;
}

std::string RedisConnector::get(const std::string& key)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (reconnecting_) {
        log_warning("正在重连，跳过SET操作: " + key);
        return "";
    }
    // 只在长时间未使用时检查连接
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUsed_);
    if (elapsed > std::chrono::seconds(30) && !checkConnection()) {
        log_error("Redis连接检查失败: " + key);
        return "";
    }
    std::string result = "";
    bool success = false;

    if (redisType_ == RedisType::HIREDIS) {

        redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());

        if (reply == NULL) {
            cout << context_->errstr << endl;
            freeReplyObject(reply);
            success = connect();
        }

        if (reply->type == REDIS_REPLY_ERROR) {
            // std::cerr << "GET错误: " << std::string(reply->str) << std::endl;
            // cout << "reply type is ERROR :" << reply->str << endl;

            char* movedInfo = reply->str;
            int slot, newport;
            char newhost[256];
            if (movedInfo != NULL) {
                sscanf(movedInfo, "MOVED %d %[^:]:%d", &slot, newhost, &newport);
            } else {

                freeReplyObject(reply);
                redisFree(context_);
                return "";
            }

            if (newhost == nullptr) {
                freeReplyObject(reply);
                redisFree(context_);
                return "";
            }

            if (context_ == nullptr) {
                freeReplyObject(reply);
                redisFree(context_);
                return "";
            }

            if (context_->err) {
                freeReplyObject(reply);
                redisFree(context_);
                return "";
            }
            host_ = string(newhost);
            port_ = newport;

            success = reconnect();
            if (success && tryCount < maxTryCount) {
                tryCount++;
                result = get(key);
            }
        } else if (reply->type == REDIS_REPLY_STRING) {
            result = std::string(reply->str, reply->len);
        } else if (reply->type == REDIS_REPLY_NIL) {
            // 键不存在是正常情况，不记录错误
        }
        freeReplyObject(reply);
    }

    setLastUsedTime();
    connected_ = success;
    tryCount = 0;
    return result;
}

std::string RedisConnector::trim(const std::string& s)
{
    auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) { return std::isspace(c); });
    auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) { return std::isspace(c); }).base();
    return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}
