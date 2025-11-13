#include "interface.h"

string NavigationReq = "NavigationReq_win";
string NavigationRsp_points = "NavigationRsp_win";
string NavigationRsp_route = "NavigationRsp_sumo";
mutex mtx;
HDMap* g_hdmap = nullptr;

void initInerface(RedisConnector* redis) {
	NavigationReq = redis->config_.getConfigOrDefault<string>("redis_NavigationReq", NavigationReq);
	NavigationRsp_points = redis->config_.getConfigOrDefault<string>("redis_NavigationRsp_points", NavigationRsp_points);
	NavigationRsp_route = redis->config_.getConfigOrDefault<string>("redis_NavigationRsp_route", NavigationRsp_route);
	
	cout << "*********Navigation Req:" << NavigationReq << " Rsp_points:" << NavigationRsp_points  << " Rsp_route:" << NavigationRsp_route << endl;
}

//NavigationReq	高精地图导航请求接口	"NavigationReq
//{
//	""id"":""001 - AcuraRL"",
//		""src"" : {12, 13, 14},
//		""dst"" : {12, 13, 14}
//}"	"接收目标实体发送的
//
//实体ID、初始位置、目标位置"	
//NavigationRsp	高精地图导航响应接口			"NavigationRsp
//{
//	""id"":""001 - AcuraRL"",
//		""points"" : {{12, 13, 14}, { 13,13,14 }, { 14,13,14 }, { 15,13,14 }, { 16,13,14 }}
//	//待讨论
//}"

static void writeToRedis(RedisConnector* redis, JSON &j) {
	if (NavigationRsp_points.size()) {
		redis->set(NavigationRsp_points, j.dump());
	}
}

static string readData(double p[], JSON& j) {
	int i = 0;
	for (auto& e : j) {
		p[i] = e.get_d();
		++i;
	}
	
	if (i < 2) {
		string info = j.dump() + " error!";
		cout << info << endl;
		return info;
	}
	else if (i == 2) {
		p[i] = 0;
	}

	return "";
}

bool doNavList(RedisConnector * redis, HDMap * pmap, JSON & NavList, JSON & rspj) {
	vector<MRoutePath> v_path;
	vector<SSD::SimVector<MRouteElement>> v_routes;
	vector<Route> v_sumo;

	int i = -1;
	auto& infoj = rspj["info"];
	string info;
	JSON nouse;

	auto size = NavList.size();
	v_path.resize(size);
	v_routes.resize(size);
	v_sumo.resize(size);

	for (auto& e : NavList) {
		++i;
		double p1[3];
		double p2[3];
		auto& out = infoj.add_back();

		info = readData(p1, e["src"]);
		if (!info.size()) {
			if (e.contains("dist")) {
				info = readData(p2, e["dist"]);
			}
			else {
				info = readData(p2, e["dst"]);
			}

			if (info.size()) {
				out = info;
				continue;
			}
		}

		info = pmap->GetRoute(p1, p2, v_path[i], v_routes[i], nouse);
		if (info.size()) {
			out = info;
			cout << get_time() << " GetRoute error " << info << endl;
			continue;
		}

		cout << get_time() << " GetRoute successfully: waypoints " << v_path[i].waypoints.size() << endl;
		out = "successful";
		bool isFirst = true;
		auto& src = v_sumo[i].src;
		auto& dst = v_sumo[i].dist;
		v_sumo[i].actorID = e["actorID"];

		auto fun = [](double xyz[], SSD::SimPoint3D& p) {
			xyz[0] = p.x;
			xyz[1] = p.y;
			xyz[2] = p.z;
			};

		auto& r = v_sumo[i].route;
		for (auto& e : v_routes[i]) {
			pmap->toSumoInfo(r, e.laneName, e.startS, e.endS);
			if (r.size()) {
				auto& waypoints = e.waypoints;

				if (isFirst) {
					isFirst = false;
					fun(src, waypoints[0]);
				}

				fun(dst, waypoints.back());
			}
			else {
				cout << "No sumo edge: " << e.laneName.GetString() << " " << e.endS - e.startS << endl;
			}
		}

		// 检查前后路由
		pmap->checkSumoInfo(r);
	}

	save_route(v_sumo);
	writeToRedis(redis, rspj);
	return true;
}

bool readFromRedis(RedisConnector* redis, HDMap *pmap[], int size) {
	auto req = redis->get(NavigationReq);
	if (req.size() == 0) {
		return false;
	}

	JSON reqj(req);
	JSON rspj;

	static string s_id;
	string id = reqj.get_str("id", "no id");

	// id相同则还是上次请求，不处理
	if (id == s_id) {
		return false;
	}

	cout << get_time() << " " << NavigationReq << ": " << req << endl;
	s_id = id;
	rspj["id"] = id;
	rspj["time"] = get_time();

	// 设置查找路由的xodr
	unsigned int mapindex = reqj["mapindex"].get_l();
	if (mapindex >= size) {
		mapindex = size - 1;
	}
	g_hdmap = pmap[mapindex];
	HDMap::SetMap(pmap[mapindex]);

	if (reqj.contains("NavigationList")) {
		return doNavList(redis, pmap[mapindex], reqj["NavigationList"], rspj);
	}

	string info;
	MRoutePath path;
	SSD::SimVector<MRouteElement> routes;
	double p1[3];
	double p2[3];

	info = readData(p1, reqj["src"]);
	if (!info.size()) {
		info = readData(p2, reqj["dst"]);
		if (info.size()) {
			rspj["info"] = info;
			writeToRedis(redis, rspj);
			return false;
		}
	}

	mtx.lock();
	info = pmap[mapindex]->GetRoute(p1, p2, path, routes, reqj["option"]);
	mtx.unlock();

	if (info.size()) {
		cerr << get_time() << " GetRoute error: " << id << " " << info << endl;
		rspj["info"] = info;
		writeToRedis(redis, rspj);
		return false;
	}

	long ptnum = path.waypoints.size();
	rspj["ptnum"] = ptnum;
	cout << get_time() << " GetRoute successfully: " << id << " waypoints " << ptnum << endl;

	if (NavigationRsp_points.size()) {
		auto& points = rspj["points"];

		for (auto& pt : path.waypoints) {
			auto& p = points.add_back();
			p.push_back(pt.x);
			p.push_back(pt.y);
			p.push_back(pt.z);
		}

		writeToRedis(redis, rspj);

		auto check = redis->config_.getConfigOrDefault<double>("Rsp_points_check", 0.0);
		if (check > 1.0) {
			auto p_point = path.waypoints[0];
			int i = -1;

			for (auto& point : path.waypoints) {
				i++;
				auto jl = pow(p_point.x - point.x, 2) + pow(p_point.y - point.y, 2);
				if (jl > check) {
					cout << "Route error:" << jl << " " << i << " " << p_point.x << "/" << p_point.y << " " << point.x << "/" << point.y << endl;
				}

				p_point = point;
			}
		}
	}

	if (NavigationRsp_route.size()) {
		bool isFirst = true;
		rspj.erase("points");
		JSON& src = rspj["src"];
		JSON& dst = rspj["dst"];

		auto fun = [](JSON& j, SSD::SimPoint3D& p) {
			j.clear();
			j.push_back(p.x);
			j.push_back(p.y);
			j.push_back(p.z);
			};

		vector<string> r;
		for (auto& e : routes) {
			if (pmap[mapindex]->toSumoInfo(r, e.laneName, e.startS, e.endS)) {
				auto& waypoints = e.waypoints;

				if (isFirst) {
					isFirst = false;
					fun(src, waypoints[0]);
				}

				fun(dst, waypoints.back());
			}
		}

		// 检查前后路由
		pmap[mapindex]->checkSumoInfo(r);
		rspj["route"] = r;
		redis->set(NavigationRsp_route, rspj.dump());
	}

	return true;
}

vector<Route> v_route_;
bool isGet = true;

void get_route(vector<Route>& v_route) {
	if (isGet) {
		return;
	}
	mtx.lock();
	isGet = true;
	v_route = move(v_route_);
	mtx.unlock();
}

void save_route(const vector<Route>& v_route) {
	mtx.lock();
	isGet = false;
	v_route_ = move(v_route);
	mtx.unlock();
}

void delete_edge(const vector<string>& v_edge) {
	if (!g_hdmap) return;
	mtx.lock();
	g_hdmap->delete_edge(v_edge);
	mtx.unlock();
}


