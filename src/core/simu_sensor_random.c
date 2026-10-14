/*
 * Copyright (c) 2026, CXSforHPU
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     CXSforHPU    the first version, random module for simulated sensor
 */

#include <rtthread.h>
#include <rthw.h>

#include "simu_sensor_random.h"

/* 把 32bit 随机数的高 24 位映射到 [0, 1) */
#define RANDOM_UNIT_SCALE       (1.0f / 16777216.0f)
/* ln(2), 用于对数计算的指数部分 */
#define RANDOM_LN2              0.69314718f
/* 对 log(0) 的兜底返回值 */
#define RANDOM_LOG_ZERO         (-3.402823466e+38f)
/* int 类型的上下限, 用于采样值限幅 */
#define RANDOM_INT_MAX          2147483647
#define RANDOM_INT_MIN          (-2147483647 - 1)

/**
 * @brief 循环左移
 */
static rt_uint32_t random_rotl(rt_uint32_t value, rt_uint8_t bits)
{
    return (value << bits) | (value >> (32U - bits));
}

/**
 * @brief splitmix32, 用于把单个种子扩展为多个互不相关的状态字
 */
static rt_uint32_t random_splitmix32(rt_uint32_t *state)
{
    rt_uint32_t value = (*state += 0x9E3779B9U);

    value ^= value >> 16;
    value *= 0x21F0AAADU;
    value ^= value >> 15;
    value *= 0x735A2D97U;
    value ^= value >> 15;

    return value;
}

/**
 * @brief 获取模块内部共享的默认实例, 首次使用时自动播种
 */
static SimuSensorRandom *random_default_instance(void)
{
    static SimuSensorRandom default_instance;
    static rt_bool_t inited = RT_FALSE;

    if (inited == RT_FALSE)
    {
        rt_base_t level = rt_hw_interrupt_disable();

        if (inited == RT_FALSE)
        {
            simu_sensor_random_init_auto(&default_instance);
            inited = RT_TRUE;
        }

        rt_hw_interrupt_enable(level);
    }

    return &default_instance;
}

/**
 * @brief 产生一个 32 位随机数, 是模块内部唯一的出口
 */
static rt_uint32_t random_next(SimuSensorRandom *rnd)
{
    rt_uint32_t result;
    rt_uint32_t temp;

    if (rnd == RT_NULL)
    {
        rnd = random_default_instance();
    }

    /* 未初始化的实例(内部状态全 0)自动播种, 避免一直输出 0 */
    if ((rnd->state[0] | rnd->state[1] | rnd->state[2] | rnd->state[3]) == 0U)
    {
        simu_sensor_random_init_auto(rnd);
    }

    /* xoshiro128** */
    result = random_rotl(rnd->state[1] * 5U, 7) * 9U;
    temp   = rnd->state[1] << 9;

    rnd->state[2] ^= rnd->state[0];
    rnd->state[3] ^= rnd->state[1];
    rnd->state[1] ^= rnd->state[2];
    rnd->state[0] ^= rnd->state[3];
    rnd->state[2] ^= temp;
    rnd->state[3]  = random_rotl(rnd->state[3], 11);

    return result;
}

/**
 * @brief 产生 [0, span) 区间内的无符号随机数, span 为 0 表示整个 32bit 区间
 *
 * 使用拒绝采样剔除不能整除的部分, 避免取模带来的偏置。
 */
static rt_uint32_t random_bounded(SimuSensorRandom *rnd, rt_uint32_t span)
{
    rt_uint32_t threshold;
    rt_uint32_t value;

    if (span == 0U)
    {
        return random_next(rnd);
    }

    /* (0 - span) % span 等价于 2^32 % span */
    threshold = (rt_uint32_t)(0U - span) % span;

    do
    {
        value = random_next(rnd);
    } while (value < threshold);

    return value % span;
}

/**
 * @brief 牛顿迭代计算平方根, 不依赖 libm
 */
static float random_sqrt(float value)
{
    union
    {
        float       f;
        rt_uint32_t u;
    } convert;
    float       result;
    rt_uint8_t  index;

    if (value <= 0.0f)
    {
        return 0.0f;
    }

    /* 把指数部分减半作为初值, 相对误差小于 5% */
    convert.f = value;
    convert.u = (convert.u >> 1) + 0x1FC00000U;
    result = convert.f;

    /* 每迭代一次精度翻倍, 5 次足以达到 float 精度 */
    for (index = 0; index < 5; index++)
    {
        result = 0.5f * (result + value / result);
    }

    return result;
}

/**
 * @brief 位分解 + 反双曲正切级数计算自然对数, 不依赖 libm
 */
static float random_log(float value)
{
    union
    {
        float       f;
        rt_uint32_t u;
    } convert;
    rt_int32_t  exponent;
    float       mantissa;
    float       y;
    float       y2;
    float       term;
    float       sum = 0.0f;
    rt_uint8_t  index;

    if (value <= 0.0f)
    {
        return RANDOM_LOG_ZERO;
    }

    /* value = mantissa * 2^exponent, 其中 mantissa 属于 [1, 2) */
    convert.f = value;
    exponent  = (rt_int32_t)((convert.u >> 23) & 0xFFU) - 127;
    convert.u = (convert.u & 0x007FFFFFU) | 0x3F800000U;
    mantissa  = convert.f;

    /* 进一步规约到 [sqrt(0.5), sqrt(2)), 使级数收敛更快 */
    if (mantissa > 1.41421356f)
    {
        mantissa *= 0.5f;
        exponent++;
    }

    /* ln(mantissa) = 2 * (y + y^3/3 + y^5/5 + ...), |y| <= 0.1716 */
    y    = (mantissa - 1.0f) / (mantissa + 1.0f);
    y2   = y * y;
    term = y;

    for (index = 0; index < 8; index++)
    {
        sum += term / (float)(2U * index + 1U);
        term *= y2;
    }

    return 2.0f * sum + (float)exponent * RANDOM_LN2;
}

/**
 * @brief 产生标准正态分布随机数(均值 0, 标准差 1)
 *
 * 使用 Marsaglia 极坐标法: 每次产生两个采样值, 多余的缓存到实例中,
 * 该方法只需要 sqrt 与 log, 不需要三角函数。
 */
static float random_normal_raw(SimuSensorRandom *rnd)
{
    float u;
    float v;
    float s;
    float scale;

    if (rnd == RT_NULL)
    {
        rnd = random_default_instance();
    }

    /* 上一次剩余的采样值可以直接使用 */
    if (rnd->spare_valid == RT_TRUE)
    {
        rnd->spare_valid = RT_FALSE;

        return rnd->spare;
    }

    do
    {
        u = simu_sensor_random_unit_r(rnd) * 2.0f - 1.0f;
        v = simu_sensor_random_unit_r(rnd) * 2.0f - 1.0f;
        s = u * u + v * v;
    } while ((s >= 1.0f) || (s <= 0.0f));

    scale = random_sqrt(-2.0f * random_log(s) / s);

    rnd->spare       = v * scale;
    rnd->spare_valid = RT_TRUE;

    return u * scale;
}

/**
 * @brief 浮点数四舍五入取整, 并做上下限保护
 */
static int random_round(float value)
{
    if (value >= 2147483000.0f)
    {
        return RANDOM_INT_MAX;
    }

    if (value <= -2147483000.0f)
    {
        return RANDOM_INT_MIN;
    }

    return (int)((value >= 0.0f) ? (value + 0.5f) : (value - 0.5f));
}

void simu_sensor_random_init(SimuSensorRandom *rnd, rt_uint32_t seed)
{
    rt_uint32_t mixer = seed;
    rt_uint8_t  index;

    if (rnd == RT_NULL)
    {
        return;
    }

    for (index = 0; index < 4; index++)
    {
        rnd->state[index] = random_splitmix32(&mixer);
    }

    /* xoshiro128** 的内部状态不允许全 0 */
    if ((rnd->state[0] | rnd->state[1] | rnd->state[2] | rnd->state[3]) == 0U)
    {
        rnd->state[0] = 0xA5A5A5A5U;
    }

    rnd->spare_valid = RT_FALSE;
    rnd->spare       = 0.0f;
}

void simu_sensor_random_init_auto(SimuSensorRandom *rnd)
{
    rt_uint32_t seed;

    if (rnd == RT_NULL)
    {
        return;
    }

    /* 系统 tick 与对象地址混合, 保证每次上电得到不同的序列 */
    seed  = (rt_uint32_t)rt_tick_get() * 0x9E3779B9U;
    seed ^= (rt_uint32_t)(rt_uintptr_t)rnd * 0x85EBCA6BU;

    simu_sensor_random_init(rnd, seed);
}

void simu_sensor_random_seed(rt_uint32_t seed)
{
    simu_sensor_random_init(random_default_instance(), seed);
}

rt_uint32_t simu_sensor_random_uint32(void)
{
    return random_next(RT_NULL);
}

rt_uint32_t simu_sensor_random_uint32_r(SimuSensorRandom *rnd)
{
    return random_next(rnd);
}

float simu_sensor_random_unit(void)
{
    return simu_sensor_random_unit_r(RT_NULL);
}

float simu_sensor_random_unit_r(SimuSensorRandom *rnd)
{
    /* 取高 24 位, 保证结果落在 [0, 1) */
    return (float)(random_next(rnd) >> 8) * RANDOM_UNIT_SCALE;
}

rt_bool_t simu_sensor_random_bool(void)
{
    return simu_sensor_random_bool_r(RT_NULL);
}

rt_bool_t simu_sensor_random_bool_r(SimuSensorRandom *rnd)
{
    return (random_next(rnd) & 0x00010000U) ? RT_TRUE : RT_FALSE;
}

rt_bool_t simu_sensor_random_chance(float probability)
{
    return simu_sensor_random_chance_r(RT_NULL, probability);
}

rt_bool_t simu_sensor_random_chance_r(SimuSensorRandom *rnd, float probability)
{
    if (probability <= 0.0f)
    {
        return RT_FALSE;
    }

    if (probability >= 1.0f)
    {
        return RT_TRUE;
    }

    return (simu_sensor_random_unit_r(rnd) < probability) ? RT_TRUE : RT_FALSE;
}

int simu_sensor_random_int(int min, int max)
{
    return simu_sensor_random_int_r(RT_NULL, min, max);
}

int simu_sensor_random_int_r(SimuSensorRandom *rnd, int min, int max)
{
    rt_uint32_t umin;
    rt_uint32_t span;

    if (min > max)
    {
        int temp = min;

        min = max;
        max = temp;
    }

    /* 用补码无符号减法计算区间宽度, 避免 max - min 溢出 */
    umin = (rt_uint32_t)min;
    span = (rt_uint32_t)max - umin + 1U;

    return (int)(umin + random_bounded(rnd, span));
}

rt_uint32_t simu_sensor_random_uint(rt_uint32_t min, rt_uint32_t max)
{
    return simu_sensor_random_uint_r(RT_NULL, min, max);
}

rt_uint32_t simu_sensor_random_uint_r(SimuSensorRandom *rnd, rt_uint32_t min, rt_uint32_t max)
{
    if (min > max)
    {
        rt_uint32_t temp = min;

        min = max;
        max = temp;
    }

    /* max - min + 1 回绕为 0 表示覆盖整个 32bit 区间 */
    return min + random_bounded(rnd, max - min + 1U);
}

float simu_sensor_random_float(float min, float max)
{
    return simu_sensor_random_float_r(RT_NULL, min, max);
}

float simu_sensor_random_float_r(SimuSensorRandom *rnd, float min, float max)
{
    if (min > max)
    {
        float temp = min;

        min = max;
        max = temp;
    }

    if (min == max)
    {
        return min;
    }

    return min + (max - min) * simu_sensor_random_unit_r(rnd);
}

float simu_sensor_random_normal(float mean, float sigma)
{
    return simu_sensor_random_normal_r(RT_NULL, mean, sigma);
}

float simu_sensor_random_normal_r(SimuSensorRandom *rnd, float mean, float sigma)
{
    if (sigma <= 0.0f)
    {
        return mean;
    }

    return mean + random_normal_raw(rnd) * sigma;
}

float simu_sensor_random_exponential(float lambda)
{
    return simu_sensor_random_exponential_r(RT_NULL, lambda);
}

float simu_sensor_random_exponential_r(SimuSensorRandom *rnd, float lambda)
{
    float unit;

    if (lambda <= 0.0f)
    {
        return 0.0f;
    }

    /* 避免 log(0) */
    do
    {
        unit = simu_sensor_random_unit_r(rnd);
    } while (unit <= 0.0f);

    return -random_log(unit) / lambda;
}

void simu_sensor_random_dist_init(SimuSensorRandomDist *dist)
{
    if (dist == RT_NULL)
    {
        return;
    }

    rt_memset(dist, 0, sizeof(SimuSensorRandomDist));

    /* 默认配置: [0.0, 1.0) 均匀(随机)分布 */
    dist->type = SIMU_SENSOR_RANDOM_DIST_UNIFORM;
    dist->min  = 0.0f;
    dist->max  = 1.0f;
}

rt_err_t simu_sensor_random_dist_set_uniform(SimuSensorRandomDist *dist, float min, float max)
{
    if ((dist == RT_NULL) || (min > max))
    {
        return -RT_EINVAL;
    }

    simu_sensor_random_dist_init(dist);

    dist->type = SIMU_SENSOR_RANDOM_DIST_UNIFORM;
    dist->min  = min;
    dist->max  = max;

    return RT_EOK;
}

rt_err_t simu_sensor_random_dist_set_normal(SimuSensorRandomDist *dist, float mean, float sigma)
{
    if ((dist == RT_NULL) || (sigma <= 0.0f))
    {
        return -RT_EINVAL;
    }

    simu_sensor_random_dist_init(dist);

    dist->type  = SIMU_SENSOR_RANDOM_DIST_NORMAL;
    dist->mean  = mean;
    dist->sigma = sigma;

    return RT_EOK;
}

rt_err_t simu_sensor_random_dist_set_exponential(SimuSensorRandomDist *dist, float lambda)
{
    if ((dist == RT_NULL) || (lambda <= 0.0f))
    {
        return -RT_EINVAL;
    }

    simu_sensor_random_dist_init(dist);

    dist->type   = SIMU_SENSOR_RANDOM_DIST_EXPONENTIAL;
    dist->lambda = lambda;

    return RT_EOK;
}

float simu_sensor_random_sample(const SimuSensorRandomDist *dist)
{
    return simu_sensor_random_sample_r(RT_NULL, dist);
}

float simu_sensor_random_sample_r(SimuSensorRandom *rnd, const SimuSensorRandomDist *dist)
{
    if (dist == RT_NULL)
    {
        return 0.0f;
    }

    switch (dist->type)
    {
    case SIMU_SENSOR_RANDOM_DIST_NORMAL:
        return simu_sensor_random_normal_r(rnd, dist->mean, dist->sigma);

    case SIMU_SENSOR_RANDOM_DIST_EXPONENTIAL:
        /* min 作为基线偏移, 便于模拟带有固有延时/偏置的物理量 */
        return dist->min + simu_sensor_random_exponential_r(rnd, dist->lambda);

    case SIMU_SENSOR_RANDOM_DIST_UNIFORM:
    default:
        return simu_sensor_random_float_r(rnd, dist->min, dist->max);
    }
}

int simu_sensor_random_sample_int(const SimuSensorRandomDist *dist)
{
    return simu_sensor_random_sample_int_r(RT_NULL, dist);
}

int simu_sensor_random_sample_int_r(SimuSensorRandom *rnd, const SimuSensorRandomDist *dist)
{
    if (dist == RT_NULL)
    {
        return 0;
    }

    /* 均匀分布直接按闭区间取整数, 避免四舍五入导致端点概率减半 */
    if (dist->type == SIMU_SENSOR_RANDOM_DIST_UNIFORM)
    {
        return simu_sensor_random_int_r(rnd, random_round(dist->min), random_round(dist->max));
    }

    return random_round(simu_sensor_random_sample_r(rnd, dist));
}
