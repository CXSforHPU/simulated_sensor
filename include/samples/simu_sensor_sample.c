/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     RT-Thread    the first version, simulated sensor samples
 */

/**
 * @file simu_sensor_sample.c
 * @brief 模拟传感器使用示例
 *
 * 本文件给出「初始化 -> 获取信息 -> 设置参数 -> 销毁」的完整示例, 每个示例对应一条 msh 命令,
 * 可直接在终端里逐条执行观察输出:
 *
 * @code
 * msh />simu_sensor_sample_init        # 1. 初始化: 静态/动态/自定义通道/批量创建
 * msh />simu_sensor_sample_info        # 2. 获取信息: 注册表、通道参数、采样数据、环境/时钟
 * msh />simu_sensor_sample_info gas    #    也可以只看某个传感器
 * msh />simu_sensor_sample_config      # 3. 设置参数: 周期、通道、故障注入、环境、时钟、种子
 * msh />simu_sensor_sample_destroy     # 4. 销毁: 按名称销毁 / 按前缀批量销毁 / all 销毁全部
 * msh />simu_sensor_sample_mqtt        # 5. (可选) 配置并用 paho-mqtt 上报
 * msh />simu_sensor_sample_all         # 一键依次执行以上示例
 * @endcode
 *
 * 说明: 示例中创建的传感器名称统一以 "sample_" 开头, 便于识别与清理;
 *       所有示例都可以重复执行(执行前会先清理同名对象)。
 */

#include <rtthread.h>

#include "simu_sensor.h"

#if defined(PKG_SIMULATED_SENSOR_MQTT) && defined(PKG_USING_PAHOMQTT) && defined(PKG_USING_CJSON)
#include "simu_sensor_mqtt.h"
#define SIMU_SENSOR_SAMPLE_USING_MQTT
#endif

#ifdef RT_USING_FINSH

/* 示例创建的全部传感器名称前缀 */
#define SAMPLE_NAME_PREFIX      "sample_"
/* 打印用文本缓冲长度 */
#define SAMPLE_TEXT_SIZE        24

/* -------------------------------------------------------------------------
 * 示例内部使用的小工具
 * ------------------------------------------------------------------------- */

/**
 * @brief 按分辨率决定打印的小数位数
 */
static rt_uint8_t sample_decimals(const SimuSensorChannel *channel)
{
    if ((channel == RT_NULL) || (channel->resolution >= 1.0f))
    {
        return 0;
    }

    if (channel->resolution >= 0.1f)
    {
        return 1;
    }

    if (channel->resolution >= 0.01f)
    {
        return 2;
    }

    return 3;
}

/**
 * @brief 定点打印一个数值(klibc 的精简版 printf 不支持 %f)
 */
static void sample_print_value(float value, const SimuSensorChannel *channel)
{
    char text[SAMPLE_TEXT_SIZE];

    simu_sensor_value_to_string(text, sizeof(text), value, sample_decimals(channel));

    rt_kprintf("%s", text);
}

/**
 * @brief 打印一条采样结果
 */
static void sample_print_data(const SimuSensor *sensor, const SimuSensorData *data)
{
    rt_uint8_t index;

    rt_kprintf("  %-14s 质量=%-12s 时间戳=%u ms\n", data->name,
               simu_sensor_quality_name(data->quality), (unsigned int)data->timestamp);

    for (index = 0; index < data->channel_count; index++)
    {
        const SimuSensorChannel *channel = simu_sensor_get_channel(sensor, index);

        if (channel == RT_NULL)
        {
            continue;
        }

        rt_kprintf("      %-14s = ", channel->name);
        sample_print_value(data->value[index], channel);
        rt_kprintf(" %s\n", (channel->unit != RT_NULL) ? channel->unit : "");
    }
}

/**
 * @brief 示例可重复执行: 先销毁同名传感器(静态对象只注销, 动态对象会释放内存)
 */
static void sample_cleanup(const char *name)
{
    SimuSensor *sensor = simu_sensor_find(name);

    if (sensor != RT_NULL)
    {
        /* simu_sensor_destroy() 对静态对象同样安全: 只注销, 不释放内存 */
        simu_sensor_destroy(sensor);
    }
}

/**
 * @brief 示例: 打印小节标题
 */
static void sample_title(const char *title)
{
    rt_kprintf("\n===== %s =====\n", title);
}

/* -------------------------------------------------------------------------
 * 示例 1: 初始化模拟传感器
 * ------------------------------------------------------------------------- */

/**
 * @brief 初始化示例
 *
 * 演示四种常见创建方式, 以及初始化后立即读取一次数据。对应命令:
 *
 * @code
 * msh />simu_sensor_sample_init
 * @endcode
 */
int simu_sensor_sample_init(int argc, char **argv)
{
    /* 方式一: 静态对象 —— 对象放静态存储区, 生命周期跟随程序, 用 init 注册 */
    static SimuSensor temp_humi_sensor;
    SimuSensorData    data;

    /* 方式二: 动态创建 —— 内部 rt_malloc, 用完需要 simu_sensor_destroy() 释放 */
    SimuSensor *gas_sensor;

    /* 方式三: 自定义通道 —— 先改通道参数, 再写回对象 */
    SimuSensor *adc_sensor;

    /* 方式四: 批量创建 —— 用一张表把常用传感器一次建好 */
    static const struct
    {
        const char    *name;
        SimuSensorType type;
    } batch[] =
    {
        { SAMPLE_NAME_PREFIX "light", SIMU_SENSOR_TYPE_ILLUMINANCE },
        { SAMPLE_NAME_PREFIX "press", SIMU_SENSOR_TYPE_PRESSURE  },
        { SAMPLE_NAME_PREFIX "noise", SIMU_SENSOR_TYPE_NOISE     }
    };
    rt_uint32_t index;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    sample_title("示例1: 初始化模拟传感器");

    /* 可重复执行: 先清理上次留下的同名对象 */
    sample_cleanup(SAMPLE_NAME_PREFIX "th");
    sample_cleanup(SAMPLE_NAME_PREFIX "gas");
    sample_cleanup(SAMPLE_NAME_PREFIX "adc");

    /* 1.1 静态对象 + 类型预设(温湿度一体, 2 个通道) */
    if (simu_sensor_init(&temp_humi_sensor, SAMPLE_NAME_PREFIX "th", SIMU_SENSOR_TYPE_TEMP_HUMI) != RT_EOK)
    {
        rt_kprintf("[1] 静态初始化失败(名称重复或参数非法)\n");

        return -1;
    }

    rt_kprintf("[1] 静态初始化: %s, 类型=%s, 通道数=%d\n", temp_humi_sensor.name,
               simu_sensor_type_name(temp_humi_sensor.type), (int)temp_humi_sensor.channel_count);

    /* 1.2 动态创建: create_default 等价于 create, 语义上表示"使用类型默认参数" */
    gas_sensor = simu_sensor_create_default(SAMPLE_NAME_PREFIX "gas", SIMU_SENSOR_TYPE_GAS);

    if (gas_sensor == RT_NULL)
    {
        rt_kprintf("[2] 动态创建失败(内存不足或名称重复)\n");

        return -1;
    }

    rt_kprintf("[2] 动态创建: %s, 类型=%s\n", gas_sensor->name, simu_sensor_type_name(gas_sensor->type));

    /* 1.3 自定义通道: 取默认通道配置 -> 修改 -> 写回(适合 ADC、电压等自定义量) */
    adc_sensor = simu_sensor_create(SAMPLE_NAME_PREFIX "adc", SIMU_SENSOR_TYPE_CUSTOM);

    if (adc_sensor != RT_NULL)
    {
        SimuSensorChannel channel = *simu_sensor_get_channel(adc_sensor, 0);

        channel.name       = "adc_raw";
        channel.unit       = "LSB";
        channel.min        = 0.0f;
        channel.max        = 4095.0f;
        channel.base       = 2048.0f;
        channel.noise      = 12.0f;
        channel.resolution = 1.0f;
        channel.drift_step = 0.0f;
        channel.diurnal    = 0.0f;
        channel.ambient_gain = 0.0f;
        channel.spike_prob = 0.0f;

        if (simu_sensor_set_channel(adc_sensor, 0, &channel) == RT_EOK)
        {
            rt_kprintf("[3] 自定义通道: %s, %s 量程 %d ~ %d %s\n", adc_sensor->name, channel.name,
                       (int)channel.min, (int)channel.max, channel.unit);
        }

        /* 固定种子: 相同种子 + 相同参数 => 完全相同的采样序列, 便于复现问题 */
        simu_sensor_set_seed(adc_sensor, 2026U);
        simu_sensor_reset(adc_sensor);
        rt_kprintf("    固定随机种子: 0x%08x\n", (unsigned int)adc_sensor->seed);
    }

    /* 1.4 批量创建 */
    for (index = 0; index < (sizeof(batch) / sizeof(batch[0])); index++)
    {
        sample_cleanup(batch[index].name);

        if (simu_sensor_create_default(batch[index].name, batch[index].type) != RT_NULL)
        {
            rt_kprintf("[4] 批量创建: %s (类型 %s)\n", batch[index].name,
                       simu_sensor_type_name(batch[index].type));
        }
    }

    /* 1.5 初始化完成后读一次数据确认可用 */
    if (simu_sensor_read(&temp_humi_sensor, &data) == RT_EOK)
    {
        rt_kprintf("[5] 读取 %s 一次:\n", temp_humi_sensor.name);
        sample_print_data(&temp_humi_sensor, &data);
    }

    rt_kprintf("\n当前共注册 %u 个模拟传感器, 可执行 simu_sensor_sample_info 查看详情\n",
               (unsigned int)simu_sensor_count());

    return 0;
}

/* -------------------------------------------------------------------------
 * 示例 2: 获取传感器信息
 * ------------------------------------------------------------------------- */

/**
 * @brief 打印单个传感器的配置与数据
 */
static void sample_show_sensor(SimuSensor *sensor)
{
    SimuSensorData data;
    rt_uint8_t     index;

    rt_kprintf("--- %s (类型=%s) ---\n", sensor->name, simu_sensor_type_name(sensor->type));
    rt_kprintf("  采样周期=%u ms 随机种子=0x%08x 故障模式=%s\n",
               (unsigned int)sensor->period, (unsigned int)sensor->seed,
               simu_sensor_fault_name(sensor->fault));

    /* 通道配置信息 */
    for (index = 0; index < sensor->channel_count; index++)
    {
        const SimuSensorChannel *channel = simu_sensor_get_channel(sensor, index);
        char                     text[SAMPLE_TEXT_SIZE];

        rt_kprintf("  通道%d %s(%s): 量程[", (int)index, channel->name, channel->unit);
        sample_print_value(channel->min, channel);
        rt_kprintf(", ");
        sample_print_value(channel->max, channel);
        rt_kprintf("] 基线=");
        sample_print_value(channel->base, channel);
        rt_kprintf(" 噪声σ=");
        simu_sensor_value_to_string(text, sizeof(text), channel->noise, 2);
        rt_kprintf("%s 分辨率=", text);
        simu_sensor_value_to_string(text, sizeof(text), channel->resolution, 3);
        rt_kprintf("%s\n", text);
    }

    /* 读取数据: read 按采样周期决定是否重新采样 */
    if (simu_sensor_read(sensor, &data) != RT_EOK)
    {
        rt_kprintf("  读取失败(可能注入了 offline 故障), 故障模式=%s\n",
                   simu_sensor_fault_name(sensor->fault));

        return;
    }

    sample_print_data(sensor, &data);

    /* 不触发采样, 直接取最近一次的采样值 */
    rt_kprintf("    最近一次缓存值(不触发采样): ");
    sample_print_value(simu_sensor_value(sensor, 0), simu_sensor_get_channel(sensor, 0));
    rt_kprintf("\n");
}

/**
 * @brief 获取信息示例
 *
 * 演示注册表遍历/查找、通道参数查询、数据读取、模块运行信息。对应命令:
 *
 * @code
 * msh />simu_sensor_sample_info          # 查看全部传感器
 * msh />simu_sensor_sample_info gas      # 只查看指定传感器
 * @endcode
 */
int simu_sensor_sample_info(int argc, char **argv)
{
    SimuSensorData data[SIMU_SENSOR_MAX_NUM];
    SimuSensor    *sensor;
    rt_uint32_t    count;
    rt_uint32_t    index;
    char           text[SAMPLE_TEXT_SIZE];

    sample_title("示例2: 获取传感器信息");

    /* 2.1 模块级运行信息: 仿真时钟与环境模型 */
    rt_kprintf("注册数量=%u\n", (unsigned int)simu_sensor_count());

    simu_sensor_value_to_string(text, sizeof(text), simu_sensor_time_of_day(), 2);
    rt_kprintf("仿真时间=%u ms 时刻=%s 时 ", (unsigned int)simu_sensor_time_ms(), text);

    simu_sensor_value_to_string(text, sizeof(text), simu_sensor_diurnal_factor(), 2);
    rt_kprintf("昼夜因子=%s ", text);

    simu_sensor_value_to_string(text, sizeof(text), simu_sensor_ambient_temperature(), 2);
    rt_kprintf("环境温度=%s C\n", text);

    /* 2.2 按名称查找 */
    if (argc > 1)
    {
        sensor = simu_sensor_find(argv[1]);

        if (sensor == RT_NULL)
        {
            rt_kprintf("未找到传感器 '%s'\n", argv[1]);

            return -1;
        }

        sample_show_sensor(sensor);

        return 0;
    }

    /* 2.3 按下标遍历注册表 */
    count = simu_sensor_count();

    for (index = 0; index < count; index++)
    {
        sensor = simu_sensor_get(index);

        if (sensor != RT_NULL)
        {
            sample_show_sensor(sensor);
        }
    }

    /* 2.4 一次性读取全部传感器: 上报任务里的典型用法 */
    count = simu_sensor_read_all(data, SIMU_SENSOR_MAX_NUM);

    rt_kprintf("--- read_all() 一次读取 %u 个传感器 ---\n", (unsigned int)count);

    for (index = 0; index < count; index++)
    {
        sensor = simu_sensor_find(data[index].name);

        if (sensor != RT_NULL)
        {
            sample_print_data(sensor, &data[index]);
        }
    }

    /* 2.5 按名称读取单个传感器 */
    if (simu_sensor_read_by_name(SAMPLE_NAME_PREFIX "gas", &data[0]) == RT_EOK)
    {
        rt_kprintf("--- read_by_name(\"%sgas\") ---\n", SAMPLE_NAME_PREFIX);
        sample_print_data(simu_sensor_find(SAMPLE_NAME_PREFIX "gas"), &data[0]);
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * 示例 3: 设置传感器参数
 * ------------------------------------------------------------------------- */

/**
 * @brief 设置参数示例
 *
 * 演示采样周期、通道物理参数、故障注入、环境模型、仿真时钟、随机种子的设置方法。
 * 对应命令:
 *
 * @code
 * msh />simu_sensor_sample_config          # 默认作用于 sample_th
 * msh />simu_sensor_sample_config gas      # 指定其它传感器
 * @endcode
 */
int simu_sensor_sample_config(int argc, char **argv)
{
    const char       *name = (argc > 1) ? argv[1] : (SAMPLE_NAME_PREFIX "th");
    SimuSensor       *sensor = simu_sensor_find(name);
    SimuSensorChannel channel;
    SimuSensorData    data;
    char              text[SAMPLE_TEXT_SIZE];

    sample_title("示例3: 设置传感器参数");

    if (sensor == RT_NULL)
    {
        rt_kprintf("未找到传感器 '%s', 请先执行 simu_sensor_sample_init\n", name);

        return -1;
    }

    /* 3.1 采样周期: 周期内重复读取返回缓存值; 设为 0 表示每次读取都重新采样 */
    simu_sensor_set_period(sensor, 3000U);
    rt_kprintf("[1] 采样周期设置为 %u ms\n", (unsigned int)sensor->period);

    /* 3.2 修改通道物理参数: 基线/噪声/漂移/分辨率/环境耦合/昼夜变化 */
    channel = *simu_sensor_get_channel(sensor, 0);

    rt_kprintf("[2] 修改通道 %s: 基线 ", channel.name);
    sample_print_value(channel.base, &channel);
    rt_kprintf(" -> 30.00, 噪声 ");
    simu_sensor_value_to_string(text, sizeof(text), channel.noise, 2);
    rt_kprintf("%s -> 0.50, 分辨率 ", text);

    channel.base         = 30.0f;   /* 基线抬到 30℃ */
    channel.noise        = 0.5f;    /* 噪声加大: 数据更"跳" */
    channel.drift_step   = 0.05f;   /* 漂移更快 */
    channel.resolution   = 0.1f;    /* 分辨率降到 0.1℃ */
    channel.ambient_gain = 1.0f;    /* 完全跟随环境温度 */

    if (simu_sensor_set_channel(sensor, 0, &channel) == RT_EOK)
    {
        rt_kprintf("0.10, 环境耦合 +1.0/℃\n");
    }

    /* 参数校验: 非法参数会被拒绝 */
    channel.min = 100.0f;
    channel.max = 10.0f;
    rt_kprintf("[3] 传入 min >= max 返回 %d (-RT_EINVAL=%d)\n",
               (int)simu_sensor_set_channel(sensor, 0, &channel), (int)(-RT_EINVAL));
    channel.min = -40.0f;
    channel.max = 85.0f;
    simu_sensor_set_channel(sensor, 0, &channel);

    /* 3.3 环境模型与仿真时钟: 影响所有与环境耦合、昼夜变化相关的传感器 */
    simu_sensor_set_ambient(30.0f, 4.0f);          /* 环境温度 30℃ ± 4℃ */
    simu_sensor_set_time_of_day(12.0f);            /* 把仿真时钟拨到正午 */
    simu_sensor_set_time_scale(60.0f);             /* 时间加速 60 倍, 便于观察昼夜变化 */

    rt_kprintf("[4] 环境温度=");
    sample_print_value(simu_sensor_ambient_temperature(), simu_sensor_get_channel(sensor, 0));
    rt_kprintf(" C, 时刻=");
    simu_sensor_value_to_string(text, sizeof(text), simu_sensor_time_of_day(), 2);
    rt_kprintf("%s 时, 时间倍率 60\n", text);

    /* 3.4 随机种子: 固定种子后序列可复现; reset 让序列从头开始 */
    simu_sensor_set_seed(sensor, 20260911U);
    simu_sensor_reset(sensor);
    simu_sensor_sample(sensor, &data);
    rt_kprintf("[5] 固定种子 0x%08x 后采样: ", (unsigned int)sensor->seed);
    sample_print_value(data.value[0], simu_sensor_get_channel(sensor, 0));
    rt_kprintf(" (复位后重新开始)\n");

    /* 3.5 故障注入: 用于验证上层对异常数据的处理 */
    rt_kprintf("[6] 故障注入演示:\n");

    simu_sensor_set_fault(sensor, SIMU_SENSOR_FAULT_OUT_OF_RANGE);
    simu_sensor_sample(sensor, &data);
    rt_kprintf("    超量程: 质量=%s ", simu_sensor_quality_name(data.quality));
    sample_print_value(data.value[0], simu_sensor_get_channel(sensor, 0));
    rt_kprintf(" (量程上限 85)\n");

    simu_sensor_set_fault(sensor, SIMU_SENSOR_FAULT_FREEZE);
    simu_sensor_sample(sensor, &data);
    rt_kprintf("    数据冻结: 质量=%s ", simu_sensor_quality_name(data.quality));
    sample_print_value(data.value[0], simu_sensor_get_channel(sensor, 0));
    rt_kprintf("\n");

    simu_sensor_set_fault(sensor, SIMU_SENSOR_FAULT_OFFLINE);
    rt_kprintf("    离线读取: 返回 %d (-RT_ERROR)\n", (int)simu_sensor_read(sensor, &data));

    /* 恢复: 恢复正常模式并清除漂移 */
    simu_sensor_set_fault(sensor, SIMU_SENSOR_FAULT_NONE);
    simu_sensor_reset(sensor);
    rt_kprintf("[7] 已恢复正常模式并复位(故障模式=%s)\n", simu_sensor_fault_name(sensor->fault));

    return 0;
}

/* -------------------------------------------------------------------------
 * 示例 4: 销毁虚拟传感器
 * ------------------------------------------------------------------------- */

/**
 * @brief 销毁示例
 *
 * 演示按名称销毁、按前缀批量销毁、全部销毁, 以及销毁后的验证。对应命令:
 *
 * @code
 * msh />simu_sensor_sample_destroy             # 销毁本示例创建的 sample_* 传感器
 * msh />simu_sensor_sample_destroy sample_gas  # 只销毁指定传感器
 * msh />simu_sensor_sample_destroy all         # 销毁全部已注册传感器
 * @endcode
 */
int simu_sensor_sample_destroy(int argc, char **argv)
{
    SimuSensorData data;
    SimuSensor    *sensor;
    rt_uint32_t    index = 0;

    sample_title("示例4: 销毁虚拟传感器");

    /* 4.1 按名称销毁单个传感器 */
    if ((argc > 1) && (rt_strcmp(argv[1], "all") != 0))
    {
        sensor = simu_sensor_find(argv[1]);

        if (sensor == RT_NULL)
        {
            rt_kprintf("未找到传感器 '%s'\n", argv[1]);

            return -1;
        }

        rt_kprintf("[1] 销毁 %s\n", sensor->name);
        simu_sensor_destroy(sensor);
        rt_kprintf("    销毁后再查找: %s\n",
                   (simu_sensor_find(argv[1]) == RT_NULL) ? "RT_NULL(已注销)" : "仍存在");

        return 0;
    }

    /* 4.2 批量销毁: 遍历注册表时在删除后不要递增下标(后面的对象会前移),
           这样即使边遍历边删除也是安全的 */
    if (argc > 1)
    {
        rt_kprintf("[1] 销毁全部已注册传感器:\n");
    }
    else
    {
        rt_kprintf("[1] 销毁本示例创建的传感器(前缀 %s):\n", SAMPLE_NAME_PREFIX);
    }

    while (index < simu_sensor_count())
    {
        sensor = simu_sensor_get(index);

        if (sensor == RT_NULL)
        {
            break;
        }

        if ((argc > 1) ||
            (rt_strncmp(sensor->name, SAMPLE_NAME_PREFIX, rt_strlen(SAMPLE_NAME_PREFIX)) == 0))
        {
            rt_kprintf("    销毁 %-14s (类型 %s)\n", sensor->name, simu_sensor_type_name(sensor->type));

            /* 动态对象会释放内存, 静态对象只从注册表注销(两者都可以用这个接口) */
            simu_sensor_destroy(sensor);
        }
        else
        {
            rt_kprintf("    跳过 %-14s (非示例对象)\n", sensor->name);
            index++;
        }
    }

    /* 4.3 销毁后的验证 */
    rt_kprintf("[2] 销毁后注册数量=%u\n", (unsigned int)simu_sensor_count());
    rt_kprintf("    查找 sample_th: %s\n",
               (simu_sensor_find(SAMPLE_NAME_PREFIX "th") == RT_NULL) ? "RT_NULL(未注册)" : "仍存在");
    rt_kprintf("    读取 sample_th: 返回 %d (未找到返回 -RT_EINVAL=%d)\n",
               (int)simu_sensor_read_by_name(SAMPLE_NAME_PREFIX "th", &data), (int)(-RT_EINVAL));
    rt_kprintf("    对已销毁对象调用 simu_sensor_deinit()/destroy() 是安全的(空指针也会被忽略)\n");
    simu_sensor_destroy(simu_sensor_find(SAMPLE_NAME_PREFIX "th"));

    return 0;
}

/* -------------------------------------------------------------------------
 * 示例 5: (可选) 使用 paho-mqtt 上报
 * ------------------------------------------------------------------------- */

#ifdef SIMU_SENSOR_SAMPLE_USING_MQTT

/**
 * @brief MQTT 上报示例(需要 paho-mqtt + cJSON 软件包)
 *
 * @code
 * msh />simu_sensor_sample_mqtt           # 配置并启动上报
 * msh />simu_sensor_sample_mqtt stop      # 停止上报
 * msh />simu_sensor_sample_mqtt status    # 查看状态
 * @endcode
 */
int simu_sensor_sample_mqtt(int argc, char **argv)
{
    sample_title("示例5: 使用 paho-mqtt 上报模拟传感器数据");

    if ((argc > 1) && (rt_strcmp(argv[1], "stop") == 0))
    {
        rt_kprintf("停止上报: %s\n", (simu_sensor_mqtt_stop() == RT_EOK) ? "已停止" : "未启动");

        return 0;
    }

    if ((argc > 1) && (rt_strcmp(argv[1], "status") == 0))
    {
        rt_kprintf("启动=%d 已连接=%d 已发布=%u 失败=%u\n",
                   (simu_sensor_mqtt_is_started() == RT_TRUE) ? 1 : 0,
                   (simu_sensor_mqtt_is_connected() == RT_TRUE) ? 1 : 0,
                   (unsigned int)simu_sensor_mqtt_get_publish_count(),
                   (unsigned int)simu_sensor_mqtt_get_fail_count());

        return 0;
    }

    /* 上报参数只能在启动前设置 */
    simu_sensor_mqtt_set_uri(SIMU_SENSOR_MQTT_DEFAULT_URI);
    simu_sensor_mqtt_set_credentials(SIMU_SENSOR_MQTT_DEFAULT_USERNAME, SIMU_SENSOR_MQTT_DEFAULT_PASSWORD);
    simu_sensor_mqtt_set_topic(SIMU_SENSOR_MQTT_DEFAULT_TOPIC);
    simu_sensor_mqtt_set_interval(5000U);
    simu_sensor_mqtt_set_publish_mode(SIMU_SENSOR_MQTT_PUBLISH_SINGLE);

    rt_kprintf("[1] 上报配置: uri=%s topic=%s 周期=%u ms 模式=聚合\n",
               simu_sensor_mqtt_get_uri(), simu_sensor_mqtt_get_topic(),
               (unsigned int)simu_sensor_mqtt_get_interval());

    if (simu_sensor_count() == 0U)
    {
        rt_kprintf("    提示: 当前没有传感器, 可先执行 simu_sensor_sample_init\n");
    }

    if (simu_sensor_mqtt_start() != RT_EOK)
    {
        rt_kprintf("[2] 启动失败(可能已启动), 可用 simu_sensor_sample_mqtt status 查看\n");

        return -1;
    }

    rt_kprintf("[2] 已启动: 每 5s 把全部模拟传感器打包成 JSON 发布到 %s\n", simu_sensor_mqtt_get_topic());
    rt_kprintf("    查看状态: simu_sensor_sample_mqtt status, 停止: simu_sensor_sample_mqtt stop\n");

    return 0;
}

#endif /* SIMU_SENSOR_SAMPLE_USING_MQTT */

/* -------------------------------------------------------------------------
 * 一键运行全部示例
 * ------------------------------------------------------------------------- */

/**
 * @brief 依次执行全部示例
 *
 * @code
 * msh />simu_sensor_sample_all
 * @endcode
 */
int simu_sensor_sample_all(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("\n########## 模拟传感器示例: 初始化 -> 信息 -> 参数 -> 销毁 ##########\n");

    simu_sensor_sample_init(0, RT_NULL);
    simu_sensor_sample_info(0, RT_NULL);
    simu_sensor_sample_config(0, RT_NULL);
    simu_sensor_sample_destroy(0, RT_NULL);

    rt_kprintf("\n########## 示例结束: 需重新使用请再次执行 simu_sensor_sample_init ##########\n");

    return 0;
}

/* 注意: 描述字符串中不能出现逗号(finsh 的导出宏按逗号切分参数) */
MSH_CMD_EXPORT_ALIAS(simu_sensor_sample_init, simu_sensor_sample_init,
                     sample: init sensors (static/dynamic/custom/batch));
MSH_CMD_EXPORT_ALIAS(simu_sensor_sample_info, simu_sensor_sample_info,
                     sample: show sensor channels and data [name]);
MSH_CMD_EXPORT_ALIAS(simu_sensor_sample_config, simu_sensor_sample_config,
                     sample: set period/channel/fault/ambient/time/seed [name]);
MSH_CMD_EXPORT_ALIAS(simu_sensor_sample_destroy, simu_sensor_sample_destroy,
                     sample: destroy virtual sensors [name]);
MSH_CMD_EXPORT_ALIAS(simu_sensor_sample_all, simu_sensor_sample_all,
                     "sample: run all simulated sensor samples");
#ifdef SIMU_SENSOR_SAMPLE_USING_MQTT
MSH_CMD_EXPORT_ALIAS(simu_sensor_sample_mqtt, simu_sensor_sample_mqtt,
                     "sample: publish sensors via paho mqtt [stop|status]");
#endif

#endif /* RT_USING_FINSH */
