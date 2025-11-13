#pragma once
#include <string>

#ifdef _PROJ_OLD_
#define ACCEPT_USE_OF_DEPRECATED_PROJ_API_H
#include <proj_api.h>
#endif

#ifdef _PROJ_NEW_
#include <proj.h>
#endif

using namespace std;

class Proj {
public:
	Proj(const string& defProj, const double x=0, const double y=0, const double z=0);
	~Proj();

	void lonlat_to_xy(int num, int offset, double* x, double* y);
	void xy_to_lonlat(int num, int offset, double* x, double* y);
	void test_xy(double x, double y);
	void test_lonlat(double lon, double lat);
	double offset_x = 0, offset_y = 0, offset_z = 0;

private:
	string proj_xy;

#ifdef _PROJ_OLD_
	projPJ xodr;
    	projPJ wgs84;
#endif

#ifdef _PROJ_NEW_
	PJ_CONTEXT* ctx = nullptr;
	PJ* trans = nullptr;
#endif

};

