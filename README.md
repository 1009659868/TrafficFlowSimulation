# 交通流系统使用文档

## 概述

交通流系统是一个基于Redis通信的分布式仿真系统，支持车辆自动导航、爆炸毁伤模拟、车辆接管等功能。本文档提供系统的完整配置和使用说明。

## 目录

- [前置条件](#前置条件)
- [项目结构](#项目结构)
- [配置说明](#配置说明)
- [功能说明](#功能说明)
- [Redis通信协议](#redis通信协议)
- [故障排查](#故障排查)

## 前置条件

在运行系统前，请确保满足以下条件：

1. **路网文件**：在 `${workPlace}/OpenDrive/` 路径下存在有效的路网文件
2. **Redis服务**：Redis连接正常，确保服务已启动且网络可达
3. **配置文件**：如使用配置文件启动，需在 `${workPlace}/config/` 路径下存在对应的配置文件

## 项目结构

```
${workPlace}/
├── Build/                # 构建输出目录
│   └── run.sh            # 启动脚本
├── config/               # 配置文件目录
│   ├── StartConfig.json  # 默认启动配置
│   ├── RedisConfig.json  # Redis连接配置
│   ├── vehicleTypes.json # 车辆类型配置
│   └── ${DomainID}.json  # 动态配置文件
├── dependencies/         # 启动依赖项
└── OpenDrive/            # 路网文件目录
```

## 配置说明

### 1. 启动配置 (StartConfig.json)

```json
{
    "instanceID": "19738",
    "simSpaceID": "XODR_1010"
}
```

| 参数 | 类型 | 说明 | 默认值 |
|------|------|------|--------|
| instanceID | string | 实例标识符，唯一ID | 必填 |
| simSpaceID | string | 仿真空间标识符 | 必填 |

### 2. Redis配置 (RedisConfig.json)

```json
{
    "host": "127.0.0.1",
    "password": "",
    "database": 0,
    "port": 6379,
    "time_out": 30,
    "reTryCount": 3
}
```

### 3. 车辆类型配置 (vehicleTypes.json)

```json
{
    "maxVehicles": 2000,
    "TypeDensity": {
        "Civilian": 1,
        "Military": 0
    },
    "Type": {
        "Civilian": [
            {
                "001-AcuraRL": {
                    "density": 0.4,
                    "maxSpeed": 20
                }
            }
        ],
        "Military": [
            {
                "DF26_VEC": {
                    "density": 1,
                    "maxSpeed": 120
                }
            }
        ]
    }
}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| maxVehicles | number | 车辆总数上限 |
| TypeDensity | object | 大类车辆密度比例 |
| Type | object | 具体车型配置，包含密度和最高速度 |

### 4. 动态配置文件 (${DomainID}.json)

动态配置文件支持运行时更新，优先级高于默认配置：

```json
{
    "simSpaceID": "XODR_1010",
    "isRun": 0,
    "stepLength": 0.03,
    "isTrafficSingal": 0,
    "maxVehicles": 2000,
    "TypeDensity": {
        "Civilian": 1,
        "Military": 0
    },
    "Type": {
        "Civilian": [
            {
                "001-AcuraRL": {
                    "density": 0.6,
                    "maxSpeed": 120
                }
            }
        ],
        "Military": [
            {
                "DF26_VEC": {
                    "density": 1,
                    "maxSpeed": 120
                }
            }
        ]
    }
}
```

## 运行指南

### 启动方式

在 `${workPlace}/Build/` 目录下执行：

```bash
# 方式1：使用默认配置启动
./run.sh

# 方式2：使用指定配置文件启动
./run.sh simulation=${DomainID}
```

### 启动参数

| 参数 | 说明 | 示例 |
|------|------|------|
| simulation | 指定配置文件ID | simulation=19738 |

### 验证启动

启动成功后，系统会：
1. 加载路网文件和配置文件
2. 连接Redis服务
3. 初始化车辆流
4. 开始仿真循环

## 功能说明

### 1. 背景车流

- **功能**：自动生成和管理背景交通车辆
- **状态**：默认启动
- **配置**：通过vehicleTypes.json控制车辆类型和密度

### 2. 路线自动计算

- **功能**：为指定车辆计算最优路径
- **触发**：通过Redis接收导航请求
- **输出**：计算完成后通过Redis返回路径信息

### 3. 车辆接管控制

- **功能**：外部系统接管指定车辆的控制权
- **触发**：通过Redis发送控制事件
- **应用**：用于特殊车辆的人工干预或特殊任务执行

### 4. 爆炸毁伤模拟

- **功能**：模拟爆炸对交通流的影响
- **触发**：通过Redis接收爆炸事件
- **效果**：影响爆炸范围内的车辆行为和路网状态

## Redis通信协议

### 消息格式说明

所有Redis消息使用JSON格式，键名格式为：`${DomainID}:${MessageType}`

### 1. 爆炸毁伤事件

**键名**: `${DomainID}:BombEvent`

```json
{
    "TimeStamp": "2025-09-25T18:06:17.230Z",
    "DataArray": [
        {
            "BuildingTag": 99999,
            "HitActorLocation": "X=0.000 Y=0.000 Z=0.000",
            "HitLocation": "X=720029.629 Y=-381913.583 Z=11268.305",
            "BombLocation": "X=720029.629 Y=-381913.583 Z=11268.305",
            "MaxDamageRadius": 150,
            "EntityType": "Entity_Road"
        }
    ]
}
```

**触发条件**: TimeStamp字段发生变化且DataArray中存在EntityType为"Entity_Road"的项

### 2. 导航请求

**键名**: `DomainID:NavigationReqList`

```json
{
    "id": "36296",
    "NavigationList": [
        {
            "actorID": "1",
            "src": [
                120.515685,
                24.23309,
                0
            ],
            "dist": [
                120.516572,
                24.235647,
                0
            ]
        },
        {
            "actorID": "2",
            "src": [
                12256.055058826,
                3325.33241075,
                165.720799862
            ],
            "dist": [
                1666.84854199,
                6278.051859324,
                39.664238658
            ]
        }
    ]
}
```

**触发条件**: id字段发生变化且NavigationList中存在有效路径请求

### 3. 车辆控制事件

**键名**: `${DomainID}:ControllEvent`

```json
{
    "TimeStamp": "0002",
    "Actors": ["56", "78"]
}
```

**触发条件**: TimeStamp字段发生变化且Actors列表中存在有效车辆ID

## 故障排查

### 常见问题

1. **启动失败**
   - 检查路网文件是否存在且格式正确
   - 验证Redis服务连接状态
   - 查看日志文件中的错误信息

2. **车辆不生成**
   - 检查vehicleTypes.json配置是否正确
   - 确认maxVehicles参数设置合理
   - 验证路网文件是否包含有效道路

3. **Redis通信异常**
   - 检查RedisConfig.json配置
   - 验证网络连接和防火墙设置
   - 确认Redis服务正常运行

## 技术支持

如遇问题，请提供以下信息以便快速定位：
1. 具体的错误描述
2. 使用的配置文件内容
3. 系统环境信息

---

*最后更新日期: 2025-10-15*