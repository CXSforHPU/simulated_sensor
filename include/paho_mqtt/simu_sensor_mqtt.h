/*
 * Copyright (c) 2026, CXSforHPU
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     CXSforHPU    the first version, publish simulated sensor data via paho mqtt
 */

#ifndef SIMU_SENSOR_MQTT_H
#define SIMU_SENSOR_MQTT_H

#include <rtthread.h>

#include "simu_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file simu_sensor_mqtt.h
 * @brief 模拟传感器数据 MQTT 上报
 *
 * 基于 paho-mqtt 软件包(packages/pahomqtt-latest), 周期性地读取模拟传感器数据,
 * 组装成 JSON 后发布到 MQTT 主题:
 *
 *   - 聚合模式(SIMU_SENSOR_MQTT_PUBLISH_SINGLE, 默认): 所有传感器打包成一条 JSON,
 *     发布到 topic(默认 simu_sensor/data):
 *     @code
 *     {"device":"rtthread-simu","ts":28841090,"hour":8.01,"ambient":29.02,"count":4,
 *      "sensors":{"temp_humi":{"temperature":27.48,"humidity":50.9},
 *                 "gas":{"concentration":399}}}
 *     @endcode
 *     数据质量正常时不输出 quality 字段, 异常(冻结/超量程)时才会带上, 例如
 *     {"gas":{"quality":"frozen","concentration":391}}。
 *   - 分主题模式(SIMU_SENSOR_MQTT_PUBLISH_PER_SENSOR): 每个传感器一条 JSON,
 *     发布到 topic/<传感器名>, 例如 simu_sensor/data/gas:
 *     @code
 *     {"device":"rtthread-simu","name":"gas","type":"gas","quality":"good",
 *      "ts":28841090,"values":{"concentration":399}}
 *     @endcode
 *
 * 报文长度限制:
 *   paho-mqtt 的 RT-Thread 移植版(pipe 模式)用固定长度的管道传递待发数据(默认 512 字节),
 *   超长消息会被截断并导致 paho 线程解析异常。本模块会检查单条消息长度(结构头 + 负载 +
 *   主题 + 结束符), 因此:
 *     - 默认的聚合报文约 300 字节(4 个传感器), 正常可容纳约 8 个传感器;
 *     - 若聚合报文超出上限, 会自动降级为按传感器分别上报并打印告警;
 *     - 确需更大的聚合报文时, 可在工程中定义 RT_PIPE_BUFSZ(如 2048)并重新编译 paho 软件包。
 *
 * 使用步骤:
 * @code
 * // 1. 配置(必须在启动前调用)
 * simu_sensor_mqtt_set_uri("tcp://192.168.1.100:1883");
 * simu_sensor_mqtt_set_topic("simu_sensor/data");
 * simu_sensor_mqtt_set_interval(5000);
 *
 * // 2. 启动: 自动创建示例传感器(温湿度/气体/光照/大气压) + 连接 broker + 周期上报
 * simu_sensor_mqtt_start();
 *
 * // 3. 停止
 * simu_sensor_mqtt_stop();
 * @endcode
 *
 * 默认配置为本工程使用的 EMQX 服务器(tcp://47.93.225.100:1883, 用户名/密码 USER1/USER1),
 * 可通过上面的接口或 msh 命令修改; 服务器不需要鉴权时调用
 * simu_sensor_mqtt_set_credentials(RT_NULL, RT_NULL) 清除用户名密码。
 *
 * msh 命令: simu_sensor_mqtt <start|stop|status|pub|uri|topic|clientid|interval|mode|autocreate>
 *
 * 说明:
 *   - 依赖 paho-mqtt(PKG_USING_PAHOMQTT) 与 cJSON(PKG_USING_CJSON) 软件包;
 *   - paho-mqtt 的 RT-Thread 移植版仅支持 QoS1, 本模块固定使用 QoS1;
 *   - 采用阻塞发布(paho_mqtt_control 的 MQTT_CTRL_PUBLISH_BLOCK), publish 返回即表示
 *     数据已真正发出, published 计数就是成功发出的报文条数。
 */

/** URI 最大长度(含结束符) */
#define SIMU_SENSOR_MQTT_URI_MAX            64
/** 主题最大长度(含结束符) */
#define SIMU_SENSOR_MQTT_TOPIC_MAX          48
/** 客户端 ID 最大长度(含结束符) */
#define SIMU_SENSOR_MQTT_CLIENT_ID_MAX      24
/** paho 收发缓冲大小 */
#define SIMU_SENSOR_MQTT_BUFFER_SIZE        2048
/** 上报线程栈大小 */
#define SIMU_SENSOR_MQTT_THREAD_STACK_SIZE  4096
/** 上报线程优先级 */
#define SIMU_SENSOR_MQTT_THREAD_PRIORITY    20

/** 默认 broker 地址(本工程使用的 EMQX 服务器) */
#define SIMU_SENSOR_MQTT_DEFAULT_URI        "tcp://47.93.225.100:1883"
/** 默认发布主题 */
#define SIMU_SENSOR_MQTT_DEFAULT_TOPIC      "simu_sensor/data"
/** 默认客户端 ID */
#define SIMU_SENSOR_MQTT_DEFAULT_CLIENT_ID  "rtthread-simu"
/** 默认用户名与密码(EMQX 服务器的认证信息) */
#define SIMU_SENSOR_MQTT_DEFAULT_USERNAME   "USER1"
#define SIMU_SENSOR_MQTT_DEFAULT_PASSWORD   "USER1"
/** 默认上报周期(ms) */
#define SIMU_SENSOR_MQTT_DEFAULT_INTERVAL   5000
/** 上报周期下限(ms) */
#define SIMU_SENSOR_MQTT_MIN_INTERVAL       500

/**
 * @brief 上报模式
 */
typedef enum
{
    SIMU_SENSOR_MQTT_PUBLISH_SINGLE = 0, /**< 所有传感器聚合为一条 JSON, 发布到 topic */
    SIMU_SENSOR_MQTT_PUBLISH_PER_SENSOR  /**< 每个传感器一条 JSON, 发布到 topic/<name> */
} SimuSensorMqttPublishMode;

/* -------------------------------------------------------------------------
 * 配置接口(需在 start 之前调用)
 * ------------------------------------------------------------------------- */

/**
 * @brief 设置 MQTT broker 地址
 *
 * @param uri 形如 "tcp://192.168.1.100:1883" 的地址
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_EBUSY 已启动
 */
rt_err_t simu_sensor_mqtt_set_uri(const char *uri);

/**
 * @brief 设置发布主题(分主题模式下作为前缀)
 *
 * @param topic 主题名, 如 "simu_sensor/data"
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_EBUSY 已启动
 */
rt_err_t simu_sensor_mqtt_set_topic(const char *topic);

/**
 * @brief 设置客户端 ID
 *
 * @param client_id 客户端 ID
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_EBUSY 已启动
 */
rt_err_t simu_sensor_mqtt_set_client_id(const char *client_id);

/**
 * @brief 设置用户名与密码
 *
 * @param username 用户名, 传 RT_NULL 表示不使用鉴权
 * @param password 密码, 传 RT_NULL 表示不使用鉴权
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_EBUSY 已启动
 */
rt_err_t simu_sensor_mqtt_set_credentials(const char *username, const char *password);

/**
 * @brief 设置上报周期
 *
 * @param interval_ms 上报周期(ms), 有效范围 [SIMU_SENSOR_MQTT_MIN_INTERVAL, 3600000]
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_EBUSY 已启动
 */
rt_err_t simu_sensor_mqtt_set_interval(rt_uint32_t interval_ms);

/**
 * @brief 设置上报模式
 *
 * @param mode 上报模式
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_EBUSY 已启动
 */
rt_err_t simu_sensor_mqtt_set_publish_mode(SimuSensorMqttPublishMode mode);

/**
 * @brief 设置启动时是否自动创建示例传感器(注册表中没有传感器时)
 *
 * @param enable RT_TRUE 自动创建(默认)
 *
 * @return RT_EOK 成功; -RT_EBUSY 已启动
 */
rt_err_t simu_sensor_mqtt_set_auto_create(rt_bool_t enable);

/**
 * @brief 获取当前配置
 */
const char             *simu_sensor_mqtt_get_uri(void);
const char             *simu_sensor_mqtt_get_topic(void);
const char             *simu_sensor_mqtt_get_client_id(void);
rt_uint32_t             simu_sensor_mqtt_get_interval(void);
SimuSensorMqttPublishMode simu_sensor_mqtt_get_publish_mode(void);

/* -------------------------------------------------------------------------
 * 运行控制
 * ------------------------------------------------------------------------- */

/**
 * @brief 启动 MQTT 上报
 *
 * 按需自动创建示例传感器, 启动 paho 客户端线程(含自动重连)与上报线程。
 *
 * @return RT_EOK 成功; -RT_EBUSY 已经启动; -RT_ENOMEM 内存不足; -RT_ERROR 启动失败
 */
rt_err_t simu_sensor_mqtt_start(void);

/**
 * @brief 停止 MQTT 上报
 *
 * @return RT_EOK 成功; -RT_ERROR 未启动
 */
rt_err_t simu_sensor_mqtt_stop(void);

/**
 * @brief 是否已启动
 *
 * @return RT_TRUE 已启动
 */
rt_bool_t simu_sensor_mqtt_is_started(void);

/**
 * @brief 是否已连接到 broker
 *
 * @return RT_TRUE 已连接
 */
rt_bool_t simu_sensor_mqtt_is_connected(void);

/**
 * @brief 已成功提交给 paho 的发布条数
 *
 * @return 发布条数
 */
rt_uint32_t simu_sensor_mqtt_get_publish_count(void);

/**
 * @brief 发布失败条数(未连接、缓冲不足等)
 *
 * @return 失败条数
 */
rt_uint32_t simu_sensor_mqtt_get_fail_count(void);

/**
 * @brief 立即上报一帧(不等待上报周期)
 *
 * @return RT_EOK 成功; -RT_ERROR 未启动/未连接/发送失败
 */
rt_err_t simu_sensor_mqtt_publish_once(void);

#ifdef __cplusplus
}
#endif

#endif /* SIMU_SENSOR_MQTT_H */
