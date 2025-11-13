#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include "hdmap.h"

// proj格式
//<![CDATA[
// +proj=utm +zone=51 +ellps=WGS84 +datum=WGS84 +units=m +no_defs
//]]>

// 偏移格式
//<offset x="268164.10" y="2683869.46" z="-0.00" hdg="0"/>
const static regex offset_pattern("<offset x=\"([0-9.+-]+)\" y=\"([0-9.+-]+)\"( z=\"([0-9.+-]+)\")*");

// 经纬度proj
const static string proj_lonlat = "+proj=latlong +datum=WGS84 +datum=WGS84 +no_defs";

Proj::Proj(const string& defProj, const double x, const double y, const double z) : proj_xy(defProj), offset_x(x), offset_y(y), offset_z(z) {
#ifdef _PROJ_OLD_
    xodr = pj_init_plus(proj_xy.c_str());
    wgs84 = pj_init_plus(proj_lonlat.c_str());
    	
    if (!xodr || !wgs84) {
        cout << "Failed to initialize projections" << std::endl;
    }

#endif

#ifdef _PROJ_NEW_
    PJ* P = proj_create_crs_to_crs(ctx, proj_lonlat.c_str(), proj_xy.c_str(), NULL);
    if (!P) {
        cout << "Failed to create transformation object." << endl;
    }
    else {
        // 规范化投影对象
        trans = proj_normalize_for_visualization(ctx, P);
        if (!trans) {
            cout << "Failed to normalize transformation object." << endl;
        }

        proj_destroy(P);
    }
#endif

    JSON j;
    j["proj"] = proj_xy;
    j["x"] = offset_x;
    j["y"] = offset_y;
    j["z"] = offset_z;

    cout << "xodr_info=" << j.dump() << endl;
}

Proj::~Proj() {
#ifdef _PROJ_OLD_
    pj_free(xodr);
    pj_free(wgs84);
#endif

#ifdef _PROJ_NEW_
    proj_destroy(trans);
    proj_context_destroy(ctx);
#endif
}

const double M_PI_TF = 3.14159265358979323846;
const double RAD2DEG = 180.0 / M_PI_TF;

void Proj::lonlat_to_xy(int num, int offset, double *x, double *y) {
#ifdef _PROJ_OLD_
    auto _x = x; 
    auto _y = y;
    for (auto i = 0; i < num; i++) {
        *_x /= RAD2DEG;
        *_y /= RAD2DEG;
        _x += offset;
        _y += offset;
    }

    int result = pj_transform(wgs84, xodr, num, offset, x, y, NULL);
    if (result != 0) {
        cout << "lonlat_to_xy Transformation failed: " << pj_strerrno(result) << std::endl;
    }
    
    for (auto i = 0; i < num; i++) {
        *x -= offset_x;
        *y -= offset_y;
        x += offset;
        y += offset;
    }
    
#endif

#ifdef _PROJ_NEW_
    PJ_COORD input;
    PJ_COORD output;

    for (auto i = 0; i < num; i++) {
        input.v[0] = *x;
        input.v[1] = *y;

        output = proj_trans(trans, PJ_FWD, input);

        *x = output.v[0] - offset_x;
        *y = output.v[1] - offset_y;

        x += offset;
        y += offset;
    }
#endif
}

void Proj::xy_to_lonlat(int num, int offset, double* x, double* y) {
#ifdef _PROJ_OLD_
    auto _x = x; 
    auto _y = y;
    for (auto i = 0; i < num; i++) {
        *_x += offset_x;
        *_y += offset_y;
        _x += offset;
        _y += offset;
    }
    
    int result = pj_transform(xodr, wgs84, num, offset, x, y, NULL);
    if (result != 0) {
        cout << "lonlat_to_xy Transformation failed: " << pj_strerrno(result) << std::endl;
    }

    for (auto i = 0; i < num; i++) {
        *x *= RAD2DEG;
        *y *= RAD2DEG;
        x += offset;
        y += offset;
    }
    
#endif

#ifdef _PROJ_NEW_
    PJ_COORD input;
    PJ_COORD output;

    for (auto i = 0; i < num; i++) {
        input.v[0] = *x + offset_x;
        input.v[1] = *y + offset_y;

        output = proj_trans(trans, PJ_INV, input);

        *x = output.v[0];
        *y = output.v[1];

        x += offset;
        y += offset;
    }
#endif
}

void Proj::test_xy(double x, double y) {
    cout << "x,y:" << x << "," << y << endl;
    xy_to_lonlat(1, 0, &x, &y);
    cout << fixed << setprecision(9) << "lon,lat:" << x << "," << y << endl;
    lonlat_to_xy(1, 0, &x, &y);
    cout << "x,y:" << x << "," << y << endl << endl;
}

void Proj::test_lonlat(double x, double y) {
    cout << fixed << setprecision(9) << "lon,lat:" << x << "," << y << endl;
    lonlat_to_xy(1, 0, &x, &y);
    cout << "x,y:" << x << "," << y << endl;
    xy_to_lonlat(1, 0, &x, &y);
    cout << fixed << setprecision(9) << "lon,lat:" << x << "," << y << endl;
}
