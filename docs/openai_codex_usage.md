# Codex Rate Limits Schema 与 Producer

本文是 `codex.rate_limits/v1` 的权威 schema 文档，同时描述 Agent 内本机 Codex 与独立 OAuth 两种 Producer 的数据来源与投影规则。

## 1. 边界

```text
codex app-server --listen stdio://
  -> Codex Producer
OpenAI OAuth + chatgpt.com/backend-api/wham/usage
  -> Codex OAuth Producer
  -> codex.rate_limits/v1
  -> ResourcePublisher
  -> BLE v4
  -> ResourceStore
  -> CodexUsageModel
  -> Full / Compact Widget
```

默认 `codex.usage` Producer 复用本机 Codex 已有登录，只调用官方 app-server stdio 接口。可选 `codex.oauth` Producer 使用 Codex CLI 官方 OAuth client、PKCE 和手动回调 URL 完成独立登录，直接读取 ChatGPT Codex 额度并自动刷新 token；它不要求安装、启动或登录 Codex。

OAuth access token、refresh token 与 ID token 只保存在 Agent 的系统凭据库。浏览器只接收授权 URL、会话 ID和脱敏账号状态，ESP32 只接收 Resource payload；两者都不会收到 token、cookie 或系统凭据内容。

## 2. Resource Envelope

固定值：

| 字段 | 值 |
|---|---|
| key | 本机账号为 `codex/default`；OAuth 账号为 `codex/{source_id}` |
| schema_id | `codex.rate_limits` |
| schema_version | `1` |
| ttl_sec | `600` |
| persistence | `snapshot` |

本机 Producer 还发布 `codex/metrics`，OAuth Producer 发布 `codex/{source_id}/metrics`，schema 均为 `generic.metrics/v1`。资源按固定顺序提供 5h 剩余、7d 剩余、5h 重置倒计时和 7d 重置倒计时，使 Home 的通用组件可以选择双数据或任一单项。

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
- `account/rateLimits/updated` 通知触发事件驱动刷新；
- 60 秒正常轮询作为通知丢失或上游不支持通知时的兜底；
- Web 手动刷新；
- battery auto-sync cycle。

事件刷新和兜底轮询都进入同一条 `sync_once -> ResourcePublisher` 链路。读取失败时轮询退避从 60 秒增长到最多 900 秒。旧设备 Resource 不会因一次采集失败而删除；超过 600 秒 TTL 后由固件标记 stale。

Publisher 规则：

- semantic payload 变化立即写；
- 不变时每 300 秒 heartbeat；
- 断连时缓存最新 payload；
- 重连后 reconcile；
- revision 为 `max(unix_now, device_revision + 1)`；
- 写失败不更新 sent hash/timestamp。

因此 mains 设备上的数据交付是 Agent 到设备的反向推送，不依赖设备每分钟请求。后续实时数据源（例如 Codex 任务状态）应订阅其上游事件，在语义 payload 变化时直接调用 `ResourcePublisher::publish`；Publisher 负责去重、断线缓存和重连补发。周期轮询只作为可选兜底。battery 设备休眠期间无法接收推送，仍由下一次 auto-sync cycle 拉齐最新缓存。

收到 `SyncCycle(id)` 后，无论成功、未登录、找不到 Codex 或 app-server 不可用，Producer 都必须通过 Publisher 报告一次 cycle completion。只有 Coordinator 在全部相关 Producer 完成且 Publisher 排空后调用 `system.sync.complete`。

### 5.1 OAuth 多账号

`codex.oauth` 是可配置、多实例 Producer，每个账号具有独立 source ID、资源键、启用状态和 60 至 3600 秒轮询间隔，最多 16 个账号。非敏感配置保存于本机私有 `codex-oauth-sources.json`；完整 OAuth 凭据按 source ID 保存到系统 keyring。

登录使用 `https://auth.openai.com/oauth/authorize`、S256 PKCE、`offline_access` scope 和固定回调 `http://localhost:1455/auth/callback`。用户把最终回调 URL 交回 Agent 后，Agent校验 `state` 并向 `https://auth.openai.com/oauth/token` 交换 token。OAuth 会话 30 分钟过期，成功交换后立即销毁。

采集前若 access token 将在 120 秒内过期，Agent 使用 refresh token 主动续期；额度接口返回 401 时强制续期并只重试一次。刷新响应未轮换 refresh token 时保留原值，轮换时原子覆盖 keyring 中的账号凭据。额度读取使用 `https://chatgpt.com/backend-api/wham/usage`，并携带 `chatgpt-account-id` 与 Codex 请求头。

### 5.2 本机任务状态

`codex.tasks` Producer 每 2 秒通过 app-server `thread/list` 获取最近任务及 rollout 路径，并增量读取 rollout 中的 `task_started`、`task_complete` 与 `turn_aborted` 事件。另一个 app-server 进程看到的 Desktop 线程状态固定为 `notLoaded`，因此不能使用 `thread/status/changed` 判断跨进程执行态。

Producer 发布 `codex/tasks`，schema 为 `generic.metrics/v1`，最多包含 4 个最近任务，正在执行的任务排在最前。每项使用项目目录名作为 label、`执行中` / `已完成` / `已中止` 作为 data、任务标题作为 description。资源 TTL 为 30 秒且 persistence 为 `volatile`；语义状态不变时 Publisher 不写设备。Home 页面可用任意 `generic.metric.value.1..4` Widget 显示对应任务。

## 6. 固件消费

专用 Page/Widget 默认绑定 `codex/default`，也可以绑定任一 OAuth 账号的 `codex/{source_id}`：

| Page | slot | Widget |
|---|---|---|
| `home` | optional active `primary` / `secondary` | `CodexUsageCompactWidget` |
| `codex.usage` | required active `codex` | `CodexUsageFullWidget` |

Home 也可把任意 slot 绑定到 `codex/metrics`，使用 `generic.metric.dual` 或 `generic.metric.{value,bar,ring}.1..4`。`home.three` 只声明单数据项通用 Widget。

`CodexUsageModel::fromSlot` 先处理 `missing/invalid/stale/fresh`，再校验 payload。`selected` 缺失或两个窗口均不可用时，fresh Resource 映射为数据异常。stale Resource 可保留最后值并明确显示过期。

## 7. 隐私与兼容性

payload 不得包含 email、token、cookie、Codex 可执行文件路径或 Agent 本地目录。email、账号类型、token 到期时间和路径只可出现在 Agent 本地 `sources[].details`。

v1 新增 optional 字段时，旧 Widget 必须能忽略；删除字段、改变类型、单位或含义时必须升级 schema version，并为消费它的 PageSlot/Model 同步升级。BLE 与 Page 契约见 [BLE Protocol v4](ble_protocol_v4.md)，新增组件步骤见 [功能组件开发规范](feature_component_development.md)。
