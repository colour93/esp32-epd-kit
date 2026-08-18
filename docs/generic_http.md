# 通用 HTTP + JMESPath 数据源

桌面 Agent 把 `http.jmespath` 注册为可多实例化、可自动同步的数据源类型。每个实例独立配置 HTTP JSON 请求、认证、JMESPath 映射、轮询周期和资源键。内置 preset 只提供 DeepSeek 与 Moonshot 余额映射；其他返回 JSON 的 HTTP 接口使用 `custom` 配置。

```text
HTTP JSON response
  -> JMESPath data / description / progress
  -> generic.metrics/v1
  -> http/{instance-id}
  -> Home 通用 Widget
```

固定值：

| 项目 | 值 |
|---|---|
| 数据源类型 ID | `http.jmespath` |
| 实例 ID | 用户创建，1–32 个安全字符 |
| 实例数量 | 最多 16 个 |
| Resource key | `http/{instance-id}` |
| schema | `generic.metrics/v1` |
| persistence | `snapshot` |
| poll | `interval_sec`，60–86400 秒 |
| TTL | `clamp(interval_sec * 3, 300, 604800)` 秒 |
| 请求超时 | `timeout_ms`，1000–30000 ms |
| 并发请求 | 最多 4 个实例 |
| 数据项 | 1–4 个 |

## 本机配置

配置文件为本机私有目录中的 `http-sources.json`，但其中不保存认证密钥。一个 custom 实例示例：

```json
{
  "id": "local-plan",
  "enabled": true,
  "title": "本地 Coding Plan",
  "preset": "custom",
  "interval_sec": 300,
  "timeout_ms": 10000,
  "method": "GET",
  "url": "http://127.0.0.1:8787/metrics",
  "network_access": "localhost",
  "headers": [
    { "name": "Accept", "value": "application/json" }
  ],
  "body": "",
  "auth": {
    "type": "header",
    "header_name": "X-Service-Key",
    "secret": "仅在写入请求中提交"
  },
  "items": [
    {
      "label": "剩余额度",
      "data_expression": "plan.remaining",
      "description_expression": "plan.name",
      "progress_expression": "plan.remaining_percent",
      "format": "percent"
    }
  ]
}
```

配置字段：

| 字段 | 说明 |
|---|---|
| `id` | 小写字母、数字、`-`、`_`；必须以字母或数字开头，创建后不可修改；`codex`、`cc-switch` 为保留 ID |
| `enabled` | 是否参与自动同步 |
| `title` | Resource 标题，最多 32 字符 |
| `preset` | `custom`、`deepseek_balance` 或 `moonshot_balance` |
| `interval_sec` | 自动轮询周期，60–86400 秒 |
| `timeout_ms` | 单次采集总预算，覆盖 DNS、连接、响应读取和投影，1000–30000 ms |
| `method` | `GET` 或 `POST` |
| `url` | HTTP URL，最长 4096 bytes |
| `network_access` | 精确选择 `public`、`private` 或 `localhost` 地址范围 |
| `headers` | 普通非敏感请求头，最多 32 项 |
| `body` | GET 必须为空；POST 可为空，非空时必须是 JSON |
| `auth.type` | `none`、`bearer` 或 `header` |
| `auth.header_name` | `header` 认证使用的请求头名，例如 `X-API-Key` |
| `auth.secret` | 仅写字段；设置或替换系统凭据库中的密钥 |
| `auth.clear_secret` | 仅写字段；为 `true` 时删除已有密钥，不能与 `secret` 同时提交 |
| `auth.secret_configured` | 只读字段；查询配置时只表示密钥是否存在 |
| `items` | 1–4 个通用指标映射 |
| `items[].label` | 指标标签，最多 24 字符 |
| `items[].data_expression` | 必填 JMESPath，最长 1024 bytes |
| `items[].description_expression` | 可选 JMESPath，最长 1024 bytes |
| `items[].progress_expression` | 可选 JMESPath，结果必须为数字，最长 1024 bytes |
| `items[].format` | `text`、`percent` 或 `countdown` |

`data_expression` 的字符串结果最多 48 字符，description 最多 96 字符；数组或对象会编码成截断后的紧凑 JSON 字符串。progress 超出 0–100 时截断。`countdown` 的 data 为 Unix seconds，由设备按当前时间显示距离重置的时长。

## 内置 Preset

DeepSeek 余额使用以下关键配置：

```json
{
  "preset": "deepseek_balance",
  "auth": {
    "type": "bearer",
    "secret": "DeepSeek API Key"
  }
}
```

Agent 会强制使用 `GET https://api.deepseek.com/user/balance`、`public` 网络和 Bearer 认证，清空自定义 headers/body，并生成：

| label | data_expression | description_expression |
|---|---|---|
| 总余额 | `balance_infos[0].total_balance` | `balance_infos[0].currency` |
| 充值 | `balance_infos[0].topped_up_balance` | `balance_infos[0].currency` |
| 赠送 | `balance_infos[0].granted_balance` | `balance_infos[0].currency` |

Moonshot 余额使用：

```json
{
  "preset": "moonshot_balance",
  "auth": {
    "type": "bearer",
    "secret": "Moonshot API Key"
  }
}
```

Agent 会强制使用 `GET https://api.moonshot.cn/v1/users/me/balance`、`public` 网络和 Bearer 认证，清空自定义 headers/body，并生成：

| label | data_expression |
|---|---|
| 可用 | `data.available_balance` |
| 现金 | `data.cash_balance` |
| 赠送 | `data.voucher_balance` |

选择 preset 后，请求方法、URL、网络范围、认证类型和指标映射均以 preset 为准。切换 preset 或修改凭据目标 host 时必须重新输入或明确清除密钥，旧平台密钥不会自动发送给新平台。切回 `custom` 后才能自由配置这些字段。

## Resource Schema

```json
{
  "key": "http/deepseek-main",
  "schema_id": "generic.metrics",
  "schema_version": 1,
  "revision": 1786441200,
  "updated_at": 1786441200,
  "ttl_sec": 900,
  "persistence": "snapshot",
  "payload": {
    "source_status": "ok",
    "title": "DeepSeek 余额",
    "items": [
      {
        "label": "总余额",
        "data": "42.50",
        "description": "CNY",
        "progress": null,
        "format": "text"
      },
      {
        "label": "充值",
        "data": "30.00",
        "description": "CNY",
        "progress": null,
        "format": "text"
      },
      {
        "label": "赠送",
        "data": "12.50",
        "description": "CNY",
        "progress": null,
        "format": "text"
      }
    ]
  }
}
```

payload 的 `source_status` 为 `ok`、`unconfigured` 或 `disabled`。未配置和禁用实例发布同一 schema 的空 items 状态 Resource。ESP32 只收到投影后的 `generic.metrics/v1`，不会收到 URL、headers、body、认证配置、密钥或原始 HTTP JSON。

## 凭据隔离

密钥只保存在系统凭据库，service 为 `dev.epd-kit.agent.http`：Windows 使用 Credential Manager，macOS 使用 Keychain，Linux 使用原生 Secret Service。凭据读取超时为 5 秒；写入和删除会等待系统操作真实完成，以保证配置与密钥不会分裂。没有文件 fallback，`http-sources.json` 永远不包含密钥；配置本身先完整写入同目录临时文件并同步，再原子替换。

查询实例时，`auth` 只用 `secret_configured` 表示密钥是否存在，不回传密钥。更新时省略 `secret` 会保留原密钥；提交新 `secret` 会替换；`clear_secret: true` 或把认证改为 `none` 会删除；删除实例也会删除对应凭据。

普通 `headers` 不能直接携带凭据，也不能配置 hop-by-hop headers。`Authorization`、`Proxy-Authorization`、`Cookie`、`Set-Cookie`、`X-API-Key` 及常见 auth/token/secret/password/credential/session-key 复合名称会被拒绝。URL 不允许用户名、密码或 fragment，query 与 POST JSON body 中相同的敏感字段名也会被拒绝；`input_tokens`、`max_tokens` 等普通用量字段不受影响。密钥应统一通过 `auth` 提交。

## 网络范围

| `network_access` | 允许的目标 | URL scheme |
|---|---|---|
| `public` | 仅公网 IPv4/IPv6 | 仅 HTTPS |
| `private` | RFC1918 IPv4、`100.64.0.0/10`、IPv6 ULA | HTTP 或 HTTPS |
| `localhost` | IPv4/IPv6 loopback | HTTP 或 HTTPS |

三个级别互不包含。Agent 不使用环境或系统代理，不跟随重定向；请求前先解析 DNS，要求返回的每个地址都属于所选范围，再把第一个已验证地址固定到 HTTP 客户端。link-local、文档保留段、组播等不属于任一允许范围的地址始终被拒绝。

## 限制与失败行为

- header name/value 最长 128/4096 bytes，普通 headers 不得重名；认证 header 不得在普通 headers 中重复；
- secret 最长 8192 bytes，POST body 最大 64 KiB；
- 只接受 2xx 响应和有效 JSON；解压并流式读取后的响应最大 256 KiB；
- HTTP、DNS、凭据库或 JMESPath 失败时，实例进入 `degraded` 并保留上一份 Resource；缺少必需密钥时进入 `auth_required`；
- 旧 Resource 超过 TTL 后由固件显示为 stale；删除实例会注销 Resource 并删除凭据。

当前固件最多保存 16 个 Resource，这个总数由 Codex、CC Switch、CLI、HTTP 和手工 Resource 共同占用；HTTP 的 16 个本机配置槽不等于 16 个设备资源槽。超过设备容量的发布会进入 `degraded`，部署时应按设备 `max_resources` 留出余量。

通用 Widget 与 CLI 数据源相同：双组件 Home 提供双数据、单数值、条形进度和环形进度；三组件 Home 提供后三种单数据组件。Widget ID 的 `.1` 到 `.4` 对应 items 数组索引。
