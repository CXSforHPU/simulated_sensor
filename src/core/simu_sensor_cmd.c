/*
 * Copyright (c) 2026, CXSforHPU
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     CXSforHPU    the first version, msh commands for simulated sensor
 */

#include <rtthread.h>

#include "simu_sensor.h"

#ifdef RT_USING_FINSH

/* 示例传感器名称 */
#define CMD_DEMO_TEMP_HUMI      "temp_humi"
#define CMD_DEMO_GAS            "gas"
#define CMD_DEMO_LIGHT          "light"
#define CMD_DEMO_PRESSURE       "pressure"

/**
 * @brief 无 libc 环境下的整数解析
 */
static rt_int32_t cmd_atoi(const char *text)
{
    rt_int32_t value = 0;
    rt_int32_t sign  = 1;

    if (*text == '-')
    {
        sign = -1;
        text++;
    }

    while ((*text >= '0') && (*text <= '9'))
    {
        value = value * 10 + (rt_int32_t)(*text - '0');
        text++;
    }

    return value * sign;
}

/**
 * @brief 根据分辨率决定打印的小数位数
 */
static rt_uint8_t cmd_decimals_of(const SimuSensorChannel *channel)
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
 * @brief 定点打印浮点数(klibc 的精简版 printf 不支持 %f)
 */
static void cmd_print_value(float value, rt_uint8_t decimals)
{
    char text[24];

    simu_sensor_value_to_string(text, sizeof(text), value, decimals);

    rt_kprintf("%s", text);
}

/**
 * @brief 打印一条传感器采样结果
 */
static void cmd_print_sample(const SimuSensor *sensor, const SimuSensorData *data)
{
    const SimuSensorChannel *channel;
    rt_uint8_t                index;

    rt_kprintf("%s: type=%s quality=%s t=%ums", data->name,
               simu_sensor_type_name(data->type),
               simu_sensor_quality_name(data->quality),
               (unsigned int)data->timestamp);

    for (index = 0; index < data->channel_count; index++)
    {
        channel = simu_sensor_get_channel(sensor, index);

        if (channel == RT_NULL)
        {
            continue;
        }

        rt_kprintf(" %s=", channel->name);
        cmd_print_value(data->value[index], cmd_decimals_of(channel));

        if ((channel->unit != RT_NULL) && (channel->unit[0] != '\0'))
        {
            rt_kprintf("%s", channel->unit);
        }
    }

    rt_kprintf("\n");
}

/**
 * @brief 读取并打印一个传感器(强制重新采样, 便于观察变化)
 */
static void cmd_read_one(SimuSensor *sensor)
{
    SimuSensorData data;

    if (simu_sensor_sample(sensor, &data) != RT_EOK)
    {
        rt_kprintf("%s: read failed (fault=%s)\n", sensor->name, simu_sensor_fault_name(sensor->fault));

        return;
    }

    cmd_print_sample(sensor, &data);
}

/**
 * @brief 读取并打印所有传感器
 */
static void cmd_read_all(void)
{
    SimuSensorData data[SIMU_SENSOR_MAX_NUM];
    rt_uint32_t    count;
    rt_uint32_t    index;

    count = simu_sensor_read_all(data, SIMU_SENSOR_MAX_NUM);

    for (index = 0; index < count; index++)
    {
        cmd_print_sample(simu_sensor_find(data[index].name), &data[index]);
    }

    if (count == 0U)
    {
        rt_kprintf("no sensor registered, try: simu_sensor demo\n");
    }
}

static void cmd_list(void)
{
    SimuSensor *sensor;
    rt_uint32_t index;
    rt_uint32_t count = simu_sensor_count();

    rt_kprintf("count=%u\n", (unsigned int)count);
    rt_kprintf("%-10s %-12s %-5s %-8s %s\n", "name", "type", "ch", "period", "fault");

    for (index = 0; index < count; index++)
    {
        sensor = simu_sensor_get(index);

        if (sensor == RT_NULL)
        {
            continue;
        }

        rt_kprintf("%-10s %-12s %-5d %-8u %s\n", sensor->name,
                   simu_sensor_type_name(sensor->type),
                   (int)sensor->channel_count,
                   (unsigned int)sensor->period,
                   simu_sensor_fault_name(sensor->fault));
    }
}

static void cmd_info(void)
{
    rt_kprintf("simu_sensor: count=%u time=%ums hour=%d diurnal=",
               (unsigned int)simu_sensor_count(), (unsigned int)simu_sensor_time_ms(),
               (int)simu_sensor_time_of_day());

    cmd_print_value(simu_sensor_diurnal_factor(), 3);

    rt_kprintf(" ambient=");
    cmd_print_value(simu_sensor_ambient_temperature(), 2);
    rt_kprintf("C\n");
}

/**
 * @brief 创建示例传感器(已存在则跳过)
 */
static rt_uint32_t cmd_demo_create(const char *name, SimuSensorType type)
{
    if (simu_sensor_find(name) != RT_NULL)
    {
        return 0;
    }

    if (simu_sensor_create_default(name, type) == RT_NULL)
    {
        return 0;
    }

    return 1;
}

static void cmd_demo(void)
{
    rt_uint32_t created = 0;
    rt_uint32_t index;

    created += cmd_demo_create(CMD_DEMO_TEMP_HUMI, SIMU_SENSOR_TYPE_TEMP_HUMI);
    created += cmd_demo_create(CMD_DEMO_GAS, SIMU_SENSOR_TYPE_GAS);
    created += cmd_demo_create(CMD_DEMO_LIGHT, SIMU_SENSOR_TYPE_ILLUMINANCE);
    created += cmd_demo_create(CMD_DEMO_PRESSURE, SIMU_SENSOR_TYPE_PRESSURE);

    rt_kprintf("created %u simulated sensor(s), sampling 3 rounds:\n", (unsigned int)created);

    for (index = 0; index < 3U; index++)
    {
        cmd_read_all();
        rt_thread_mdelay(1000);
    }

    rt_kprintf("now=%ds, use 'simu_sensor read all' to sample again\n",
               (int)(simu_sensor_time_ms() / 1000U));
}

static void cmd_usage(void)
{
    rt_kprintf("usage:\n");
    rt_kprintf("  simu_sensor list                      - list sensors\n");
    rt_kprintf("  simu_sensor read <name|all>           - read one or all sensors\n");
    rt_kprintf("  simu_sensor monitor <name> [n] [ms]   - sample repeatedly (force sampling)\n");
    rt_kprintf("  simu_sensor fault <name> <mode>       - inject fault: none|freeze|out_of_range|offline\n");
    rt_kprintf("  simu_sensor time <hour>               - set simulated time of day\n");
    rt_kprintf("  simu_sensor scale <factor>            - set simulated time scale\n");
    rt_kprintf("  simu_sensor info                      - show module state\n");
    rt_kprintf("  simu_sensor demo                      - create demo sensors and sample\n");
    rt_kprintf("types: temperature humidity temp_humi gas illuminance pressure noise custom\n");
}

static void cmd_monitor(const char *name, rt_int32_t count, rt_int32_t interval)
{
    SimuSensor *sensor = simu_sensor_find(name);
    rt_int32_t  index;

    if (sensor == RT_NULL)
    {
        rt_kprintf("sensor '%s' not found\n", name);

        return;
    }

    if (count <= 0)
    {
        count = 5;
    }

    if (interval <= 0)
    {
        interval = 1000;
    }

    for (index = 0; index < count; index++)
    {
        cmd_read_one(sensor);

        if (index + 1 < count)
        {
            rt_thread_mdelay((rt_int32_t)interval);
        }
    }
}

static void cmd_fault(const char *name, const char *mode)
{
    SimuSensor     *sensor = simu_sensor_find(name);
    SimuSensorFault fault;

    if (sensor == RT_NULL)
    {
        rt_kprintf("sensor '%s' not found\n", name);

        return;
    }

    for (fault = SIMU_SENSOR_FAULT_NONE; fault <= SIMU_SENSOR_FAULT_OFFLINE;
         fault = (SimuSensorFault)(fault + 1))
    {
        if (rt_strcmp(mode, simu_sensor_fault_name(fault)) == 0)
        {
            simu_sensor_set_fault(sensor, fault);
            rt_kprintf("%s: fault=%s\n", sensor->name, simu_sensor_fault_name(fault));

            return;
        }
    }

    rt_kprintf("unknown fault '%s'\n", mode);
}

static int simu_sensor_cmd(int argc, char **argv)
{
    if (argc < 2)
    {
        cmd_usage();

        return -1;
    }

    if (rt_strcmp(argv[1], "list") == 0)
    {
        cmd_list();
    }
    else if (rt_strcmp(argv[1], "read") == 0)
    {
        if (argc < 3)
        {
            cmd_usage();

            return -1;
        }

        if (rt_strcmp(argv[2], "all") == 0)
        {
            cmd_read_all();
        }
        else
        {
            SimuSensor *sensor = simu_sensor_find(argv[2]);

            if (sensor == RT_NULL)
            {
                rt_kprintf("sensor '%s' not found\n", argv[2]);
            }
            else
            {
                cmd_read_one(sensor);
            }
        }
    }
    else if (rt_strcmp(argv[1], "monitor") == 0)
    {
        if (argc < 3)
        {
            cmd_usage();

            return -1;
        }

        cmd_monitor(argv[2], (argc > 3) ? cmd_atoi(argv[3]) : 0, (argc > 4) ? cmd_atoi(argv[4]) : 0);
    }
    else if (rt_strcmp(argv[1], "fault") == 0)
    {
        if (argc < 4)
        {
            cmd_usage();

            return -1;
        }

        cmd_fault(argv[2], argv[3]);
    }
    else if (rt_strcmp(argv[1], "time") == 0)
    {
        if (argc < 3)
        {
            cmd_usage();

            return -1;
        }

        simu_sensor_set_time_of_day((float)cmd_atoi(argv[2]));
        cmd_info();
    }
    else if (rt_strcmp(argv[1], "scale") == 0)
    {
        if (argc < 3)
        {
            cmd_usage();

            return -1;
        }

        simu_sensor_set_time_scale((float)cmd_atoi(argv[2]));
        cmd_info();
    }
    else if (rt_strcmp(argv[1], "info") == 0)
    {
        cmd_info();
    }
    else if (rt_strcmp(argv[1], "demo") == 0)
    {
        cmd_demo();
    }
    else
    {
        cmd_usage();

        return -1;
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(simu_sensor_cmd, simu_sensor, simulated sensor: list/read/monitor/fault/demo);

#endif /* RT_USING_FINSH */
