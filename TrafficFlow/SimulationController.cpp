#include "SimulationController.h"

TrafficSimulationController::TrafficSimulationController() {}
TrafficSimulationController::~TrafficSimulationController() {}

void TrafficSimulationController::initialize(SConfig& config)
{

    stepLength = config.getConfigOrDefault<float>("stepLength", 0.03);
    frameRate = config.getConfigOrDefault<int>("frameRate", 30);
    frameInterval = 1000.0 / frameRate;
}
// 启动仿真
bool TrafficSimulationController::startSimulation(const std::vector<std::string>& options)
{
    try {

        std::cout << "启动命令行模式仿真..." << std::endl;
        libsumo::Simulation::load(options);
        std::cout << "启动仿真成功..." << std::endl;
        isSimulationRunning = true;

        return true;
    } catch (const std::exception& e) {
        cerr << "SUMO启动失败: " << e.what() << endl;
        return false;
    }
}
// 关闭仿真
void TrafficSimulationController::closeSimulation()
{
    if (isSimulationRunning) {
        libsumo::Simulation::close();
        isSimulationRunning = false;
    }
}
// 按帧推进仿真
void TrafficSimulationController::step()
{
    if (!isSimulationRunning)
        return;
    simulationTimeAccumulator += frameInterval / 1000.0; // 转换为秒
    try {
        // 当累积时间达到步长时执行仿真步进
        while (simulationTimeAccumulator >= stepLength) {
            // 推进仿真
            libsumo::Simulation::step();
            simulationTimeAccumulator -= stepLength;
        }

    } catch (const std::exception& e) {
        cerr << "仿真步进错误: " << e.what() << endl;
    }
}

void TrafficSimulationController::initializeVehicles(VehicleType& vTypes)
{
    try{
        for (auto& type : vTypes.typeList) {
            auto& detailTypeList = vTypes.detailTypeListMap[type];
            for(auto& detailType:detailTypeList){
                //按类型设置最大速度
                libsumo::VehicleType::setMaxSpeed(detailType,vTypes.getTypeParamOrDefault<double>(detailType,VehicleType::TypeParam::MAXSPEED,33.3));
                //设置碰撞箱
                // libsumo::VehicleType::setHeight();
                // libsumo::VehicleType::setWidth();
                // libsumo::VehicleType::setLength();
                
            }
        }
    }catch(const exception& e){
        cerr<<e.what()<<endl;
    }
    
}
