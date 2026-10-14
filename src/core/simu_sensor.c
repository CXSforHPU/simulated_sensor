/*
 * Copyright (c) 2026, CXSforHPU
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     CXSforHPU    the first version, simulated sensor objects
 */

#include <rtthread.h>

#include "simu_sensor.h"

/* 仿真一天的长度(ms) */
#define SIMU_SENSOR_DAY_MS          86400000U
/* 仿真时钟的起始时刻: 08:00 */
#define SIMU_SENSOR_START_MS        (8U * 3600U * 1000U)
/* 圆周率 */
#define SIMU_SENSOR_PI              3.14159265f
/* 漂移回中系数: 每次采样漂移量保留的比例 */
#define SIMU_SENSOR_DRIFT_RECENTER  0.98f
/* 默认采样周期(ms) */
#define SIMU_SENSOR_DEFAULT_PERIOD  1000U
/* 超量程故障输出的越限比例 */
#define SIMU_SENSOR_OOR_RATIO       0.1f

/**
 * @brief 传感器类型预设
 */
typedef struct SimuSensorPreset
{
    const char       *type_name;                        /**< 类型名称 */
    rt_uint8_t        channel_count;                    /**< 通道数量 */
    SimuSensorChannel channel[SIMU_SENSOR_CHANNEL_MAX]; /**< 默认通道配置 */
} SimuSensorPreset;

/**
 * @brief 通道默认参数(宏形式便于在多个类型间复用)
 */
#define SIMU_PRESET_CHANNEL_TEMPERATURE                                     \
    {                                                                      \
        .name = "temperature", .unit = "C",                                 \
        .min = -40.0f, .max = 85.0f, .base = 25.0f,                         \
        .noise = 0.12f, .resolution = 0.01f,                                \
        .drift_step = 0.01f, .drift_limit = 0.5f,                           \
        .diurnal = 0.02f, .ambient_gain = 0.6f,                             \
        .spike_prob = 0.0f, .spike_gain = 0.0f                              \
    }

#define SIMU_PRESET_CHANNEL_HUMIDITY                                        \
    {                                                                      \
        .name = "humidity", .unit = "%RH",                                  \
        .min = 0.0f, .max = 100.0f, .base = 55.0f,                          \
        .noise = 0.4f, .resolution = 0.1f,                                  \
        .drift_step = 0.05f, .drift_limit = 2.0f,                           \
        .diurnal = 0.03f, .ambient_gain = -1.2f,                            \
        .spike_prob = 0.0f, .spike_gain = 0.0f                              \
    }

#define SIMU_PRESET_CHANNEL_GAS                                             \
    {                                                                      \
        .name = "concentration", .unit = "ppm",                             \
        .min = 0.0f, .max = 5000.0f, .base = 400.0f,                        \
        .noise = 10.0f, .resolution = 1.0f,                                 \
        .drift_step = 1.5f, .drift_limit = 50.0f,                           \
        .diurnal = 0.0f, .ambient_gain = 0.0f,                              \
        .spike_prob = 0.01f, .spike_gain = 1500.0f                          \
    }

#define SIMU_PRESET_CHANNEL_ILLUMINANCE                                     \
    {                                                                      \
        .name = "illuminance", .unit = "lux",                               \
        .min = 0.0f, .max = 20000.0f, .base = 800.0f,                       \
        .noise = 20.0f, .resolution = 1.0f,                                 \
        .drift_step = 8.0f, .drift_limit = 150.0f,                          \
        .diurnal = 0.95f, .ambient_gain = 0.0f,                             \
        .spike_prob = 0.0f, .spike_gain = 0.0f                              \
    }

#define SIMU_PRESET_CHANNEL_PRESSURE                                        \
    {                                                                      \
        .name = "pressure", .unit = "hPa",                                  \
        .min = 300.0f, .max = 1100.0f, .base = 1013.25f,                    \
        .noise = 0.05f, .resolution = 0.01f,                                \
        .drift_step = 0.02f, .drift_limit = 3.0f,                           \
        .diurnal = 0.0f, .ambient_gain = 0.0f,                              \
        .spike_prob = 0.0f, .spike_gain = 0.0f                              \
    }

#define SIMU_PRESET_CHANNEL_NOISE                                           \
    {                                                                      \
        .name = "noise", .unit = "dB",                                      \
        .min = 30.0f, .max = 120.0f, .base = 45.0f,                         \
        .noise = 1.5f, .resolution = 0.1f,                                  \
        .drift_step = 0.4f, .drift_limit = 8.0f,                            \
        .diurnal = 0.05f, .ambient_gain = 0.0f,                             \
        .spike_prob = 0.02f, .spike_gain = 25.0f                            \
    }

#define SIMU_PRESET_CHANNEL_CUSTOM                                          \
    {                                                                      \
        .name = "value", .unit = "",                                        \
        .min = -1000.0f, .max = 1000.0f, .base = 0.0f,                      \
        .noise = 0.5f, .resolution = 0.01f,                                 \
        .drift_step = 0.0f, .drift_limit = 0.0f,                            \
        .diurnal = 0.0f, .ambient_gain = 0.0f,                              \
        .spike_prob = 0.0f, .spike_gain = 0.0f                              \
    }

/**
 * @brief 各类型的默认物理参数
 */
static const SimuSensorPreset sensor_presets[SIMU_SENSOR_TYPE_MAX] =
{
    /* SIMU_SENSOR_TYPE_TEMPERATURE: 温度计 */
    {
        .type_name     = "temperature",
        .channel_count = 1,
        .channel       = { SIMU_PRESET_CHANNEL_TEMPERATURE }
    },
    /* SIMU_SENSOR_TYPE_HUMIDITY: 湿度计 */
    {
        .type_name     = "humidity",
        .channel_count = 1,
        .channel       = { SIMU_PRESET_CHANNEL_HUMIDITY }
    },
    /* SIMU_SENSOR_TYPE_TEMP_HUMI: 温湿度一体(如 DHT11/DHT22/SHT3x) */
    {
        .type_name     = "temp_humi",
        .channel_count = 2,
        .channel       =
        {
            SIMU_PRESET_CHANNEL_TEMPERATURE,
            SIMU_PRESET_CHANNEL_HUMIDITY
        }
    },
    /* SIMU_SENSOR_TYPE_GAS: 气体浓度(如 CO2、可燃气体, 带随机超限脉冲) */
    {
        .type_name     = "gas",
        .channel_count = 1,
        .channel       = { SIMU_PRESET_CHANNEL_GAS }
    },
    /* SIMU_SENSOR_TYPE_ILLUMINANCE: 光照强度(随昼夜变化) */
    {
        .type_name     = "illuminance",
        .channel_count = 1,
        .channel       = { SIMU_PRESET_CHANNEL_ILLUMINANCE }
    },
    /* SIMU_SENSOR_TYPE_PRESSURE: 大气压 */
    {
        .type_name     = "pressure",
        .channel_count = 1,
        .channel       = { SIMU_PRESET_CHANNEL_PRESSURE }
    },
    /* SIMU_SENSOR_TYPE_NOISE: 噪声 */
    {
        .type_name     = "noise",
        .channel_count = 1,
        .channel       = { SIMU_PRESET_CHANNEL_NOISE }
    },
    /* SIMU_SENSOR_TYPE_CUSTOM: 自定义 */
    {
        .type_name     = "custom",
        .channel_count = 1,
        .channel       = { SIMU_PRESET_CHANNEL_CUSTOM }
    }
};

/* 注册表链表头 */
static SimuSensor *g_sensor_list = RT_NULL;

/* 模块互斥锁(在组件初始化阶段创建, 用于保护注册表与采样状态) */
static struct rt_mutex g_sensor_mutex_object;
static rt_mutex_t       g_sensor_mutex = RT_NULL;

/* 仿真时间 */
static rt_uint32_t g_time_ms    = SIMU_SENSOR_START_MS;
static rt_tick_t   g_time_tick  = 0;
static float       g_time_scale = 1.0f;

/* 环境温度模型 */
static float g_ambient_base  = 26.0f;
static float g_ambient_swing = 6.0f;

/* -------------------------------------------------------------------------
 * 内部工具
 * ------------------------------------------------------------------------- */

/**
 * @brief 加锁(组件初始化之前或中断中使用时退化为无锁, 见 README 说明)
 */
static void simu_sensor_lock(void)
{
    if (g_sensor_mutex != RT_NULL)
    {
        rt_mutex_take(g_sensor_mutex, RT_WAITING_FOREVER);
    }
}

/**
 * @brief 解锁
 */
static void simu_sensor_unlock(void)
{
    if (g_sensor_mutex != RT_NULL)
    {
        rt_mutex_release(g_sensor_mutex);
    }
}

/**
 * @brief 正弦函数, 用于昼夜变化(不依赖 libm)
 */
static float simu_sensor_sin(float value)
{
    float   square;
    float   result;
    rt_int8_t sign = 1;

    if (value < 0.0f)
    {
        value = -value;
        sign  = -1;
    }

    /* 归约到 [0, π/2] */
    while (value > 2.0f * SIMU_SENSOR_PI)
    {
        value -= 2.0f * SIMU_SENSOR_PI;
    }

    if (value > SIMU_SENSOR_PI)
    {
        value -= SIMU_SENSOR_PI;
        sign   = -sign;
    }

    if (value > 0.5f * SIMU_SENSOR_PI)
    {
        value = SIMU_SENSOR_PI - value;
    }

    /* sin(x) = x - x^3/6 + x^5/120 - x^7/5040, |x| <= π/2 */
    square = value * value;
    result = value * (1.0f - square / 6.0f * (1.0f - square / 20.0f * (1.0f - square / 42.0f)));

    return (sign > 0) ? result : -result;
}

/**
 * @brief 更新并返回累计仿真时间(调用者需已加锁)
 */
static rt_uint32_t simu_sensor_time_locked(void)
{
    rt_tick_t   now        = rt_tick_get();
    rt_uint32_t elapsed_ms = (rt_uint32_t)(now - g_time_tick) * 1000U / RT_TICK_PER_SECOND;

    if (elapsed_ms != 0U)
    {
        g_time_tick = now;
        g_time_ms  += (rt_uint32_t)((float)elapsed_ms * g_time_scale);
    }

    return g_time_ms;
}

/**
 * @brief 昼夜因子: 午夜为 -1, 正午为 1(调用者需已加锁)
 */
static float simu_sensor_diurnal_locked(void)
{
    float day_fraction = (float)(g_time_ms % SIMU_SENSOR_DAY_MS) / (float)SIMU_SENSOR_DAY_MS;

    return simu_sensor_sin(2.0f * SIMU_SENSOR_PI * (day_fraction - 0.25f));
}

/**
 * @brief 环境温度(调用者需已加锁)
 */
static float simu_sensor_ambient_locked(float diurnal)
{
    return g_ambient_base + g_ambient_swing * diurnal;
}

/**
 * @brief 按分辨率量化
 */
static float simu_sensor_quantize(float value, float resolution)
{
    float scaled;

    if (resolution <= 0.0f)
    {
        return value;
    }

    scaled = value / resolution;

    if (scaled > 2147483000.0f)
    {
        return value;
    }

    if (scaled < -2147483000.0f)
    {
        return value;
    }

    scaled = (scaled >= 0.0f) ? (float)(rt_int32_t)(scaled + 0.5f)
                              : (float)(rt_int32_t)(scaled - 0.5f);

    return scaled * resolution;
}

/**
 * @brief 名称散列, 用于为每个对象生成互不相同的默认种子
 */
static rt_uint32_t simu_sensor_name_hash(const char *name)
{
    rt_uint32_t hash = 2166136261U;

    while (*name != '\0')
    {
        hash ^= (rt_uint32_t)(rt_uint8_t)(*name);
        hash *= 16777619U;
        name++;
    }

    return hash;
}

/**
 * @brief 注册到链表尾部
 */
static void simu_sensor_register_locked(SimuSensor *sensor)
{
    SimuSensor **link = &g_sensor_list;

    while (*link != RT_NULL)
    {
        link = &(*link)->next;
    }

    *link       = sensor;
    sensor->next = RT_NULL;
}

/**
 * @brief 名称是否已被注册
 */
static rt_bool_t simu_sensor_name_used_locked(const char *name)
{
    SimuSensor *sensor;

    for (sensor = g_sensor_list; sensor != RT_NULL; sensor = sensor->next)
    {
        if (rt_strcmp(sensor->name, name) == 0)
        {
            return RT_TRUE;
        }
    }

    return RT_FALSE;
}

/**
 * @brief 数据质量
 */
static rt_uint8_t simu_sensor_quality_of(const SimuSensor *sensor)
{
    switch (sensor->fault)
    {
    case SIMU_SENSOR_FAULT_FREEZE:
        return SIMU_SENSOR_QUALITY_FROZEN;

    case SIMU_SENSOR_FAULT_OUT_OF_RANGE:
        return SIMU_SENSOR_QUALITY_OUT_OF_RANGE;

    default:
        return SIMU_SENSOR_QUALITY_GOOD;
    }
}

/**
 * @brief 生成一个通道的采样值(调用者需已加锁)
 */
static float simu_sensor_channel_value_locked(SimuSensor *sensor, rt_uint8_t index, float diurnal, float ambient)
{
    const SimuSensorChannel *channel = &sensor->channel[index];
    SimuSensorRandom        *rnd     = &sensor->priv.rnd;
    float                    value   = channel->base;

    /* 昼夜变化 */
    if (channel->diurnal != 0.0f)
    {
        value += channel->base * channel->diurnal * diurnal;
    }

    /* 与环境温度耦合: 例如湿度随温度升高而下降 */
    if (channel->ambient_gain != 0.0f)
    {
        value += channel->ambient_gain * (ambient - SIMU_SENSOR_AMBIENT_REF);
    }

    /* 缓慢漂移: 随机游走并缓慢回中 */
    if ((channel->drift_step > 0.0f) && (channel->drift_limit > 0.0f))
    {
        float drift = sensor->priv.drift[index];

        drift += simu_sensor_random_normal_r(rnd, 0.0f, channel->drift_step);
        drift *= SIMU_SENSOR_DRIFT_RECENTER;

        if (drift > channel->drift_limit)
        {
            drift = channel->drift_limit;
        }
        else if (drift < -channel->drift_limit)
        {
            drift = -channel->drift_limit;
        }

        sensor->priv.drift[index] = drift;
        value += drift;
    }

    /* 瞬态噪声 */
    if (channel->noise > 0.0f)
    {
        value += simu_sensor_random_normal_r(rnd, 0.0f, channel->noise);
    }

    /* 异常脉冲(气体泄漏、噪声尖峰等) */
    if ((channel->spike_prob > 0.0f) && (channel->spike_gain > 0.0f) &&
        (simu_sensor_random_unit_r(rnd) < channel->spike_prob))
    {
        value += channel->spike_gain * (0.5f + simu_sensor_random_unit_r(rnd));
    }

    value = simu_sensor_quantize(value, channel->resolution);

    if (value < channel->min)
    {
        value = channel->min;
    }
    else if (value > channel->max)
    {
        value = channel->max;
    }

    return value;
}

/**
 * @brief 重新采样(调用者需已加锁)
 */
static void simu_sensor_update_locked(SimuSensor *sensor)
{
    rt_uint32_t stamp   = simu_sensor_time_locked();
    float       diurnal = simu_sensor_diurnal_locked();
    float       ambient = simu_sensor_ambient_locked(diurnal);
    rt_uint8_t  index;

    for (index = 0; index < sensor->channel_count; index++)
    {
        const SimuSensorChannel *channel = &sensor->channel[index];
        float                    value;

        switch (sensor->fault)
        {
        case SIMU_SENSOR_FAULT_FREEZE:
            /* 数据冻结: 保持上一次的数值 */
            value = sensor->priv.cache[index];
            break;

        case SIMU_SENSOR_FAULT_OUT_OF_RANGE:
            /* 超量程: 输出量程上限的 110% */
            value = channel->max + (channel->max - channel->min) * SIMU_SENSOR_OOR_RATIO;
            break;

        default:
            value = simu_sensor_channel_value_locked(sensor, index, diurnal, ambient);
            break;
        }

        sensor->priv.cache[index] = value;
    }

    sensor->priv.stamp   = stamp;
    sensor->priv.sampled = RT_TRUE;
}

/**
 * @brief 填充读取结果
 */
static void simu_sensor_data_fill(const SimuSensor *sensor, SimuSensorData *data)
{
    rt_uint8_t index;

    data->name          = sensor->name;
    data->type          = sensor->type;
    data->channel_count = sensor->channel_count;
    data->quality       = simu_sensor_quality_of(sensor);
    data->timestamp     = sensor->priv.stamp;

    for (index = 0; index < sensor->channel_count; index++)
    {
        data->value[index] = sensor->priv.cache[index];
    }
}

/**
 * @brief 读取(调用者需已加锁)
 */
static rt_err_t simu_sensor_read_locked(SimuSensor *sensor, SimuSensorData *data, rt_bool_t force)
{
    if (sensor->priv.inited != RT_TRUE)
    {
        return -RT_EINVAL;
    }

    if (sensor->fault == SIMU_SENSOR_FAULT_OFFLINE)
    {
        /* 离线故障: 模拟通讯失败 */
        return -RT_ERROR;
    }

    if ((force == RT_TRUE) || (sensor->priv.sampled != RT_TRUE) || (sensor->period == 0U) ||
        ((rt_uint32_t)(simu_sensor_time_locked() - sensor->priv.stamp) >= sensor->period))
    {
        simu_sensor_update_locked(sensor);
    }

    simu_sensor_data_fill(sensor, data);

    return RT_EOK;
}

/* -------------------------------------------------------------------------
 * 对象生命周期
 * ------------------------------------------------------------------------- */

rt_err_t simu_sensor_init(SimuSensor *sensor, const char *name, SimuSensorType type)
{
    const SimuSensorPreset *preset;
    rt_uint8_t              index;

    if ((sensor == RT_NULL) || (name == RT_NULL) || (name[0] == '\0') ||
        (rt_strlen(name) >= SIMU_SENSOR_NAME_MAX) || (type >= SIMU_SENSOR_TYPE_MAX))
    {
        return -RT_EINVAL;
    }

    preset = &sensor_presets[type];

    /* 重复初始化同一个对象时, 先把它从注册表里注销 */
    if (sensor->priv.inited == RT_TRUE)
    {
        simu_sensor_deinit(sensor);
    }

    rt_memset(sensor, 0, sizeof(SimuSensor));

    rt_strncpy(sensor->name, name, SIMU_SENSOR_NAME_MAX - 1);
    sensor->name[SIMU_SENSOR_NAME_MAX - 1] = '\0';
    sensor->type                           = type;
    sensor->channel_count                  = preset->channel_count;
    sensor->fault                          = SIMU_SENSOR_FAULT_NONE;
    sensor->period                         = SIMU_SENSOR_DEFAULT_PERIOD;

    for (index = 0; index < preset->channel_count; index++)
    {
        sensor->channel[index]    = preset->channel[index];
        sensor->priv.cache[index] = preset->channel[index].base;
    }

    /* 每个对象一条独立序列: 以 tick、对象地址与名称散列生成默认种子 */
    sensor->seed = ((rt_uint32_t)rt_tick_get() * 0x9E3779B9U) ^
                   ((rt_uint32_t)(rt_uintptr_t)sensor) ^
                   simu_sensor_name_hash(name);

    simu_sensor_random_init(&sensor->priv.rnd, sensor->seed);
    sensor->priv.sampled = RT_FALSE;
    sensor->priv.stamp   = 0U;
    sensor->priv.inited  = RT_TRUE;

    simu_sensor_lock();

    if (simu_sensor_name_used_locked(name) == RT_TRUE)
    {
        sensor->priv.inited = RT_FALSE;
        simu_sensor_unlock();

        return -RT_EINVAL;
    }

    simu_sensor_register_locked(sensor);
    simu_sensor_unlock();

    return RT_EOK;
}

void simu_sensor_deinit(SimuSensor *sensor)
{
    SimuSensor **link;

    if (sensor == RT_NULL)
    {
        return;
    }

    simu_sensor_lock();

    link = &g_sensor_list;

    while (*link != RT_NULL)
    {
        if (*link == sensor)
        {
            *link = sensor->next;
            break;
        }

        link = &(*link)->next;
    }

    sensor->priv.inited = RT_FALSE;
    sensor->next        = RT_NULL;

    simu_sensor_unlock();
}

SimuSensor *simu_sensor_create(const char *name, SimuSensorType type)
{
    SimuSensor *sensor;

    sensor = (SimuSensor *)rt_malloc(sizeof(SimuSensor));

    if (sensor == RT_NULL)
    {
        return RT_NULL;
    }

    rt_memset(sensor, 0, sizeof(SimuSensor));

    if (simu_sensor_init(sensor, name, type) != RT_EOK)
    {
        rt_free(sensor);

        return RT_NULL;
    }

    simu_sensor_lock();
    sensor->priv.dynamic = RT_TRUE;
    simu_sensor_unlock();

    return sensor;
}

SimuSensor *simu_sensor_create_default(const char *name, SimuSensorType type)
{
    return simu_sensor_create(name, type);
}

void simu_sensor_destroy(SimuSensor *sensor)
{
    rt_bool_t dynamic;

    if (sensor == RT_NULL)
    {
        return;
    }

    simu_sensor_lock();
    dynamic = sensor->priv.dynamic;
    simu_sensor_unlock();

    simu_sensor_deinit(sensor);

    if (dynamic == RT_TRUE)
    {
        rt_free(sensor);
    }
}

rt_err_t simu_sensor_reset(SimuSensor *sensor)
{
    rt_uint8_t index;

    if (sensor == RT_NULL)
    {
        return -RT_EINVAL;
    }

    simu_sensor_lock();

    if (sensor->priv.inited != RT_TRUE)
    {
        simu_sensor_unlock();

        return -RT_EINVAL;
    }

    for (index = 0; index < sensor->channel_count; index++)
    {
        sensor->priv.drift[index] = 0.0f;
        sensor->priv.cache[index] = sensor->channel[index].base;
    }

    simu_sensor_random_init(&sensor->priv.rnd, sensor->seed);
    sensor->priv.sampled = RT_FALSE;
    sensor->priv.stamp   = 0U;

    simu_sensor_unlock();

    return RT_EOK;
}

rt_err_t simu_sensor_set_period(SimuSensor *sensor, rt_uint32_t period)
{
    if (sensor == RT_NULL)
    {
        return -RT_EINVAL;
    }

    simu_sensor_lock();
    sensor->period = period;
    simu_sensor_unlock();

    return RT_EOK;
}

rt_err_t simu_sensor_set_seed(SimuSensor *sensor, rt_uint32_t seed)
{
    if (sensor == RT_NULL)
    {
        return -RT_EINVAL;
    }

    simu_sensor_lock();
    sensor->seed = seed;
    simu_sensor_random_init(&sensor->priv.rnd, seed);
    sensor->priv.sampled = RT_FALSE;
    simu_sensor_unlock();

    return RT_EOK;
}

rt_err_t simu_sensor_set_fault(SimuSensor *sensor, SimuSensorFault fault)
{
    if ((sensor == RT_NULL) || (fault > SIMU_SENSOR_FAULT_OFFLINE))
    {
        return -RT_EINVAL;
    }

    simu_sensor_lock();
    sensor->fault = fault;
    simu_sensor_unlock();

    return RT_EOK;
}

rt_err_t simu_sensor_set_channel(SimuSensor *sensor, rt_uint8_t index, const SimuSensorChannel *channel)
{
    if ((sensor == RT_NULL) || (channel == RT_NULL) || (index >= sensor->channel_count))
    {
        return -RT_EINVAL;
    }

    if (channel->min >= channel->max)
    {
        return -RT_EINVAL;
    }

    simu_sensor_lock();
    sensor->channel[index]    = *channel;
    sensor->priv.drift[index] = 0.0f;
    sensor->priv.cache[index] = channel->base;
    simu_sensor_unlock();

    return RT_EOK;
}

const SimuSensorChannel *simu_sensor_get_channel(const SimuSensor *sensor, rt_uint8_t index)
{
    if ((sensor == RT_NULL) || (index >= sensor->channel_count))
    {
        return RT_NULL;
    }

    return &sensor->channel[index];
}

/* -------------------------------------------------------------------------
 * 注册表
 * ------------------------------------------------------------------------- */

SimuSensor *simu_sensor_find(const char *name)
{
    SimuSensor *sensor;

    if (name == RT_NULL)
    {
        return RT_NULL;
    }

    simu_sensor_lock();

    for (sensor = g_sensor_list; sensor != RT_NULL; sensor = sensor->next)
    {
        if (rt_strcmp(sensor->name, name) == 0)
        {
            break;
        }
    }

    simu_sensor_unlock();

    return sensor;
}

rt_uint32_t simu_sensor_count(void)
{
    rt_uint32_t count  = 0;
    SimuSensor *sensor;

    simu_sensor_lock();

    for (sensor = g_sensor_list; sensor != RT_NULL; sensor = sensor->next)
    {
        count++;
    }

    simu_sensor_unlock();

    return count;
}

SimuSensor *simu_sensor_get(rt_uint32_t index)
{
    rt_uint32_t position = 0;
    SimuSensor *sensor;

    simu_sensor_lock();

    for (sensor = g_sensor_list; sensor != RT_NULL; sensor = sensor->next)
    {
        if (position == index)
        {
            break;
        }

        position++;
    }

    simu_sensor_unlock();

    return sensor;
}

/* -------------------------------------------------------------------------
 * 数据读取
 * ------------------------------------------------------------------------- */

rt_err_t simu_sensor_read(SimuSensor *sensor, SimuSensorData *data)
{
    rt_err_t result;

    if ((sensor == RT_NULL) || (data == RT_NULL))
    {
        return -RT_EINVAL;
    }

    simu_sensor_lock();
    result = simu_sensor_read_locked(sensor, data, RT_FALSE);
    simu_sensor_unlock();

    return result;
}

rt_err_t simu_sensor_sample(SimuSensor *sensor, SimuSensorData *data)
{
    rt_err_t result;

    if ((sensor == RT_NULL) || (data == RT_NULL))
    {
        return -RT_EINVAL;
    }

    simu_sensor_lock();
    result = simu_sensor_read_locked(sensor, data, RT_TRUE);
    simu_sensor_unlock();

    return result;
}

rt_err_t simu_sensor_read_by_name(const char *name, SimuSensorData *data)
{
    SimuSensor *sensor;

    if ((name == RT_NULL) || (data == RT_NULL))
    {
        return -RT_EINVAL;
    }

    sensor = simu_sensor_find(name);

    if (sensor == RT_NULL)
    {
        return -RT_EINVAL;
    }

    return simu_sensor_read(sensor, data);
}

rt_uint32_t simu_sensor_read_all(SimuSensorData *data, rt_uint32_t max_count)
{
    rt_uint32_t count = 0;
    SimuSensor *sensor;

    if ((data == RT_NULL) || (max_count == 0U))
    {
        return 0;
    }

    simu_sensor_lock();

    for (sensor = g_sensor_list; (sensor != RT_NULL) && (count < max_count); sensor = sensor->next)
    {
        /* 离线的传感器会被跳过 */
        if (simu_sensor_read_locked(sensor, &data[count], RT_FALSE) == RT_EOK)
        {
            count++;
        }
    }

    simu_sensor_unlock();

    return count;
}

float simu_sensor_value(const SimuSensor *sensor, rt_uint8_t index)
{
    if ((sensor == RT_NULL) || (index >= sensor->channel_count))
    {
        return 0.0f;
    }

    return sensor->priv.cache[index];
}

/* -------------------------------------------------------------------------
 * 环境与时间模型
 * ------------------------------------------------------------------------- */

float simu_sensor_ambient_temperature(void)
{
    float ambient;

    simu_sensor_lock();
    simu_sensor_time_locked();
    ambient = simu_sensor_ambient_locked(simu_sensor_diurnal_locked());
    simu_sensor_unlock();

    return ambient;
}

void simu_sensor_set_ambient(float base, float swing)
{
    simu_sensor_lock();
    g_ambient_base  = base;
    g_ambient_swing = swing;
    simu_sensor_unlock();
}

rt_uint32_t simu_sensor_time_ms(void)
{
    rt_uint32_t time_ms;

    simu_sensor_lock();
    time_ms = simu_sensor_time_locked();
    simu_sensor_unlock();

    return time_ms;
}

void simu_sensor_set_time_scale(float scale)
{
    if (scale <= 0.0f)
    {
        scale = 1.0f;
    }

    simu_sensor_lock();
    simu_sensor_time_locked();
    g_time_scale = scale;
    simu_sensor_unlock();
}

float simu_sensor_time_of_day(void)
{
    float hour;

    simu_sensor_lock();
    hour = (float)(simu_sensor_time_locked() % SIMU_SENSOR_DAY_MS) / 3600000.0f;
    simu_sensor_unlock();

    return hour;
}

void simu_sensor_set_time_of_day(float hour)
{
    rt_uint32_t day_ms;

    if (hour < 0.0f)
    {
        hour = 0.0f;
    }
    else if (hour >= 24.0f)
    {
        hour = 23.9999f;
    }

    day_ms = (rt_uint32_t)(hour * 3600000.0f);

    if (day_ms >= SIMU_SENSOR_DAY_MS)
    {
        day_ms = 0U;
    }

    simu_sensor_lock();
    simu_sensor_time_locked();
    g_time_ms = (g_time_ms / SIMU_SENSOR_DAY_MS) * SIMU_SENSOR_DAY_MS + day_ms;
    simu_sensor_unlock();
}

float simu_sensor_diurnal_factor(void)
{
    float factor;

    simu_sensor_lock();
    simu_sensor_time_locked();
    factor = simu_sensor_diurnal_locked();
    simu_sensor_unlock();

    return factor;
}

/* -------------------------------------------------------------------------
 * 元信息
 * ------------------------------------------------------------------------- */

const char *simu_sensor_type_name(SimuSensorType type)
{
    if (type >= SIMU_SENSOR_TYPE_MAX)
    {
        return "unknown";
    }

    return sensor_presets[type].type_name;
}

const char *simu_sensor_fault_name(SimuSensorFault fault)
{
    switch (fault)
    {
    case SIMU_SENSOR_FAULT_NONE:
        return "none";

    case SIMU_SENSOR_FAULT_FREEZE:
        return "freeze";

    case SIMU_SENSOR_FAULT_OUT_OF_RANGE:
        return "out_of_range";

    case SIMU_SENSOR_FAULT_OFFLINE:
        return "offline";

    default:
        return "unknown";
    }
}

const char *simu_sensor_quality_name(rt_uint8_t quality)
{
    switch (quality)
    {
    case SIMU_SENSOR_QUALITY_GOOD:
        return "good";

    case SIMU_SENSOR_QUALITY_FROZEN:
        return "frozen";

    case SIMU_SENSOR_QUALITY_OUT_OF_RANGE:
        return "out_of_range";

    default:
        return "unknown";
    }
}

rt_size_t simu_sensor_value_to_string(char *buffer, rt_size_t size, float value, rt_uint8_t decimals)
{
    static const rt_int32_t scales[4] = { 1, 10, 100, 1000 };
    rt_int32_t scale;
    rt_int32_t scaled;
    rt_int32_t integral;
    rt_int32_t fraction;
    rt_size_t  length;

    if ((buffer == RT_NULL) || (size == 0U))
    {
        return 0;
    }

    if (decimals > 3U)
    {
        decimals = 3U;
    }

    scale    = scales[decimals];
    scaled   = (rt_int32_t)(value * (float)scale + ((value >= 0.0f) ? 0.5f : -0.5f));
    integral = scaled / scale;
    fraction = scaled % scale;

    if (fraction < 0)
    {
        fraction = -fraction;
    }

    if (decimals == 0U)
    {
        length = (rt_size_t)rt_snprintf(buffer, size, "%d", (int)integral);
    }
    else
    {
        length = (rt_size_t)rt_snprintf(buffer, size, "%d.%0*d", (int)integral, (int)decimals, (int)fraction);
    }

    /* rt_snprintf 返回理论长度, 这里按实际可写入长度返回 */
    if (length >= size)
    {
        length = size - 1U;
    }

    return length;
}

/* -------------------------------------------------------------------------
 * 模块初始化
 * ------------------------------------------------------------------------- */

/**
 * @brief 创建模块互斥锁并启动仿真时钟
 */
static int simu_sensor_module_init(void)
{
    if (rt_mutex_init(&g_sensor_mutex_object, "simu_sen", RT_IPC_FLAG_PRIO) == RT_EOK)
    {
        g_sensor_mutex = &g_sensor_mutex_object;
    }

    g_time_tick  = rt_tick_get();
    g_time_ms    = SIMU_SENSOR_START_MS;

    return RT_EOK;
}
INIT_COMPONENT_EXPORT(simu_sensor_module_init);
