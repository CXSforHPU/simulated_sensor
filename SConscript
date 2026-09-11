from building import *
import os
cwd = GetCurrentDir()

src = []
CPPPATH = []

#core path: 随机数模块 + 模拟传感器对象
core_src = os.path.join(cwd,'src','core')
core_include = os.path.join(cwd,'include','core')

#paho mqtt path: 模拟传感器数据上报(依赖 paho-mqtt 与 cJSON 软件包)
paho_mqtt_src = os.path.join(cwd,'src','paho_mqtt')
paho_mqtt_include = os.path.join(cwd,'include','paho_mqtt')

#samples path: 使用示例(默认参与编译, 定义 PKG_SIMULATED_SENSOR_SAMPLE_DISABLE 可关闭)
samples_src = os.path.join(cwd,'include','samples')

def collect_source(path):
    files = []
    if os.path.isdir(path):
        for file in os.listdir(path):
            file_path = os.path.join(path, file)
            if file.endswith('.c') and os.path.isfile(file_path):
                files.append(file_path)
    return files


#add core
src += collect_source(core_src)
CPPPATH.append(core_include)

#paho mqtt path: 模拟传感器数据上报(依赖 paho-mqtt 与 cJSON 软件包)
if GetDepend(['PKG_SIMULATED_SENSOR_MQTT']):
    src += collect_source(paho_mqtt_src)
    CPPPATH.append(paho_mqtt_include)

#samples: 初始化/获取信息/设置参数/销毁示例
if GetDepend(['PKG_SIMULATED_SENSOR_SAMPLE_ENABLED']):
    src += collect_source(samples_src)

group = DefineGroup('simulated_sensor',
                    src,
                    depend=["PKG_USING_SIMULATED_SENSOR"],
                    CPPPATH=CPPPATH)

Return('group')
