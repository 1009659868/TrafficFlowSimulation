#include "RedisConnector.h"
#include "TrafficFlowManager.h"

#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <utility>
using namespace std;

// 命令行参数解析
std::pair<Parameters, bool> parseCommandLine(int nArgc, char* argv[])
{
    Parameters params;

    // 解析参数 必须要传入实例ID,所以参数从2开始计算
    if (nArgc < 2) {
        cerr << "Usage: " << argv[0] << " <InstanceID> [--simSpaceID <ID>] " << endl;
        return pair<Parameters, bool>(params, true);
    }
    // 第一个参数是实例ID
    // 解析实例ID
    std::string arg = argv[1];
    std::size_t equalsPos = arg.find("=");
    if (equalsPos != std::string::npos) {
        params.instanceID = arg.substr(equalsPos + 1);
    } else {
        params.instanceID = arg;
    }
    std::string configPath = "../config/" + params.instanceID + ".json";
    bool loaded = params.loadFromJSON(configPath);

    return pair<Parameters, bool>(params, loaded);
}
int main(int nArgc, char* argv[])
{
    // config
    try {
        TrafficFlowManager& manager = TrafficFlowManager::getInstance();

        // 参数优先级：命令行 > 配置文件 > 默认参数
        auto [params, loaded] = parseCommandLine(nArgc, argv);

        // 创建并启动实例
        if (loaded && manager.createInstance(params)) {
            manager.startInstanceThreads(params.instanceID);
            cout << "交通流实例 " << params.instanceID << " 已启动" << endl;
        } else {
            cerr << "实例 " << params.instanceID << "启动失败" << endl;
        }

        // 主线程等待
        while (true) {
            this_thread::sleep_for(chrono::seconds(10000));
            // 此处添加状态监控逻辑
        }
        manager.removeInstance(params.instanceID);
    } catch (const exception& e) {
        cerr << "Main error: " << e.what() << endl;
        return 1;
    }
    return 0;
}