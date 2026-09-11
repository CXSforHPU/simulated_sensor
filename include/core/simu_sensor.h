/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     RT-Thread    the first version, simulated sensor objects
 */

#ifndef SIMU_SENSOR_H
#define SIMU_SENSOR_H

#include <rtthread.h>

#include "simu_sensor_random.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file simu_sensor.h
 * @brief 模拟传感器对象
 *
 * 在随机数模块(simu_sensor_random)之上搭建模拟传感器对象, 把随机数包装成
 * "可读取的传感器": 每个对象有名称、类型、量程、单位、采样周期, 并按照物理量的
 * 特点生成数据, 可直接替代真实传感器用于上层业务(采集、上报、告警)的联调。
 *
 * 当前内置的传感器类型:
 *   - 温度             SIMU_SENSOR_TYPE_TEMPERATURE   ℃
 *   - 湿度             SIMU_SENSOR_TYPE_HUMIDITY      %RH
 *   - 温湿度一体       SIMU_SENSOR_TYPE_TEMP_HUMI     2 个通道(温度 + 湿度)
 *   - 气体浓度         SIMU_SENSOR_TYPE_GAS           ppm(含随机超限脉冲, 便于测告警)
 *   - 光照强度         SIMU_SENSOR_TYPE_ILLUMINANCE   lux(随昼夜变化)
 *   - 大气压           SIMU_SENSOR_TYPE_PRESSURE      hPa
 *   - 噪声             SIMU_SENSOR_TYPE_NOISE         dB
 *   - 自定义           SIMU_SENSOR_TYPE_CUSTOM        由用户配置通道
 *
 * 数据生成模型(每个通道):
 * @code
 * 采样值 = 基线 base
 *        + base * diurnal * 昼夜因子        // 日变化, 昼夜因子在 [-1, 1]
 *        + ambient_gain * (环境温度 - 25℃)  // 与环境温度耦合, 如湿度随温度升高而下降
 *        + 缓慢漂移(随机游走 + 回中, 受 drift_limit 限制)
 *        + 高斯噪声 noise
 *        + 异常脉冲(概率 spike_prob, 幅度 spike_gain)   // 如气体浓度突发、噪声尖峰
 * 采样值 = 量化(resolution) 后限幅到 [min, max]
 * @endcode
 * 另外每个对象都支持故障注入(SIMU_SENSOR_FAULT_*), 用于验证上层对异常数据的处理。
 *
 * 使用示例:
 * @code
 * // 1. 静态创建: 温湿度一体传感器, 使用内置物理参数预设
 * static SimuSensor th_sensor;
 * SimuSensorData  data;
 *
 * simu_sensor_init(&th_sensor, "th", SIMU_SENSOR_TYPE_TEMP_HUMI);
 * simu_sensor_set_period(&th_sensor, 2000);           // 2s 采样一次
 *
 * if (simu_sensor_read(&th_sensor, &data) == RT_EOK)
 * {
 *     // data.value[0] 温度(℃), data.value[1] 湿度(%RH)
 * }
 *
 * // 2. 动态创建并按名称读取(适合 MQTT 上报任务遍历所有模拟传感器)
 * SimuSensor *gas = simu_sensor_create_default("gas", SIMU_SENSOR_TYPE_GAS);
 * SimuSensorData all[SIMU_SENSOR_MAX_NUM];
 * rt_uint32_t    count = simu_sensor_read_all(all, SIMU_SENSOR_MAX_NUM);
 *
 * // 3. 自定义通道参数
 * SimuSensorChannel channel = *simu_sensor_get_channel(&th_sensor, 0);
 * channel.base = 30.0f;                                // 基线改为 30℃
 * channel.noise = 0.3f;                                // 加大噪声
 * simu_sensor_set_channel(&th_sensor, 0, &channel);
 * @endcode
 *
 * msh 命令(见 simu_sensor_cmd.c): simu_sensor list / read / monitor / fault / demo / info
 */

/** 单个传感器支持的通道数上限(温湿度一体传感器需要 2 个) */
#define SIMU_SENSOR_CHANNEL_MAX         2

/** 传感器名称最大长度(含结束符) */
#define SIMU_SENSOR_NAME_MAX            16

/** 环境耦合的参考温度(℃), 通道的 ambient_gain 以该温度为基准 */
#define SIMU_SENSOR_AMBIENT_REF         25.0f

/** 读取所有传感器时可容纳的传感器数量 */
#define SIMU_SENSOR_MAX_NUM             16

/* 数据质量(SimuSensorData.quality) */
#define SIMU_SENSOR_QUALITY_GOOD        0   /**< 数据正常 */
#define SIMU_SENSOR_QUALITY_FROZEN      1   /**< 数据冻结(故障注入) */
#define SIMU_SENSOR_QUALITY_OUT_OF_RANGE 2  /**< 数据超量程(故障注入) */

/**
 * @brief 模拟传感器类型
 */
typedef enum
{
    SIMU_SENSOR_TYPE_TEMPERATURE = 0, /**< 温度, ℃ */
    SIMU_SENSOR_TYPE_HUMIDITY,        /**< 湿度, %RH */
    SIMU_SENSOR_TYPE_TEMP_HUMI,       /**< 温湿度一体, ℃ + %RH */
    SIMU_SENSOR_TYPE_GAS,             /**< 气体浓度, ppm */
    SIMU_SENSOR_TYPE_ILLUMINANCE,     /**< 光照强度, lux */
    SIMU_SENSOR_TYPE_PRESSURE,        /**< 大气压, hPa */
    SIMU_SENSOR_TYPE_NOISE,           /**< 噪声, dB */
    SIMU_SENSOR_TYPE_CUSTOM,          /**< 自定义, 通道由用户配置 */
    SIMU_SENSOR_TYPE_MAX
} SimuSensorType;

/**
 * @brief 故障注入模式
 */
typedef enum
{
    SIMU_SENSOR_FAULT_NONE = 0,    /**< 正常 */
    SIMU_SENSOR_FAULT_FREEZE,      /**< 数据冻结: 数值不再变化 */
    SIMU_SENSOR_FAULT_OUT_OF_RANGE,/**< 超量程: 输出超出量程的数值 */
    SIMU_SENSOR_FAULT_OFFLINE      /**< 离线: 读取返回 -RT_ERROR */
} SimuSensorFault;

/**
 * @brief 传感器通道配置
 *
 * 一个通道描述一个物理量, 其中的参数共同决定采样值的生成方式。
 */
typedef struct SimuSensorChannel
{
    const char *name;         /**< 通道名称, 如 "temperature" */
    const char *unit;         /**< 单位, 如 "C"、"%RH" */
    float       min;          /**< 量程下限 */
    float       max;          /**< 量程上限 */
    float       base;         /**< 基线值(均值) */
    float       noise;        /**< 高斯噪声标准差, 0 表示无噪声 */
    float       resolution;   /**< 输出分辨率(量化步长), 0 表示不量化 */
    float       drift_step;   /**< 每次采样的随机游走步长, 0 表示无漂移 */
    float       drift_limit;  /**< 漂移量绝对值上限 */
    float       diurnal;      /**< 昼夜变化幅度(基线比例), 0 表示无昼夜变化 */
    float       ambient_gain; /**< 环境温度耦合增益(每 ℃), 湿度可设为负值 */
    float       spike_prob;   /**< 每次采样出现异常脉冲的概率, 0 表示无脉冲 */
    float       spike_gain;   /**< 异常脉冲幅度(绝对量) */
} SimuSensorChannel;

/**
 * @brief 传感器内部状态(由模块维护, 请勿直接修改)
 */
typedef struct SimuSensorPrivate
{
    SimuSensorRandom rnd;                           /**< 本对象独立的随机数生成器 */
    float            drift[SIMU_SENSOR_CHANNEL_MAX];/**< 各通道当前漂移量 */
    float            cache[SIMU_SENSOR_CHANNEL_MAX];/**< 各通道最近一次采样值 */
    rt_uint32_t      stamp;                         /**< 最近一次采样时刻(仿真 ms) */
    rt_bool_t        sampled;                       /**< 是否已经采样过 */
    rt_bool_t        inited;                        /**< 对象是否已初始化 */
    rt_bool_t        dynamic;                       /**< 是否由 simu_sensor_create 分配 */
} SimuSensorPrivate;

/**
 * @brief 模拟传感器对象
 *
 * 可直接静态定义(配合 simu_sensor_init), 也可用 simu_sensor_create 动态创建。
 * 名称、类型、通道、故障模式、采样周期为配置项; priv 为内部运行状态。
 */
typedef struct SimuSensor
{
    char              name[SIMU_SENSOR_NAME_MAX];   /**< 传感器名称 */
    SimuSensorType    type;                         /**< 传感器类型 */
    rt_uint8_t        channel_count;                /**< 通道数量 */
    SimuSensorChannel channel[SIMU_SENSOR_CHANNEL_MAX]; /**< 通道配置 */
    SimuSensorFault   fault;                        /**< 故障注入模式 */
    rt_uint32_t       period;                       /**< 采样周期(ms), 0 表示每次读取都重新采样 */
    rt_uint32_t       seed;                         /**< 随机种子 */
    SimuSensorPrivate priv;                         /**< 内部状态 */
    struct SimuSensor *next;                        /**< 注册表链表指针 */
} SimuSensor;

/**
 * @brief 一次采样得到的数据
 */
typedef struct SimuSensorData
{
    const char    *name;                            /**< 传感器名称(指向对象内部缓冲) */
    SimuSensorType type;                            /**< 传感器类型 */
    rt_uint8_t     channel_count;                   /**< 通道数量 */
    rt_uint8_t     quality;                         /**< 数据质量, SIMU_SENSOR_QUALITY_* */
    rt_uint32_t    timestamp;                       /**< 采样时刻(仿真 ms) */
    float          value[SIMU_SENSOR_CHANNEL_MAX];  /**< 各通道采样值 */
} SimuSensorData;

/* -------------------------------------------------------------------------
 * 对象生命周期
 * ------------------------------------------------------------------------- */

/**
 * @brief 初始化一个模拟传感器对象并注册到模块(静态分配对象时使用)
 *
 * 按类型套用内置的物理参数预设(基线、量程、噪声、漂移、昼夜变化、耦合等)。
 *
 * @param sensor 传感器对象, 不能为 RT_NULL
 * @param name   传感器名称, 长度需小于 SIMU_SENSOR_NAME_MAX 且不与已注册的重复
 * @param type   传感器类型, 取值小于 SIMU_SENSOR_TYPE_MAX
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法或名称重复
 */
rt_err_t simu_sensor_init(SimuSensor *sensor, const char *name, SimuSensorType type);

/**
 * @brief 注销传感器对象(不释放内存)
 *
 * @param sensor 传感器对象, 可为 RT_NULL
 */
void simu_sensor_deinit(SimuSensor *sensor);

/**
 * @brief 动态创建一个模拟传感器对象并注册
 *
 * @param name 传感器名称
 * @param type 传感器类型
 *
 * @return 传感器对象; RT_NULL 表示参数非法或内存不足
 */
SimuSensor *simu_sensor_create(const char *name, SimuSensorType type);

/**
 * @brief 动态创建并套用类型预设的模拟传感器对象
 *
 * 与 simu_sensor_create 等价(init 内部已套用预设), 便于语义化调用。
 *
 * @param name 传感器名称
 * @param type 传感器类型
 *
 * @return 传感器对象; RT_NULL 表示失败
 */
SimuSensor *simu_sensor_create_default(const char *name, SimuSensorType type);

/**
 * @brief 注销并释放动态创建的传感器对象
 *
 * @param sensor 由 simu_sensor_create 创建的对象, 可为 RT_NULL
 */
void simu_sensor_destroy(SimuSensor *sensor);

/**
 * @brief 复位传感器运行状态(清除漂移与缓存值, 重新播种)
 *
 * @param sensor 传感器对象
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_reset(SimuSensor *sensor);

/**
 * @brief 设置采样周期
 *
 * 周期内重复读取返回缓存的数值, 周期外重新采样; 设置为 0 表示每次读取都重新采样。
 *
 * @param sensor 传感器对象
 * @param period 采样周期(ms)
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_set_period(SimuSensor *sensor, rt_uint32_t period);

/**
 * @brief 设置随机种子(相同种子产生相同的采样序列, 便于复现问题)
 *
 * @param sensor 传感器对象
 * @param seed   随机种子
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_set_seed(SimuSensor *sensor, rt_uint32_t seed);

/**
 * @brief 设置故障注入模式
 *
 * @param sensor 传感器对象
 * @param fault  故障模式
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_set_fault(SimuSensor *sensor, SimuSensorFault fault);

/**
 * @brief 修改某个通道的配置
 *
 * @param sensor  传感器对象
 * @param index   通道下标, 需小于 channel_count
 * @param channel 新的通道配置(会拷贝一份), 不能为 RT_NULL
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_set_channel(SimuSensor *sensor, rt_uint8_t index, const SimuSensorChannel *channel);

/**
 * @brief 获取某个通道的配置
 *
 * @param sensor 传感器对象
 * @param index  通道下标
 *
 * @return 通道配置指针; RT_NULL 表示参数非法
 */
const SimuSensorChannel *simu_sensor_get_channel(const SimuSensor *sensor, rt_uint8_t index);

/* -------------------------------------------------------------------------
 * 注册表操作
 * ------------------------------------------------------------------------- */

/**
 * @brief 按名称查找传感器
 *
 * @param name 传感器名称
 *
 * @return 传感器对象; RT_NULL 表示未找到
 */
SimuSensor *simu_sensor_find(const char *name);

/**
 * @brief 获取已注册传感器的数量
 *
 * @return 传感器数量
 */
rt_uint32_t simu_sensor_count(void);

/**
 * @brief 按下标获取已注册的传感器
 *
 * @param index 下标, 从 0 开始
 *
 * @return 传感器对象; RT_NULL 表示下标越界
 */
SimuSensor *simu_sensor_get(rt_uint32_t index);

/* -------------------------------------------------------------------------
 * 数据读取
 * ------------------------------------------------------------------------- */

/**
 * @brief 读取传感器数据(按采样周期决定是否重新采样)
 *
 * @param sensor 传感器对象
 * @param data   读取结果, 不能为 RT_NULL
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_ERROR 传感器处于离线故障
 */
rt_err_t simu_sensor_read(SimuSensor *sensor, SimuSensorData *data);

/**
 * @brief 强制重新采样并读取(忽略采样周期)
 *
 * @param sensor 传感器对象
 * @param data   读取结果, 不能为 RT_NULL
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法; -RT_ERROR 传感器处于离线故障
 */
rt_err_t simu_sensor_sample(SimuSensor *sensor, SimuSensorData *data);

/**
 * @brief 按名称读取传感器数据
 *
 * @param name 传感器名称
 * @param data 读取结果, 不能为 RT_NULL
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法或未找到; -RT_ERROR 传感器离线
 */
rt_err_t simu_sensor_read_by_name(const char *name, SimuSensorData *data);

/**
 * @brief 读取所有已注册传感器的数据(离线传感器会被跳过)
 *
 * @param data      结果数组
 * @param max_count 数组可容纳的元素个数
 *
 * @return 实际读取到的传感器数量
 */
rt_uint32_t simu_sensor_read_all(SimuSensorData *data, rt_uint32_t max_count);

/**
 * @brief 获取某通道最近一次的采样值(不会触发采样)
 *
 * 未采样过时返回该通道的基线值。
 *
 * @param sensor 传感器对象
 * @param index  通道下标
 *
 * @return 最近一次采样值; 参数非法时返回 0.0f
 */
float simu_sensor_value(const SimuSensor *sensor, rt_uint8_t index);

/* -------------------------------------------------------------------------
 * 环境与时间模型
 * ------------------------------------------------------------------------- */

/**
 * @brief 获取当前环境温度(℃)
 *
 * 由模块的日变化模型驱动: 环境温度 = 基线 + 幅度 * 昼夜因子。
 *
 * @return 环境温度
 */
float simu_sensor_ambient_temperature(void);

/**
 * @brief 设置环境温度模型
 *
 * @param base  环境温度基线(℃)
 * @param swing 昼夜温差幅度(℃)
 */
void simu_sensor_set_ambient(float base, float swing);

/**
 * @brief 获取累计仿真时间(ms)
 *
 * @return 仿真时间
 */
rt_uint32_t simu_sensor_time_ms(void);

/**
 * @brief 设置仿真时间加速倍数(默认 1.0)
 *
 * 用于在几分钟内观察到昼夜变化等慢过程, 例如设置为 3600 表示 1 秒等于 1 小时。
 *
 * @param scale 加速倍数, 需大于 0
 */
void simu_sensor_set_time_scale(float scale);

/**
 * @brief 获取仿真时钟的当前时刻(小时)
 *
 * @return 0.0 ~ 24.0
 */
float simu_sensor_time_of_day(void);

/**
 * @brief 设置仿真时钟的当前时刻
 *
 * @param hour 小时数, 取值 [0, 24)
 */
void simu_sensor_set_time_of_day(float hour);

/**
 * @brief 获取昼夜因子
 *
 * @return -1.0 ~ 1.0, 午夜为 -1, 正午为 1
 */
float simu_sensor_diurnal_factor(void);

/* -------------------------------------------------------------------------
 * 元信息
 * ------------------------------------------------------------------------- */

/**
 * @brief 获取传感器类型名称
 *
 * @param type 传感器类型
 *
 * @return 类型名称字符串
 */
const char *simu_sensor_type_name(SimuSensorType type);

/**
 * @brief 获取故障模式名称
 *
 * @param fault 故障模式
 *
 * @return 故障名称字符串
 */
const char *simu_sensor_fault_name(SimuSensorFault fault);

/**
 * @brief 获取数据质量名称
 *
 * @param quality 数据质量
 *
 * @return 质量名称字符串
 */
const char *simu_sensor_quality_name(rt_uint8_t quality);

/**
 * @brief 把浮点数值转换为定点字符串
 *
 * klibc 的精简版 printf 不支持 %f, 这里提供统一的定点转换, 便于打印采样值或组装报文。
 * 例如 value=27.485、decimals=2 时输出 "27.49"。
 *
 * @param buffer   输出缓冲区
 * @param size     缓冲区长度
 * @param value    数值
 * @param decimals 小数位数(0 ~ 3, 超过 3 时按 3 处理)
 *
 * @return 写入的字符数(不含结束符); 参数非法时返回 0
 */
rt_size_t simu_sensor_value_to_string(char *buffer, rt_size_t size, float value, rt_uint8_t decimals);

#ifdef __cplusplus
}
#endif

#endif /* SIMU_SENSOR_H */
