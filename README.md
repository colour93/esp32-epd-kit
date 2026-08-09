# ESP32 E-Paper Toolkit

面向 Waveshare 2.13inch e-Paper Cloud Module V4 的 PlatformIO + Arduino 低功耗墨水屏运行时。首个静态应用显示 Codex 5h/7d 剩余额度。

## 硬件

| 功能 | GPIO |
|---|---:|
| EPD SCK | 13 |
| EPD MOSI | 14 |
| EPD CS | 15 |
| EPD BUSY | 25 |
| EPD RST | 26 |
| EPD DC | 27 |
| KEY（低有效） | 12 |
| BAT ADC（VBAT/3） | 36 |

默认屏幕类为 `GxEPD2_213_B74`，逻辑画布为 250×122 横屏。若实物 FPC 明确标记 `GDEY0213B74`，可在 `DisplayManager` 中替换为 GxEPD2 对应 GDEY 类，显示接口和刷新策略不变。

## 架构

每次唤醒只运行一遍状态机：

```text
配置/电池 → 模式判断 → Wi-Fi/SNTP → 应用更新 → LVGL I1
        → 脏矩形/全刷策略 → EPD hibernate → Wi-Fi/BLE off → deep sleep
```

- LVGL 9.5.0 负责 UI，使用 I1、16 行 draw buffer、无动画/阴影/渐变。
- GxEPD2 1.6.9 驱动 SSD1680，并显式写 previous/current RAM 做差分局刷。
- NimBLE-Arduino 2.5.1 提供加密、MITM、bonding 的配置服务。
- ArduinoJson 7.4.3 处理配置、BLE NDJSON 和额度响应。
- 配置使用 NVS 双槽 + CRC + active marker；成功额度快照每小时最多写一次。
- RTC memory 保存旧逻辑帧、局刷计数、失败次数和临界电量锁止状态。
- Wi-Fi 可选 DHCP 或手动 IPv4/网关/子网掩码/DNS，均通过 BLE 暂存、测试后原子提交。
- Codex 可选 HTTP CONNECT 代理和 Basic Authentication；tunnel 内仍严格校验 `chatgpt.com` 的 TLS 证书与主机名。

## 构建和测试

```bash
pio test -e native
pio run -e esp32dev
pio run -e esp32dev_release
```

固件使用 `huge_app.csv` 单应用分区，因为项目不支持 OTA，而默认 1.25 MiB 应用分区不足以容纳 LVGL、NimBLE 和 TLS。上传端口可在 `platformio.ini` 中按本机调整。

### 串口调试

Debug 环境使用 115200 baud，Toolkit 自身日志统一带 `[toolkit]` 前缀，且不会输出 Wi-Fi 密码、Token、Authorization 或响应正文：

```bash
pio run -e esp32dev -t upload
pio device monitor -e esp32dev
```

`esp32dev` 同时启用 Arduino Core Info 日志；ADC 校准、NimBLE 启停等消息没有 `[toolkit]` 前缀。`esp32dev_release` 设置 `CORE_DEBUG_LEVEL=0` 并编译掉 Toolkit 串口日志。

## 按键和功耗模式

- 短按/按键唤醒：立即执行正常查询。
- 长按 3 秒：进入 BLE 配置窗口。
- 长按 10 秒：进入恢复出厂预备 BLE 会话；仍必须完成 nonce 和物理确认。
- Token 到期或 HTTP 401：停止定时无线电唤醒，只保留 KEY 唤醒。
- 电池低于 `critical_mv`：无线电锁止并睡眠 6 小时；达到 `recovery_mv` 才恢复。

GPIO12 是 ESP32 启动绑带。代码不在深睡期间保持 RTC 上拉，依赖开发板现有按键电路提供空闲电平；实机验收必须覆盖按住/释放按键的冷启动和深睡唤醒。

## 文档

- [BLE Protocol](docs/ble_protocal.md)
- [Codex Usage App](docs/openai_codex_usage.md)

## 安全与接口稳定性

Codex 应用调用 `https://chatgpt.com/backend-api/wham/usage`，这是非公开 ChatGPT Backend API，不是 OpenAI Platform 的稳定公开接口。固件固定域名、拒绝重定向、强制 TLS 验证，并限制响应为 16 KiB，但接口仍可能随时变化。

代理仅支持 Codex HTTPS 的 HTTP CONNECT，不代理 Wi-Fi、DHCP、DNS 或 SNTP，也不支持 HTTPS proxy、SOCKS 或 PAC。代理能观察连接目标和流量元数据；Basic 代理凭据在 CONNECT 控制连接上不加密，因此只能使用可信局域网/VPN 内的代理。Codex Token 和 API 正文始终位于 tunnel 内的 TLS 中。

首版是原型安全级别：BLE 凭据传输加密、读取脱敏，但 NVS 未启用 Flash Encryption，且不包含 Secure Boot、OTA、OAuth 登录或 refresh token。不得将其直接视为生产级秘密存储。

## 尚需实机验收

构建和主机逻辑测试不能替代硬件测量。发布前必须在 USB 拔除、电池供电下验证：

- 深睡、无线电关闭且 EPD hibernate 后整板不高于 0.5 mA。
- RSSI 高于 -65 dBm、接口 1 秒内响应时，两小时平均不高于 3 mA。
- 成功查询后至少 95% 周期在 8 秒内回到深睡。
- MTU 23/185/247、4 KiB Token、TLS/HTTP 错误和墨水屏局刷边界。
- DHCP/static 往返切换、静态 DNS、无认证/Basic CONNECT 代理、407/超时及代理后的 TLS 主机名校验。
- ADC 三分压换算和电池阈值的板级校准。

若功耗不达标，应分别测量板上升压、电池 ADC 常开分压和电源链路，而不是用 ESP32 芯片理论深睡电流代替整板结果。
