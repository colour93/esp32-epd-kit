# LAN Transport for Protocol v4

本文定义 Protocol v4 在本地 WiFi/TCP 上的承载方式。业务 envelope、分片 header、RPC operation、错误与事件语义均与 [BLE Protocol v4](ble_protocol_v4.md) 相同；本文件只定义发现、认证和 TCP 帧边界。

## 1. 使用边界

- USB-TTL 串口只用于配置/清除 WiFi、查看状态和读取 LAN 设备密钥，不承载日常 Protocol v4 RPC；
- ESP32 仅作为 WiFi station 加入现有 2.4 GHz 网络，不提供 SoftAP；
- Agent 通过 mDNS 发现设备并直接建立本地 TCP 连接，不经过云端；
- BLE 与 LAN 共用一个协议处理器，任一时刻只允许一个已认证会话；新会话会断开另一传输上的旧会话；
- LAN 已认证客户端按物理 owner 处理。设备密钥应视为 owner 凭据，不应写入浏览器配置、日志或普通配置文件。
- 当前链路是经过 HMAC 身份认证的明文 TCP，不是 TLS；应只在可信 WiFi/LAN 中使用，不能直接暴露到公网或不可信访客网络。

## 2. 串口配网

串口参数为 `115200 8N1`，需使用 3.3V 电平 USB-TTL：

```text
wifi status
wifi ssid <1..32 byte SSID>
wifi password <empty or 8..63 byte password>
wifi apply
wifi key
wifi forget
```

`wifi key` 输出：

```text
device_id=A1B2C3D4E5F6
device_key=000102...1f
```

`device_key` 是设备首次启动时随机生成的 32 字节密钥，以 64 个十六进制字符显示。`wifi forget` 保留该密钥；factory reset 会同时擦除 WiFi 凭据和设备密钥，并在重启后生成新密钥。

## 3. mDNS 发现

服务类型：

```text
_epdkit._tcp.local.
```

默认端口为 TCP `38474`。TXT 字段：

| 字段 | 含义 |
|---|---|
| `id` | 12 位设备 ID |
| `name` | 人类可读设备名/hostname |
| `proto` | 必须为 `4` |
| `fw` | 固件版本 |

Agent 只接受 `proto=4` 且具有 IPv4 地址的结果。首次连接成功后可缓存 device ID 与 endpoint；地址失效时应重新通过 mDNS 解析 device ID。

## 4. TCP 认证

TCP 建立后由设备先发送一行 greeting：

```text
EPD4 <device-id> <nonce-hex>\n
```

`nonce-hex` 是 16 个随机字节的小写十六进制表示。Agent 必须确认 `<device-id>` 与 mDNS 结果一致，然后计算：

```text
message = UTF8("EPD4:" + device-id + ":" + nonce-hex)
digest  = HMAC-SHA256(device-key, message)
```

Agent 回复：

```text
AUTH <digest-hex>\n
```

设备使用常量时间比较验证 digest。成功时返回允许的 Protocol v4 frame 大小：

```text
OK 1024\n
```

失败时返回 `ERR authentication` 并断开。所有 handshake 行不得超过 256 字节；认证与连接应设置有限超时。

## 5. TCP 帧边界

认证成功后，每个已有 Protocol v4 frame 前增加 2 字节 little-endian 无符号长度：

```text
+----------------+-------------------------------+
| u16_le length  | one Protocol v4 frame         |
+----------------+-------------------------------+
```

- `length` 只包含 Protocol v4 frame，不包含自身；
- frame 仍使用 BLE 文档定义的 8 字节 v4 header 与 MessagePack payload；
- 当前发送端按 `OK 1024` 分片，接收端拒绝小于 8 或大于 2048 字节的 wire frame；
- request/response/event 的 assembly、CRC、request ID 和错误行为不变；
- 设备 30 秒无 TCP 活动会断开，Agent 当前每 5 秒执行一次 `system.status` heartbeat。

## 6. Agent API

工作台通过 loopback API 选择传输：

```json
POST /api/v1/device/scan
{"transport":"lan"}

POST /api/v1/device/connect
{"transport":"lan","id":"A1B2C3D4E5F6","secret":"<64 hex>"}

POST /api/v1/device/auto-connect
{"transport":"lan"}
```

`secret` 只在设备密钥尚未保存时提交。Agent 校验后将它写入操作系统凭据库；snapshot 只暴露候选设备的 `paired: true/false`，绝不返回密钥。BLE 使用相同接口并将 `transport` 设为 `ble`，且不接受 `secret`。
