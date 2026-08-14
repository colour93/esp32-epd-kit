# 通用 CLI + JMESPath 数据源

桌面 Agent 把 `cli.jmespath` 注册为可多实例化的数据源类型。用户创建的每个实例都保存独立 CLI、JMESPath 映射、运行状态和资源键。类型不包含飞书、Meegle、认证或业务字段约束。

```text
任意 CLI JSON stdout
  -> JMESPath data / description / progress
  -> generic.metrics/v1
  -> cli/{instance-id}
  -> Home 通用 Widget
```

固定值：

| 项目 | 值 |
|---|---|
| 数据源类型 ID | `cli.jmespath` |
| 实例 ID | 用户创建，1–32 个安全字符 |
| Resource key | `cli/{instance-id}` |
| schema | `generic.metrics/v1` |
| TTL / poll | 900 秒 / 300 秒 |
| 数据项 | 1–4 个 |

## 本机配置

```json
{
  "sources": [
    {
      "id": "team-issues",
      "enabled": true,
      "title": "我的进行中缺陷",
      "command": "meegle workitem query ... --format json",
      "items": [
        {
          "label": "缺陷",
          "data_expression": "length(data)",
          "description_expression": "session_id",
          "progress_expression": "percent",
          "format": "text"
        }
      ]
    }
  ]
}
```

配置文件为本机私有目录中的 `cli-sources.json`。实例 ID 只能包含小写字母、数字、`-`、`_`，必须以字母或数字开头，创建后不可修改。Agent 用 `shell-words` 拆分命令后直接启动可执行文件，不经 shell，因此不会解释管道、重定向、变量替换或命令替换。CLI 必须把有效 JSON 写到 stdout。执行超时 30 秒，命令最长 8192 bytes，stdout 最大 256 KiB。

`data_expression` 与 `label` 必填；`description_expression` 和 `progress_expression` 可空。progress 必须得到 0–100 的数字，超出范围时截断。format 支持：

- `text`：直接显示 data；
- `percent`：显示百分号，progress 可供条形或环形组件使用；
- `countdown`：data 是 Unix seconds，设备按当前时间显示距离重置的时长。

## Resource Schema

```json
{
  "key": "cli/team-issues",
  "schema_id": "generic.metrics",
  "schema_version": 1,
  "revision": 1786441200,
  "updated_at": 1786441200,
  "ttl_sec": 900,
  "persistence": "snapshot",
  "payload": {
    "source_status": "ok",
    "title": "我的进行中缺陷",
    "items": [
      {
        "label": "缺陷",
        "data": 3,
        "description": "2 个处理中，1 个待验证",
        "progress": 60,
        "format": "text"
      }
    ]
  }
}
```

payload 规则：`source_status` 为 `ok`、`unconfigured` 或 `disabled`；title 最多 32 字符；items 最多 4 个；每项包含 label 与标量 data，可选 description、progress 和 format。数组或对象投影会编码成截断后的紧凑 JSON 字符串。

## 通用 Widget

双组件 Home 提供双数据、单数值、条形进度和环形进度；三组件 Home 只提供后三种单数据组件。Widget ID 的 `.1` 到 `.4` 对应 items 数组索引。数据源只要发布 `generic.metrics/v1` 就可以复用这些组件，无需新增固件业务组件。

Codex Producer 也发布 `codex/metrics`：数据 1/2 是 5h/7d 剩余百分比，数据 3/4 是对应重置倒计时。因此 Codex 可以在双组件模式显示两个窗口，也可以在三组件模式分别选择窗口或重置时间。

CLI、JMESPath 或传输失败时该实例保留上一份 Resource 并独立报告错误；旧资源超过 TTL 后由固件显示为 stale。删除实例会注销对应资源。ESP32 不执行 CLI，也不会收到命令、参数、环境变量、凭据或原始 stdout。
