# 飞书项目卡片

本文定义飞书项目扩展在 v4 架构中的 schema、配置和展示边界。数据源由桌面 Agent 执行用户配置的 Meegle CLI 命令，并通过 JMESPath 表达式投影为最小语义 Resource。

## 架构与实例

```text
用户配置的 Meegle CLI 命令
  -> Agent JSON 解码
  -> JMESPath value/detail 投影
  -> feishu.project_card/v1
  -> ResourcePublisher
  -> feishu/default
  -> Home feishu_project slot
```

- Producer ID：`feishu.project`
- Resource key：`feishu/default`
- schema ID/version：`feishu.project_card/v1`
- Home slot：`feishu_project`，optional active
- TTL：900 秒
- 常规轮询：300 秒
- battery auto-sync：参与

一个 Agent 当前保存一份卡片配置，对应一个 `feishu/default` 实例。需要展示不同项目或不同问题时，用户直接修改命令与表达式；未来如支持多卡片，应增加新的稳定 resource key，而不是改变现有 key 的含义。

## Agent 配置

完整配置只保存在 Agent 本机私有文件中，不进入 ESP32 或 BLE Resource；Producer snapshot details 只暴露展示名和运行状态，不包含命令或表达式：

```json
{
  "enabled": true,
  "display_name": "我的进行中缺陷",
  "command": "meegle workitem query --project-key 68701aedc892a1674bc53400 --mql 'SELECT `name`, `work_item_status`, `current_status_operator` FROM `68701aedc892a1674bc53400`.`issue` WHERE array_contains(`current_status_operator`, current_login_user())' --format json",
  "value_expression": "length(data)",
  "detail_expression": "session_id"
}
```

约束：

- `command` 的可执行文件必须是 `meegle`，并且输出格式必须为 JSON；Agent 不通过 shell 执行命令；
- 每次业务查询前，Agent 使用同一个 Meegle 可执行文件检查 `auth status`；未登录时进入 `auth_required`；
- 命令最长 8192 bytes，执行超时 30 秒，stdout 最大 256 KiB；
- `value_expression` 与 `detail_expression` 使用 JMESPath；value 必填，detail 可空；
- 表达式结果为字符串时直接使用，number/bool 转成字面值，array/object 编码成紧凑 JSON，null 转成 `--`；
- Web 可以用草稿配置执行一次测试；只有保存后才影响 Producer。

## Resource Schema

payload 字段：

| 字段 | 类型 | 必填 | 语义与限制 |
|---|---|---|---|
| `source_status` | string enum | 是 | `ok` / `unconfigured` / `disabled` |
| `display_name` | string | 是 | 用户配置的展示名，最多 32 个 Unicode 字符 |
| `value` | string | `ok` 时是 | value 表达式结果，最多 48 个 Unicode 字符 |
| `detail` | string | 否 | detail 表达式结果，最多 96 个 Unicode 字符；空结果省略 |

`unconfigured` 表示没有完整命令或表达式；`disabled` 表示配置完整但被用户停用。CLI、认证或表达式执行失败时 Producer 保留上一份 Resource，并在 Agent 状态中报告错误；旧 Resource 最终由 TTL 转为 stale，不把 transport error 伪装成 fresh 值。

示例：

```json
{
  "key": "feishu/default",
  "schema_id": "feishu.project_card",
  "schema_version": 1,
  "revision": 1786441200,
  "updated_at": 1786441200,
  "ttl_sec": 900,
  "persistence": "snapshot",
  "payload": {
    "source_status": "ok",
    "display_name": "我的进行中缺陷",
    "value": "3",
    "detail": "2 个处理中，1 个待验证"
  }
}
```

编码后的 payload 必须小于 4096 bytes；Agent 的字段上限会使正常 payload 远低于该限制。

## 显示与状态

Home Compact Widget 使用固定 `{8,86,234,34}` bounds：第一行显示展示名和 value，第二行显示 detail 或状态。状态映射：

| Resource/业务状态 | 屏幕文本 |
|---|---|
| binding/resource missing | 等待数据 |
| schema/version/字段 invalid | 数据异常 |
| stale | 保留 value，第二行显示“数据已过期” |
| `unconfigured` | 未配置 |
| `disabled` | 已停用 |
| fresh + `ok` | 用户投影的 value/detail |

## 隐私边界

- ESP32 不执行 OAuth、HTTP、MCP 或 Meegle CLI；
- ESP32 NVS 和 Resource payload 不保存 command、MQL、project key、表达式、token、cookie 或本地路径；
- 浏览器只通过 loopback Agent API 读写配置，Agent API 继续使用本地 session 与 Origin 校验；
- Producer details 只暴露启用状态、展示名、结果大小和耗时，不回传命令 stdout；
- Meegle CLI 自身负责凭据存储和 OAuth，Agent 不复制 refresh token。
