# BLE Protocol v4 Host Implementation Guide

本文是 EPD-KIT 固件 `0.3.x` 的主机实现规范。关键词“必须”“不得”“应”具有规范含义。线上唯一应用传输为 BLE；ESP32 不连接云服务，也不保存 Codex、飞书或其他数据源凭据。

## 1. v4 兼容边界

v4 是破坏性版本，不兼容 v3：

- 不读取 `epd_cfg3`、`epd_res3`、`epd_sec3`；
- 首次找不到 v4 配置时创建默认配置并清除 NimBLE bonds；
- Service UUID、frame magic、配置结构、Page RPC 均已变化；
- 主机升级后必须删除操作系统中的旧配对记录并重新配对；
- 不存在 migration，也不得尝试把 v3 `view` 映射为 v4 `page`。

## 2. 发现与 GATT

| 项目 | 值 |
|---|---|
| 设备名 | `EPD-KIT-` 加 6 位大写十六进制后缀 |
| Application Service | `f0a40000-0451-4000-b000-000000000001` |
| RX | `f0a40001-0451-4000-b000-000000000001` |
| TX | `f0a40002-0451-4000-b000-000000000001` |
| 建议 MTU | 247 |
| 最大完整消息 | 8192 bytes |

RX 使用 encrypted/authenticated write，TX 使用 encrypted/authenticated indication。主机必须订阅 TX indication 后再发 RPC，并按顺序确认 indication。

Manufacturer Data 使用内部 Company ID `0xffff`，其 value 为：

| offset | 含义 |
|---:|---|
| 0 | protocol major，固定 `0x04` |
| 1 bit 0 | 已存在 owner |
| 1 bit 1 | 启用电池硬件 |
| 1 bit 2 | IO12 key 模式 |
| 1 bit 3 | fast advertising |

主机只接受 major 4。服务 UUID 是首选匹配条件；设备名只可用于辅助恢复已保存目标，多个同名/候选设备时必须由用户选择。

## 3. 配对与角色

设备要求 LE Secure Connections、MITM 和 bonding，IO capability 为 Display Only。设备屏幕显示六位 passkey，主机输入完成配对。

- 第一个通过认证的 bond 自动成为 `owner`；
- owner 可开启 120 秒 enrollment，新增最多 3 个 `trusted` bond；
- 总 bond 上限为 4；
- enrollment 关闭时，未知 bond 会被拒绝并删除；
- owner 不可直接撤销，必须先转移 owner；
- bond ID 是设备生成的匿名 `b-xxxxxxxx`，主机不得依赖底层地址。

`system.hello` 可在认证后、角色解析完成前调用；其余 RPC 要求 trusted。下表标为 owner 的操作还要求当前 bond 为 owner。

## 4. v4 分帧

每个 GATT value 包含一个 frame。多字节整数均为 little-endian。

### 4.1 固定头

| offset | size | 字段 |
|---:|---:|---|
| 0 | 1 | magic，固定 `0xe4` |
| 1 | 1 | flags |
| 2 | 4 | message ID |
| 6 | 2 | sequence，从 0 连续递增 |

flags：

- bits 0..1：kind，`0=request`、`1=response`、`2=event`；
- bit 2：START；
- bit 3：END；
- 其余位必须为 0。

START frame 在固定头后增加 6 bytes：

| offset | size | 字段 |
|---:|---:|---|
| 8 | 2 | 完整 MessagePack payload 长度 |
| 10 | 4 | 完整 payload 的标准 CRC-32/IEEE |

frame payload 容量为 `ATT_MTU - 3 - 8 - start_metadata`。单帧消息同时设置 START 和 END。sequence 不连续、ID/kind 改变、长度不符、CRC 不符、超过 8192 bytes 或组包超过 5 秒都必须丢弃本轮组包。

主机对 RX frame 使用 Write With Response；一次只发送一个完整请求。设备 TX 串行发送 indication；主机必须允许事件夹在请求和响应之间，并按 message ID 匹配响应。

### 4.2 MessagePack 包络

请求：

```json
{"op":"resource.list","args":{}}
```

成功响应：

```json
{"ok":true,"result":{"resources":[]}}
```

失败响应：

```json
{
  "ok": false,
  "error": {
    "code": "invalid_args",
    "message": "...",
    "retryable": false
  }
}
```

事件：

```json
{"name":"display.completed","data":{"result":"partial"}}
```

常见错误码：`invalid_frame`、`too_large`、`invalid_args`、`unauthorized`、`forbidden`、`not_found`、`conflict`、`storage_error`。只有 `retryable:true` 才应原样重试；`conflict` 应先重新读取设备状态。

## 5. 推荐会话流程

1. 扫描 major 4 或 v4 Service，选定并保存稳定目标。
2. 连接；若系统保留 v3 bond，先删除旧配对再重新配对。
3. 发现服务和 RX/TX，订阅 TX，协商 MTU。
4. 调用 `system.hello`，再次确认 `protocol_major == 4`。
5. 调用 `system.time.set` 写入 Unix 时间和当前 UTC offset。
6. 并行概念上读取 `config.get`、`capabilities.get`、`resource.list`，但 BLE 写入必须串行。
7. 读取需要的安全、诊断状态。
8. Producer 采集语义数据，由 Publisher 串行执行 `resource.put`。
9. 自动连接的 battery 会话中，所有相关 Producer 完成本 cycle 且发布队列排空后，唯一调用一次 `system.sync.complete`。
10. 等待其响应 indication；设备随后渲染并深睡，断连属于正常结果。

## 6. RPC 清单

| op | 权限 | args | result 要点 |
|---|---|---|---|
| `system.hello` | authenticated | `{}` | 协议、固件、名称、MTU、角色、功耗档位 |
| `system.status` | trusted | `{}` | uptime、连接、配置 revision、page、heap |
| `diagnostics.get` | trusted | `{}` | 与 `system.status` 相同的诊断快照 |
| `system.time.set` | trusted | `{unix_seconds:u64,utc_offset_minutes:i16}` | `{applied:true}` |
| `system.sync.complete` | trusted | `{}` | 是否计划睡眠及数据同步周期 |
| `system.restart` | owner | `{}` | `{scheduled:true}` |
| `capabilities.get` | trusted | `{}` | Pages、Slots、Timed Regions 与容量 |
| `config.get` | trusted | `{}` | `{config:{...}}` |
| `config.patch` | owner | `{patch:{...}}` | staged、restart_required |
| `config.discard` | trusted | `{}` | 丢弃 staged config |
| `config.commit` | owner | `{expected_revision?:u32}` | 新 revision |
| `resource.list` | trusted | `{}` | 不含 payload 的资源摘要 |
| `resource.get` | trusted | `{key:string}` | 完整资源 |
| `resource.put` | trusted | `{resource:Resource}` | changed、render_scheduled |
| `resource.delete` | owner | `{key:string}` | deleted |
| `page.get` | trusted | `{}` | 当前 PageSettings |
| `page.set` | owner | `{page:PageSettings}` | 配置 revision |
| `display.refresh` | trusted | `{mode:"auto"|"full"}` | scheduled |
| `security.owner.get` | trusted | `{}` | role、owned |
| `security.enrollment.open` | owner | `{}` | 120 秒窗口 |
| `security.enrollment.close` | owner | `{}` | closed |
| `security.bonds.list` | trusted | `{}` | bond ID 与 role |
| `security.bonds.revoke` | owner | `{bond_id:string}` | applied |
| `security.owner.transfer` | owner | `{bond_id:string}` | applied |
| `factory_reset.prepare` | owner | `{}` | 30 秒确认窗口，码显示在设备上 |
| `factory_reset.commit` | owner | `{code:u32}` | scheduled |

## 7. Config v4

```json
{
  "version": 4,
  "revision": 7,
  "device": {
    "name": "epd-kit",
    "locale": "zh-CN",
    "timezone_iana": "Asia/Shanghai"
  },
  "hardware": {
    "battery": {
      "enabled": false,
      "low_mv": 3550,
      "critical_mv": 3400,
      "recovery_mv": 3650
    },
    "io12": {"mode":"disabled"}
  },
  "power": {"profile":"mains","wake_interval_sec":300},
  "display": {
    "full_after_partial_count": 60,
    "full_max_age_sec": 86400,
    "full_area_threshold_percent": 70
  },
  "page": {
    "id": "home",
    "bindings": {"codex":"codex/default"}
  }
}
```

`config.patch` 是递归到各一级配置对象的局部覆盖，写入 staged config；`config.commit` 才原子保存。`page.set` 是独立的立即提交操作。硬件 battery、IO12 或 power profile 变化需要重启。

Config 使用 `epd_cfg4` 双槽、CRC 和 active marker；资源快照使用 `epd_res4`；owner 使用 `epd_sec4`。不存在 v3 fallback。

## 8. Page、Slot 与 Binding

`capabilities.get.pages[]`：

```json
{
  "id": "home",
  "title": "Home",
  "slots": [
    {
      "id": "codex",
      "status": "active",
      "required": true,
      "schema_id": "codex.rate_limits",
      "schema_version": 1
    },
    {
      "id": "feishu_project",
      "status": "active",
      "required": false,
      "schema_id": "feishu.project_card",
      "schema_version": 1
    }
  ],
  "timed_regions": [
    {
      "id": "clock",
      "interval_sec": 60,
      "bounds": {"x":8,"y":0,"width":234,"height":30}
    }
  ]
}
```

PageSettings：

```json
{"id":"home","bindings":{"codex":"codex/default"}}
```

校验规则：

- page ID 与 resource key 最长 64 bytes；slot ID 最长 32 bytes；
- 最多 8 个 binding，slot ID 不得重复；
- page 必须已注册；binding 的 slot 必须属于该 page；
- `reserved` slot 不可绑定；
- required active slot 必须绑定；optional active slot 可省略；
- 被绑定资源可以暂时不存在，此时页面得到 `missing`；
- 若资源已存在，`schema_id` 和 `schema_version` 必须与 slot 精确相等；
- 未知 page 在显示层显示诊断页，不回退到 Codex。

页面读取资源时只得到 slot 解析结果：`missing`、`invalid`、`stale`、`fresh`。`stale` 条件为 `ttl_sec > 0 && now - updated_at > ttl_sec`。资源变化仅在 key 属于活动 page binding 时安排整页构建；任一 binding 的 TTL 状态变化也会安排构建；无关资源不触发屏幕 IO。

## 9. Resource v4

```json
{
  "key": "codex/default",
  "schema_id": "codex.rate_limits",
  "schema_version": 1,
  "revision": 1723340000,
  "updated_at": 1723340000,
  "ttl_sec": 600,
  "persistence": "snapshot",
  "payload": {}
}
```

限制：最多 8 个资源；key/schema ID 最长 64 bytes；schema version、revision 必须大于 0；TTL 最大 604800；payload 必须为 map，MessagePack 编码后最多 4096 bytes；全部 snapshot 最多 16384 bytes。

Revision 规则：

- 新 revision 大于已存 revision：替换；
- 相同 revision 且完整内容 CRC 相同：幂等成功，`changed:false`；
- 相同 revision 但内容不同，或更小 revision：`conflict`；
- Publisher 应以 `max(unix_now, device_revision + 1)` 生成下一 revision；
- semantic payload 未变化时不必每分钟写 BLE；当前 Publisher 每 300 秒 heartbeat 一次，TTL 为 600 秒。

`snapshot` 会写 NVS，但最短自动写间隔为 1 小时；删除会强制保存。`volatile` 只驻留内存。

## 10. 事件

| name | data |
|---|---|
| `input.key` | `{}` |
| `battery.updated` | `{millivolts,percent}` |
| `display.started` | `{mode:"auto"|"full"}` |
| `display.completed` | `{result:"full"|"partial"|"unchanged"}` |

事件是提示，不是持久状态。丢事件后通过相应 GET RPC 重建状态。

## 11. 电池同步与时钟快路径

设备在 RTC 中分别维护 `next_sync_at` 与 `next_page_tick_at`。

- `next_sync_at` 到期或按键唤醒：进入完整 BLE 会话；
- 仅 `next_page_tick_at` 到期：校验 Unix 时间、PageSettings hash、RTC frame CRC 与 page identity；
- 校验通过：不初始化 BLE、不加载 resource snapshot，只用 `RuntimeContext` 局刷时钟 region，然后再次深睡；
- 校验失败：进入完整启动同步；
- mains 模式在整分钟执行相同 region render；
- Timed Region 不得读取 Resource，因此主机无需为时钟 tick 建立连接。

自动 battery 会话的正确结束条件是：本 cycle 相关 Producer 都上报完成，ResourcePublisher BLE 队列排空，然后 SyncCoordinator 唯一调用一次 `system.sync.complete`。手动管理连接不得自动调用该 RPC。

## 12. 主机验收清单

- 只发现 major 4，能提示用户删除 v3 配对并重新配对；
- TX indication 先订阅，分帧可处理事件穿插、CRC、超时和错误响应；
- BLE RPC 严格串行，重连后重新读取 capability/config/resource；
- Page 表单按 slot schema 过滤资源，reserved 禁用，optional 可空；
- revision 冲突时重新读取而不是盲目重试；
- Producer 不直接控制休眠，只有 Coordinator 发送 `system.sync.complete`；
- 将断连视为 battery 睡眠的正常结果；
- 云凭据只存在 Agent 数据源模块，不进入 Resource payload、浏览器或 ESP32。
