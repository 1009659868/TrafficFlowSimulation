#pragma once
#include "hdmap.h"
#include "RedisConnector.h"

using namespace std;
void initInerface(RedisConnector* redis);
bool readFromRedis(RedisConnector* redis, HDMap *pmap[], int size = 1);