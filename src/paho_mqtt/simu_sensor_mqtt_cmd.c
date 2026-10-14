/*
 * Copyright (c) 2026, CXSforHPU
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     CXSforHPU    the first version, msh commands for simulated sensor mqtt
 */

#include <rtthread.h>

#include "simu_sensor.h"
#include "simu_sensor_mqtt.h"

#ifdef RT_USING_FINSH

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

static void cmd_usage(void)
{
    rt_kprintf("usage:\n");
    rt_kprintf("  simu_sensor_mqtt start                 - start mqtt publishing\n");
    rt_kprintf("  simu_sensor_mqtt stop                  - stop mqtt publishing\n");
    rt_kprintf("  simu_sensor_mqtt status                - show state and counters\n");
    rt_kprintf("  simu_sensor_mqtt pub                   - publish one frame now\n");
    rt_kprintf("  simu_sensor_mqtt uri <uri>             - set broker uri (tcp://host:port)\n");
    rt_kprintf("  simu_sensor_mqtt topic <topic>         - set publish topic\n");
    rt_kprintf("  simu_sensor_mqtt clientid <id>         - set client id\n");
    rt_kprintf("  simu_sensor_mqtt user <u> [p]          - set username/password\n");
    rt_kprintf("  simu_sensor_mqtt interval <ms>         - set publish interval\n");
    rt_kprintf("  simu_sensor_mqtt mode <single|per_sensor> - set publish mode\n");
    rt_kprintf("  simu_sensor_mqtt autocreate <0|1>      - auto create demo sensors\n");
    rt_kprintf("note: uri/topic/... can only be changed when stopped\n");
}

static void cmd_status(void)
{
    rt_kprintf("started=%d connected=%d\n",
               (simu_sensor_mqtt_is_started() == RT_TRUE) ? 1 : 0,
               (simu_sensor_mqtt_is_connected() == RT_TRUE) ? 1 : 0);
    rt_kprintf("uri=%s\n", simu_sensor_mqtt_get_uri());
    rt_kprintf("topic=%s\n", simu_sensor_mqtt_get_topic());
    rt_kprintf("clientid=%s\n", simu_sensor_mqtt_get_client_id());
    rt_kprintf("interval=%ums mode=%s\n",
               (unsigned int)simu_sensor_mqtt_get_interval(),
               (simu_sensor_mqtt_get_publish_mode() == SIMU_SENSOR_MQTT_PUBLISH_SINGLE) ? "single" : "per_sensor");
    rt_kprintf("published=%u failed=%u\n",
               (unsigned int)simu_sensor_mqtt_get_publish_count(),
               (unsigned int)simu_sensor_mqtt_get_fail_count());
    rt_kprintf("sensors=%u\n", (unsigned int)simu_sensor_count());
}

static void cmd_set_result(const char *name, rt_err_t result)
{
    if (result == RT_EOK)
    {
        rt_kprintf("%s updated\n", name);
    }
    else if (result == -RT_EBUSY)
    {
        rt_kprintf("%s: please stop mqtt first\n", name);
    }
    else
    {
        rt_kprintf("%s: invalid value\n", name);
    }
}

static int simu_sensor_mqtt_cmd(int argc, char **argv)
{
    if (argc < 2)
    {
        cmd_usage();

        return -1;
    }

    if (rt_strcmp(argv[1], "start") == 0)
    {
        rt_err_t result = simu_sensor_mqtt_start();

        if (result == RT_EOK)
        {
            rt_kprintf("mqtt publishing started\n");
        }
        else if (result == -RT_EBUSY)
        {
            rt_kprintf("already started\n");
        }
        else
        {
            rt_kprintf("start failed (%d)\n", (int)result);
        }
    }
    else if (rt_strcmp(argv[1], "stop") == 0)
    {
        if (simu_sensor_mqtt_stop() != RT_EOK)
        {
            rt_kprintf("not started\n");
        }
    }
    else if (rt_strcmp(argv[1], "status") == 0)
    {
        cmd_status();
    }
    else if (rt_strcmp(argv[1], "pub") == 0)
    {
        if (simu_sensor_mqtt_publish_once() != RT_EOK)
        {
            rt_kprintf("publish failed (started=%d connected=%d)\n",
                       (simu_sensor_mqtt_is_started() == RT_TRUE) ? 1 : 0,
                       (simu_sensor_mqtt_is_connected() == RT_TRUE) ? 1 : 0);
        }
    }
    else if ((rt_strcmp(argv[1], "uri") == 0) || (rt_strcmp(argv[1], "topic") == 0) ||
             (rt_strcmp(argv[1], "clientid") == 0) || (rt_strcmp(argv[1], "interval") == 0) ||
             (rt_strcmp(argv[1], "mode") == 0) || (rt_strcmp(argv[1], "autocreate") == 0) ||
             (rt_strcmp(argv[1], "user") == 0))
    {
        if (argc < 3)
        {
            cmd_usage();

            return -1;
        }

        if (rt_strcmp(argv[1], "uri") == 0)
        {
            cmd_set_result("uri", simu_sensor_mqtt_set_uri(argv[2]));
        }
        else if (rt_strcmp(argv[1], "topic") == 0)
        {
            cmd_set_result("topic", simu_sensor_mqtt_set_topic(argv[2]));
        }
        else if (rt_strcmp(argv[1], "clientid") == 0)
        {
            cmd_set_result("clientid", simu_sensor_mqtt_set_client_id(argv[2]));
        }
        else if (rt_strcmp(argv[1], "interval") == 0)
        {
            cmd_set_result("interval", simu_sensor_mqtt_set_interval((rt_uint32_t)cmd_atoi(argv[2])));
        }
        else if (rt_strcmp(argv[1], "mode") == 0)
        {
            if (rt_strcmp(argv[2], "single") == 0)
            {
                cmd_set_result("mode", simu_sensor_mqtt_set_publish_mode(SIMU_SENSOR_MQTT_PUBLISH_SINGLE));
            }
            else if (rt_strcmp(argv[2], "per_sensor") == 0)
            {
                cmd_set_result("mode", simu_sensor_mqtt_set_publish_mode(SIMU_SENSOR_MQTT_PUBLISH_PER_SENSOR));
            }
            else
            {
                rt_kprintf("mode: single or per_sensor\n");
            }
        }
        else if (rt_strcmp(argv[1], "autocreate") == 0)
        {
            cmd_set_result("autocreate",
                           simu_sensor_mqtt_set_auto_create((cmd_atoi(argv[2]) != 0) ? RT_TRUE : RT_FALSE));
        }
        else
        {
            cmd_set_result("user",
                           simu_sensor_mqtt_set_credentials(argv[2], (argc > 3) ? argv[3] : RT_NULL));
        }
    }
    else
    {
        cmd_usage();

        return -1;
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(simu_sensor_mqtt_cmd, simu_sensor_mqtt, simulated sensor mqtt publish: start/stop/status/pub);

#endif /* RT_USING_FINSH */
