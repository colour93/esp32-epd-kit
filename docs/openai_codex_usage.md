# Codex Rate Limits Schema 与 Producer

本文是 `codex.rate_limits/v1` 的权威 schema 文档，同时描述 Agent 内 `codex.usage` Producer 的数据来源与投影规则。

## 1. 边界

```text
codex app-server --listen stdio://
  -> Codex Producer
  -> codex.rate_limits/v1
  -> ResourcePublisher
  -> BLE v4
  -> ResourceStore
  -> CodexUsageModel
  -> Full / Compact Widget
```

Agent 复用本机 Codex 已有登录，只调用官方 app-server stdio 接口。Agent 不实现 Codex 登录、登出、OAuth、token refresh 或 ChatGPT HTTP 请求。ESP32 与浏览器均不会收到 Codex token、cookie 或 refresh token。

## 2. Resource Envelope

固定值：

| 字段 | 值 |
|---|---|
| key | `codex/default` |
| schema_id | `codex.rate_limits` |
| schema_version | `1` |
| ttl_sec | `600` |
| persistence | `snapshot` |

`revision` 与 `updated_at` 由 `ResourcePublisher` 写入，Producer 不生成。

完整示例：

```json
{
  "key": "codex/default",
  "schema_id": "codex.rate_limits",
  "schema_version": 1,
  "revision": 1786406400,
  "updated_at": 1786406400,
  "ttl_sec": 600,
  "persistence": "snapshot",
  "payload": {
    "source_status": "ok",
    "plan_type": "plus",
    "limit_reached": false,
    "selected": {
      "limit_id": "codex",
      "limit_name": "Codex",
      "plan_type": "plus",
      "primary": {
        "used_percent": 24,
        "window_duration_mins": 300,
        "resets_at": 1786417200
      },
      "secondary": {
        "used_percent": 41,
        "window_duration_mins": 10080,
        "resets_at": 1786752000
      },
      "rate_limit_reached_type": null,
      "credits": null
    },
    "limits": [],
    "rate_limit_reset_credits": null
  }
}
```

## 3. Payload 字段

### 3.1 顶层

| 字段 | 类型 | 必填 | 含义 |
|---|---|---|---|
| `source_status` | string | 是 | 当前固定为 `ok`；未来业务错误需升级实现文档 |
| `plan_type` | string | 是 | app-server bucket 或账号返回的计划类型；未知时 `unknown` |
| `limit_reached` | boolean | 是 | selected bucket 是否存在非 null 的 reached type |
| `selected` | Bucket | 是 | 设备主要展示的 Codex bucket |
| `limits` | Bucket[] | 是 | app-server 返回的全部 bucket；顺序不构成契约 |
| `rate_limit_reset_credits` | JSON/null | 是 | app-server 原样语义值；v1 Widget 不读取 |

### 3.2 Bucket

| 字段 | 类型 | 可为 null | 含义 |
|---|---|---|---|
| `limit_id` | string/null | 是 | 限额标识 |
| `limit_name` | string/null | 是 | 人类可读名称 |
| `plan_type` | string/null | 是 | bucket 自带计划类型 |
| `primary` | Window/null | 是 | 主窗口 |
| `secondary` | Window/null | 是 | 次窗口 |
| `rate_limit_reached_type` | string/null | 是 | 达限原因 |
| `credits` | JSON/null | 是 | app-server credits 语义；v1 Widget 不读取 |

### 3.3 Window

| 字段 | 类型 | 单位/范围 | 含义 |
|---|---|---|---|
| `used_percent` | unsigned integer | 0..100 | 已使用百分比 |
| `window_duration_mins` | unsigned integer/null | 分钟 | 窗口长度 |
| `resets_at` | unsigned integer/null | Unix seconds | 重置时刻 |

设备 Widget 显示剩余百分比 `100 - used_percent`。固件 v1 参考布局识别 300 分钟为“5 小时”、10080 分钟为“7 天”；其他时长仍可传输，但显示为未知窗口。

## 4. app-server 投影

Producer 使用：

- `account/read`，参数 `refreshToken:false`；
- `account/rateLimits/read`；
- `account/rateLimits/updated`，仅作为刷新触发器。

选择规则：优先 `rateLimitsByLimitId["codex"]`，否则使用 `rateLimits`。字段投影：

| app-server | payload |
|---|---|
| `limitId` | `limit_id` |
| `limitName` | `limit_name` |
| `planType` | `plan_type` |
| `usedPercent` | `used_percent` |
| `windowDurationMins` | `window_duration_mins` |
| `resetsAt` | `resets_at` |
| `rateLimitReachedType` | `rate_limit_reached_type` |

支持的账号类型为 `chatgpt` 与 `chatgptAuthTokens`。API-key-only、未登录和未知类型进入 Producer `auth_required` 状态，不启动登录流程。

## 5. Producer 生命周期

`EPD_CODEX_PATH`、`PATH` 和常见 Homebrew 路径用于定位 `codex`。Producer 启动 `codex app-server --listen stdio://`，初始化一次 stdio session；进程退出或协议失败时按 1 到 60 秒指数退避重启。

刷新触发：

- Producer 启动；
- 60 秒正常轮询；
- `account/rateLimits/updated` 通知；
- Web 手动刷新；
- battery auto-sync cycle。

读取失败时轮询退避从 60 秒增长到最多 900 秒。旧设备 Resource 不会因一次采集失败而删除；超过 600 秒 TTL 后由固件标记 stale。

Publisher 规则：

- semantic payload 变化立即写；
- 不变时每 300 秒 heartbeat；
- 断连时缓存最新 payload；
- 重连后 reconcile；
- revision 为 `max(unix_now, device_revision + 1)`；
- 写失败不更新 sent hash/timestamp。

收到 `SyncCycle(id)` 后，无论成功、未登录、找不到 Codex 或 app-server 不可用，Producer 都必须通过 Publisher 报告一次 cycle completion。只有 Coordinator 在全部相关 Producer 完成且 Publisher 排空后调用 `system.sync.complete`。

## 6. 固件消费

两个 Page 绑定同一 Resource：

| Page | slot | Widget |
|---|---|---|
| `home` | required active `codex` | `CodexUsageCompactWidget` |
| `codex.usage` | required active `codex` | `CodexUsageFullWidget` |

`CodexUsageModel::fromSlot` 先处理 `missing/invalid/stale/fresh`，再校验 payload。`selected` 缺失或两个窗口均不可用时，fresh Resource 映射为数据异常。stale Resource 可保留最后值并明确显示过期。

## 7. 隐私与兼容性

payload 不得包含 email、token、cookie、Codex 可执行文件路径或 Agent 本地目录。email、账号类型和路径只可出现在 Agent 本地 `producers[].details`。

v1 新增 optional 字段时，旧 Widget 必须能忽略；删除字段、改变类型、单位或含义时必须升级 schema version，并为消费它的 PageSlot/Model 同步升级。BLE 与 Page 契约见 [BLE Protocol v4](ble_protocol_v4.md)，新增组件步骤见 [功能组件开发规范](feature_component_development.md)。
