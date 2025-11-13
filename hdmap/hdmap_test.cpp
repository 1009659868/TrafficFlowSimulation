#include <thread>
#include <chrono>
#include "interface.h"
#include "hdmap.h"

using namespace std;

void hamap_run(RedisConnector* redisConnector, const string &xodrfile) {
	string file[] = { 
#ifdef _WIN64
		"D:/TrafficFlow/OpenDrive/XODR_1010/XODR_1010.xodr",
		//"D:/TrafficFlow/OpenDrive/XODR_1010.xodr",
#else
		xodrfile,
#endif
	};

	const int size = sizeof(file) / sizeof(string);
	HDMap* pmap[size];

	for (int i = 0; i < size; i++) {
		// 循环，不释放内存
		pmap[i] = new HDMap(redisConnector->config_, file[i]);
		if (!pmap[i]->IsMapLoaded()) {
			return;
		}
	}

	long loopNum = 0;
	long sleep_time_hm = redisConnector->config_.getConfigOrDefault<int>("sleep_time_hm", 200);
	long mod = redisConnector->config_.getConfigOrDefault<int>("print_time_hm", 10000) / sleep_time_hm + 1;

	// 初始化redis读取字段名称
	initInerface(redisConnector);

    while (1) {
		if (++loopNum % mod == 0) {
			log_info("HDMap loopNum="+to_string(loopNum));
			// cout << get_time() << " HDMap loopNum=" << loopNum << endl;
		}

        // 从redis获取数据并处理
        readFromRedis(redisConnector, pmap, size);
        // 一段时间后再获取
        this_thread::sleep_for(chrono::milliseconds(sleep_time_hm));
    }
}

void startHDMap(RedisConnector* redisConnector, RedisConfig& cfg, const string &xodrfile, bool only) {
	hamap_run(redisConnector, xodrfile);
}

