#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <set>
#include <regex>
using namespace std;

// 读取文件到string
string read_content(const string& file) {
    ifstream fin(file);
    if (!fin.is_open()) {
        cout << "文件打开错: " << file << endl;
        return "";
    }

    stringstream buffer;
    buffer << fin.rdbuf();
    string content(buffer.str());
    fin.close();

    return content;
}

// 获取xml中element
// s     --- 起始位置
// name  --- 要获取的element名称，多个名称使用/分割，任意匹配一个就返回；空匹配任意
// ename --- 获取到的element名称
// es    --- element起始位置
// ee    --- element的>位置
// fe    --- 是否需要找element的结束位置
// 返回值--- element结束位置，没有子element可能和ee相同，返回string::npos说明没有找到
int get_element(const string_view& xml, int s, const char* name, string& ename, int& es, int& ee, bool fe = true) {
    string f = string("/") + name + "/";

    while (1) {
        es = xml.find("<", s);
        if (es == string::npos) {
            return string::npos;
        }

        ee = xml.find_first_of(" />", es);
        if (ee == string::npos) {
            cout << "XML文件格式错，没有>, " << es << endl;
            return string::npos;
        }

        // element名称
        ename = xml.substr(es + 1, ee - es - 1);
        if (f != "//" && (ee == es + 1 /*名称为空，XML可能格式错*/ || f.find(ename) == string::npos)) {
            s = ee + 1;
            continue;
        }

        ee = xml.find(">", ee);
        ee += 1;
        if (xml[ee - 2] == '/') {
            // 没有子element
            return ee;
        }

        if (!fe) return ee;

        f = string("</") + ename + ">";
        auto r = xml.find(f, ee);
        if (r == string::npos) {
            cout << "XML文件格式错，没有" << f << ", " << es << endl;
            return string::npos;
        }

        return r + f.size();
    }
}

// 单个element查询
int get_element(const string_view& xml, int s, const char* name, int& es, int& ee, bool fe = true) {
    string nouse;
    return get_element(xml, s, name, nouse, es, ee, fe);
}


// 提取XML中属性值
string_view get_attr(const string_view& ele, const char* attr, int* s = nullptr, int* e = nullptr) {
    regex pattern(string(" ") + attr + "=\"([^\"]+)\"");
    //smatch matches;
    match_results<string_view::const_iterator> matches;

    if (regex_search(ele.begin(), ele.end(), matches, pattern)) {
        if (s) {
            *s = matches.position(0);
            *e = *s + matches.length(0);
        }
        //return matches.str(1);
        return ele.substr(matches.position(1), matches.length(1));
    }

    if (s) *s = -1;
    return "";
}

// 返回string
string _get_attr(const string_view& ele, const char* attr, int* s = nullptr, int* e = nullptr) {
    return string(get_attr(ele, attr, s, e));
}

// 保留的road类型
set<string> typeSet;

// 要删除的road
bool needDel = true;
set<string> needDoRoadSet;

// 完全删除的road
set<string> delRoadSet;

// 删除的lane数量
int laneDelNum = 0;

// 记录删除的lane，map key为road
// 车道ID为负的车道方向与参考线（Reference Line）的方向一致，车道ID为正的车道方向则与参考线方向相反。
map<string, set<string>> delLaneMapHead;
map<string, set<string>> delLaneMapTail;

// 记录road的laneSection数量
map<string, long> roadSectionNum;


// 处理lane，返回road包含的lane数量
void do_lane(const string_view& xml, string& out, string& rid, long minf, long maxz, bool isHead, bool isTail) {
    int s, e = 0, es, ee;
    out.reserve(xml.size());

    while (true) {
        s = e;
        e = get_element(xml, s, "lane", es, ee);

        if (e == string::npos) {
            out.append(xml, s);
            return;
        }

        auto id = _get_attr(xml.substr(es, ee - es), "id");
        long lid = stol(id);

        if (lid < minf || lid > maxz) {
            // 删除
            laneDelNum += 1;
            out.append(xml, s, es - s);

            if (isHead) delLaneMapHead[rid].insert(id);
            if (isTail) delLaneMapTail[rid].insert(id);
        }
        else {
            out.append(xml, s, e - s);
        }
    }
}

// 处理road
void do_road(const string_view& xml, string& out, const map<string, vector<long>>& msvl) {
    int s, e = 0, es, ee;
    out.reserve(xml.size());

    // 计算road的laneSection数量
    for (auto& e : msvl) {
        auto p = e.first.find("_");
        auto id = e.first.substr(0, p);
        roadSectionNum[id] = max(roadSectionNum[id], stol(e.first.substr(p + 1)));
    }

    while (true) {
        s = e;
        e = get_element(xml, s, "road", es, ee);
        if (e == string::npos) {
            out.append(xml, s);
            break;
        }
        out.append(xml, s, es - s);

        auto road = xml.substr(es, e - es);
        string id = _get_attr(road, "id");
        bool isDel = needDoRoadSet.count(id) ? needDel : !needDel;

        if (!isDel) {
            if (!msvl.count(id + "_0")) {
                cout << "do_road inpur eror: " << id << endl;
                out += road;
                continue;
            }

            // 判断是否整条路删除
            int maxSid = roadSectionNum[id];
            bool roadDel = true;

            for (int sid = 0; sid <= maxSid; ++sid) {
                auto& vl = msvl.at(id + "_" + to_string(sid));
                if (vl[0] != 0 || vl[1] != 0) {
                    roadDel = false;
                    break;
                }
            }

            if (roadDel) {
                delRoadSet.insert(id);
                continue;
            }

            int ss, se = 0, ses, see;
            for (int sid = 0; sid <= maxSid; ++sid) {
                ss = se;
                se = get_element(road, ss, "laneSection", ses, see);
                out.append(road, ss, ses - ss);

                auto rsid = id + "_" + to_string(sid);
                auto& vl = msvl.at(rsid);
                auto minf = vl[0];
                auto maxz = vl[1];
                auto minf_all = vl[2];
                auto maxz_all = vl[3];

                if (minf == minf_all && maxz == maxz_all) {
                    // 不修改
                    out.append(road, ses, se - ses);
                    continue;
                }
                else {
                    string rout;
                    do_lane(road.substr(ses, se - ses), rout, id, minf, maxz, sid==0, sid==maxSid);
                    out += rout;
                }
            }

            out.append(road, se);
        }
        else {
            delRoadSet.insert(id);
        }
    }
}

// 处理链路
int do_link(string_view& xml, string& out, string& incomingRoad, bool incoming, string& connectingRoad, bool connecting) {
    int linkNum = 0;
    int s, e = 0, es, ee;
    out.reserve(xml.size());

    while (true) {
        s = e;
        e = get_element(xml, s, "laneLink", es, ee);
        if (e == string::npos) {
            out.append(xml, s);
            break;
        }

        out.append(xml, s, es - s);
        auto ele = xml.substr(es, ee - es);

        if (incoming) {
            auto from = _get_attr(ele, "from");

            if (delLaneMapTail[incomingRoad].count(from)) {
                continue;
            }
        }

        if (connecting) {
            auto to = _get_attr(ele, "to");

            if (delLaneMapHead[connectingRoad].count(to)) {
                    continue;
            }
        }

        ++linkNum;
        out.append(xml, es, e - es);
    }

    return linkNum;
}

// 处理junction，返回connection数量
int do_junction(string_view& xml, string& out) {
    int s, e = 0, es, ee;
    int linkNum = 0;
    out.reserve(xml.size());

    while (true) {
        s = e;
        e = get_element(xml, s, "connection", es, ee);
        if (e == string::npos) {
            out.append(xml, s);
            break;
        }

        out.append(xml, s, es - s);
        auto ele = xml.substr(es, ee - es);

        string incomingRoad = _get_attr(ele, "incomingRoad");
        string connectingRoad = _get_attr(ele, "connectingRoad");

        if (delRoadSet.count(incomingRoad) || delRoadSet.count(connectingRoad)) {
            // road已经删除，该连接关系也需要删除
            continue;
        }

        bool incoming = delLaneMapTail.count(incomingRoad);
        bool connecting = delLaneMapHead.count(connectingRoad);
        ele = xml.substr(es, e - es);

        if (incoming || connecting) {
            string lout;
            auto num = do_link(ele, lout, incomingRoad, incoming, connectingRoad, connecting);
            if (num) {
                linkNum += num;
                out += lout;
            }
        }
        else {
            out += ele;
            linkNum += 1;
        }
    }

    return linkNum;
}

void do_rmv(const string_view& xml, string& out, const map<string, vector<long>>& msvl) {
    // 删除junction中引用已经删除的road和lane
    // road中前后继
    // <predecessor elementType = "road" elementId = "8" contactPoint = "end" />
    // <successor elementType = "road" elementId = "39" contactPoint = "start" />
    // lane中前后继，只处理第一个和最后一个laneSection
    // <link>
    //    <predecessor id = "2"/>
    //    <successor id = "3"/>
    // </link>

    // junction中
    // <connection id = "0" incomingRoad = "46" connectingRoad = "76" contactPoint = "end">
        //<laneLink from = "1" to = "1"/>

    int s, e = 0, es, ee;
    string ename;

    while (1) {
        s = e;
        e = get_element(xml, s, "road/junction", ename, es, ee);
        if (e == string::npos) {
            out.append(xml, s);
            break;
        }

        out.append(xml, s, es - s);
        string_view ele = xml.substr(es, e - es);

        if (ename == "road") {
            string id = _get_attr(ele, "id");

            if (!msvl.count(id + "_0")) {
                out += ele;
                continue;
            }

            // 每条road都有planView，并且在planView前面查找predecessor/successor
            int ss, se, ses, see;
            se = get_element(ele, 0, "planView", ses, see, false);
            if (se != string::npos) {
                out += ele;
                continue;
            }

            string_view find = ele.substr(0, ses);
            string pid, sid;

            se = get_element(find, 0, "predecessor", ses, see);
            if (se != string::npos) {
                string_view predecessor = find.substr(ses, see - ses);
                if (_get_attr(predecessor, "elementType") == "road") {
                    pid = _get_attr(predecessor, "elementId");

                    if (delRoadSet.count(pid)) {
                        // 整条路删除，直接删除road相关信息，不在处理lane相关信息
                        pid = "";
                        out += ele.substr(0, ses);
                        ele = ele.substr(se);
                    }
                }
            }

            se = get_element(find, 0, "successor", ses, see);
            if (se != string::npos) {
                string_view successor = find.substr(ses, see - ses);
                if (_get_attr(successor, "elementType") == "road") {
                    sid = _get_attr(successor, "elementId");

                    if (delRoadSet.count(sid)) {
                        // 整条路删除，直接删除road相关信息，不在处理lane相关信息
                        sid = "";
                        out += ele.substr(0, es);
                        ele = ele.substr(se);
                    }
                }
            }

            if (pid == "" && sid == "") {
                out += ele;
                continue;
            }

            int maxSid = roadSectionNum[id];
            se = 0;
            for (int secid = 0; secid <= maxSid; ++secid) {
                ss = se;
                se = get_element(ele, ss, "laneSection", ses, see);
                out += ele.substr(ss, ses - ss);

                string_view laneSection = ele.substr(ses, se - ses);
                string pout;
                
                if (pid != "" && secid == 0) {
                    // 处理predecessor
                    pout.reserve(se - ses);

                    int ds, de = 0, des, dee;
                    while (true) {
                        ds = de;
                        de = get_element(laneSection, ds, "predecessor", des, dee);
                        if (de == string::npos) {
                            pout += laneSection.substr(ds);
                            break;
                        }

                        string_view predecessor = laneSection.substr(des, dee - des);
                        string id = _get_attr(predecessor, "id");

                        if (delLaneMapTail[pid].count(id)) {
                            pout += laneSection.substr(ds, de - ds);
                        }
                        else {
                            pout += laneSection.substr(ds, des - ds);
                        }
                    }
                }

                if (pout.size()) {
                    laneSection = pout;
                }

                if (sid != "" && secid == maxSid) {
                    // 处理successor
                    int ds, de = 0, des, dee;
                    while (true) {
                        ds = de;
                        de = get_element(laneSection, ds, "successor", des, dee);
                        if (de == string::npos) {
                            out += laneSection.substr(ds);
                            break;
                        }

                        string_view predecessor = laneSection.substr(des, dee - des);
                        string id = _get_attr(predecessor, "id");

                        if (delLaneMapTail.count(pid + id)) {
                            out += laneSection.substr(ds, de - ds);;
                        }
                        else {
                            out += laneSection.substr(ds, des - ds);
                        }
                    }
                }
                else {
                    out += laneSection;
                }
            }

            out += ele.substr(se);
        }
        else {
            string jout;
            if (do_junction(ele, jout)) {
                out += jout;
            }
        }
    }
}

// 删除不在typeSet中的lane
// 若road只有id=0的lane删除road
// rmvRoads，以/分割，要删除的road列表
void xodr_mod(string& xml, const map<string, vector<long>>& msvl, const string& rmvRoads = "") {
    // xodr中仅仅保留center(id=0)和下面类型的lane，否则获取路由可能会到非driving的lane
    // 只能删除最大的laneid，删除中间lane会导致位置不正确
    typeSet.insert("driving");
    typeSet.insert("bidirectional");
    typeSet.insert("entry");
    typeSet.insert("exit");
    typeSet.insert("offRamp");
    typeSet.insert("onRamp");
    typeSet.insert("connectionRamp");

    if (rmvRoads.size()) {
        int s = 0;

        while (1) {
            int p = rmvRoads.find('/', s);

            if (p == string::npos) {
                needDoRoadSet.insert(rmvRoads.substr(s));
                break;
            }
            else {
                auto r = rmvRoads.substr(s, p - s);
                if (r == "*") {
                    // 转为输入的road为要保留
                    needDel = false;
                }
                else {
                    needDoRoadSet.insert(r);
                }

                s = p + 1;
            }
        }
    }

    string out;
    do_road(xml, out, msvl);
    xml.resize(0);
    do_rmv(out, xml, msvl);


    if (delRoadSet.size() || laneDelNum) {
        cout << "删除" << delRoadSet.size() << "条road，删除" << laneDelNum << "条lane" << endl;
    }
}
