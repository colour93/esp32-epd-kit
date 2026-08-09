# ESP32 E-Paper Toolkit BLE Protocol v1

> 状态：Frozen for firmware `0.2.x`  
> 文件名 `ble_protocal.md` 的拼写为兼容既有路径而保留。  
> 本协议面向任意 BLE 上位机；首版不包含 Web App 实现。

## 1. 范围与兼容性

本协议用于首次配置、Wi-Fi 测试、应用配置、手动刷新、Codex 凭据重授权和恢复出厂。应用和 UUID 均编译进固件；协议不提供 OTA、动态应用下载、OAuth 登录或 refresh token 管理。

协议版本由每条消息的 `v` 字段声明。v1 的 UUID、消息外层结构、操作名和错误码均冻结。后续新增字段必须保持向后兼容；客户端必须忽略无法识别的响应字段。无法兼容的变更必须使用新的 `v` 值或新的 Service UUID。

## 2. 广播和会话

- 广播名：`EPD-KIT-<MAC 后 6 位大写十六进制>`，例如 `EPD-KIT-A1B2C3`。
- 广播中包含 Toolkit Service UUID。
- 未配置设备在首次启动时最多广播 10 分钟。
- 已配置设备只有在物理 KEY 低有效长按至少 3 秒后才广播，默认窗口 180 秒，可由 `power.ble_window_sec` 配置为 30–600 秒。
- 已建立连接的有效写操作会续期空闲窗口，但从 BLE 启动起的单次硬上限始终为 10 分钟。
- 断开连接时，所有未 `config.commit` 的暂存配置立即丢弃。
- 单次只服务一个 BLE 连接。其他上位机应退避并在当前会话结束后重试。

短按 KEY 触发正常的立即查询；长按 10 秒只进入恢复出厂预备的 BLE 会话，不直接擦除数据。

## 3. GATT 定义

### 3.1 标准服务

| 服务 | UUID | 特征 | UUID | 属性 | 值 |
|---|---|---|---|---|---|
| Device Information | `0x180A` | Manufacturer Name | `0x2A29` | Read | `Waveshare / esp32-epd-kit` |
| Device Information | `0x180A` | Model Number | `0x2A24` | Read | `2.13inch e-Paper Cloud Module` |
| Device Information | `0x180A` | Firmware Revision | `0x2A26` | Read | 固件版本 |
| Device Information | `0x180A` | Serial Number | `0x2A25` | Read | 与广播名相同 |
| Battery | `0x180F` | Battery Level | `0x2A19` | Read, Notify | `uint8`，0–100 |

Battery Level 仅在 BLE 会话期间有效。电量值是 GPIO36 ADC 的估算结果，不应用作计费或精密电量计。

### 3.2 Toolkit Service

| 名称 | UUID | 属性 | 最大单次 GATT value |
|---|---|---|---|
| Toolkit Service | `f0a10000-0451-4000-b000-000000000001` | Primary Service | — |
| RX | `f0a10001-0451-4000-b000-000000000001` | Encrypted + Authenticated Write | 512 bytes |
| TX | `f0a10002-0451-4000-b000-000000000001` | Encrypted + Authenticated Indicate | 512 bytes |

客户端必须先订阅 TX indication，再向 RX 写请求。不要把 TX 当作无确认 notification。

## 4. 安全要求

- 使用 LE Secure Connections、MITM 和 bonding。
- 设备采用 Display Only 模式，在墨水屏显示随机六位 Passkey。
- RX/TX 要求加密且认证的链路；未加密访问由 ATT 层拒绝。
- 已配置状态下，固件不在后台广播。只有本次物理长按打开的会话才允许新客户端到达配对入口。
- 未配置设备允许在首次配置窗口内建立新 bond。
- 恢复出厂会清除配置、额度快照和所有 bond。
- `config.get` 永不返回 Wi-Fi 密码、Codex access token 或代理密码。

链路加密只保护无线传输。首版 Token 存于普通 NVS，未启用 Flash Encryption、Secure Boot 或 eFuse，因此不能视为生产级秘密存储。

## 5. NDJSON 传输

### 5.1 编码

- UTF-8 JSON，每条消息后必须有且只有一个行结束符 `\n`。
- 接收方兼容行尾 `\r\n`，但发送方应使用 `\n`。
- 一条 JSON 文本最大 8192 bytes，不含末尾 `\n`。
- JSON 顶层必须是 object。
- 客户端必须支持按 20 bytes 分片；协商较大 MTU 后可使用 `ATT_MTU - 3`。
- 固件 TX 分片大小为 `min(244, max(20, ATT_MTU - 3))`。
- 分片必须严格有序，不包含序号，也不支持重传或乱序重排。
- 从一条消息的首字节开始计时，5 秒内未收到换行符即超时并清空组包缓冲。
- 超长、超时或非法 JSON 会清空当前消息。客户端收到错误后必须从完整 JSON 的第一个字节重发。
- 一个连接同一时刻只允许一个请求处于处理状态；收到 `busy` 后等待当前响应再重试。

建议客户端每次只发送一条完整 NDJSON 请求，等待相同 `id` 的响应后再发送下一条。

### 5.2 请求与响应外层

请求：

```json
{"v":1,"id":1,"op":"config.get","args":{}}
```

成功：

```json
{"v":1,"id":1,"ok":true,"result":{}}
```

失败：

```json
{"v":1,"id":1,"ok":false,"error":{"code":"invalid_config","message":"human-readable detail"}}
```

字段约束：

| 字段 | 方向 | 类型 | 约束 |
|---|---|---|---|
| `v` | 双向 | integer | v1 固定为 `1` |
| `id` | 双向 | uint32 | 由客户端生成；一次连接内未完成请求不可复用 |
| `op` | 请求 | string | 本文冻结的操作名 |
| `args` | 请求 | object | 无参数时发送 `{}` |
| `ok` | 响应 | boolean | 成功为 `true` |
| `result` | 成功响应 | object | 操作结果 |
| `error.code` | 失败响应 | string | 固定错误码 |
| `error.message` | 失败响应 | string | 仅供诊断，不应由客户端解析 |

组包级错误无法可靠关联请求时使用 `id: 0`。

## 6. 配置模型

完整的内部配置为：

```json
{
  "version": 1,
  "device": {
    "name": "epd-kit",
    "locale": "zh-CN",
    "timezone": {"iana": "Asia/Shanghai", "posix": "CST-8"}
  },
  "wifi": {
    "ssid": "",
    "password": "",
    "ipv4": {
      "mode": "dhcp",
      "address": "",
      "gateway": "",
      "subnet": "",
      "dns1": "",
      "dns2": ""
    }
  },
  "power": {
    "poll_interval_sec": 300,
    "ble_window_sec": 180,
    "offline_backoff_sec": [300, 900, 1800, 3600]
  },
  "display": {
    "full_after_partial_count": 12,
    "full_max_age_sec": 86400,
    "full_area_threshold_percent": 40
  },
  "battery": {"low_mv": 3550, "critical_mv": 3400, "recovery_mv": 3650},
  "active_app": "codex_usage",
  "apps": {
    "codex_usage": {
      "account_id": "",
      "access_token": "",
      "expires_at": 0,
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

### 6.1 字段约束

| 路径 | 约束 |
|---|---|
| `version` | 必须为 `1` |
| `device.name` | 1–24 printable bytes |
| `device.locale` | 最多 16 bytes |
| `device.timezone.iana` | 最多 64 bytes，供上位机/展示识别 |
| `device.timezone.posix` | 1–96 bytes，传给 ESP32 `configTzTime()` |
| `wifi.ssid` | 最多 32 bytes；仅支持 ESP32 的 2.4 GHz Wi-Fi |
| `wifi.password` | 最多 64 bytes；空字符串表示开放网络或清除旧值 |
| `wifi.ipv4.mode` | `dhcp` 或 `static`；默认 `dhcp` |
| `wifi.ipv4.address` | static 时必填，合法的 IPv4 单播主机地址 |
| `wifi.ipv4.gateway` | static 时必填；必须是同一子网内、与 address 不同的可用主机地址 |
| `wifi.ipv4.subnet` | static 时必填；连续掩码，前缀长度 `/1`–`/30` |
| `wifi.ipv4.dns1` | static 时必填，IPv4 单播地址 |
| `wifi.ipv4.dns2` | static 时可选，非空时必须是 IPv4 单播地址 |
| `power.poll_interval_sec` | 60–86400 |
| `power.ble_window_sec` | 30–600 |
| `power.offline_backoff_sec` | 恰好 4 个整数，每个 60–86400 |
| `display.full_after_partial_count` | 1–100 |
| `display.full_max_age_sec` | 不小于 3600 |
| `display.full_area_threshold_percent` | 10–100 |
| `battery.*` | `3000 <= critical_mv < low_mv < recovery_mv <= 4300` |
| `active_app` | v1 仅 `codex_usage` |
| `apps.codex_usage.account_id` | 最多 128 bytes |
| `apps.codex_usage.access_token` | 最多 4096 bytes |
| `apps.codex_usage.expires_at` | Unix seconds；`0` 表示未知 |
| `apps.codex_usage.proxy.enabled` | boolean；true 时通过 HTTP CONNECT 代理访问固定的 `chatgpt.com:443` |
| `apps.codex_usage.proxy.host` | 最多 253 ASCII bytes；只填主机名或 IPv4，不含 scheme、端口、路径；不支持 IPv6 literal |
| `apps.codex_usage.proxy.port` | 1–65535，默认 `8080` |
| `apps.codex_usage.proxy.username` | 可选，最多 128 bytes；不得含 `:` 或控制字符 |
| `apps.codex_usage.proxy.password` | 可选，最多 256 bytes；不得含控制字符；仅与非空 username 一起使用 |

IANA 和 POSIX TZ 必须同时保存。固件不把 IANA 名称自动转换为 POSIX 规则。

DHCP 模式忽略 `address`、`gateway`、`subnet`、`dns1` 和 `dns2`；这些字段可为空。切换回 DHCP 时固件会显式重启 DHCP client，不会沿用同一会话中 `wifi.test` 设置过的静态地址。static 模式还会拒绝网络地址、广播地址、不同子网网关、非连续掩码以及 `/31`、`/32`。

代理仅支持普通 TCP 上的 HTTP/1.1 CONNECT，可选 Basic Authentication；不支持 HTTPS proxy、SOCKS、PAC 或代理端 TLS。代理只作用于 Codex HTTPS 请求，Wi-Fi、DHCP、DNS 和 SNTP 不走代理。代理目标和 Codex API 域名均不可通过 BLE 修改。

### 6.2 脱敏读取

`config.get` 返回：

```json
{
  "wifi": {
    "ssid":"MyWiFi",
    "password_set":true,
    "ipv4": {
      "mode":"static",
      "address":"192.168.50.42",
      "gateway":"192.168.50.1",
      "subnet":"255.255.255.0",
      "dns1":"1.1.1.1",
      "dns2":"8.8.8.8"
    }
  },
  "apps": {
    "codex_usage": {
      "account_id":"account-id",
      "expires_at":1780000000,
      "access_token_set":true,
      "proxy": {
        "enabled":true,
        "host":"proxy.lan",
        "port":8080,
        "username":"epd-kit",
        "password_set":true
      }
    }
  }
}
```

不会出现 `wifi.password`、`apps.codex_usage.access_token` 或 `apps.codex_usage.proxy.password`。要保留旧秘密，patch 中省略对应字段；要清除，显式发送空字符串。`password_set` 只表示对应秘密是否已保存，不能用于还原秘密。

## 7. 操作定义

### `hello`

请求参数：无。

```json
{"v":1,"id":1,"op":"hello","args":{}}
```

结果字段：`protocol`、`firmware`、`device_name`、`max_message_bytes`、`mtu`、`security`。

### `device.status`

返回 `configured`、`config_committed`、`active_app`、`uptime_ms` 和 `wifi_ssid`。该操作不发起 Wi-Fi 或 Codex 查询。

### `config.get`

返回 `result.config`，按 6.2 节脱敏。返回的是已提交配置，不是 session staging。

### `config.patch`

请求：

```json
{
  "v":1,
  "id":10,
  "op":"config.patch",
  "args":{"patch":{"wifi":{"ssid":"MyWiFi","password":"secret"}}}
}
```

patch 递归合并到会话暂存副本，随后执行完整配置校验。它不写 Flash。成功结果包含 `staged` 和按暂存值计算的 `configured`。

静态 IPv4 示例：

```json
{
  "v":1,
  "id":11,
  "op":"config.patch",
  "args":{"patch":{"wifi":{"ipv4":{
    "mode":"static",
    "address":"192.168.50.42",
    "gateway":"192.168.50.1",
    "subnet":"255.255.255.0",
    "dns1":"1.1.1.1",
    "dns2":"8.8.8.8"
  }}}}
}
```

恢复 DHCP 只需 patch `{"wifi":{"ipv4":{"mode":"dhcp"}}}`；其他 IPv4 字段可以保留，运行时会忽略。

Codex HTTP CONNECT 代理示例：

```json
{
  "v":1,
  "id":12,
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

无认证代理将 `username`、`password` 设为空字符串。禁用代理可只 patch `enabled:false`，其余字段会保留供下次启用。

### `config.commit`

无参数。将整个暂存配置写入非活动 NVS 槽，校验 schema、长度、CRC 和序号后切换 active marker。掉电发生在 marker 切换前时旧槽仍是有效配置。成功后当前会话的 committed config 立即更新。

### `wifi.scan`

无参数。返回最多 10 个 2.4 GHz 网络：

```json
{"networks":[{"ssid":"MyWiFi","rssi":-51,"channel":6,"open":false}]}
```

扫描是高功耗操作，可能持续数秒。

### `wifi.test`

使用暂存 Wi-Fi 配置连接，超时约 12 秒，然后立即关闭 Wi-Fi。失败为 `wifi_failed`。该操作不提交配置、不做 SNTP、也不查询 Codex；它只测试局域网关联和 IPv4 配置，不测试代理或 Codex API。

成功结果示例：

```json
{
  "connected":true,
  "rssi":-51,
  "ipv4_mode":"static",
  "ip":"192.168.50.42",
  "gateway":"192.168.50.1",
  "subnet":"255.255.255.0",
  "dns1":"1.1.1.1",
  "dns2":"8.8.8.8"
}
```

DHCP 模式返回 DHCP 实际租约；static 模式返回固件应用后的地址。上位机应核对所有地址字段，而不只看 `connected`。

### `app.list`

返回固件静态注册的应用：`id`、`name`、`version`、`active`。v1 只有 `codex_usage`。

### `app.activate`

参数 `args.id`。只更新暂存配置，必须再调用 `config.commit`。

### `refresh.now`

无参数。成功返回 `scheduled: true`，固件结束 BLE 会话后立即执行一次正常联网/查询/显示流程。

### `factory_reset.prepare`

生成非零 uint32 nonce，有效期 30 秒，并在屏幕显示物理确认提示：

```json
{
  "nonce": 305419896,
  "expires_in_sec": 30,
  "physical_confirmation_required": true
}
```

在有效期内按住 KEY 至少 2 秒完成物理确认。

### `factory_reset.commit`

参数为 prepare 返回的 nonce：

```json
{"v":1,"id":31,"op":"factory_reset.commit","args":{"nonce":305419896}}
```

只有 nonce 未过期且物理确认完成时才擦除 NVS 配置、额度快照和所有 BLE bonds，随后设备重启。任一条件不满足均返回 `unauthorized`。

## 8. 固定错误码

| code | 含义 | 客户端处理 |
|---|---|---|
| `unsupported_version` | `v` 不受支持 | 停止并提示升级客户端/固件 |
| `invalid_request` | JSON、字段或 op 非法 | 修正完整请求后重发 |
| `unauthorized` | 加密/确认/nonce 条件不满足 | 重新配对或重新 prepare |
| `invalid_config` | patch 或完整配置校验失败 | 展示 message，修改配置 |
| `busy` | 已有请求处理中 | 等待当前响应后重试 |
| `timeout` | NDJSON 组包超过 5 秒 | 从首字节重发完整消息 |
| `too_large` | JSON 超过 8192 bytes | 缩小请求；Token 上限 4096 bytes |
| `wifi_failed` | Wi-Fi 测试失败 | 检查 2.4 GHz、密码和信号 |
| `auth_expired` | 应用凭据已失效 | 重新下发 Token |
| `internal_error` | NVS、BLE 或固件内部错误 | 保存诊断信息并有限重试 |

错误 `message` 可调整或本地化，客户端逻辑只能依赖 `code`。

## 9. 完整时序

### 9.1 首次配网和 Codex 配置

1. 设备未配置启动，显示 Passkey 并广播最多 10 分钟。
2. 客户端连接、使用 Passkey 完成 Secure Connections bonding、订阅 TX。
3. `hello`，确认 `protocol == 1`。
4. 可选 `wifi.scan`。
5. `config.patch` 一次性或分多次下发 Wi-Fi、时区和 Codex 凭据。
6. `wifi.test`。
7. `config.get` 仅核对脱敏后的已提交值；暂存值由 patch 响应确认。
8. `config.commit`。
9. `refresh.now`。
10. 设备结束 BLE，联网、SNTP、查询、刷新墨水屏并深睡。

### 9.2 修改普通配置

1. 已配置设备长按 KEY 3 秒。
2. 使用已有 bond 连接；若是新上位机，使用屏幕 Passkey 配对。
3. `hello` → `config.get` → 一个或多个 `config.patch`。
4. 可选 `wifi.test`。
5. `config.commit` → `refresh.now`。

### 9.3 Token 重授权

1. 屏幕显示 `REAUTHORIZE`；设备停止定时无线电唤醒，只保留物理 KEY 唤醒。
2. 长按 KEY 3 秒进入 BLE。
3. `config.patch` 仅发送新的 `account_id`、`access_token`、`expires_at`；省略 Wi-Fi 密码以保留旧值。
4. `config.commit` → `refresh.now`。
5. 成功查询后恢复正常 5 分钟调度。

固件不接收或存储 refresh token，也不负责 OAuth 登录。

### 9.4 恢复出厂

1. 长按 KEY 进入 BLE；长按 10 秒可先显示恢复预备提示，但不会擦除。
2. 连接并订阅 TX。
3. `factory_reset.prepare`，保存 nonce。
4. 在 30 秒内按住 KEY 至少 2 秒。
5. `factory_reset.commit` 携带同一个 nonce。
6. 收到成功 indication 后连接断开，设备清除配置、快照和 bonds 并重启。

## 10. 上位机伪代码

```ts
async function transact(op: string, args: object = {}): Promise<any> {
  if (pendingRequest) throw new Error("one request at a time");
  const id = nextUint32Id();
  const wire = utf8(JSON.stringify({ v: 1, id, op, args }) + "\n");
  if (wire.length - 1 > 8192) throw new Error("request too large");

  pendingRequest = { id, rx: new Uint8Array() };
  const chunk = Math.min(244, Math.max(20, negotiatedMtu - 3));
  for (let offset = 0; offset < wire.length; offset += chunk) {
    await rxCharacteristic.writeValueWithResponse(wire.slice(offset, offset + chunk));
  }

  const response = await waitForMatchingIndication(id, 15_000);
  pendingRequest = undefined;
  if (response.v !== 1 || response.id !== id) throw new Error("protocol mismatch");
  if (!response.ok) throw new ToolkitError(response.error.code, response.error.message);
  return response.result;
}

function onTxIndication(fragment: Uint8Array) {
  indicationBuffer = concat(indicationBuffer, fragment);
  while (containsNewline(indicationBuffer)) {
    const [line, rest] = splitFirstLine(indicationBuffer);
    indicationBuffer = rest;
    const response = JSON.parse(decodeUtf8(stripOptionalCR(line)));
    deliverById(response.id, response);
  }
}

async function provision(input: ProvisioningInput) {
  await connectAndPairUsingDisplayedPasskey();
  await subscribeToTxIndications();
  await transact("hello");
  await transact("config.patch", { patch: input.fullConfigPatch });
  await transact("wifi.test");
  await transact("config.commit");
  await transact("refresh.now");
}
```

生产上位机还应处理：用户取消配对、ATT 权限错误、MTU 23/185/247、断连后丢弃半包、`timeout` 后完整重发、`busy` 退避、4 KiB Token、以及 indication 去重。

## 11. 验收矩阵

- MTU：23（20-byte payload）、185、247。
- 长消息：4096-byte access token；总 NDJSON 不超过 8192 bytes。
- 分片：断连、乱序模拟、缺失换行、5 秒超时、8193-byte 拒绝。
- 安全：未加密 RX/TX、新 bond、已有 bond、bond 清除、两个上位机竞争。
- 脱敏：任何 `config.get`、错误和 Release 日志均不得出现密码或 Token。
- 配置：patch 不 commit 后断连、原子槽掉电点、无效时区/阈值/数组长度。
- 恢复出厂：nonce 错误、过期、无物理确认、确认后成功擦除。
