# Codex 额度查询应用

> 固件应用 ID：`codex_usage`  
> 实现状态：实验性  
> 最后核对官方资料：2026-08-09

## 1. 契约边界

OpenAI 官方文档确认 Codex 本地消息与云端任务共享五小时用量窗口，并说明可能存在额外周限额；当前用量可在 Codex usage dashboard 或会话 `/status` 查看。参考：[OpenAI Codex pricing and usage limits](https://developers.openai.com/codex/pricing/)。

官方文档没有发布供第三方设备查询个人 ChatGPT/Codex 剩余额度的稳定 API。本文使用的：

```http
GET https://chatgpt.com/backend-api/wham/usage
```

属于 ChatGPT 内部 Backend API。URL、认证方式、请求头和响应字段均可能无通知变化，不能视为 OpenAI Platform API 契约。固件将它隔离在 `CodexUsageClient` 中，以便通过未来固件版本替换；首版不提供 BLE 可改域名。

## 2. 固件请求

固定请求：

```http
GET /backend-api/wham/usage HTTP/1.1
Host: chatgpt.com
Authorization: Bearer <access_token>
chatgpt-account-id: <account_id>
openai-beta: codex-1
Accept: application/json
oai-language: <device.locale>
originator: Codex Desktop
User-Agent: esp32-epd-kit/<firmware-version>
```

安全约束：

- URL 和主机名编译进固件，BLE 不能修改。
- HTTP 3xx 不跟随，避免把 Token 发送到其他主机。
- TLS 使用 `WiFiClientSecure` 和内置受信任根证书校验，禁止 `setInsecure()`。
- 在 TLS 前必须完成 SNTP；系统时间无效时不发送 Token。
- 连接和读取超时约 10 秒。
- 响应边读边计数，最多 16 KiB；`Content-Length` 或实际流量超过上限均拒绝。
- Release 构建不记录 Authorization、account ID、Wi-Fi 密码或响应正文。

启用代理时，固件先连接配置的 HTTP proxy，发送：

```http
CONNECT chatgpt.com:443 HTTP/1.1
Host: chatgpt.com:443
Proxy-Authorization: Basic <可选>
```

只有代理返回 HTTP 200 后，固件才在该 tunnel 内对 `chatgpt.com` 建立 TLS。CA 和主机名校验与直连相同；代理返回 407、非 200、超时或超大响应头时不会发送 Codex Token。固件不接受代理提供的替换证书，也不因代理启用而调用 `setInsecure()`。

内置 CA 不是永久配置。证书链或根证书变化时必须发布固件更新；TLS 失败不能降级为不校验证书。

## 3. BLE 配置映射

配置位于：

```json
{
  "apps": {
    "codex_usage": {
      "account_id": "chatgpt-account-id",
      "access_token": "oauth-access-token",
      "expires_at": 1780000000,
      "proxy": {
        "enabled": false,
        "host": "",
        "port": 8080,
        "username": "",
        "password": ""
      }
    }
  }
}
```

| 配置字段 | 请求用途 | 约束 |
|---|---|---|
| `apps.codex_usage.account_id` | `chatgpt-account-id` | 必填，最多 128 bytes |
| `apps.codex_usage.access_token` | `Authorization: Bearer ...` | 必填，最多 4096 bytes |
| `apps.codex_usage.expires_at` | 本地到期预检 | Unix seconds；`0` 表示未知 |
| `apps.codex_usage.proxy.enabled` | 选择传输 | false 直连；true 使用 HTTP CONNECT |
| `apps.codex_usage.proxy.host` | 代理 TCP 目标 | 主机名或 IPv4，最多 253 bytes；不含 scheme/端口/路径 |
| `apps.codex_usage.proxy.port` | 代理 TCP 端口 | 1–65535，默认 8080 |
| `apps.codex_usage.proxy.username` | 可选 Basic 用户名 | 最多 128 bytes，不得含 `:` |
| `apps.codex_usage.proxy.password` | 可选 Basic 密码 | 最多 256 bytes；`config.get` 只返回 `password_set` |
| `device.locale` | `oai-language` | 默认 `zh-CN` |
| `device.timezone.posix` | SNTP 后的本地时间/屏幕时间 | 默认 `CST-8` |
| `power.poll_interval_sec` | 成功后的下一次查询 | 默认 300 秒 |
| `power.offline_backoff_sec` | 失败退避 | 默认 300/900/1800/3600 秒 |

下发示例：

```json
{
  "v": 1,
  "id": 20,
  "op": "config.patch",
  "args": {
    "patch": {
      "apps": {
        "codex_usage": {
          "account_id": "account-id",
          "access_token": "token",
          "expires_at": 1780000000
        }
      }
    }
  }
}
```

随后必须 `config.commit`；建议紧接着发送 `refresh.now`。`config.get` 只返回 `account_id`、`expires_at`、`access_token_set` 和脱敏代理配置，不会回传 Token 或代理密码。

代理 patch 示例：

```json
{
  "v":1,
  "id":21,
  "op":"config.patch",
  "args":{"patch":{"apps":{"codex_usage":{"proxy":{
    "enabled":true,
    "host":"proxy.lan",
    "port":8080,
    "username":"epd-kit",
    "password":"proxy-secret"
  }}}}}
}
```

代理连接只在 Codex 请求期间存在，查询结束后与 Wi-Fi 一起关闭，不会形成常驻隧道。

该代理不承载 SNTP。冷启动后固件仍需通过普通网络完成 DNS/SNTP 才能获得可用于证书验证的系统时间；若网络只允许经代理出站而阻止 NTP，设备会显示 `TIME`，且不会发送 Token。

## 4. 观察到的响应模型

以下结构是对内部接口的兼容性观察，不是官方保证：

```json
{
  "plan_type": "plus",
  "rate_limit": {
    "allowed": true,
    "limit_reached": false,
    "primary_window": {
      "used_percent": 35,
      "limit_window_seconds": 604800,
      "reset_after_seconds": 345600,
      "reset_at": 1780000000
    },
    "secondary_window": {
      "used_percent": 12,
      "limit_window_seconds": 18000,
      "reset_after_seconds": 7200,
      "reset_at": 1780000000
    }
  },
  "additional_rate_limits": []
}
```

固件只读取展示所需字段，不保存 `email`、`user_id` 等身份信息。

### 4.1 窗口识别

不能假设 `primary_window` 一定是 weekly 或 `secondary_window` 一定是 5h。固件按 `limit_window_seconds` 分类：

| 秒数 | 分类 | 屏幕 |
|---:|---|---|
| `18000` | 5 hours | `5h` |
| `604800` | 7 days / weekly | `7d` |
| 其他正数 | unknown generic window | 当前主页面不占用固定 5h/7d 行，状态保留供诊断 |

窗口顺序改变不会影响分类。若响应中一个已知窗口都没有，则此次更新为 `protocol_error`，保留最后成功页面。

### 4.2 百分比

接口字段是已用百分比：

```text
remaining = round(100 - clamp(used_percent, 0, 100))
```

例如 `used_percent: 35` 显示剩余 `65%`。`allowed` 和 `limit_reached` 单独保留；`limit_reached` 为 true 时底部显示 `EMPTY`。

### 4.3 重置时间

- 优先使用 `reset_at`（Unix seconds）与 SNTP 时间计算倒计时。
- `reset_at` 不可用或已经过去时，回退到 `reset_after_seconds`。
- 小于 24 小时显示 `reset HH:MM`。
- 大于等于 24 小时显示 `reset Nd HHh`。

### 4.4 Additional limits

固件最多解析前两个可识别的 `additional_rate_limits`，保留：

- `limit_name`
- `metered_feature`
- 嵌套 `rate_limit.primary_window`
- 嵌套 `rate_limit.secondary_window`

首版固定 250×122 页面不展示额外行，数据仅进入标准化状态/快照，为后续 UI 版本预留。固件不调用额度重置、credit 消费或任何写接口。

## 5. 标准化状态

| 内部状态 | 典型原因 | 屏幕代码 | 调度行为 |
|---|---|---|---|
| `never` | 尚未成功查询 | `SETUP` | 等待配置或正常尝试 |
| `ok` | HTTP 200 且响应有效 | `OK` | 恢复 `poll_interval_sec`，默认 5 分钟 |
| `offline` | Wi-Fi 连接失败 | `OFFLINE` | 5/15/30/60 分钟退避 |
| `auth_expired` | Token 本地到期或 HTTP 401 | `REAUTHORIZE` | 停止定时无线电唤醒，仅物理 KEY 唤醒 |
| `forbidden` | HTTP 403 | `DENIED` | 保留快照并退避 |
| `throttled` | HTTP 429 | `HTTP 429` | 保留快照并退避 |
| `proxy_error` | 代理 TCP/CONNECT 超时、407、非 200 或响应头异常 | `PROXY` | 保留快照并退避 |
| `tls_error` | TLS 初始化、握手、连接或读取失败 | `TLS` | 保留快照并退避 |
| `time_error` | SNTP 后时间仍无效 | `TIME` | 不发送 Token，退避 |
| `protocol_error` | 非 200、3xx、超大响应、JSON/字段错误 | `DATA` | 保留快照并退避 |
| `low_battery` | 临界电量锁止 | 独立低电量页 | 无线电关闭，睡眠 6 小时 |

401 与 Token 到期属于需要用户动作的终止状态，不能按普通网络失败继续定时发送。403 不自动解释为 Token 到期，因为也可能是账号策略或内部接口变化。

## 6. 屏幕语义

250×122 横屏固定布局：

```text
CODEX   <plan>                         BAT 3.92V
------------------------------------------------
5h   [remaining progress bar]  88%  reset 02:00
7d   [remaining progress bar]  65%  reset 4d 00h
OK  last 08-10 14:35
```

- 顶部：应用名、观察到的 `plan_type`、电池电压。
- 中部：5h/7d 剩余百分比和进度条；缺失窗口显示 `--%`。
- 每行下方：重置倒计时。
- 底部：状态、最后一次成功同步的本地时间、可选 `EMPTY`。
- 网络/TLS/JSON 失败只改变底部状态，之前成功的百分比仍保留。
- `REAUTHORIZE` 表示必须通过 BLE 下发新 Token。

显示刷新由逻辑帧差分决定。页面内容完全相同时跳过 EPD 初始化；状态或时间文本变化只刷新合并后的脏矩形，达到全刷策略阈值时转为全刷。

## 7. Token 生命周期

### 7.1 首次下发

上位机完成 OAuth/登录和凭据获取，设备只接收最终的 `access_token`、`account_id`、`expires_at`。首版不在 ESP32 上实现 OAuth，也不接收 refresh token。

### 7.2 到期前

每次查询前，如果系统时间有效且 `now >= expires_at`，固件不启动 Wi-Fi 请求，直接进入 `auth_expired`。如果 `expires_at == 0`，只能依靠服务器 401 判断。

### 7.3 到期或 401 后

1. 保留最后成功额度和 `synced_at`。
2. 屏幕显示 `REAUTHORIZE`。
3. 关闭 Wi-Fi/BLE，进入只保留 KEY 的深睡，不再每 5 分钟联网。
4. 用户长按 KEY 3 秒，使用 BLE 更新三项 Codex 配置。
5. `config.commit` → `refresh.now`。
6. 新 Token 查询成功后恢复默认调度。

### 7.4 凭据清除

在 `config.patch` 中显式传空字符串可清除 Token/account ID。省略字段会保留旧值。恢复出厂会清除整个配置槽、快照和 BLE bonds。

## 8. 缓存和 Flash 磨损

- 当前完整逻辑帧和局刷策略状态主要保存在 RTC memory，深睡后继续用于差分。
- 最后成功的 Codex 标准化快照保存在独立 NVS record，包含 schema、长度、写入时间和 CRC。
- 只有成功响应且系统时间有效时才考虑写快照。
- 两次 NVS 快照写入至少间隔 3600 秒；默认 5 分钟轮询不会每次写 Flash。
- 启动时快照可恢复最后成功页面；随后的错误只更新状态，不覆盖成功数据。
- 配置 commit 不受该一小时限制，因为它是显式用户操作，并使用 NVS 双槽原子切换。

## 9. 低电量行为

Waveshare 模块的 GPIO36 ADC 电压约为电池电压的三分之一，固件中值采样后乘以 3。阈值默认：

| 阈值 | 默认值 | 行为 |
|---|---:|---|
| `low_mv` | 3550 mV | 页面可提示低电量，仍允许正常周期 |
| `critical_mv` | 3400 mV | 锁止无线电，显示低电量页，睡眠 6 小时 |
| `recovery_mv` | 3650 mV | 达到后解除锁止并恢复联网 |

ADC 仅为板级估算，必须用实测电压校准。USB 供电、无电池或 ADC 无效时不会错误触发临界锁止。

## 10. 安全警告

- ChatGPT access token 不是 OpenAI Platform `sk-...` API key。
- 不要把 Token 写入源码、提交到版本控制、打印到串口或包含在崩溃日志。
- BLE 的 MITM/bonding 保护传输，不保护未加密 Flash 中的静态数据。
- 内部 API 可能触发 ChatGPT 风控或随时停止工作；不要承诺长期可用性。
- 固件拒绝重定向并固定 `chatgpt.com`，但 CA bundle 仍需随证书生态更新。
- HTTP proxy 能看到设备地址、`chatgpt.com:443` 目标、时序和流量大小；若启用 Basic，还能直接看到代理用户名和密码，因为 CONNECT 控制连接本身不加密。只应使用可信局域网或受 VPN 保护的代理。
- Codex access token、account ID、请求路径和响应正文位于 tunnel 内的端到端 TLS 中。代理若尝试 TLS 中间人替换证书，固件会因 CA/主机名校验失败而拒绝连接。
- 固件不支持 HTTPS proxy、SOCKS、PAC、IPv6 proxy literal，也不支持通过 BLE 添加企业私有 CA。
- Web App 应在本地完成 OAuth/授权流程，并在内存中尽量短暂持有 Token；`config.get` 不能用于取回 Token。
- 原型进入量产前应评估 Secure Boot、Flash Encryption、受控签名更新和秘密轮换；这些均不在 v1 范围内。

## 11. 故障诊断

| 现象 | 检查项 |
|---|---|
| 一直 `OFFLINE` | 仅 2.4 GHz、SSID/密码、RSSI、电源峰值能力 |
| 一直 `TIME` | DNS、NTP 出站、POSIX TZ 格式；Token 未被发送 |
| `TLS` | 系统时间、CA/证书链变化、DNS、网络劫持；禁止临时 `setInsecure()` |
| `PROXY` | 代理 host/port、路由、防火墙、CONNECT 权限或 Basic 凭据；407 表示代理认证失败 |
| `REAUTHORIZE` | `expires_at`、HTTP 401；通过 BLE 下发新 Token |
| `DENIED` | HTTP 403、账号/workspace 策略、内部 API 变化 |
| `HTTP 429` | 保留页面并等待退避，不高频重试 |
| `DATA` | 3xx、非预期 HTTP、超过 16 KiB、畸形 JSON、未知响应结构 |
| 百分比颠倒 | 确认显示的是 `100 - used_percent` |
| 5h/7d 互换 | 必须按 `limit_window_seconds`，不得按 primary/secondary 顺序 |

抓取诊断时只记录状态码、错误类别、响应长度和时间，不记录 header 值或响应正文。

## 12. 验收用例

- 有效响应中 primary/secondary 顺序互换，5h/7d 仍正确。
- `used_percent` 小于 0 或大于 100 时正确 clamp。
- 未知窗口被保留但不覆盖已知窗口。
- 200 但无任何额度窗口时返回 `protocol_error`。
- `Content-Length > 16384` 和无长度的流式超限均拒绝。
- 3xx 不跟随；确认 Token 不出现在第二主机请求中。
- 无认证 HTTP CONNECT、正确/错误 Basic、407、非 200、超时和超过 2 KiB 的 CONNECT 响应头分类为 `proxy_error`。
- tunnel 建立后仍按内置 CA 与 `chatgpt.com` 主机名校验；代理 MITM 证书必须失败，Token 不得出现在 CONNECT 明文中。
- 401 和本地过期均停止 timer wake，只允许 KEY 唤醒。
- 403、429、TLS、SNTP 和畸形 JSON 保留最后成功页面。
- 连续失败按 5/15/30/60 分钟退避，成功后回到 5 分钟。
- 12 次局刷、40% 脏区、24 小时、RTC 无效和 brownout 均触发全刷。
- 一小时内多次成功查询只发生最多一次额度快照 NVS 写入。
