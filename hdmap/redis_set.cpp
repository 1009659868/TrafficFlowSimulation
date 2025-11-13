#include <thread>
#include <chrono>
#include "interface.h"
#include "hdmap.h"

using namespace std;

int main0(int argc, char**argv) {
	if (argc < 2) {
		cout << "Please input name and json!" << endl;
		return 1;
	}

	auto redisConnector = new RedisConnector();

	if (argc == 2) {
		cout << "Read from redis:" << argv[1] << endl;
		cout << redisConnector->get(argv[1]) << endl;
		return 0;
	}

	ifstream infile(argv[2]);

	if (!infile.is_open()) {
		cout << "Open file error: " << argv[2] << endl;
		return 1;
	}

	json redisSet;
	try {
		infile >> redisSet;
		infile.close();
	}
	catch (const std::exception& e) {
		cout << " File json error:" << argv[2] << endl;
		return 1;
	}

	redisConnector->set(argv[1], redisSet.dump());

	return 0;
}

