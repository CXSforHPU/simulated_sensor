# 模拟传感器使用示例

本目录提供 `simulated_sensor` 软件包的使用示例，覆盖「初始化 → 获取信息 → 设置参数 → 销毁」的完整流程。
示例源码随软件包一起编译（可用 `PKG_SIMULATED_SENSOR_SAMPLE_DISABLE` 关闭），通过 msh 命令运行，
每一条命令对应一个示例，可反复执行。

| 命令 | 说明 |
| -- | -- |
| `simu_sensor_sample_init` | 初始化：静态对象、动态创建、自定义通道、批量创建，并读一次数据 |
| `simu_sensor_sample_info [name]` | 获取信息：注册表遍历/查找、通道参数、采样数据、缓存值、环境与仿真时钟 |
| `simu_sensor_sample_config [name]` | 设置参数：采样周期、通道物理参数、故障注入、环境模型、时钟、随机种子 |
| `simu_sensor_sample_destroy [name\|all]` | 销毁：按名称销毁、按前缀批量销毁、`all` 销毁全部，并验证销毁结果 |
| `simu_sensor_sample_mqtt [stop\|status]` | （依赖 paho-mqtt + cJSON）配置并启动 MQTT 上报 |
| `simu_sensor_sample_all` | 一键依次执行以上示例 |

## 1 一键演示

```
msh />simu_sensor_sample_all

########## 模拟传感器示例: 初始化 -> 信息 -> 参数 -> 销毁 ##########

===== 示例1: 初始化模拟传感器 =====
[1] 静态初始化: sample_th, 类型=temp_humi, 通道数=2
[2] 动态创建: sample_gas, 类型=gas
[3] 自定义通道: sample_adc, adc_raw 量程 0 ~ 4095 LSB
    固定随机种子: 0x000007ea
[4] 批量创建: sample_light (类型 illuminance)
...
===== 示例3: 设置传感器参数 =====
[1] 采样周期设置为 3000 ms
[2] 修改通道 temperature: 基线 25.00 -> 30.00, 噪声 0.12 -> 0.50, 分辨率 0.10, 环境耦合 +1.0/℃
[3] 传入 min >= max 返回 -22 (-RT_EINVAL=-22)
[4] 环境温度=34.0 C, 时刻=12.00 时, 时间倍率 60
[5] 固定种子 0x0135282f 后采样: 39.0 (复位后重新开始)
[6] 故障注入演示:
    超量程: 质量=out_of_range 97.5 (量程上限 85)
    数据冻结: 质量=frozen 97.5
    离线读取: 返回 -255 (-RT_ERROR)
[7] 已恢复正常模式并复位(故障模式=none)

===== 示例4: 销毁虚拟传感器 =====
[1] 销毁本示例创建的传感器(前缀 sample_):
    跳过 temp_humi      (非示例对象)
    销毁 sample_th      (类型 temp_humi)
...
[2] 销毁后注册数量=0
```

## 2 关键接口速查

初始化：

```c
static SimuSensor sensor;                                              /* 静态对象 */
simu_sensor_init(&sensor, "th", SIMU_SENSOR_TYPE_TEMP_HUMI);           /* 按类型预设初始化 */

SimuSensor *gas = simu_sensor_create_default("gas", SIMU_SENSOR_TYPE_GAS); /* 动态创建(rt_malloc) */

SimuSensor *adc = simu_sensor_create("adc", SIMU_SENSOR_TYPE_CUSTOM);  /* 自定义通道 */
SimuSensorChannel channel = *simu_sensor_get_channel(adc, 0);
channel.name = "adc_raw"; channel.unit = "LSB";
channel.min = 0.0f; channel.max = 4095.0f; channel.base = 2048.0f;
simu_sensor_set_channel(adc, 0, &channel);
```

获取信息：

```c
rt_uint32_t count = simu_sensor_count();                  /* 注册数量 */
SimuSensor *sensor = simu_sensor_find("th");              /* 按名称查找 */
SimuSensor *first  = simu_sensor_get(0);                  /* 按下标遍历 */

const SimuSensorChannel *ch = simu_sensor_get_channel(sensor, 0);  /* 通道参数: 名称/单位/量程... */

SimuSensorData data;
simu_sensor_read(sensor, &data);                          /* 按采样周期读取 */
simu_sensor_sample(sensor, &data);                        /* 强制重新采样 */
simu_sensor_read_by_name("gas", &data);                   /* 按名称读取 */

SimuSensorData all[SIMU_SENSOR_MAX_NUM];
rt_uint32_t n = simu_sensor_read_all(all, SIMU_SENSOR_MAX_NUM);    /* 一次读取全部 */

float last = simu_sensor_value(sensor, 0);                /* 最近一次值(不触发采样) */

simu_sensor_type_name(sensor->type);                      /* 类型名 */
simu_sensor_quality_name(data.quality);                   /* 质量名 */
simu_sensor_ambient_temperature();                        /* 环境温度 */
simu_sensor_time_of_day();                                /* 仿真时刻 */
```

设置参数：

```c
simu_sensor_set_period(sensor, 3000);                     /* 采样周期(ms), 0 = 每次读取都重新采样 */
simu_sensor_set_channel(sensor, 0, &channel);             /* 量程/基线/噪声/漂移/分辨率/耦合/脉冲 */
simu_sensor_set_seed(sensor, 20260911U);                  /* 固定种子, 序列可复现 */
simu_sensor_reset(sensor);                                /* 清除漂移与缓存, 序列从头开始 */
simu_sensor_set_fault(sensor, SIMU_SENSOR_FAULT_FREEZE);  /* 故障注入 */

simu_sensor_set_ambient(30.0f, 4.0f);                     /* 环境温度模型: 基线 ± 幅度 */
simu_sensor_set_time_of_day(12.0f);                       /* 仿真时钟: 拨到正午 */
simu_sensor_set_time_scale(60.0f);                        /* 仿真时间加速 60 倍 */
```

销毁：

```c
simu_sensor_deinit(&sensor);                              /* 注销(静态对象) */
simu_sensor_destroy(sensor);                              /* 注销 + 释放(动态对象; 静态对象也安全) */
```

## 3 注意事项

- 示例创建的传感器统一以 `sample_` 开头，`simu_sensor_sample_destroy` 默认只清理这些对象，
  不会影响业务代码创建的传感器；需要全部清理时使用 `simu_sensor_sample_destroy all`。
- 遍历注册表并删除对象时，删除后不要递增下标（后面的对象会前移），示例中演示了这种安全写法。
- `simu_sensor_destroy()` 对静态对象同样安全：只从注册表注销，不释放内存（内存不属于模块）。
- klibc 的精简版 printf 不支持 `%f`，示例统一使用 `simu_sensor_value_to_string()` 打印浮点数值。
- 若不需要示例参与编译，在工程中定义 `PKG_SIMULATED_SENSOR_SAMPLE_DISABLE` 即可。
