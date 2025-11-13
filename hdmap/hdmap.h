#pragma once
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>

#include "public/MHDMap.h"
#include "public/MRouting.h"
#include "public/threadSafe/MHDMapTS.h"
#include "public/threadSafe/MRoutingTS.h"
#include "json.hpp"
#include "RedisConnector.h"
#include "proj_tf.h"
#include "json.h"

using JSON = LB::JSON;
using namespace HDMapStandalone;
using namespace std;
namespace fs = std::filesystem;

void startHDMap(RedisConnector* redisConnector, RedisConfig& cfg, const string& xodrfile, bool only);

class HDMap {
public:
	HDMap(RedisConfig &rCfg, const string &file);
	~HDMap();
	bool IsMapLoaded();
	static void SetMap(HDMap *m);
	string GetRoute(double* p1, double* p2, MRoutePath& path, SSD::SimVector<MRouteElement>& routes, JSON& option);
	string GetFile(const string& file);

#ifdef _WIN64
	void createSumoInfo(const string& file);
	double getHeight(double x, double y, bool isSumo=true);
	void sumo_AddHeight_doTrafficLight(const string& file, const unordered_set<string>& saveLT, int tlNum=13);
#endif

	void readSumoInfo(const string& file);
	bool toSumoInfo(vector<string>& r, const SSD::SimString& laneName, double startS, double ends);
	void checkSumoInfo(vector<string>& r);
	void delete_edge(const vector<string>& v_edge);

private:
	void load(const string& file);
	void saveLane(const string &lanefile, bool xyz=true);
	void saveRoute(SSD::SimPoint3D &pt1, SSD::SimPoint3D &pt2, MRoutePath& path, string &routefile);
	void deleteRouteLane(const double *p, bool delSide = false, bool delRoad = false);
	void readHdmapInfo();

	unique_ptr<Proj> proj = nullptr;
	unique_ptr<MHDMapTS> mapTS_ = nullptr;
	bool isLoadOk_;
	string file_;
	string name_;
	string path_;
	RedisConfig &rCfg_;
	bool useTS;
	unordered_set<string> forbidden_lane_id_set_;

	// sumo info
	// key   --- laneid
	// value --- unordered_map<string, SUMO_INFO>
	struct SUMO_INFO {
		double start = 100000000;
		double end = 0.0;
	};

	// ��¼����
	struct Info {
		string edge;
		double starts;
		double ends;

		// ת��Ϊtuple���ã����������&��ֻ����ֵ���ã���ֵ��������ʱ���������ܵ���
		auto values()& {
			return tie(edge, starts, ends);
		}
		static auto names() {
			static string r[] = { "edge", "starts", "ends" };
			return r;
		}

		string_view GetString() const {
			return edge;
		}
	};

	// key: rid_sid, laneName, hdmap --> sumo edge 
	map<string, vector<Info>> sumoInfo;
	// key: sumo edge, value: rid_sid(-)
	map<string, unordered_set<string>> hdmapInfo;
	// �ڶ���map��value��ǰû��
	map<string, map<string, string>> sumoRouteFrom;
	map<string, map<string, string>> sumoRouteTo;

	// hdmap�����Ϣ
	// key: rid_sid(-)
	map<string, int> hdmapLaneNum;
	// key: rid, laneName
	map<string, int> hdmapSectionNum;
	// ley: LaneName
	map<string, vector<string>> hdmapFrom;
	map<string, vector<string>> hdmapTo;

};

struct Route {
	string actorID;
	double src[3];
	double dist[3];
	vector<string> route;
};

void get_route(vector<Route>& v_route);
void save_route(const vector<Route>& v_route);
void delete_edge(const vector<string>& v_edge);

int get_element(const string_view& xml, int s, const char* name, string& ename, int& es, int& ee, bool fe=true);
int get_element(const string_view& xml, int s, const char* name, int& es, int& ee, bool fe=true);
string_view get_attr(const string_view& ele, const char* attr, int* s=nullptr, int* e=nullptr);
string _get_attr(const string_view& ele, const char* attr, int* s = nullptr, int* e = nullptr);
string read_content(const string& file);
void xodr_mod(string& xml, const map<string, vector<long>>& msvl, const string& rmvRoads = "");
void qgx_SetInstance();
void qgx_SetInstance(void *);
void qgx_save_value(int index, void* v);
// ��ȡ��ǰʱ��
string get_time();



