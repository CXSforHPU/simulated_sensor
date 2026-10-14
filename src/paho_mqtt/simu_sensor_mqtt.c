/*
 * Copyright (c) 2026, CXSforHPU
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-11     CXSforHPU    the first version, publish simulated sensor data via paho mqtt
 */

#include <rtthread.h>

#include <cJSON.h>

#include "paho_mqtt.h"

#include "simu_sensor.h"
#include "simu_sensor_mqtt.h"

/* 上报线程名称 */
#define SIMU_SENSOR_MQTT_THREAD_NAME        "simu_mqtt"
/* 未连接时的重试间隔(ms), paho 内部会自动重连 */
#define SIMU_SENSOR_MQTT_RETRY_INTERVAL     500
/* keepalive 时间(s) */
#define SIMU_SENSOR_MQTT_KEEPALIVE          30
/* 用户名/密码缓冲长度 */
#define SIMU_SENSOR_MQTT_CREDENTIAL_MAX     32

/*
 * paho-mqtt 的 RT-Thread 移植版(pipe 模式)通过一个固定大小的管道传递待发送数据,
 * 管道长度由 PAHOMQTT 软件包的 PIPE_BUFSZ(RT_PIPE_BUFSZ)决定, 默认 512 字节。
 * 超过该长度的消息会被截断, 进而导致 paho 线程解析出错误长度并崩溃,
 * 因此这里限定单条消息(结构头 + 负载 + 主题 + 结束符)不超过安全上限。
 * 需要更大的聚合报文时, 可在工程中定义 RT_PIPE_BUFSZ(如 2048)后重新编译 paho 软件包。
 */
#define SIMU_SENSOR_MQTT_PIPE_SIZE          512U
#define SIMU_SENSOR_MQTT_PIPE_MARGIN        32U
#define SIMU_SENSOR_MQTT_MAX_MESSAGE        (SIMU_SENSOR_MQTT_PIPE_SIZE - SIMU_SENSOR_MQTT_PIPE_MARGIN)

/**
 * @brief 模块配置
 */
typedef struct SimuSensorMqttConfig
{
    char                      uri[SIMU_SENSOR_MQTT_URI_MAX];           /**< broker 地址 */
    char                      topic[SIMU_SENSOR_MQTT_TOPIC_MAX];       /**< 发布主题 */
    char                      client_id[SIMU_SENSOR_MQTT_CLIENT_ID_MAX];/**< 客户端 ID */
    char                      username[SIMU_SENSOR_MQTT_CREDENTIAL_MAX];/**< 用户名 */
    char                      password[SIMU_SENSOR_MQTT_CREDENTIAL_MAX];/**< 密码 */
    rt_uint32_t               interval;                                /**< 上报周期(ms) */
    SimuSensorMqttPublishMode mode;                                    /**< 上报模式 */
    rt_bool_t                 auto_create;                             /**< 自动创建示例传感器 */
} SimuSensorMqttConfig;

static SimuSensorMqttConfig g_config =
{
    .uri         = SIMU_SENSOR_MQTT_DEFAULT_URI,
    .topic       = SIMU_SENSOR_MQTT_DEFAULT_TOPIC,
    .client_id   = SIMU_SENSOR_MQTT_DEFAULT_CLIENT_ID,
    .username    = SIMU_SENSOR_MQTT_DEFAULT_USERNAME,
    .password    = SIMU_SENSOR_MQTT_DEFAULT_PASSWORD,
    .interval    = SIMU_SENSOR_MQTT_DEFAULT_INTERVAL,
    .mode        = SIMU_SENSOR_MQTT_PUBLISH_SINGLE,
    .auto_create = RT_TRUE
};

static MQTTClient  g_client;
static rt_thread_t g_thread          = RT_NULL;
static rt_bool_t   g_started         = RT_FALSE;
static rt_bool_t   g_oversize_warned = RT_FALSE;
static rt_uint32_t g_publish_count   = 0;
static rt_uint32_t g_fail_count      = 0;

/* -------------------------------------------------------------------------
 * 内部工具
 * ------------------------------------------------------------------------- */

/**
 * @brief 字符串拷贝(带结束符, 超长返回错误)
 */
static rt_err_t simu_sensor_mqtt_strcpy(char *destination, rt_size_t size, const char *source)
{
    if ((destination == RT_NULL) || (source == RT_NULL) || (size == 0U))
    {
        return -RT_EINVAL;
    }

    if (rt_strlen(source) >= size)
    {
        return -RT_EINVAL;
    }

    rt_strncpy(destination, source, size - 1U);
    destination[size - 1U] = '\0';

    return RT_EOK;
}

/**
 * @brief 分段等待, 便于及时响应停止请求
 */
static void simu_sensor_mqtt_delay(rt_uint32_t milliseconds)
{
    while ((g_started == RT_TRUE) && (milliseconds >= 100U))
    {
        rt_thread_mdelay(100);
        milliseconds -= 100U;
    }

    if ((g_started == RT_TRUE) && (milliseconds > 0U))
    {
        rt_thread_mdelay((rt_int32_t)milliseconds);
    }
}

/**
 * @brief 连接成功回调
 */
static void simu_sensor_mqtt_online_callback(MQTTClient *client)
{
    RT_UNUSED(client);

    rt_kprintf("[simu_mqtt] connected to %s\n", g_config.uri);
}

/**
 * @brief 连接断开回调(paho 内部会自动重连)
 */
static void simu_sensor_mqtt_offline_callback(MQTTClient *client)
{
    RT_UNUSED(client);

    rt_kprintf("[simu_mqtt] disconnected, reconnecting...\n");
}

/**
 * @brief 把一条 JSON 发布出去
 *
 * 单条消息超过 paho 管道长度时返回 -RT_EFULL, 由调用者决定降级策略。
 */
static rt_err_t simu_sensor_mqtt_publish_json(const char *topic, const cJSON *root)
{
    char       *payload;
    rt_uint32_t message_size;
    int         result;

    payload = cJSON_PrintUnformatted(root);

    if (payload == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    /* paho 侧单条消息 = MQTTMessage 结构头 + 负载 + 主题 + 结束符 */
    message_size = (rt_uint32_t)rt_strlen(payload) + (rt_uint32_t)rt_strlen(topic) +
                   (rt_uint32_t)sizeof(MQTTMessage) + 1U;

    if (message_size > SIMU_SENSOR_MQTT_MAX_MESSAGE)
    {
        rt_free(payload);

        return -RT_EFULL;
    }

    result = paho_mqtt_publish(&g_client, QOS1, topic, payload);

    rt_free(payload);

    if (result != PAHO_SUCCESS)
    {
        g_fail_count++;

        return -RT_ERROR;
    }

    g_publish_count++;

    rt_kprintf("[simu_mqtt] pub #%u %s (%u bytes)\n",
               (unsigned int)g_publish_count, topic, (unsigned int)message_size);

    return RT_EOK;
}

/**
 * @brief 根据分辨率决定 JSON 中的小数位数
 */
static rt_uint8_t simu_sensor_mqtt_decimals_of(float resolution)
{
    if (resolution >= 1.0f)
    {
        return 0;
    }

    if (resolution >= 0.1f)
    {
        return 1;
    }

    if (resolution >= 0.01f)
    {
        return 2;
    }

    return 3;
}

/**
 * @brief 以定点文本写入 JSON 数值
 *
 * cJSON 打印 double 时会输出完整有效数字(如 27.590000152587891),
 * 这里按通道分辨率转成定点文本, 既便于阅读也节省报文长度。
 */
static void simu_sensor_mqtt_add_value(cJSON *parent, const char *name, float value, float resolution)
{
    char text[24];

    simu_sensor_value_to_string(text, sizeof(text), value, simu_sensor_mqtt_decimals_of(resolution));

    cJSON_AddRawToObject(parent, name, text);
}

/**
 * @brief 把各通道采样值填入 JSON 对象的 values 字段
 */
static void simu_sensor_mqtt_add_values(cJSON *parent, const SimuSensor *sensor, const SimuSensorData *data)
{
    cJSON     *values;
    rt_uint8_t index;

    values = cJSON_AddObjectToObject(parent, "values");

    if (values == RT_NULL)
    {
        return;
    }

    for (index = 0; index < data->channel_count; index++)
    {
        const SimuSensorChannel *channel = simu_sensor_get_channel(sensor, index);

        if (channel == RT_NULL)
        {
            continue;
        }

        simu_sensor_mqtt_add_value(values, channel->name, data->value[index], channel->resolution);
    }
}

/**
 * @brief 组装聚合模式 JSON
 *
 * 采用紧凑结构以适配 paho 的 512 字节发布通道:
 * @code
 * {"device":"rtthread-simu","ts":28841090,"hour":8,"ambient":29,"count":4,
 *  "sensors":{"temp_humi":{"temperature":27.48,"humidity":50.9},
 *             "gas":{"concentration":399}}}
 * @endcode
 * 数据质量正常时不输出 quality 字段, 异常(冻结/超量程)时才会输出, 便于上层告警。
 */
static cJSON *simu_sensor_mqtt_build_snapshot(const SimuSensorData *data, rt_uint32_t count)
{
    cJSON *root;
    cJSON *sensors;
    rt_uint32_t index;

    root = cJSON_CreateObject();

    if (root == RT_NULL)
    {
        return RT_NULL;
    }

    cJSON_AddStringToObject(root, "device", g_config.client_id);
    cJSON_AddNumberToObject(root, "ts", (count > 0U) ? (double)data[0].timestamp : 0.0);
    simu_sensor_mqtt_add_value(root, "hour", simu_sensor_time_of_day(), 0.01f);
    simu_sensor_mqtt_add_value(root, "ambient", simu_sensor_ambient_temperature(), 0.01f);
    cJSON_AddNumberToObject(root, "count", (double)count);

    sensors = cJSON_AddObjectToObject(root, "sensors");

    if (sensors == RT_NULL)
    {
        cJSON_Delete(root);

        return RT_NULL;
    }

    for (index = 0; index < count; index++)
    {
        const SimuSensor *sensor = simu_sensor_find(data[index].name);
        cJSON            *item;

        if (sensor == RT_NULL)
        {
            continue;
        }

        item = cJSON_AddObjectToObject(sensors, data[index].name);

        if (item == RT_NULL)
        {
            continue;
        }

        /* 仅在数据异常时附带质量标志 */
        if (data[index].quality != SIMU_SENSOR_QUALITY_GOOD)
        {
            cJSON_AddStringToObject(item, "quality", simu_sensor_quality_name(data[index].quality));
        }

        simu_sensor_mqtt_add_values(item, sensor, &data[index]);
    }

    return root;
}

/**
 * @brief 组装单个传感器 JSON
 */
static cJSON *simu_sensor_mqtt_build_sensor(const SimuSensor *sensor, const SimuSensorData *data)
{
    cJSON *root = cJSON_CreateObject();

    if (root == RT_NULL)
    {
        return RT_NULL;
    }

    cJSON_AddStringToObject(root, "device", g_config.client_id);
    cJSON_AddStringToObject(root, "name", data->name);
    cJSON_AddStringToObject(root, "type", simu_sensor_type_name(data->type));
    cJSON_AddStringToObject(root, "quality", simu_sensor_quality_name(data->quality));
    cJSON_AddNumberToObject(root, "ts", (double)data->timestamp);
    simu_sensor_mqtt_add_values(root, sensor, data);

    return root;
}

/**
 * @brief 按传感器分别上报(每个传感器一条消息, 主题为 topic/<name>)
 */
static rt_err_t simu_sensor_mqtt_publish_each(const SimuSensorData *data, rt_uint32_t count)
{
    rt_err_t   result = RT_EOK;
    rt_uint32_t index;

    for (index = 0; index < count; index++)
    {
        const SimuSensor *sensor = simu_sensor_find(data[index].name);
        char              topic[SIMU_SENSOR_MQTT_TOPIC_MAX + SIMU_SENSOR_NAME_MAX];
        cJSON            *root;

        if (sensor == RT_NULL)
        {
            continue;
        }

        rt_snprintf(topic, sizeof(topic), "%s/%s", g_config.topic, data[index].name);

        root = simu_sensor_mqtt_build_sensor(sensor, &data[index]);

        if (root == RT_NULL)
        {
            g_fail_count++;
            result = -RT_ENOMEM;
            continue;
        }

        if (simu_sensor_mqtt_publish_json(topic, root) != RT_EOK)
        {
            result = -RT_ERROR;
        }

        cJSON_Delete(root);
    }

    return result;
}

/**
 * @brief 上报一帧数据
 */
static rt_err_t simu_sensor_mqtt_publish_frame(void)
{
    SimuSensorData data[SIMU_SENSOR_MAX_NUM];
    rt_uint32_t    count;
    rt_err_t       result;

    /* 一次读取全部模拟传感器(离线传感器会被跳过) */
    count = simu_sensor_read_all(data, SIMU_SENSOR_MAX_NUM);

    if (count == 0U)
    {
        g_fail_count++;

        return -RT_ERROR;
    }

    if (g_config.mode == SIMU_SENSOR_MQTT_PUBLISH_PER_SENSOR)
    {
        return simu_sensor_mqtt_publish_each(data, count);
    }

    /* 聚合模式: 所有传感器一条消息 */
    {
        cJSON *root = simu_sensor_mqtt_build_snapshot(data, count);

        if (root == RT_NULL)
        {
            g_fail_count++;

            return -RT_ENOMEM;
        }

        result = simu_sensor_mqtt_publish_json(g_config.topic, root);
        cJSON_Delete(root);
    }

    if (result == -RT_EFULL)
    {
        /* 聚合报文超过 paho 发布通道上限: 降级为按传感器分别上报 */
        if (g_oversize_warned == RT_FALSE)
        {
            g_oversize_warned = RT_TRUE;
            rt_kprintf("[simu_mqtt] warning: aggregate payload exceeds paho pipe, "
                       "fallback to per-sensor publish (or use 'mode per_sensor')\n");
        }

        return simu_sensor_mqtt_publish_each(data, count);
    }

    return result;
}

/**
 * @brief 上报线程: 未连接时等待重连, 已连接时按周期上报
 */
static void simu_sensor_mqtt_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (g_started == RT_TRUE)
    {
        if (g_client.isconnected == 0)
        {
            /* paho 内部会自动重连, 这里只等待 */
            simu_sensor_mqtt_delay(SIMU_SENSOR_MQTT_RETRY_INTERVAL);
            continue;
        }

        /* 计数在 simu_sensor_mqtt_publish_json 里按消息统计 */
        (void)simu_sensor_mqtt_publish_frame();

        simu_sensor_mqtt_delay(g_config.interval);
    }

    g_thread = RT_NULL;

    rt_kprintf("[simu_mqtt] publish thread exit, %u published / %u failed\n",
               (unsigned int)g_publish_count, (unsigned int)g_fail_count);
}

/**
 * @brief 注册表中没有传感器时创建示例传感器
 */
static rt_err_t simu_sensor_mqtt_prepare_sensors(void)
{
    if (simu_sensor_count() > 0U)
    {
        return RT_EOK;
    }

    if (g_config.auto_create != RT_TRUE)
    {
        rt_kprintf("[simu_mqtt] warning: no sensor registered\n");

        return RT_EOK;
    }

    if ((simu_sensor_create_default("temp_humi", SIMU_SENSOR_TYPE_TEMP_HUMI) == RT_NULL) ||
        (simu_sensor_create_default("gas", SIMU_SENSOR_TYPE_GAS) == RT_NULL) ||
        (simu_sensor_create_default("light", SIMU_SENSOR_TYPE_ILLUMINANCE) == RT_NULL) ||
        (simu_sensor_create_default("pressure", SIMU_SENSOR_TYPE_PRESSURE) == RT_NULL))
    {
        return -RT_ENOMEM;
    }

    rt_kprintf("[simu_mqtt] created 4 demo sensor(s): temp_humi/gas/light/pressure\n");

    return RT_EOK;
}

/* -------------------------------------------------------------------------
 * 配置接口
 * ------------------------------------------------------------------------- */

rt_err_t simu_sensor_mqtt_set_uri(const char *uri)
{
    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    return simu_sensor_mqtt_strcpy(g_config.uri, sizeof(g_config.uri), uri);
}

rt_err_t simu_sensor_mqtt_set_topic(const char *topic)
{
    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    return simu_sensor_mqtt_strcpy(g_config.topic, sizeof(g_config.topic), topic);
}

rt_err_t simu_sensor_mqtt_set_client_id(const char *client_id)
{
    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    return simu_sensor_mqtt_strcpy(g_config.client_id, sizeof(g_config.client_id), client_id);
}

rt_err_t simu_sensor_mqtt_set_credentials(const char *username, const char *password)
{
    rt_err_t result;

    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    if (username == RT_NULL)
    {
        g_config.username[0] = '\0';
        g_config.password[0] = '\0';

        return RT_EOK;
    }

    result = simu_sensor_mqtt_strcpy(g_config.username, sizeof(g_config.username), username);

    if (result != RT_EOK)
    {
        return result;
    }

    return simu_sensor_mqtt_strcpy(g_config.password, sizeof(g_config.password),
                                   (password != RT_NULL) ? password : "");
}

rt_err_t simu_sensor_mqtt_set_interval(rt_uint32_t interval_ms)
{
    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    if ((interval_ms < SIMU_SENSOR_MQTT_MIN_INTERVAL) || (interval_ms > 3600000U))
    {
        return -RT_EINVAL;
    }

    g_config.interval = interval_ms;

    return RT_EOK;
}

rt_err_t simu_sensor_mqtt_set_publish_mode(SimuSensorMqttPublishMode mode)
{
    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    if (mode > SIMU_SENSOR_MQTT_PUBLISH_PER_SENSOR)
    {
        return -RT_EINVAL;
    }

    g_config.mode = mode;

    return RT_EOK;
}

rt_err_t simu_sensor_mqtt_set_auto_create(rt_bool_t enable)
{
    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    g_config.auto_create = (enable == RT_TRUE) ? RT_TRUE : RT_FALSE;

    return RT_EOK;
}

const char *simu_sensor_mqtt_get_uri(void)
{
    return g_config.uri;
}

const char *simu_sensor_mqtt_get_topic(void)
{
    return g_config.topic;
}

const char *simu_sensor_mqtt_get_client_id(void)
{
    return g_config.client_id;
}

rt_uint32_t simu_sensor_mqtt_get_interval(void)
{
    return g_config.interval;
}

SimuSensorMqttPublishMode simu_sensor_mqtt_get_publish_mode(void)
{
    return g_config.mode;
}

/* -------------------------------------------------------------------------
 * 运行控制
 * ------------------------------------------------------------------------- */

rt_err_t simu_sensor_mqtt_start(void)
{
    MQTTPacket_connectData condata = MQTTPacket_connectData_initializer;
    rt_err_t               result;
    int                    blocking = 0;

    if (g_started == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    result = simu_sensor_mqtt_prepare_sensors();

    if (result != RT_EOK)
    {
        return result;
    }

    rt_memset(&g_client, 0, sizeof(g_client));

    /* 连接参数 */
    g_client.uri = g_config.uri;
    rt_memcpy(&g_client.condata, &condata, sizeof(condata));
    g_client.condata.clientID.cstring   = g_config.client_id;
    g_client.condata.keepAliveInterval  = SIMU_SENSOR_MQTT_KEEPALIVE;
    g_client.condata.cleansession       = 1;

    if (g_config.username[0] != '\0')
    {
        g_client.condata.username.cstring = g_config.username;
        g_client.condata.password.cstring = g_config.password;
    }

    /*
     * 使用阻塞发布: publish 返回时数据已经由 paho 线程真正发出, 管道中不再残留
     * 待发消息(否则停止时 DISCONNECT 命令会与残留消息被一次性读出而丢失)。
     */
    blocking = 1;
    paho_mqtt_control(&g_client, MQTT_CTRL_PUBLISH_BLOCK, &blocking);

    /* 收发缓冲(paho 在断开时会释放) */
    g_client.buf_size     = SIMU_SENSOR_MQTT_BUFFER_SIZE;
    g_client.readbuf_size = SIMU_SENSOR_MQTT_BUFFER_SIZE;
    g_client.buf          = rt_malloc(g_client.buf_size);
    g_client.readbuf      = rt_malloc(g_client.readbuf_size);

    if ((g_client.buf == RT_NULL) || (g_client.readbuf == RT_NULL))
    {
        if (g_client.buf != RT_NULL)
        {
            rt_free(g_client.buf);
        }

        if (g_client.readbuf != RT_NULL)
        {
            rt_free(g_client.readbuf);
        }

        g_client.buf     = RT_NULL;
        g_client.readbuf = RT_NULL;

        return -RT_ENOMEM;
    }

    g_client.online_callback  = simu_sensor_mqtt_online_callback;
    g_client.offline_callback = simu_sensor_mqtt_offline_callback;

    g_started = RT_TRUE;
    g_publish_count   = 0;
    g_fail_count      = 0;
    g_oversize_warned = RT_FALSE;

    if (paho_mqtt_start(&g_client) != PAHO_SUCCESS)
    {
        g_started = RT_FALSE;
        rt_free(g_client.buf);
        rt_free(g_client.readbuf);
        g_client.buf     = RT_NULL;
        g_client.readbuf = RT_NULL;

        return -RT_ERROR;
    }

    g_thread = rt_thread_create(SIMU_SENSOR_MQTT_THREAD_NAME,
                                simu_sensor_mqtt_thread_entry, RT_NULL,
                                SIMU_SENSOR_MQTT_THREAD_STACK_SIZE,
                                SIMU_SENSOR_MQTT_THREAD_PRIORITY, 10);

    if (g_thread == RT_NULL)
    {
        g_started = RT_FALSE;
        paho_mqtt_stop(&g_client);

        return -RT_ENOMEM;
    }

    rt_thread_startup(g_thread);

    rt_kprintf("[simu_mqtt] started: uri=%s topic=%s interval=%ums mode=%s\n",
               g_config.uri, g_config.topic, (unsigned int)g_config.interval,
               (g_config.mode == SIMU_SENSOR_MQTT_PUBLISH_SINGLE) ? "single" : "per_sensor");

    return RT_EOK;
}

rt_err_t simu_sensor_mqtt_stop(void)
{
    rt_uint32_t wait_ms;
    rt_uint8_t  retry;

    if (g_started != RT_TRUE)
    {
        return -RT_ERROR;
    }

    /* 1. 通知上报线程退出并等待(它可能正阻塞在一次发布上, 最长 5s) */
    g_started = RT_FALSE;

    for (wait_ms = 0; (g_thread != RT_NULL) && (wait_ms < 8000U); wait_ms += 20U)
    {
        rt_thread_mdelay(20);
    }

    /*
     * 2. 通知 paho 线程断开: 由于发布采用阻塞确认方式, 此时发布管道已排空,
     *    DISCONNECT 命令会被单独读取, paho 线程随后释放缓冲并退出。
     */
    for (retry = 0; retry < 3U; retry++)
    {
        if (g_client.isconnected == 0)
        {
            break;
        }

        paho_mqtt_stop(&g_client);

        for (wait_ms = 0; (g_client.isconnected != 0) && (wait_ms < 700U); wait_ms += 20U)
        {
            rt_thread_mdelay(20);
        }
    }

    if (g_client.isconnected != 0)
    {
        /* 未连接(如 broker 不可达)时 DISCONNECT 无法立即生效,
           这里把命令排入管道, paho 线程重连成功后会立即读取并退出 */
        g_client.isconnected = 1;
        paho_mqtt_stop(&g_client);
        g_client.isconnected = 0;

        rt_kprintf("[simu_mqtt] warning: disconnected client, quit command queued\n");
    }

    rt_kprintf("[simu_mqtt] stopped, %u published / %u failed\n",
               (unsigned int)g_publish_count, (unsigned int)g_fail_count);

    return RT_EOK;
}

rt_bool_t simu_sensor_mqtt_is_started(void)
{
    return g_started;
}

rt_bool_t simu_sensor_mqtt_is_connected(void)
{
    return (g_client.isconnected != 0) ? RT_TRUE : RT_FALSE;
}

rt_uint32_t simu_sensor_mqtt_get_publish_count(void)
{
    return g_publish_count;
}

rt_uint32_t simu_sensor_mqtt_get_fail_count(void)
{
    return g_fail_count;
}

rt_err_t simu_sensor_mqtt_publish_once(void)
{
    if (g_started != RT_TRUE)
    {
        return -RT_ERROR;
    }

    if (g_client.isconnected == 0)
    {
        return -RT_ERROR;
    }

    return simu_sensor_mqtt_publish_frame();
}
