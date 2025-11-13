#include <iostream>
#include <fstream>
#include <mutex>
#include "hdmap.h"

HDMap::HDMap(RedisConfig& rCfg, const string& file) : rCfg_(rCfg) {
	// proj格式
	//<![CDATA[
	// +proj=utm +zone=51 +ellps=WGS84 +datum=WGS84 +units=m +no_defs
	//]]>

	// 偏移格式
	//<offset x="268164.10" y="2683869.46" z="-0.00" hdg="0"/>

	string defproj = rCfg.getConfigOrDefault<string>("xodr_proj", "+proj=tmerc +lat_0=24.23845 +lon_0=120.6288 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +vunits=m +no_defs");
	double x = rCfg.getConfigOrDefault<double>("xodr_offset_x", 0.0);
	double y = rCfg.getConfigOrDefault<double>("xodr_offset_y", 0.0);
	double z = rCfg.getConfigOrDefault<double>("xodr_offset_z", 0.0);
	proj = make_unique<Proj>(defproj, x, y, z);

	useTS = rCfg.getConfigOrDefault<string>("useTS", "") == "true";
	this->load(file);
}

void HDMap::load(const string& file) {
	file_ = file;
	fs::path filePath = file_;
	name_ = filePath.stem().string();
	path_ = filePath.parent_path().string();

	isLoadOk_ = false;
	cout << get_time() << " Load " << (useTS ? "TS" : "") << "map start : " << file << endl;

	string content = read_content(file);
	if (content.size() == 0) {
		return;
	}

	auto maxid = path_ + "/" + name_ + "_lane_maxid.json";
	auto newf = file + ".new";
	if (fs::exists(maxid) && !fs::exists(newf)) {
		LB::JSON j;
		j.load(maxid);
		map<string, vector<long>> msvl = j;

		xodr_mod(content, msvl);

		ofstream fout(newf);
		fout << content;
		fout.close();
	}

	SSD::SimString xodrContent(content.c_str());

	try 
	{
		MLoadErrorCode error;

		if (!useTS) {
			isLoadOk_ = MHDMap::LoadDataFromContent(xodrContent, error);
		}
		else {
			mapTS_ = make_unique<MHDMapTS>(xodrContent, MXodrSourceType::eXodrContent);
			isLoadOk_ = mapTS_->IsMapLoaded(error);
		}

		if (isLoadOk_) {
			cout << get_time() << " Load map successfully!" << endl;
			SetMap(this);
		}
	}
	catch (const std::exception& e) {
		cout << get_time() << " Load map file try error:" << e.what() << endl;
		return;
	}

	readHdmapInfo();
	readSumoInfo(path_ + "/" + name_ + ".json");

#ifdef _WIN64
	//saveLane("D:/QGX/Python/HDMAPLane_0828/lane.txt");
	//createSumoInfo("D:/QGX/Python/XML/" + name_ + ".json");
	//sumo_AddHeight_doTrafficLight(path_ + "/" + name_ + ".net.xml", { "1651", "2469"}, 13);
#endif // _WIN64
}

HDMap::~HDMap() {
}

void HDMap::SetMap(HDMap *m) {
	// HDMapStandalone::MHDMap时把相关变量存在HdmapInstance中
	// 但路由部分又和MHDMapTS共享，要重新设置修改HdmapInstance中变量值，否则MHDMapTS时路由会出错
	qgx_save_value(0, &m->forbidden_lane_id_set_);

	if (!m->useTS) {
		qgx_SetInstance();
	}
	else {
		qgx_SetInstance(m->mapTS_->GetMapTSImp());
	}
}

void HDMap::deleteRouteLane(const double *p, bool delSide, bool delRoad) {
	SSD::SimPoint3D pt(p[0], p[1], p[2]);
	void* v = MRouting::DeleteRouteLane(pt, delSide, delRoad);

	for (auto& e : *((unordered_set<string>*)v)) {
		cout << "route forbiddenlane: " << e << endl;
		forbidden_lane_id_set_.insert(e);
	}
	delete (unordered_set<std::string>*)v;
}

bool HDMap::IsMapLoaded() {
	return isLoadOk_;
}

string HDMap::GetFile(const string& file) {
	fs::path filePath = file;
	string r = filePath.parent_path().string();
	
	r += "/";
	r += name_;
	r += "_";
	r += filePath.filename().string();
	return r;
}

void HDMap::saveLane(const string& lanefile, bool xyz) {
	auto fn = GetFile(lanefile);
	ofstream  f(fn);
	if (!f.is_open()) {
		cout << "文件打开错: " << fn << endl;
		return;
	}

	f << file_ << endl;

	SSD::SimVector<MLaneInfo> vmi = MHDMap::GetLaneData();
	for (MLaneInfo& mi : vmi) {
		string name = mi.laneName.GetString();
		if (name.substr(name.rfind("_") + 1) == "0") {
			//continue;
		}

		//if (name.substr(0, 5) != "10_0_") {
		//	continue;
		//}

		MLaneLink mll = MHDMap::GetLaneLink(mi.laneName);

		f << "lane: " << name << " " << MHDMap::IsDriving(mi.laneName) << " " << (int)MHDMap::GetLaneType(mi.laneName) << " " << MHDMap::GetLaneLength(mi.laneName) << endl;
		f << "left: " << mll.leftNeighborLaneName.GetString() << endl;
		f << "right: " << mll.rightNeighborLaneName.GetString() << endl;

		f << "predecessor: " << to_string(hdmapFrom[name].size()) << " " << LB::containerToString(hdmapFrom[name], " ") << endl;
		f << "successor: " << to_string(hdmapTo[name].size()) << " " << LB::containerToString(hdmapTo[name], " ") << endl;

		for (SSD::SimPoint3D& p : mi.centerLine) {
			if (xyz) {
				f << to_string(p.x) << " " << to_string(p.y) << " " << to_string(p.z) << std::endl;
			}
			else {
				auto x = p.x;
				auto y = p.y;
				proj->xy_to_lonlat(1, 0, &x, &y);
				f << LB::doubleToString(x) << " " << LB::doubleToString(y) << " " << to_string(p.z) << std::endl;
			}
		}
	}

	f << "lane end!" << endl;
	f.close();
}

void HDMap::saveRoute(SSD::SimPoint3D &pt1, SSD::SimPoint3D &pt2, MRoutePath& path, string &routefile) {
	auto fn = GetFile(routefile).c_str();
	ofstream  f(fn);
	if (!f.is_open()) {
		cout << "文件打开错: " << fn << endl;
		return;
	}

	f << file_ << endl;

	// 2个点
	f << 2 << endl;
	f << to_string(pt1.x) << " " << to_string(pt1.y) << " " << to_string(pt1.z) << endl;
	f << to_string(pt2.x) << " " << to_string(pt2.y) << " " << to_string(pt2.z) << endl;

	for (auto& p : path.waypoints) {
		f << to_string(p.x) << " " << to_string(p.y) << " " << to_string(p.z) << std::endl;
	}

	f.close();
}


string HDMap::GetRoute(double* p1, double* p2, MRoutePath &path, SSD::SimVector<MRouteElement>& routes, JSON& option) {
	string test = option["test"];

#ifdef _WIN64
	if (test == "reload") {
		string file = option["file"];
		this->load(file);
		return "reload:" + to_string(isLoadOk_);
	}

	if (test == "getLane") {
		SSD::SimString laneName;
		double s, t;
		SSD::SimPoint3D pt(p1[0], p1[1], p1[2]);

		if (MRouting::ValidatePoint_tf(pt, laneName, s, t)) {
			string name = laneName.GetString();

			string ret = "getLane:" + name + "/" + to_string(hdmapSectionNum[name]) + "/" + LB::to_string_t0(MHDMap::GetLaneLength(laneName), 2) + "/" + LB::to_string_t0(s, 2);
			ret += " predecessor:" + LB::containerToString(hdmapFrom[name]);
			ret += " successor:" + LB::containerToString(hdmapTo[name]);

			if (hdmapTo[name].size() == 1) {
				auto to = hdmapTo[hdmapTo[name][0]];
				if (to.size() == 1 && to[0] == name) {
					ret += "(loop)";
				}
			}

			return ret;
		}

		return "getLane error!";
	}

	if (test == "saveLane") {
		string file = option["file"];
		saveLane(file);
		return "saveLane";
	}

	if (test == "createSumoInfo") {
		string file = option["file"];
		createSumoInfo(file);
		return "createSumoInfo";
	}

	if (test == "sumo_AddHeight_doTrafficLight") {
		string file = option["file"];
		vector<string> vs = option["saveLT"];
		unordered_set<string> saveLT(vs.begin(), vs.end());
		int tlNum = static_cast<int>(option["tlNum"]);

		sumo_AddHeight_doTrafficLight(file, saveLT, tlNum);
		return "sumo_AddHeight_doTrafficLight";
	}
#endif

	if (test == "") {

	}
	else if (test == "deleteLane") {
		cout << "before: " << LB::containerToString(forbidden_lane_id_set_) << endl;
		deleteRouteLane(p1);
		cout << "after: " << LB::containerToString(forbidden_lane_id_set_) << endl;

		return "forbidden_lane_id_set_ = " + to_string(forbidden_lane_id_set_.size());
	}
	else if (test == "deleteEdge") {
		cout << "before: " << LB::containerToString(forbidden_lane_id_set_) << endl;
		vector<string> v = option["sumoEdges"];
		delete_edge(v);
		cout << "after: " << LB::containerToString(forbidden_lane_id_set_) << endl;

		return "forbidden_lane_id_set_ = " + to_string(forbidden_lane_id_set_.size());
	}
	else if (test == "hdmap_to_sumo") {
		string v = option["lane"];
		auto& info = sumoInfo[v];

		cout << "hdmap_to_sumo: " << v << " " << LB::containerToString(info) << endl;
		return "hdmap_to_sumo = " + to_string(info.size());
	}
	else if (test == "sumo_to_hdmap") {
		string v = option["edge"];
		auto& info = hdmapInfo[v];

		cout << "sumo_to_hdmap: " << v << " " << LB::containerToString(info) << endl;
		return "sumo_to_hdmap = " + to_string(info.size());
	}
	else if (test == "proj") {
		proj->test_xy(0, 0);
		proj->test_xy(-15000, -20000);
		proj->test_xy(-15000, 15000);
		proj->test_xy(20000, -20000);
		proj->test_xy(20000, 15000);
		return "proj";
	}
	else if (test == "proj_xy") {
		auto x = static_cast<double>(option["x"]);
		auto y = static_cast<double>(option["y"]);
		proj->test_xy(x, y);
		return "proj_xy";
	}
	else if (test == "proj_lonlat") {
		auto x = static_cast<double>(option["lon"]);
		auto y = static_cast<double>(option["lat"]);
		proj->test_lonlat(x, y);
		return "proj_lonlat";
	}

	if (!isLoadOk_) {
		return "Map error:" + file_;
	}

	if (abs(p1[0]) <= 180.0 && abs(p1[1]) <= 90.0 && abs(p1[0] - p2[0]) <= 2.0 && abs(p1[1] - p2[1]) <= 2.0) {
		// 根据数据大小判断是否为经纬度，并且转换为xy
		proj->lonlat_to_xy(1, 0, p1, p1 + 1);
		proj->lonlat_to_xy(1, 0, p2, p2 + 1);
	}
	
	string info;
	SSD::SimPoint3D pt1(p1[0], p1[1], p1[2]);
	SSD::SimPoint3D pt2(p2[0], p2[1], p2[2]);

	// 先清除上次的点
	MRouting::ClearPoints();

	SSD::SimString laneName1;
	double s1 = -1;
	if (!MRouting::AddPoint_tf(pt1, laneName1, s1)) {
		info = "Src point add error!";
	}

	SSD::SimString laneName2;
	double s2 = -1;
	if (!MRouting::AddPoint_tf(pt2, laneName2, s2)) {
		info += "Dst point add error!";
	}

	if (!info.size()) {
		cout << "src-dst: " << laneName1.GetString() << "/" << to_string(s1) << "-" << laneName2.GetString() << "/" << to_string(s2) << endl;

		if (!MRouting::GenerateRoute(path, routes)) {
			info += string(" GenerateRoute error:") + laneName1.GetString() + "/" + to_string(s1) + "-" + laneName2.GetString() + "/" + to_string(s2);
		}
		else if (0) {
			cout << "segmentInfos!!" << endl;
			for (auto& e : path.segmentInfos) {
				for (auto& name : e.laneNameList) {
					cout << name.GetString() << " ";
				}

				cout << e.roadId << " " << e.from  << " " << e.to << endl;
			}
		}
	}

	if (!info.size()) {
		string routefile = option["routefile"];
		if (routefile.size()) {
			saveRoute(pt1, pt2, path, routefile);
		}
	}

    return info;
}

#ifdef _WIN64
double HDMap::getHeight(double x, double y, bool isSumo) {
	SSD::SimString laneName;
	double s, t;

	SSD::SimPoint3D pt(x, y, 0);
	SSD::SimPoint3D targetPoint;

	if (isSumo) {
		// 处理xodr文件中偏移
		pt.x -= proj->offset_x;
		pt.y -= proj->offset_y;
	}

	if (MRouting::ValidatePoint_tf(pt, laneName, s, t, &targetPoint)) {
		return targetPoint.z;
	}

	cout << "getHeight error:" << x << " " << y << endl;
	return 0.0;
}

void HDMap::createSumoInfo(const string& file) {
	LB::Time t;
	ifstream infile(file);
	ofstream outfile(file + ".out");
	if (!infile.is_open() || !outfile.is_open()) {
		cout << "open file error:" << file << endl;
		return;
	}

	LB::JSON ls;
	infile >> ls;
	infile.close();
	int i = 0;
	double maxt = 0;

	double offset_x = ls["offset_x"].get_d();
	double offset_y = ls["offset_y"].get_d();

	for (auto& l : ls["lanes"].items()) {
		if (i++ % 100 == 0) {
			cout << "i=" << i << endl;
		}

		for (auto& p : l.second["shape"]) {
			SSD::SimString laneName;
			double s, t;
			SSD::SimPoint3D pt(p[0].get_d() - offset_x, p[1].get_d() - offset_y, p[2].get_d());

			// 处理xodr文件中偏移
			pt.x -= proj->offset_x;
			pt.y -= proj->offset_y;


			if (MRouting::ValidatePoint_tf(pt, laneName, s, t)) {
				p.push_back(string(laneName.GetString()));
				p.push_back(s);
				p.push_back(t);
				maxt = max(maxt, abs(t));
			}
		}
	}

	outfile << ls.dump<1>();
	outfile.close();
	cout << "End createSumoInfo:" << t.get() << " maxt:" << maxt << endl;
}

// saveLT: 要保留的tlLogic的id
// tlNum: 交通灯（state长度）小于tlNum将删除，<phase duration="42" state="rrrrrGGGGGgrrrrrGGGg"/>
void HDMap::sumo_AddHeight_doTrafficLight(const string& file, const unordered_set<string>& saveLT, int tlNum) {
	string content = read_content(file);
	if (!content.size()) {
		return;
	}

	ofstream fout(file + ".add");
	if (!fout.is_open()) {
		cout << "Open write file error:" << file + ".add" << endl;
		return;
	}

	double offset_x = 0, offset_y = 0;
	double maxZ = 0;
	unordered_set<string> delTl;

	// 返回string_view，实际数据可能保存在r
	auto do_xyz = [&](string_view& xml, string& r) -> string_view {
		int as, ae;
		string x = _get_attr(xml, "x", &as, &ae);
		if (as == -1) {
			return xml;
		}

		get_attr(xml, "z", &as, &ae);
		if (as != -1) {
			return xml;
		}

		string y = _get_attr(xml, "y", &as, &ae);
		if (as == -1) {
			return xml;
		}

		r = string(xml, 0, ae);
		double z = getHeight(stod(x) - offset_x, stod(y) - offset_y);
		maxZ = max(maxZ, z);

		r += " z=\"";
		r += LB::to_string_t0(z, 2);
		r += "\"";
		r += xml.substr(ae);

		return r;
		};

	auto do_shape = [&](string_view& xml, string& r) -> string_view {
		int tls, tle;
		auto tl = _get_attr(xml, "tl", &tls, &tle);
		bool isDelTl = tls != -1 && delTl.count(tl);

		auto ret = [&]() -> string_view {
			if (isDelTl) {
				r = xml.substr(0, tls);
				r += xml.substr(tle);
				return r;
			}

			return xml;
			};

		int as, ae;
		auto shape = get_attr(xml, "shape", &as, &ae);
		if (as == -1) {
			return ret();
		}

		r = string(xml, 0, as);
		int s = 0;
		string shape_new = " shape=\"";

		while (1) {
			auto f = shape.find(" ", s);
			auto xyz = f==string::npos ? shape.substr(s) : shape.substr(s, f - s);

			auto f1 = xyz.find(",");
			if (f1 == string::npos) {
				return ret();
			}

			auto f2 = xyz.find(",", f1 + 1);
			if (f2 != string::npos) {
				return ret();
			}
			else {
				string x(xyz.substr(0, f1));
				string y(xyz.substr(f1 + 1));
				double z = getHeight(stod(x) - offset_x, stod(y) - offset_y);
				maxZ = max(maxZ, z);

				shape_new += x;
				shape_new += ",";
				shape_new += y;
				shape_new += ",";
				shape_new += LB::to_string_t0(z, 2);
			}

			if (f == string::npos) {
				shape_new += "\"";
				break;
			}
			else {
				s = f + 1;
				shape_new += " ";
			}
		}

		r += shape_new;

		if (isDelTl) {
			r += xml.substr(ae, tls - ae);
			r += xml.substr(tle);
		}
		else {
			r += xml.substr(ae);
		}

		return r;
		};

	int s = 0, es, ee;

	while (1) {
		string ename;
		if (get_element(content, s, "", ename, es, ee, false) == string::npos) {
			break;
		}

		fout << content.substr(s, es - s);
		string_view elev = string_view(content.c_str() + es, ee - es);

		// <location netOffset="12507.18,17310.77" convBoundary="0.00,0.00,25205.70,26532.96" origBoundary="-12508.45,-17311.64,12699.23,9221.04" projParameter="!"/>
		if (ename == "location") {
			auto netOffset = _get_attr(elev, "netOffset");
			auto p = netOffset.find(",");

			if (p == string::npos) {
				cout << "location netOffset error:" << netOffset << endl;
				return;
			}

			offset_x = stod(netOffset.substr(0, p));
			offset_y = stod(netOffset.substr(p + 1));

			fout << elev;
			s = ee;
			continue;
		}

		if (ename == "tlLogic") {
			// 处理红绿灯
			int e = content.find("</tlLogic>", ee);
			if (e == string::npos) {
				cout << "XML tlLogic error:" << es << endl;
				break;
			}

			string_view phasev = string_view(content.c_str() + ee, e - ee);
			auto state = get_attr(phasev, "state");
			auto id = _get_attr(elev, "id");

			s = e + 10;
			if (state.size() < tlNum && !saveLT.count(id)) {
				delTl.insert(id);
			}
			else {
				fout << content.substr(es, s - es);
			}

			continue;
		}
		
		string junction;
		if (ename == "junction") {
			int as, ae;
			auto type = get_attr(elev, "type", &as, &ae);

			if (type == "traffic_light") {
				if (delTl.count(_get_attr(elev, "id"))) {
					// 修改为"priority"
					junction = elev.substr(0, as);
					junction += " type=\"priority\"";
					junction += elev.substr(ae);
					elev = junction;
				}
			}
		}

		string xyz, shape;
		elev = do_xyz(elev, xyz);
		elev = do_shape(elev, shape);

		fout << elev;

		s = ee;
	}

	fout << content.substr(s);
	fout.close();
	cout << "sumoAddHeight end:" << file << " maxZ=" << maxZ << endl;
}
#endif

void HDMap::readHdmapInfo() {
	SSD::SimVector<MLaneInfo> vmi = MHDMap::GetLaneData();
	for (MLaneInfo& mi : vmi) {
		string name = mi.laneName.GetString();
		auto p1 = name.find("_");
		auto p2 = name.rfind("_");

		auto rid = name.substr(0, p1);
		auto sid = stol(name.substr(p1 + 1, p2));
		auto rsid = name.substr(0, p2);
		long lid = stol(name.substr(p2 + 1));

		if (lid < 0) {
			rsid += '-';
		}
		if (lid != 0) {
			hdmapLaneNum[rsid] += 1;
		}

		if (hdmapSectionNum[rid] <= sid) {
			hdmapSectionNum[rid] = sid + 1;
		}
	}

	// 循环第二遍
	for (MLaneInfo& mi : vmi) {
		string name = mi.laneName.GetString();
		auto p1 = name.find("_");
		auto rid = name.substr(0, p1);

		hdmapSectionNum[name] = hdmapSectionNum[rid];

		MLaneLink mll = MHDMap::GetLaneLink(mi.laneName);

		for (auto& e : mll.predecessorLaneNameList) {
			if (MHDMap::ContainsLane(e)) {
				//hdmapFrom[name].push_back(e.GetString());
			}
			else {
				cout << "predecessor" << " Lane do not exist:" << e.GetString() << endl;
			}
		}

		for (auto& e : mll.successorLaneNameList) {
			if (MHDMap::ContainsLane(e)) {
				auto to = e.GetString();
				hdmapTo[name].push_back(to);
				// hdmapFrom用hdmapTo生成
				hdmapFrom[to].push_back(name);
			}
			else {
				cout << "successor" << " Lane do not exist:" << e.GetString() << endl;
			}
		}
	}
}

void HDMap::readSumoInfo(const string& file) {
	ifstream infile(file);
	if (!infile.is_open()) {
		cout << "open file error:" << file << endl;
		return;
	}

	LB::JSON ls;
	infile >> ls;
	infile.close();

	for (auto& e : ls.items()) {
		if (e.first == "edge_connection") {
			for (auto& r : e.second.items()) {
				sumoRouteFrom[r.first] = r.second["from"];
				sumoRouteTo[r.first] = r.second["to"];
			}
		}
		else {
			auto p = e.first.find_last_of("_-");
			auto road = e.first.substr(0, p);
			if (sumoInfo.count(road) == 0) {
				sumoInfo[road] = e.second;
			}

			sumoInfo[e.first] = e.second;
			for (auto& e2 : e.second) {
				hdmapInfo[e2["edge"]].insert(road);
			}
		}
	}
}

void HDMap::delete_edge(const vector<string>& v_edge) {
	for (auto& edge : v_edge) {
		for (auto& rsid : hdmapInfo[edge]) {
			auto num = hdmapLaneNum[rsid];

			for (int no = 1; no <= num; ++no) {
				forbidden_lane_id_set_.insert(rsid + to_string(no));
			}
		}
	}
}

// 判断是否有交接
bool check(double starts1, double ends1, double starts2, double ends2) {
	if (starts1 > ends2 || starts2 > ends1) {
		return false;
	}
	return true;
}

bool HDMap::toSumoInfo(vector<string>& r, const SSD::SimString& laneName, double starts, double ends) {
	string name(laneName.GetString());
	auto it = sumoInfo.find(name);

	if (it == sumoInfo.end()) {
		auto p = name.find_last_of("_-");
		auto road = name.substr(0, p);
		it = sumoInfo.find(road);
	}

	if (it != sumoInfo.end()) {
		for (auto& e : it->second) {
			if (r.size() == 0 || r.back() != e.edge) {
				if (check(starts, ends, e.starts, e.ends)) {
					r.push_back(e.edge);
				}
			}
		}

		return true;
	}
	else {
		cout << "No sumo edge: " << name << " " << starts << "-" << ends << endl;
		return false;
	}
}

void HDMap::checkSumoInfo(vector<string>& r) {
	if (r.size() < 2) {
		return;
	}

	auto it = r.begin();
	auto end = prev(r.end());

	while (it != end) {
		auto from = *it++;
		auto to = *it;

		auto& rf = sumoRouteTo[from];
		if (rf.count(to) == 0) {
			auto& rt = sumoRouteTo[to];
			if (rt.count(from)) {
				// 路由前后位置错了
				*it = from;
				*(prev(it)) = to;

				cout << "route reverse:" << from << " " << to << endl;
				continue;
			}

			// 判断能否中间加一跳而通路由
			rt = sumoRouteFrom[to];
			bool isok = false;

			for (auto& e : rf) {
				if (rt.count(e.first)) {
					r.insert(it, e.first);
					cout << "add sumo route:" << from << " " << e.first << " " << to << endl;
					
					isok = true;
					break;
				}
			}

			if (!isok) {
				cout << "no route:" << from << " " << to << endl;
			}
		}
	}
}