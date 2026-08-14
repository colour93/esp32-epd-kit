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

```bash
pio run -e esp32dev
pio run -e esp32dev_release
```

默认面板为 `GxEPD2_213_B74`，逻辑画布为 250x122。项目使用 `huge_app.csv`，不支持 OTA。调试串口为 `115200`。

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

## 串口恢复

```bash
pio device monitor -b 115200
```

```text
status
io12 disable
restart
factory-reset prepare
factory-reset confirm <code>
```

Factory reset 清除 v4 配置、资源、安全状态和全部 BLE bonds。GPIO12 同时是 ESP32 boot-strapping pin；若硬件持续拉高导致设备无法启动，需先修复电路或复位时拉低 GPIO12。

## 权威文档

- [v4 架构](docs/architecture_v4.md)
- [BLE Protocol v4 Host Implementation Guide](docs/ble_protocol_v4.md)
- [功能组件开发规范](docs/feature_component_development.md)
- [Codex Rate Limits Schema 与 Producer](docs/openai_codex_usage.md)
- [通用 CLI + JMESPath 数据源](docs/generic_cli.md)
- [Desktop Agent 与 Web 工作台](../esp32-epd-kit-web/README.md)
