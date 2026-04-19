# Smart Buddy

将 ESP32-S3-BOX-3 变成 AI Agent 的实体伴侣设备。触摸屏实时显示 Agent 的工作状态，支持点击屏幕批准或拒绝工具调用请求，通过 BLE、USB 或 WebSocket 与 Agent 通信。

灵感来自 [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)。支持三种协议模式，可在 `menuconfig` 中选择：**Claude Buddy**（与 claude-desktop-buddy 直接兼容）、**OpenClaw** 和 **Hermes**。

[English](README.md)

---

## 硬件要求

| 组件 | 规格 |
|------|------|
| 开发板 | ESP32-S3-BOX-3 |
| 显示屏 | 2.4" 320×240 ILI9341，GT911 电容触摸 |
| 音频 | ES7210 麦克风阵列 + ES8311 扬声器编解码 |
| IMU | ICM-42670-P（摇晃 / 翻面检测） |
| 按键 | 3 个实体按键 |
| 存储 | 16 MB Flash + 8 MB PSRAM |

---

## 前置条件

- **ESP-IDF v5.4 或 v6.0** — 参考 [Espressif 快速入门](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/)
- Python 3.8+、CMake 3.24+、Ninja（IDF 安装脚本一并安装）
- USB-C 数据线连接到 BOX-3 的 JTAG/UART 口

### 安装 ESP-IDF（仅首次）

```bash
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
git checkout v6.0          # 或 v5.4
./install.sh esp32s3
```

---

## 快速上手

### 1. 克隆仓库

```bash
git clone https://github.com/yourorg/smart-buddy.git
cd smart-buddy
```

### 2. 激活 ESP-IDF 环境

```bash
source ~/esp-idf/export.sh
```

> 每次新开终端都需要执行此命令。可以将其加入 shell 配置文件（`.bashrc` / `.zshrc`）。

### 3. 配置

```bash
idf.py menuconfig
```

进入 **Smart-Buddy Configuration** 菜单，重点配置以下项目：

| 配置项 | 路径 | 默认值 |
|--------|------|--------|
| 协议模式 | Protocol Adapters → Buddy Protocol Mode | Claude Buddy |
| BLE 设备名 | Transport Layer → BLE Advertised Name | SmartBuddy |
| WebSocket 服务器地址 | Transport Layer → Default WebSocket server URL | ws://192.168.1.100:8080/buddy |
| Wi-Fi SSID | Wi-Fi → Default Wi-Fi SSID | （空） |
| Wi-Fi 密码 | Wi-Fi → Default Wi-Fi Password | （空） |

按 `S` 保存，按 `Q` 退出。

### 4. 编译

```bash
idf.py build
```

首次编译会自动下载托管组件（约 200 MB），耗时 3–5 分钟。后续增量编译很快。

### 5. 烧录并查看日志

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

根据操作系统替换串口号：

| 系统 | 典型串口 |
|------|--------|
| Linux | `/dev/ttyACM0` |
| macOS | `/dev/tty.usbmodem*` |
| Windows | `COM3`（在设备管理器中查看） |

设备启动后先显示 Logo 画面，随后切换到主伴侣界面。

---

## 接入 Agent

三种传输方式均使用相同的**换行符分隔 JSON** 帧格式，可以同时连接多个传输通道，设备会向所有已连接的传输广播输出消息。

### USB（最简单，即插即用）

设备启动即呈现为 USB 串口设备，无需配网或配对。

```bash
# 发送心跳帧
echo '{"running":1,"waiting":0,"tokens":1234}' > /dev/ttyACM0

# 持续接收响应
cat /dev/ttyACM0
```

### BLE

设备以 **SmartBuddy** 为名广播蓝牙，采用 Nordic UART Service（NUS）。使用任意 BLE 串口 App（nRF Connect、LightBlue 等）连接后，向 RX 特征值写入 JSON 帧即可。

### WebSocket

在 menuconfig 中填写服务器地址和 Wi-Fi 凭据，设备启动后自动连接，断线后指数退避自动重连。

---

## 协议模式

在 menuconfig → **Protocol Adapters → Buddy Protocol Mode** 中选择。协议模式在编译期确定，切换后重新编译烧录即可。

### Claude Buddy（默认）

与 [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) 线协议完全兼容。

```jsonc
// Agent → 设备：心跳
{"running": 2, "waiting": 1, "tokens": 8500}

// Agent → 设备：工具调用许可请求
{"id": "abc123", "tool": "bash", "description": "执行 shell 命令"}

// 设备 → Agent：许可响应
{"decision": "allow_once", "id": "abc123"}
// 或拒绝：{"decision": "deny", "id": "abc123"}
```

### OpenClaw

```jsonc
// Agent → 设备：心跳
{"total": 3, "running": 2, "tokens": 8500}

// Agent → 设备：许可请求
{"prompt": {"id": "abc123", "tool": "bash", "hint": "执行 shell 命令"}}

// 设备 → Agent：许可响应
{"cmd": "permission", "id": "abc123", "decision": "once"}
```

### Hermes

字段名称可在编译时通过 `proto_hermes_cfg_t` 配置，适配不同的字段映射。详见 [docs/protocol.md](docs/protocol.md)。

---

## 设备操作

| 操作 | 功能 |
|------|------|
| 左键长按 | 打开状态屏（连接状态、Token 统计、剩余堆内存） |
| 右键长按 | 打开设置屏 |
| 触摸 **批准** | 授权当前待处理的工具调用请求 |
| 触摸 **拒绝** | 拒绝当前待处理的工具调用请求 |

**IMU 手势：**

| 手势 | 效果 |
|------|------|
| 摇晃 | 晕眩动画（2 秒后恢复） |
| 屏幕朝下 | 屏幕熄灭/休眠 |
| 屏幕朝上 | 屏幕唤醒 |

---

## 设备状态

```
SLEEP（休眠）──连接──► IDLE（空闲）──Agent 工作──► BUSY（忙碌）
                          ▲                               │
                          │                       工具调用请求
                          │                               ▼
                       已处理 ◄────────────────── ATTENTION（等待审批）
                                                  （触摸批准/拒绝）
```

额外的短暂状态：**CELEBRATE**（Token 里程碑）、**DIZZY**（摇晃）、**HEART**（快速批准）。

---

## 项目结构

```
smart-buddy/
├── main/                   # app_main、Kconfig.projbuild
├── components/
│   ├── buddy_hal/          # 硬件抽象层接口（与板型无关）
│   ├── boards/
│   │   ├── esp32s3_box3/   # ESP32-S3-BOX-3 HAL 实现
│   │   └── esp32s3_devkit/ # 通用 ESP32-S3 最小存根实现
│   ├── transport/          # BLE / WebSocket / USB 传输层
│   ├── protocol/           # Claude Buddy / OpenClaw / Hermes 协议适配器
│   ├── agent_core/         # 中央事件队列与状态机驱动
│   ├── state_machine/      # 7 状态有限状态机
│   ├── ui/                 # LVGL 屏幕（启动、主界面、审批、状态、设置）
│   └── imu_monitor/        # 50 Hz 手势检测任务
├── assets/                 # LVGL 字体与 Avatar 图片
├── docs/                   # 深度技术文档
└── tools/                  # 图片转换器、版本头文件生成器
```

---

## 深度文档

| 文档 | 内容 |
|------|------|
| [docs/architecture.md](docs/architecture.md) | 组件架构图、FreeRTOS 任务表、完整状态机 |
| [docs/protocol.md](docs/protocol.md) | 三种协议模式的完整帧格式参考 |
| [docs/porting.md](docs/porting.md) | 如何为新板型添加支持 |

---

## 常见问题

**`idf.py: command not found`** — 先执行 `source ~/esp-idf/export.sh`。

**Linux 下串口提示权限拒绝** — 执行 `sudo usermod -aG dialout $USER` 后重新登录。

**BLE 初始化失败** — IDF 6.x 与部分芯片版本的 NimBLE 兼容性问题，不影响 USB 和 WebSocket 传输，两者均可正常使用。

**WebSocket 持续重连** — 未配置 Wi-Fi 时的正常行为，重连循环无害。在 menuconfig 或设备设置屏中填写 SSID/密码即可解决。

**屏幕 30 秒后变黑** — 自动熄屏计时器正常工作。触摸屏幕或按任意键唤醒。

---

## 开源许可

GPL-3.0
