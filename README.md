# ESP32 E-Paper Toolkit

Waveshare E-Paper ESP32 固件。固件 `0.3.x` 使用 Protocol v4 接收版本化语义 Resource，并由 Page 编排多个 Resource Slot 与可复用 Widget；主机既可通过 BLE 连接，也可先用 USB-TTL 串口配置 WiFi，再通过局域网连接。

```text
Desktop Agent -> DeviceGateway -> BLE ---------+
                              -> WiFi / TCP ----+-> Protocol v4 -> ResourceStore
USB-TTL ------------------------> WiFi 配置                         |
                                      PageResources -> Model -> Widget
                                      RuntimeContext -------> Timed Region
```

ESP32 不访问云服务，也不保存 Codex、CLI 或其他服务凭据。配置 WiFi 后只在本地网络提供经设备密钥认证的 TCP 服务。

## 当前功能

- `home` / `home.three`：每分钟时钟与 2/3 组件布局；4.2 寸固件额外提供 `home.six` 两行三列布局；
- `codex.usage` Page：Codex 完整额度页；
- fixed-capacity `PageRegistry` 与严格 Page/Slot/Binding 校验；
- Resource missing/invalid/stale/fresh 状态；
- framebuffer diff、dirty rect、局刷/全刷策略和无变化抑制；
- 电池模式时钟 RTC 快路径，不启动 BLE、不加载 Resource snapshot；
- LE Secure Connections、MITM、bonding、owner/trusted 权限；
- USB-TTL 串口配网、`_epdkit._tcp.local.` mDNS 发现与 HMAC-SHA256 LAN 认证；
- BLE/LAN 共用 Protocol v4 RPC，且任一时刻只允许一个活动会话。

默认配置使用 `home`，绑定 `primary -> codex/default`。全刷策略为 60 次局刷、24 小时或 70% dirty area。

## v4 升级

v4 不兼容 v3，不提供 migration。固件只使用 `epd_cfg4`、`epd_res4`、`epd_sec4`；首次没有有效 v4 配置时会清除 NimBLE bonds 并创建新设备配置。升级后需在操作系统删除旧配对并重新配对。

## 构建

`esp32_2_13` 使用 `GxEPD2_213_B74`。`esp32_2_9` 和 `esp32c3_2_9`
分别支持经典 ESP32、ESP32-C3 开发板与微雪 2.9 寸黑白屏 E029A01，驱动为
`GxEPD2_290`。`esp32_4_2` 使用 uPesy ESP32 Wroom DevKit，支持
GDEY042T81/SSD1683
400x300 黑白屏，驱动为 `GxEPD2_420_GDEY042T81`。逻辑画布分别为
250x122、296x128 和 400x300；4.2 寸固件
额外注册两行三列的 6 组件主页。由于 400x300 帧超过 RTC 慢速内存容量，
4.2 寸固件深睡唤醒后重新渲染整页，不保留跨深睡的局刷帧。
该环境与参考项目一致，使用 pioarduino stable 平台和 `upesy_wroom` board。
项目使用 `huge_app.csv`，不支持 OTA。调试串口为 `115200`。

```bash
# 2.13 寸 GDEM0213B74
pio run -e esp32_2_13
pio run -e esp32_2_13_release

# 2.9 寸 E029A01
pio run -e esp32_2_9
pio run -e esp32_2_9_release
pio run -e esp32c3_2_9
pio run -e esp32c3_2_9_release

# 4.2 寸 400x300
pio run -e esp32_4_2
pio run -e esp32_4_2_release
```

## 硬件

| Function | GPIO |
|---|---:|
| EPD SCK | 13 |
| EPD MOSI | 14 |
| EPD CS | 15 |
| EPD BUSY | 25 |
| EPD RST | 26 |
| EPD DC | 27 |
| optional key | 12 |
| optional VBAT/3 ADC | 36 |

经典 ESP32 的 2.9 寸环境使用以下接线：

| Function | GPIO |
|---|---:|
| EPD SCK | 13 |
| EPD MOSI | 14 |
| EPD CS | 15 |
| EPD BUSY | 25 |
| EPD RST | 26 |
| EPD DC | 27 |
| optional key | 12 |
| optional VBAT/3 ADC | 36 |

经典 ESP32 的 4.2 寸 GDEY042T81 环境使用以下接线：

| Function | GPIO |
|---|---:|
| EPD SCK | 18 |
| EPD MOSI | 23 |
| EPD CS | 5 |
| EPD BUSY | 4 |
| EPD RST | 16 |
| EPD DC | 17 |
| optional key | 12 |
| optional VBAT/3 ADC | 36 |

ESP32-C3 的 E029A01 环境使用以下接线：

| Function | GPIO |
|---|---:|
| EPD SCK | 2 |
| EPD MOSI | 3 |
| EPD CS | 7 |
| EPD BUSY | 0 |
| EPD RST | 10 |
| EPD DC | 6 |
| BUTTON1 / user key | 4 |
| BUTTON2 / reserved | 13 |

`esp32c3_2_9` 使用 PlatformIO 的 `airm2m_core_esp32c3` board。
GPIO4 的 BUTTON1 采用外部 10K 下拉、按下接 3.3V，高电平触发现有用户按键
及深睡唤醒；GPIO13 的 BUTTON2 暂未绑定业务功能。该硬件配置没有提供电池
ADC，因此电量采样在此环境中停用。

## 串口恢复

```bash
pio device monitor -b 115200
```

```text
status
setup
io12 disable
wifi status
wifi ssid <value>
wifi password <value>
wifi apply
wifi key
wifi forget
restart
factory-reset prepare
factory-reset confirm <code>
```

`setup` 通过物理串口打开 120 秒新主机绑定窗口，屏幕进入配置模式，串口同时输出六位 BLE passkey。窗口期间设备保持快速广播；Windows Agent 若检测到本机残留的陈旧配对，会在首次安全握手失败后自动重新配对。

不支持 BLE 的主机按以下顺序配置：

1. 使用 **3.3V 电平** USB-TTL 连接设备 TX/RX/GND，以 `115200 8N1` 打开串口；
2. 执行 `wifi ssid <SSID>`、`wifi password <密码>` 和 `wifi apply`；开放网络可执行空值的 `wifi password`；
3. 用 `wifi status` 确认 IP，用 `wifi key` 读取 `device_id` 与 64 位 `device_key`；
4. 在 Agent 工作台选择 WiFi，扫描 `_epdkit._tcp.local.`，首次连接时输入 `device_key`。

LAN 服务固定监听 TCP `38474`，mDNS TXT 包含 `id`、`name`、`proto=4` 和 `fw`。USB-TTL 只负责首次配网和恢复，日常数据传输走 WiFi。配置 WiFi 会增加常驻功耗，电池配置应结合唤醒周期评估。

Factory reset 清除 v4 配置、资源、WiFi/LAN 密钥、安全状态和全部 BLE bonds；`wifi forget` 只清除 WiFi SSID/密码，保留 LAN 设备密钥。GPIO12 同时是 ESP32 boot-strapping pin；若硬件持续拉高导致设备无法启动，需先修复电路或复位时拉低 GPIO12。

## 权威文档

- [v4 架构](docs/architecture_v4.md)
- [BLE Protocol v4 Host Implementation Guide](docs/ble_protocol_v4.md)
- [LAN Transport for Protocol v4](docs/lan_transport_v4.md)
- [功能组件开发规范](docs/feature_component_development.md)
- [Codex Rate Limits Schema 与 Producer](docs/openai_codex_usage.md)
- [通用 CLI + JMESPath 数据源](docs/generic_cli.md)
- [通用 HTTP + JMESPath 数据源](docs/generic_http.md)
- [CC Switch 今日用量源](../esp32-epd-kit-web/docs/cc_switch_usage.md)
- [Desktop Agent 与 Web 工作台](../esp32-epd-kit-web/README.md)
