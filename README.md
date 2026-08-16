# ESP32 E-Paper Toolkit

Waveshare 2.13inch e-Paper Cloud Module V4 的 BLE-only 固件。固件 `0.3.x` 使用 BLE Protocol v4 接收版本化语义 Resource，并由 Page 编排多个 Resource Slot 与可复用 Widget。

```text
Desktop Agent -> BLE v4 -> ResourceStore -> PageResources -> Model -> Widget
                                      RuntimeContext -------> Timed Region
```

ESP32 不连接 Wi-Fi 或云服务，也不保存 Codex、CLI 或其他服务凭据。

## 当前功能

- `home` / `home.three`：每分钟时钟与 2/3 组件布局，支持通用数值、条形和环形组件；
- `codex.usage` Page：Codex 完整额度页；
- fixed-capacity `PageRegistry` 与严格 Page/Slot/Binding 校验；
- Resource missing/invalid/stale/fresh 状态；
- framebuffer diff、dirty rect、局刷/全刷策略和无变化抑制；
- 电池模式时钟 RTC 快路径，不启动 BLE、不加载 Resource snapshot；
- LE Secure Connections、MITM、bonding、owner/trusted 权限。

默认配置使用 `home`，绑定 `primary -> codex/default`。全刷策略为 60 次局刷、24 小时或 70% dirty area。

## v4 升级

v4 不兼容 v3，不提供 migration。固件只使用 `epd_cfg4`、`epd_res4`、`epd_sec4`；首次没有有效 v4 配置时会清除 NimBLE bonds 并创建新设备配置。升级后需在操作系统删除旧配对并重新配对。

## 构建

默认面板为 `GxEPD2_213_B74`。`esp32c3_e029a01` 环境支持 ESP32-C3
开发板与微雪
2.9 寸黑白屏 E029A01，驱动为 `GxEPD2_290`。逻辑画布分别使用面板的
250x122 和 296x128 真实分辨率；页面保持相同结构，并按画布尺寸重新计算
全屏、两分栏和三分栏布局。
项目使用 `huge_app.csv`，不支持 OTA。调试串口为 `115200`。

```bash
# 2.13 寸 GDEM0213B74
pio run -e esp32dev
pio run -e esp32dev_release

# 2.9 寸 E029A01
pio run -e esp32c3_e029a01
pio run -e esp32c3_e029a01_release
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

E029A01 环境使用以下接线：

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

`esp32c3_e029a01` 使用 PlatformIO 的 `airm2m_core_esp32c3` board。
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
restart
factory-reset prepare
factory-reset confirm <code>
```

`setup` 通过物理串口打开 120 秒新主机绑定窗口，屏幕进入配置模式，串口同时输出六位 BLE passkey。窗口期间设备保持快速广播；Windows Agent 若检测到本机残留的陈旧配对，会在首次安全握手失败后自动重新配对。

Factory reset 清除 v4 配置、资源、安全状态和全部 BLE bonds。GPIO12 同时是 ESP32 boot-strapping pin；若硬件持续拉高导致设备无法启动，需先修复电路或复位时拉低 GPIO12。

## 权威文档

- [v4 架构](docs/architecture_v4.md)
- [BLE Protocol v4 Host Implementation Guide](docs/ble_protocol_v4.md)
- [功能组件开发规范](docs/feature_component_development.md)
- [Codex Rate Limits Schema 与 Producer](docs/openai_codex_usage.md)
- [通用 CLI + JMESPath 数据源](docs/generic_cli.md)
- [通用 HTTP + JMESPath 数据源](docs/generic_http.md)
- [CC Switch 今日用量源](../esp32-epd-kit-web/docs/cc_switch_usage.md)
- [Desktop Agent 与 Web 工作台](../esp32-epd-kit-web/README.md)
