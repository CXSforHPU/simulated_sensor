# simulated_sensor

RT-Thread 模拟传感器软件包。提供三个模块：

| 模块 | 文件 | 说明 |
| -- | -- | -- |
| 随机数 | `simu_sensor_random.h/.c` | 整数/浮点/均匀/正态/指数分布随机数，不依赖 libc、libm |
| 模拟传感器对象 | `simu_sensor.h/.c`、`simu_sensor_cmd.c` | 把随机数包装成可读取的传感器对象（温湿度、气体浓度、光照强度等） |
| MQTT 上报 | `include/paho_mqtt/`、`src/paho_mqtt/` | 基于 paho-mqtt 软件包，把模拟传感器数据组装成 JSON 周期上报 |

## 1 目录结构

```
simulated_sensor/
├── SConscript                          # 收集 src/core、src/paho_mqtt（依赖 paho + cJSON）与 include/samples
├── include/
│   ├── core/
│   │   ├── simu_sensor_random.h        # 随机数模块接口
│   │   └── simu_sensor.h               # 模拟传感器对象接口
│   ├── paho_mqtt/
│   │   └── simu_sensor_mqtt.h          # MQTT 上报接口
│   └── samples/
│       ├── simu_sensor_sample.c        # 使用示例(初始化/获取信息/设置参数/销毁)
│       └── README.md                   # 示例说明与接口速查
└── src/
    ├── core/
    │   ├── simu_sensor_random.c        # 随机数模块实现
    │   ├── simu_sensor.c               # 传感器对象、注册表、数据生成模型
    │   └── simu_sensor_cmd.c           # msh 命令(列表/读取/监视/故障注入/演示)
    └── paho_mqtt/
        ├── simu_sensor_mqtt.c          # 周期采集 + JSON 组装 + paho 发布
        └── simu_sensor_mqtt_cmd.c      # msh 命令(start/stop/status/pub/配置)
```

## 2 随机数模块

| 分类 | 接口 |
| -- | -- |
| 整数 | `simu_sensor_random_int()` / `simu_sensor_random_uint()`（`[min, max]` 闭区间） |
| 浮点 | `simu_sensor_random_float()`（`[min, max)`）、`simu_sensor_random_unit()`（`[0,1)`） |
| 正态分布 | `simu_sensor_random_normal(mean, sigma)` |
| 指数分布 | `simu_sensor_random_exponential(lambda)` |
| 均匀分布 | `simu_sensor_random_dist_set_uniform()` + `simu_sensor_random_sample()` |
| 布尔/概率 | `simu_sensor_random_bool()` / `simu_sensor_random_chance(p)` |
| 生成器管理 | `simu_sensor_random_init()` / `simu_sensor_random_init_auto()` / `simu_sensor_random_seed()` |

设计要点：

- **不依赖 libc/libm**：伪随机数采用 xoshiro128\*\*（splitmix32 播种），正态分布采用 Marsaglia
  极坐标法，平方根与自然对数由模块内部实现，因此 `rand()/srand()/sqrt()/log()` 均可不用，
  在未开启 `RT_USING_LIBC`、链接时未加 `-lm` 的工程中也能直接编译链接。
- **拒绝采样消偏**：整数区间剔除不能整除的部分，避免取模引入的偏置。
- **两套 API**：不带 `_r` 后缀用模块内部默认实例（首次使用自动播种）；带 `_r` 后缀用调用者
  提供的 `SimuSensorRandom` 实例，各实例序列互相独立，适合多线程；`rnd` 传 `RT_NULL` 时回退默认实例。

## 3 模拟传感器对象

### 3.1 内置类型与默认参数

| 类型 | 通道 | 单位 | 量程 | 基线 | 噪声 σ | 分辨率 | 漂移步长/上限 | 昼夜变化 | 环境耦合 | 异常脉冲 |
| -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- |
| `SIMU_SENSOR_TYPE_TEMPERATURE` | temperature | ℃ | -40 ~ 85 | 25 | 0.12 | 0.01 | 0.01 / 0.5 | ±2% | +0.6/℃ | - |
| `SIMU_SENSOR_TYPE_HUMIDITY` | humidity | %RH | 0 ~ 100 | 55 | 0.4 | 0.1 | 0.05 / 2 | ±3% | -1.2/℃ | - |
| `SIMU_SENSOR_TYPE_TEMP_HUMI` | 上述 2 个通道 | ℃ / %RH | - | 25 / 55 | - | - | - | - | - | - |
| `SIMU_SENSOR_TYPE_GAS` | concentration | ppm | 0 ~ 5000 | 400 | 10 | 1 | 1.5 / 50 | - | - | 1% / 1500ppm |
| `SIMU_SENSOR_TYPE_ILLUMINANCE` | illuminance | lux | 0 ~ 20000 | 800 | 20 | 1 | 8 / 150 | ±95% | - | - |
| `SIMU_SENSOR_TYPE_PRESSURE` | pressure | hPa | 300 ~ 1100 | 1013.25 | 0.05 | 0.01 | 0.02 / 3 | - | - | - |
| `SIMU_SENSOR_TYPE_NOISE` | noise | dB | 30 ~ 120 | 45 | 1.5 | 0.1 | 0.4 / 8 | ±5% | - | 2% / 25dB |
| `SIMU_SENSOR_TYPE_CUSTOM` | value | - | -1000 ~ 1000 | 0 | 0.5 | 0.01 | - | - | - | - |

### 3.2 数据生成模型

每个通道的采样值由下面几项叠加、量化、限幅得到：

```
采样值 = base                                    基线
       + base * diurnal * 昼夜因子                日变化(昼夜因子 -1 ~ 1, 午夜 -1, 正午 1)
       + ambient_gain * (环境温度 - 25℃)          环境温度耦合(湿度为负值, 温度升高湿度下降)
       + 漂移(随机游走 + 回中, 受 drift_limit 限制) 缓慢老化
       + noise * N(0, 1)                          瞬态噪声
       + 异常脉冲(概率 spike_prob, 幅度 spike_gain) 气体泄漏/噪声尖峰
量化到 resolution, 限幅到 [min, max]
```

- **仿真时钟**：由系统 tick 驱动，可用 `simu_sensor_set_time_scale(3600)` 加速（1 秒真实时间 = 1 小时仿真时间），
  便于快速观察昼夜变化；`simu_sensor_set_time_of_day(12)` 可直接把时钟拨到正午。
- **环境温度模型**：`环境温度 = base + swing * 昼夜因子`，默认 26℃ ± 6℃，由
  `simu_sensor_set_ambient()` 调整；它通过通道的 `ambient_gain` 影响各个传感器，形成相互关联的数据
  （例如环境升温 10℃ → 温度通道 +6℃、湿度通道 -12%RH）。
- **采样周期**：默认 1000ms，周期内重复读取返回缓存值，超出周期重新采样；
  `simu_sensor_set_period(sensor, 0)` 表示每次读取都重新采样，`simu_sensor_sample()` 强制重新采样。
- **线程安全**：注册表与采样状态由模块内部互斥锁保护，应用线程与 msh 命令可同时读取不同传感器。

### 3.3 接口一览

对象管理：

| 接口 | 说明 |
| -- | -- |
| `simu_sensor_init(sensor, name, type)` | 静态对象初始化并按类型套用预设 |
| `simu_sensor_create(name, type)` / `simu_sensor_create_default(...)` | 动态创建（内部 `rt_malloc`） |
| `simu_sensor_deinit(sensor)` / `simu_sensor_destroy(sensor)` | 注销 / 注销并释放 |
| `simu_sensor_reset(sensor)` | 清除漂移与缓存，随机序列从头开始 |
| `simu_sensor_set_period()` / `simu_sensor_set_seed()` / `simu_sensor_set_fault()` | 采样周期 / 随机种子 / 故障注入 |
| `simu_sensor_set_channel()` / `simu_sensor_get_channel()` | 修改 / 读取通道参数 |

注册表：

| 接口 | 说明 |
| -- | -- |
| `simu_sensor_find(name)` | 按名称查找 |
| `simu_sensor_count()` / `simu_sensor_get(index)` | 遍历已注册的传感器 |

数据读取：

| 接口 | 说明 |
| -- | -- |
| `simu_sensor_read(sensor, &data)` | 按采样周期读取 |
| `simu_sensor_sample(sensor, &data)` | 强制重新采样 |
| `simu_sensor_read_by_name(name, &data)` | 按名称读取 |
| `simu_sensor_read_all(data, max)` | 一次读取全部传感器（离线传感器跳过），适合上报任务 |
| `simu_sensor_value(sensor, index)` | 最近一次采样值（不触发采样） |

环境与时钟：`simu_sensor_ambient_temperature()`、`simu_sensor_set_ambient()`、`simu_sensor_time_ms()`、
`simu_sensor_set_time_scale()`、`simu_sensor_time_of_day()`、`simu_sensor_set_time_of_day()`、`simu_sensor_diurnal_factor()`。

元信息：`simu_sensor_type_name()`、`simu_sensor_fault_name()`、`simu_sensor_quality_name()`。

### 3.4 使用示例

```c
#include <rtthread.h>
#include "simu_sensor.h"

/* 方式一：静态创建温湿度传感器 */
static SimuSensor th_sensor;

void sensor_demo(void)
{
    SimuSensorData data;

    simu_sensor_init(&th_sensor, "th", SIMU_SENSOR_TYPE_TEMP_HUMI);
    simu_sensor_set_period(&th_sensor, 2000);            /* 2s 采样一次 */

    if (simu_sensor_read(&th_sensor, &data) == RT_EOK)
    {
        /* data.value[0]: 温度(℃)   data.value[1]: 湿度(%RH) */
        rt_kprintf("temp=%d.%02d humi=%d.%02d\n",
                   (int)data.value[0], (int)(data.value[0] * 100) % 100,
                   (int)data.value[1], (int)(data.value[1] * 100) % 100);
    }
}

/* 方式二：动态创建多种传感器，供上报任务统一读取 */
void sensor_setup(void)
{
    simu_sensor_create_default("temp_humi", SIMU_SENSOR_TYPE_TEMP_HUMI);
    simu_sensor_create_default("gas",       SIMU_SENSOR_TYPE_GAS);
    simu_sensor_create_default("light",     SIMU_SENSOR_TYPE_ILLUMINANCE);
    simu_sensor_create_default("pressure",  SIMU_SENSOR_TYPE_PRESSURE);

    simu_sensor_set_time_scale(600.0f);                  /* 加速 600 倍，便于观察昼夜变化 */
}

void sensor_publish_loop(void)
{
    SimuSensorData data[SIMU_SENSOR_MAX_NUM];
    rt_uint32_t    count;
    rt_uint32_t    index;

    while (1)
    {
        count = simu_sensor_read_all(data, SIMU_SENSOR_MAX_NUM);   /* 一次读取全部传感器 */

        for (index = 0; index < count; index++)
        {
            /* 组装 JSON 并通过 MQTT 上报 data[index] ... */
        }

        rt_thread_mdelay(5000);
    }
}
```

### 3.5 msh 命令

```
msh />simu_sensor list
msh />simu_sensor read all
msh />simu_sensor read temp_humi
msh />simu_sensor monitor gas 5 1000          # 连续采样 5 次，间隔 1s（强制采样，不受采样周期限制）
msh />simu_sensor fault gas out_of_range      # 故障注入: none|freeze|out_of_range|offline
msh />simu_sensor time 12                     # 把仿真时钟拨到 12:00
msh />simu_sensor scale 600                   # 仿真时间加速 600 倍
msh />simu_sensor info
msh />simu_sensor demo                        # 创建 4 个示例传感器并采样 3 轮
```

典型输出：

```
msh />simu_sensor read all
temp_humi: type=temp_humi quality=good t=28800000ms temperature=29.72C humidity=50.9%RH
gas: type=gas quality=good t=28800000ms concentration=401ppm
light: type=illuminance quality=good t=28800000ms illuminance=1602lux
pressure: type=pressure quality=good t=28800000ms pressure=1013.24hPa
```

### 3.6 故障注入

| 模式 | 行为 | 读取结果 |
| -- | -- | -- |
| `SIMU_SENSOR_FAULT_NONE` | 正常 | `RT_EOK`，质量 `good` |
| `SIMU_SENSOR_FAULT_FREEZE` | 数值冻结不再变化 | `RT_EOK`，质量 `frozen` |
| `SIMU_SENSOR_FAULT_OUT_OF_RANGE` | 输出量程上限的 110% | `RT_EOK`，质量 `out_of_range` |
| `SIMU_SENSOR_FAULT_OFFLINE` | 模拟通讯失败 | `-RT_ERROR`，不填充数据 |

用于验证上层（告警、上报、界面）对异常数据的处理，`SimuSensorData.quality` 可用于区分数据质量。

### 3.7 使用示例（samples）

`include/samples/` 提供覆盖「初始化 → 获取信息 → 设置参数 → 销毁」的完整示例，随软件包一起编译，
通过 msh 命令运行（详见 `include/samples/README.md`）：

| 命令 | 说明 |
| -- | -- |
| `simu_sensor_sample_init` | 静态对象、动态创建、自定义通道、批量创建 |
| `simu_sensor_sample_info [name]` | 注册表遍历/查找、通道参数、采样数据、环境与时钟 |
| `simu_sensor_sample_config [name]` | 采样周期、通道参数、故障注入、环境模型、时钟、种子 |
| `simu_sensor_sample_destroy [name\|all]` | 按名称/按前缀批量销毁、销毁后验证 |
| `simu_sensor_sample_mqtt [stop\|status]` | 配置并启动 MQTT 上报（依赖 paho + cJSON） |
| `simu_sensor_sample_all` | 一键依次执行以上示例 |

不需要示例参与编译时，定义 `PKG_SIMULATED_SENSOR_SAMPLE_DISABLE` 即可。

## 4 MQTT 上报（paho-mqtt）

`simu_sensor_mqtt` 基于 `packages/pahomqtt-latest` 软件包，周期读取全部模拟传感器、组装 JSON 后
发布到 MQTT 服务器（默认使用本工程的 EMQX 服务器 `tcp://47.93.225.100:1883`，用户名/密码 `USER1/USER1`）。

### 4.1 两种上报模式

**聚合模式**（默认，`SIMU_SENSOR_MQTT_PUBLISH_SINGLE`）——所有传感器一条报文，发布到 `simu_sensor/data`：

```json
{"device":"rtthread-simu","ts":28841090,"hour":8.01,"ambient":29.02,"count":4,
 "sensors":{"temp_humi":{"temperature":27.48,"humidity":50.9},
            "gas":{"concentration":399},
            "light":{"illuminance":1209},
            "pressure":{"pressure":1013.25}}}
```

数据质量正常时不输出 `quality` 字段，异常（冻结/超量程）时才会带上，便于上层告警：

```json
{"gas":{"quality":"frozen","concentration":391}}
```

**分主题模式**（`SIMU_SENSOR_MQTT_PUBLISH_PER_SENSOR`）——每个传感器一条报文，发布到 `simu_sensor/data/<名称>`：

```json
{"device":"rtthread-simu","name":"gas","type":"gas","quality":"good","ts":28871400,
 "values":{"concentration":402}}
```

### 4.2 接口

| 分类 | 接口 |
| -- | -- |
| 运行控制 | `simu_sensor_mqtt_start()` / `simu_sensor_mqtt_stop()` / `simu_sensor_mqtt_publish_once()` |
| 状态查询 | `simu_sensor_mqtt_is_started()` / `simu_sensor_mqtt_is_connected()` / `simu_sensor_mqtt_get_publish_count()` / `simu_sensor_mqtt_get_fail_count()` |
| 配置（需停机） | `simu_sensor_mqtt_set_uri()` / `_set_topic()` / `_set_client_id()` / `_set_credentials()` / `_set_interval()` / `_set_publish_mode()` / `_set_auto_create()` |

启动时会自动创建示例传感器（温湿度/气体/光照/大气压，可用 `simu_sensor_mqtt_set_auto_create()` 关闭），
因此单条命令就能跑通「模拟数据 → JSON → MQTT」链路。

### 4.3 msh 命令

```
msh />simu_sensor_mqtt status                      # 查看配置与统计
msh />simu_sensor_mqtt interval 2000               # 2s 上报一次
msh />simu_sensor_mqtt start                       # 开始上报
msh />simu_sensor_mqtt pub                         # 立即上报一帧
msh />simu_sensor_mqtt mode per_sensor             # 切换为分主题模式
msh />simu_sensor_mqtt stop                        # 停止上报
msh />simu_sensor_mqtt uri tcp://192.168.1.100:1883
msh />simu_sensor_mqtt user USER1 USER1
```

典型运行输出：

```
msh />simu_sensor_mqtt start
[simu_mqtt] created 4 demo sensor(s): temp_humi/gas/light/pressure
[simu_mqtt] started: uri=tcp://47.93.225.100:1883 topic=simu_sensor/data interval=2000ms mode=single
[D/mqtt] HOST = '47.93.225.100'
[I/mqtt] MQTT server connect success.
[simu_mqtt] connected to tcp://47.93.225.100:1883
[simu_mqtt] pub #1 simu_sensor/data (306 bytes)
[simu_mqtt] pub #2 simu_sensor/data (306 bytes)
msh />simu_sensor_mqtt stop
[simu_mqtt] publish thread exit, 12 published / 0 failed
[D/mqtt] pub_sock recv 11 byte: DISCONNECT
[I/mqtt] MQTT server is disconnected.
[simu_mqtt] stopped, 12 published / 0 failed
```

### 4.4 报文长度限制（重要）

paho-mqtt 的 RT-Thread 移植版采用「管道 + 独立线程」发送，管道长度固定为 `RT_PIPE_BUFSZ`（默认
**512 字节**），超过该长度的消息会被静默截断并导致 paho 线程解析异常（实测会出现 data abort）。
本模块的处理方式：

- 发布前检查「结构头 + 负载 + 主题 + 结束符」总长度，超过安全上限（512 - 32）时：
  - 聚合模式自动降级为按传感器分别上报，并打印一次告警；
  - 分主题模式下该条报文计为失败，不会发送；
- 聚合报文实际约 300 字节（4 个传感器、每通道 1 个数值），正常情况下可容纳约 8 个传感器；
- 需要更大报文时，可在工程中定义 `RT_PIPE_BUFSZ`（例如 2048）后重新编译 paho 软件包，
  同时可调整 `SIMU_SENSOR_MQTT_PIPE_SIZE`。

另外，上报采用**阻塞发布**（`MQTT_CTRL_PUBLISH_BLOCK`）：`published` 计数表示已真正发出的报文条数；
停止时管道中不会残留待发消息，`DISCONNECT` 命令可被 paho 单独读取，从而干净地退出客户端线程。

## 5 命名约定

- 函数：全部下划线命名，统一前缀 `simu_sensor_` / `simu_sensor_random_` / `simu_sensor_mqtt_`；
- 结构体/枚举/类型：首字母大写的驼峰命名，如 `SimuSensor`、`SimuSensorChannel`、`SimuSensorData`、
  `SimuSensorRandom`、`SimuSensorRandomDist`、`SimuSensorMqttConfig`；
- 宏与枚举常量：全大写，如 `SIMU_SENSOR_TYPE_TEMP_HUMI`、`SIMU_SENSOR_MQTT_PUBLISH_SINGLE`；
- 文件内静态函数：`random_` / `simu_sensor_*_locked` 前缀，不对外暴露。

## 6 编译说明

软件包由 `packages/simulated_sensor/SConscript` 自动收集源码：`src/core/*.c` 随
`PKG_USING_SIMULATED_SENSOR` 编译；`src/paho_mqtt/*.c` 额外依赖 `PKG_SIMULATED_SENSOR_MQTT`、
`PKG_USING_PAHOMQTT` 与 `PKG_USING_CJSON`（本工程均已开启），未开启时该目录自动跳过；
`include/samples/*.c` 默认参与编译，定义 `PKG_SIMULATED_SENSOR_SAMPLE_DISABLE` 可关闭。
msh 命令部分由 `RT_USING_FINSH` 保护。

## 7 验证情况

- **随机数模块**：48 项宿主端统计检查全部通过（取值范围、均值、卡方均匀性、正态分布均值/标准差/
  分位数概率、自相关、指数分布参数、可复现性、边界输入等）。
- **传感器对象**：70 项宿主端逻辑检查全部通过（注册表增删查、8 种类型预设、20000 次采样量程与
  分辨率、采样周期缓存与强制采样、分布统计、环境耦合（+10℃ → 温度 +6℃/湿度 -12%RH）、
  昼夜变化（正午 1585 lux / 午夜 41 lux）、4 种故障注入、气体脉冲、自定义通道、种子复现、
  `read_all` 与离线跳过、仿真时钟倍率等）。
- **交叉编译**：`scons -j4` 全工程编译链接通过。
- **QEMU 实机运行**：在 vexpress-a9 上通过 msh 命令实际运行（`simu_sensor list/read/monitor/fault/time/demo/info`），
  输出与宿主端逻辑一致（如 08:00 时刻昼夜因子 0.5、环境温度 29.00℃；故障注入后质量标志与返回值符合预期）。
- **MQTT 端到端**：QEMU 中通过 DHCP 接入网络后连接本工程的 EMQX 服务器
  （`tcp://47.93.225.100:1883`），聚合模式连续上报 12 条、分主题模式连续上报 28 条，均为 0 失败；
  用独立 MQTT 订阅端订阅 `simu_sensor/#` 实测收到全部报文，例如：

  ```
  topic=simu_sensor/data {"device":"rtthread-simu","ts":28845350,"hour":8.01,"ambient":29.02,"count":4,
                          "sensors":{"temp_humi":{"temperature":27.72,"humidity":50.7}, ...}}
  topic=simu_sensor/data/gas {"device":"rtthread-simu","name":"gas","type":"gas","quality":"good",
                              "ts":28871400,"values":{"concentration":402}}
  ```

  停止命令后 paho 客户端线程正常退出（日志中可见 `DISCONNECT` 被 paho 读取并断开）。
- **使用示例**：`include/samples/` 的示例已编译进固件并在 QEMU 上逐条实测通过——
  `simu_sensor_sample_init`（6 个对象：静态温湿度、动态气体、自定义 ADC、批量光照/气压/噪声）、
  `simu_sensor_sample_info`（模块/通道/数据/缓存值输出正常，`read_all` 一次读出 10 个传感器）、
  `simu_sensor_sample_config`（周期与通道参数生效，`min>=max` 被拒，故障注入输出 `out_of_range`/
  `frozen`/`-RT_ERROR`）、`simu_sensor_sample_destroy`（默认只清理 `sample_*`，`all` 清理全部）、
  `simu_sensor_sample_mqtt`（启动后连续上报 3 条到 EMQX，0 失败，停止时干净退出）、
  `simu_sensor_sample_all`（一键跑完整流程）。
