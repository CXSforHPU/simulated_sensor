/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     RT-Thread    the first version, random module for simulated sensor
 */

#ifndef SIMU_SENSOR_RANDOM_H
#define SIMU_SENSOR_RANDOM_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file simu_sensor_random.h
 * @brief 模拟传感器随机数模块
 *
 * 该模块为 simulated_sensor 软件包提供随机数能力, 用于产生模拟传感器数据:
 *   - 整数随机数: simu_sensor_random_int()/simu_sensor_random_uint()
 *   - 浮点随机数: simu_sensor_random_float()/simu_sensor_random_unit()
 *   - 均匀(随机)分布: SIMU_SENSOR_RANDOM_DIST_UNIFORM
 *   - 正态(高斯)分布: simu_sensor_random_normal()/SIMU_SENSOR_RANDOM_DIST_NORMAL
 *   - 指数分布:     simu_sensor_random_exponential()/SIMU_SENSOR_RANDOM_DIST_EXPONENTIAL
 *   - 布尔/概率:    simu_sensor_random_bool()/simu_sensor_random_chance()
 *
 * 特性说明:
 *   - 自实现伪随机数算法(xoshiro128**), 不依赖 libc 的 rand()/srand(),
 *     也不依赖 libm, 可在无 libc 的 RT-Thread 配置下直接使用;
 *   - 提供两套 API:
 *       不带 _r 后缀: 使用模块内部共享的默认实例, 使用最简单;
 *       带   _r 后缀: 使用调用者提供的 SimuSensorRandom 实例, 各自独立、
 *                     适合多线程或需要互不干扰的多个模拟传感器。
 *   - 带 _r 后缀的接口允许传入 RT_NULL, 此时等价于使用默认实例。
 *
 * 命名约定: 函数采用下划线命名, 结构体/枚举/类型名采用首字母大写的驼峰命名。
 *
 * 使用示例:
 * @code
 * // 1. 最简单用法: 直接使用默认实例
 * int   temp = simu_sensor_random_int(20, 30);            // [20, 30] 闭区间整数
 * float volt = simu_sensor_random_float(0.0f, 3.3f);      // [0.0, 3.3) 浮点数
 * float humi = simu_sensor_random_normal(50.0f, 2.0f);    // 均值 50, 标准差 2
 *
 * // 2. 使用独立的生成器实例(推荐在多线程中使用)
 * static SimuSensorRandom rnd;
 * simu_sensor_random_init_auto(&rnd);
 * float value = simu_sensor_random_normal_r(&rnd, 25.0f, 0.5f);
 *
 * // 3. 用一个分布描述结构体描述传感器输出, 反复采样
 * SimuSensorRandomDist dist;
 * simu_sensor_random_dist_set_normal(&dist, 25.0f, 0.5f);
 * float sample = simu_sensor_random_sample(&dist);
 * @endcode
 */

/**
 * @brief 随机数生成器状态
 *
 * 保存一条伪随机序列的全部上下文, 不同实例之间互不影响。
 * 使用前必须调用 simu_sensor_random_init() 或 simu_sensor_random_init_auto() 初始化;
 * 若未初始化(内部状态全为 0), 首次采样时会自动以系统 tick 播种。
 */
typedef struct SimuSensorRandom
{
    rt_uint32_t state[4];    /**< 伪随机数内部状态(xoshiro128**) */
    rt_bool_t   spare_valid; /**< 正态分布缓存采样值是否有效 */
    float       spare;       /**< 正态分布缓存的第二个采样值 */
} SimuSensorRandom;

/**
 * @brief 分布类型
 */
typedef enum
{
    SIMU_SENSOR_RANDOM_DIST_UNIFORM     = 0, /**< 均匀(随机)分布, 使用 min/max */
    SIMU_SENSOR_RANDOM_DIST_NORMAL      = 1, /**< 正态(高斯)分布, 使用 mean/sigma */
    SIMU_SENSOR_RANDOM_DIST_EXPONENTIAL = 2  /**< 指数分布, 使用 lambda, 并以 min 为基线偏移 */
} SimuSensorRandomDistType;

/**
 * @brief 随机分布描述
 *
 * 用于描述一个模拟传感器输出量的分布规律, 配合 simu_sensor_random_sample()
 * 或 simu_sensor_random_sample_int() 使用。
 */
typedef struct SimuSensorRandomDist
{
    SimuSensorRandomDistType type; /**< 分布类型 */
    float min;                     /**< 均匀分布下限, 指数分布基线偏移 */
    float max;                     /**< 均匀分布上限 */
    float mean;                    /**< 正态分布均值 */
    float sigma;                   /**< 正态分布标准差(> 0) */
    float lambda;                  /**< 指数分布速率参数(> 0) */
} SimuSensorRandomDist;

/* -------------------------------------------------------------------------
 * 生成器状态管理
 * ------------------------------------------------------------------------- */

/**
 * @brief 使用指定种子初始化随机数生成器
 *
 * @param rnd  生成器实例, 不能为 RT_NULL
 * @param seed 随机种子, 相同种子产生完全相同的序列(便于复现问题)
 */
void simu_sensor_random_init(SimuSensorRandom *rnd, rt_uint32_t seed);

/**
 * @brief 使用系统 tick 及对象地址初始化随机数生成器(自动播种)
 *
 * 每次上电会得到不同的随机序列。
 *
 * @param rnd 生成器实例, 不能为 RT_NULL
 */
void simu_sensor_random_init_auto(SimuSensorRandom *rnd);

/**
 * @brief 重新播种模块内部的默认实例
 *
 * @param seed 随机种子
 */
void simu_sensor_random_seed(rt_uint32_t seed);

/* -------------------------------------------------------------------------
 * 基础随机数
 * ------------------------------------------------------------------------- */

/**
 * @brief 产生一个 32 位无符号随机数(默认实例)
 *
 * @return 32 位无符号随机数, 取值范围 [0, 0xFFFFFFFF]
 */
rt_uint32_t simu_sensor_random_uint32(void);

/**
 * @brief 产生一个 32 位无符号随机数
 *
 * @param rnd 生成器实例, 为 RT_NULL 时使用默认实例
 *
 * @return 32 位无符号随机数, 取值范围 [0, 0xFFFFFFFF]
 */
rt_uint32_t simu_sensor_random_uint32_r(SimuSensorRandom *rnd);

/**
 * @brief 产生 [0.0, 1.0) 区间的浮点随机数(默认实例)
 *
 * @return 归一化随机数
 */
float simu_sensor_random_unit(void);

/**
 * @brief 产生 [0.0, 1.0) 区间的浮点随机数
 *
 * @param rnd 生成器实例, 为 RT_NULL 时使用默认实例
 *
 * @return 归一化随机数
 */
float simu_sensor_random_unit_r(SimuSensorRandom *rnd);

/**
 * @brief 产生一个布尔随机值(默认实例), 真/假概率各 50%
 *
 * @return RT_TRUE 或 RT_FALSE
 */
rt_bool_t simu_sensor_random_bool(void);

/**
 * @brief 产生一个布尔随机值, 真/假概率各 50%
 *
 * @param rnd 生成器实例, 为 RT_NULL 时使用默认实例
 *
 * @return RT_TRUE 或 RT_FALSE
 */
rt_bool_t simu_sensor_random_bool_r(SimuSensorRandom *rnd);

/**
 * @brief 以指定概率产生 RT_TRUE(默认实例)
 *
 * @param probability 返回 RT_TRUE 的概率, 有效范围 [0.0, 1.0]
 *
 * @return RT_TRUE 或 RT_FALSE
 */
rt_bool_t simu_sensor_random_chance(float probability);

/**
 * @brief 以指定概率产生 RT_TRUE
 *
 * @param rnd         生成器实例, 为 RT_NULL 时使用默认实例
 * @param probability 返回 RT_TRUE 的概率, 有效范围 [0.0, 1.0]
 *
 * @return RT_TRUE 或 RT_FALSE
 */
rt_bool_t simu_sensor_random_chance_r(SimuSensorRandom *rnd, float probability);

/* -------------------------------------------------------------------------
 * 整数随机数
 * ------------------------------------------------------------------------- */

/**
 * @brief 产生 [min, max] 闭区间内的整数随机数(默认实例)
 *
 * min 与 max 顺序颠倒时会自动交换; min 等于 max 时直接返回该值。
 *
 * @param min 区间下限(含)
 * @param max 区间上限(含)
 *
 * @return 区间内的整数随机数
 */
int simu_sensor_random_int(int min, int max);

/**
 * @brief 产生 [min, max] 闭区间内的整数随机数
 *
 * @param rnd 生成器实例, 为 RT_NULL 时使用默认实例
 * @param min 区间下限(含)
 * @param max 区间上限(含)
 *
 * @return 区间内的整数随机数
 */
int simu_sensor_random_int_r(SimuSensorRandom *rnd, int min, int max);

/**
 * @brief 产生 [min, max] 闭区间内的无符号整数随机数(默认实例)
 *
 * @param min 区间下限(含)
 * @param max 区间上限(含)
 *
 * @return 区间内的无符号整数随机数
 */
rt_uint32_t simu_sensor_random_uint(rt_uint32_t min, rt_uint32_t max);

/**
 * @brief 产生 [min, max] 闭区间内的无符号整数随机数
 *
 * @param rnd 生成器实例, 为 RT_NULL 时使用默认实例
 * @param min 区间下限(含)
 * @param max 区间上限(含)
 *
 * @return 区间内的无符号整数随机数
 */
rt_uint32_t simu_sensor_random_uint_r(SimuSensorRandom *rnd, rt_uint32_t min, rt_uint32_t max);

/* -------------------------------------------------------------------------
 * 浮点随机数
 * ------------------------------------------------------------------------- */

/**
 * @brief 产生 [min, max) 区间内的浮点随机数(默认实例)
 *
 * min 与 max 顺序颠倒时会自动交换; min 等于 max 时直接返回该值。
 *
 * @param min 区间下限(含)
 * @param max 区间上限(不含)
 *
 * @return 区间内的浮点随机数
 */
float simu_sensor_random_float(float min, float max);

/**
 * @brief 产生 [min, max) 区间内的浮点随机数
 *
 * @param rnd 生成器实例, 为 RT_NULL 时使用默认实例
 * @param min 区间下限(含)
 * @param max 区间上限(不含)
 *
 * @return 区间内的浮点随机数
 */
float simu_sensor_random_float_r(SimuSensorRandom *rnd, float min, float max);

/* -------------------------------------------------------------------------
 * 概率分布
 * ------------------------------------------------------------------------- */

/**
 * @brief 产生正态(高斯)分布随机数(默认实例)
 *
 * 使用 Marsaglia 极坐标法, 不需要 libm。
 *
 * @param mean  均值
 * @param sigma 标准差, 小于等于 0 时直接返回 mean
 *
 * @return 正态分布随机数
 */
float simu_sensor_random_normal(float mean, float sigma);

/**
 * @brief 产生正态(高斯)分布随机数
 *
 * @param rnd   生成器实例, 为 RT_NULL 时使用默认实例
 * @param mean  均值
 * @param sigma 标准差, 小于等于 0 时直接返回 mean
 *
 * @return 正态分布随机数
 */
float simu_sensor_random_normal_r(SimuSensorRandom *rnd, float mean, float sigma);

/**
 * @brief 产生指数分布随机数(默认实例)
 *
 * 常用于模拟传感器采样间隔、故障间隔等。
 *
 * @param lambda 速率参数(> 0), 其倒数为均值
 *
 * @return 指数分布随机数, lambda 小于等于 0 时返回 0
 */
float simu_sensor_random_exponential(float lambda);

/**
 * @brief 产生指数分布随机数
 *
 * @param rnd    生成器实例, 为 RT_NULL 时使用默认实例
 * @param lambda 速率参数(> 0), 其倒数为均值
 *
 * @return 指数分布随机数, lambda 小于等于 0 时返回 0
 */
float simu_sensor_random_exponential_r(SimuSensorRandom *rnd, float lambda);

/* -------------------------------------------------------------------------
 * 随机分布描述结构体操作
 * ------------------------------------------------------------------------- */

/**
 * @brief 初始化随机分布描述结构体
 *
 * 默认配置为浮点均匀(随机)分布, 区间 [0.0, 1.0)。
 *
 * @param dist 待初始化的分布描述, 不能为 RT_NULL
 */
void simu_sensor_random_dist_init(SimuSensorRandomDist *dist);

/**
 * @brief 将分布描述配置为均匀(随机)分布
 *
 * @param dist 分布描述, 不能为 RT_NULL
 * @param min  区间下限
 * @param max  区间上限
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_random_dist_set_uniform(SimuSensorRandomDist *dist, float min, float max);

/**
 * @brief 将分布描述配置为正态(高斯)分布
 *
 * @param dist  分布描述, 不能为 RT_NULL
 * @param mean  均值
 * @param sigma 标准差(> 0)
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_random_dist_set_normal(SimuSensorRandomDist *dist, float mean, float sigma);

/**
 * @brief 将分布描述配置为指数分布
 *
 * @param dist   分布描述, 不能为 RT_NULL
 * @param lambda 速率参数(> 0)
 *
 * @return RT_EOK 成功; -RT_EINVAL 参数非法
 */
rt_err_t simu_sensor_random_dist_set_exponential(SimuSensorRandomDist *dist, float lambda);

/**
 * @brief 按分布描述进行一次浮点采样(默认实例)
 *
 * @param dist 分布描述, 不能为 RT_NULL
 *
 * @return 采样值, 参数非法时返回 0.0f
 */
float simu_sensor_random_sample(const SimuSensorRandomDist *dist);

/**
 * @brief 按分布描述进行一次浮点采样
 *
 * @param rnd  生成器实例, 为 RT_NULL 时使用默认实例
 * @param dist 分布描述, 不能为 RT_NULL
 *
 * @return 采样值, 参数非法时返回 0.0f
 */
float simu_sensor_random_sample_r(SimuSensorRandom *rnd, const SimuSensorRandomDist *dist);

/**
 * @brief 按分布描述进行一次整数采样(默认实例)
 *
 * 均匀分布时按 [min, max] 闭区间取整, 不会出现端点概率减半;
 * 其余分布取最近整数。
 *
 * @param dist 分布描述, 不能为 RT_NULL
 *
 * @return 采样值, 参数非法时返回 0
 */
int simu_sensor_random_sample_int(const SimuSensorRandomDist *dist);

/**
 * @brief 按分布描述进行一次整数采样
 *
 * @param rnd  生成器实例, 为 RT_NULL 时使用默认实例
 * @param dist 分布描述, 不能为 RT_NULL
 *
 * @return 采样值, 参数非法时返回 0
 */
int simu_sensor_random_sample_int_r(SimuSensorRandom *rnd, const SimuSensorRandomDist *dist);

#ifdef __cplusplus
}
#endif

#endif /* SIMU_SENSOR_RANDOM_H */
