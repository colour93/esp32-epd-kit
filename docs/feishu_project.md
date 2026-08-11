# 飞书项目扩展方向

本文记录飞书项目在 v4 架构中的产品边界。当前版本不实现飞书 schema、Producer、OAuth 或查询逻辑。

## 当前状态

`home` Page manifest 包含 reserved slot：

```cpp
{"feishu_project", nullptr, 0, false, SlotStatus::kReserved}
```

该 slot 不可绑定，屏幕固定显示“未配置”。它只用于明确未来布局位置，不表示协议已经支持飞书 payload。

## 强制架构边界

未来飞书功能必须实现为桌面 Agent Producer：

```text
飞书项目 API/MCP
  -> Agent 内 Feishu Producer
  -> versioned semantic Resource
  -> ResourcePublisher
  -> BLE v4
  -> Home active slot / 独立 Page
```

- ESP32 不执行 OAuth discovery、device code、token refresh 或 HTTP/MCP 请求；
- ESP32 NVS 不保存 `client_id`、`access_token`、`refresh_token`、cookie 或 client secret；
- Resource payload 不包含任何凭据；
- 浏览器只控制本机 Agent，不接收 refresh token；
- BLE 只传输页面需要的最小语义结果。

旧的“ESP32 保存 OAuth 凭据并直接查询飞书”方向已废弃，不得据此实现。

## 后续实施前置决策

实现前必须先确定并写成 schema 文档：

1. 页面要回答的具体问题，例如我的进行中事项、项目健康度或迭代进度；
2. Resource key 的实例模型，单账号、单空间还是单项目；
3. payload 字段、状态枚举、时间单位、最大条目数和隐私裁剪；
4. TTL、Producer 轮询周期、battery auto-sync 参与策略；
5. Agent 本地凭据存储和重新授权流程；
6. Home Compact Widget 与可选完整 Page 的稳定 bounds。

这些决策完成前，不应给 reserved slot 补虚构的 schema/version。

## 预期改造步骤

1. 按 [功能组件开发规范](feature_component_development.md) 编写 `feishu.*` schema 文档。
2. 在 Agent 新增 Feishu Producer，凭据和 API 客户端全部封装在模块内。
3. Producer 仅发布 `SemanticResource`，不直接调用 BLE 或控制休眠。
4. 固件新增 Model 与 Compact/Full Widget。
5. 把 Home 的 `feishu_project` 从 reserved 改为 active slot，声明精确 schema/version。
6. 如有需要新增独立 Page，并在编译期 Registry 注册。
7. 通过通用 Web Resource 编辑器先验证 schema，再接入真实 Producer。

飞书认证端点和 MCP tool 的实际可用性必须在实施时依据官方资料重新验证；本文不冻结尚未实现的第三方接口细节。
