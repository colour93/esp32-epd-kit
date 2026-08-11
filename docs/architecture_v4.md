# EPD-KIT v4 架构

本文定义固件 `0.3.x`、Agent `0.2.x` 与 Web 工作台的系统边界和运行时关系。协议细节以 [BLE Protocol v4](ble_protocol_v4.md) 为准；新增功能的逐项做法以 [功能组件开发规范](feature_component_development.md) 为准。

## 1. 设计目标

v4 把页面、数据源和绘制组件解耦：

```text
数据源 -> Producer -> SemanticResource -> ResourcePublisher -> BLE v4
                                                            |
                                                            v
ResourceStore -> Page Binding -> PageResources -> Model -> Widget
                                      ^                       |
                                      |                       v
RuntimeContext -----------------------+--------------------> LVGL -> EPD
```

- Page 是顶层 UI unit，可以编排多个 Resource Slot 和多个 Widget；
- Resource 是带 schema/version 的语义数据，不包含布局或云凭据；
- Producer 与 Page 不要求一一对应；
- Widget 可被多个 Page 复用，只消费已解析 Model；
- 时钟等纯运行时内容通过 Timed Region 局部刷新；
- 页面和 Producer 都使用编译期 Registry，不支持运行时插件或代码生成。

## 2. 组件职责

### 2.1 固件

| 组件 | 职责 | 不负责 |
|---|---|---|
| `ResourceStore` | 校验、revision 冲突、TTL 元数据、snapshot 持久化 | 页面选择、云请求 |
| `PageRegistry` | 固定容量注册和按 ID 查找 Page | fallback、动态加载 |
| `PageResources` | 按 slot 解析 binding，返回资源及 freshness 状态 | 业务 payload 解析 |
| Model | schema payload 到确定类型的投影 | BLE、屏幕提交 |
| Widget | 在稳定 bounds 内绘制 Model | 读取 Resource、调度刷新 |
| Page | 声明 manifest，组合 Widget 和错误态 | 遍历 ResourceStore |
| `DisplayManager` | LVGL target、frame diff、局刷/全刷、RTC frame | 数据采集 |
| `BleProtocolService` | v4 分帧、RPC、权限、配置和事件 | 访问云服务 |

Page manifest 包含 active/reserved slots 与 timed regions。active slot 精确声明 schema ID/version；reserved slot 不可绑定。`PageSettings` 只保存 Page ID 与 slot 到 resource key 的映射。

### 2.2 Agent

| 组件 | 职责 |
|---|---|
| `Producer` | 数据源认证、采集、字段投影、业务状态 |
| `ProducerRegistry` | 编译期注册、重复 ID 拒绝、按 ID 刷新、auto-sync 集合 |
| `ResourcePublisher` | revision、语义 hash、300 秒 heartbeat、重连 reconcile、串行 BLE 写 |
| `SyncCoordinator` | battery 自动连接的 cycle 生命周期和唯一 `system.sync.complete` 调用 |
| `BleGateway` | 扫描、目标恢复、配对、v4 RPC 串行化和状态 reload |
| `SharedState` | 通用 `producers[]`、设备状态、日志和 SSE snapshot |

Producer 发布的 `SemanticResource` 不含 `revision` 或 `updated_at`。这两个字段只由 Publisher 在写设备时生成。Producer 不直接调用 `resource.put`、`resource.list` 或 `system.sync.complete`。

### 2.3 Web

React 只访问 Agent 的 loopback HTTP/SSE，不使用 Web Bluetooth，也不直接访问云服务。

- `capabilities.pages` 动态生成 Page/Slot/Binding 表单；
- active slot 只显示 schema/version 精确匹配的 Resource；
- `producers[]` 动态生成 Producer 状态和刷新入口；
- owner-only Resource JSON PUT 用于新 schema 联调；
- Codex 账号等专有信息位于对应 Producer `details`，不进入设备公共模型。

## 3. 数据与控制流

### 3.1 常规资源更新

1. Producer 采集数据并生成 semantic payload。
2. Publisher 计算 payload hash。
3. payload 变化或 300 秒 heartbeat 到期时，Publisher 生成单调 revision 和 `updated_at`。
4. `resource.put` 进入 ResourceStore。
5. 只有该 key 属于活动 Page binding 时，固件安排整页重绘。
6. `DisplayManager` 比较新旧 frame；像素未变化时不执行屏幕 IO。

Resource freshness 由 `updated_at + ttl_sec` 决定。固件对活动 Page 的所有 binding 计算 freshness signature；任一资源跨越 missing/invalid/stale/fresh 状态边界时安排重绘。无关资源不触发屏幕 IO。

### 3.2 Page 切换

`page.set` 接收完整 `PageSettings` 并执行：

1. Page ID 必须已注册；
2. slot 必须属于该 Page；
3. reserved slot 禁止绑定；
4. required active slot 必须绑定；
5. 已存在 Resource 必须精确匹配 schema/version；
6. 配置原子保存成功后切换活动 Page 并安排重绘。

目标 Resource 可以尚不存在，此时 Page 获得 `missing`。未知 Page 不回退到 Codex，而是显示诊断页。

### 3.3 Battery 自动同步

```text
BLE auto connected
  -> Coordinator 创建唯一 cycle
  -> 触发所有 auto_sync Producer
  -> Producer 发布或报告失败
  -> completion 经 Publisher 排队
  -> 所有 Producer 完成
  -> Publisher flush barrier
  -> Coordinator 调用 system.sync.complete
  -> 固件完成 render/indication 后深睡
```

手动连接不自动结束会话。Producer 无论成功、认证缺失或数据源不可用，都必须完成收到的 cycle，避免设备只能等待 120 秒超时。

## 4. Timed Region 与 RTC

固件在 RTC 中保留：

- 上次逻辑 framebuffer、CRC、局刷次数和上次全刷时间；
- PageSettings identity hash；
- `next_sync_at` 与 `next_page_tick_at`；
- UTC offset。

市电模式在 region 周期边界局部构建。电池模式若仅时钟 deadline 到期，则校验时间、frame CRC 和 Page hash；通过后不初始化 BLE、不加载 Resource snapshot，只从保留 frame 重绘 region 并再次深睡。校验失败则进入完整启动流程。

Timed Region 只能读取 `RuntimeContext`。当前调度器只支持每个 Page 的第一个 region；增加多个 region 前必须扩展为逐 region deadline。

## 5. 刷新策略

`DisplayManager::present()` 根据实际像素 dirty rect 决定刷新：

- 没有 dirty pixel 且未强制全刷：不进行屏幕 IO；
- dirty 面积达到阈值：全刷；
- 局刷次数达到阈值：全刷；
- 距上次全刷超过最大时间：全刷；
- 其他情况：只刷新 dirty rect。

v4 默认阈值为 60 次局刷、24 小时或 70% dirty area。Home 时钟每分钟更新，因此 60 次阈值约每小时进行一次全刷。

## 6. 版本与持久化边界

v4 使用独立标识：

| 项目 | v4 值 |
|---|---|
| Config schema | `4` |
| Config slot magic | `CFG4` |
| Config namespace | `epd_cfg4` |
| Resource namespace | `epd_res4` |
| Security namespace | `epd_sec4` |
| BLE service prefix | `f0a4` |
| frame magic | `0xe4` |

固件不读取 v3 namespace，不提供 migration。首次缺少有效 v4 配置时创建默认 `home` 配置并清除 NimBLE bonds，主机必须重新配对。

## 7. 当前参考实现

- `home`：默认 Page，组合时钟、Codex Compact Widget 和不可绑定的飞书预留区；
- `codex.usage`：Codex 完整 Page；
- `codex.rate_limits/v1`：两个 Page 共享的语义 Resource；
- `codex.usage` Producer：当前唯一 Producer，读取本机 Codex app-server；
- 飞书：本版只保留 reserved slot，不定义 schema 或 Producer。

新增功能应从 schema 和 Producer/Page 边界开始，完整步骤见 [功能组件开发规范](feature_component_development.md)。
