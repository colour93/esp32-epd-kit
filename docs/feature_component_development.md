# 功能组件开发规范

本文是 EPD-KIT v4 新功能开发的单一权威规范。目标是：只阅读本文和目标 schema 文档，即可实现新的 Resource、Widget、Page、Agent Producer，并自动接入通用 Web 管理界面，不依赖隐含步骤。

## 1. 核心边界

```text
云服务或本机数据源
        │ 凭据只在 Agent
        ▼
Producer -> semantic payload -> ResourcePublisher -> BLE resource.put
                                                  │
                                                  ▼
Page binding -> PageResources -> parsed Model -> Widget -> LVGL
```

- **Resource**：有版本的语义数据，不包含布局和凭据。
- **Producer**：Agent 内的数据采集与 payload 投影单元。
- **Page**：设备上的顶层编排单元，可依赖多个 Resource Slot。
- **Slot**：Page 对 Resource schema 的精确类型依赖。
- **Model**：把 `SlotResource` 转成 widget 可用的确定类型。
- **Widget**：只绘制已解析 model，可被多个 Page 复用。
- **Timed Region**：只依赖 `RuntimeContext` 的局部定时区域。

不得恢复“一页对应一个数据源”或“一 renderer 对应一个 resource”的设计。Page 可以组合任意多个 slot；同一 schema 可以有多个 widget；同一 Resource 可以绑定到多个 Page。

## 2. 目录与命名

当前仓库保持小型、显式目录，不使用代码生成。

固件：

```text
include/toolkit/<feature>_app.h     # Model、Widget、Page 声明
src/<feature>_app.cpp               # schema 解析与绘制
include/toolkit/<page>_page.h       # 纯编排 Page 可单独建文件
src/<page>_page.cpp
src/main.cpp                        # 唯一 Page 注册点
```

Agent/Web：

```text
agent/src/<feature>.rs              # 数据源类型、实例管理与采集客户端
agent/src/producer.rs               # 公共 Producer 契约
agent/src/publisher.rs              # 公共发布器，不放业务逻辑
agent/src/coordinator.rs            # 公共同步 cycle
agent/src/main.rs                   # 唯一 Producer 注册点
src/lib/agent.ts                    # 通用协议类型；新数据源类型通常不改
src/App.tsx                         # 通用 Page/Resource/数据源 UI；新类型通常不改
```

ID 规则：

| 类型 | 形式 | 示例 |
|---|---|---|
| schema ID | `<domain>.<noun>` | `codex.rate_limits` |
| Page ID | `<domain>.<page>` 或稳定短名 | `codex.usage`、`home` |
| 数据源类型 ID | `<domain>.<feature>` | `codex.usage`、`cli.jmespath` |
| 数据源实例 ID | 稳定安全短名 | `team-issues` |
| Resource key | `<domain>/<instance>` | `codex/default` |
| slot ID | Page 内局部小写 snake_case | `primary`、`secondary` |
| timed region ID | Page 内局部小写 snake_case | `clock` |

schema/page/resource key 最长 64 bytes；slot ID 最长 32 bytes。ID 一旦发布不得改变含义。破坏字段语义时升级 schema version，不在同一 version 下兼容猜测。

## 3. Resource Schema

先写 schema 文档，再写代码。文档至少定义：schema ID/version、字段类型、必填性、单位、时间基准、枚举、空值、示例、兼容策略和隐私边界。

通用 envelope：

```json
{
  "key": "example/default",
  "schema_id": "example.card",
  "schema_version": 1,
  "revision": 1723340000,
  "updated_at": 1723340000,
  "ttl_sec": 600,
  "persistence": "snapshot",
  "payload": {
    "source_status": "ok",
    "title": "Example",
    "value": 42
  }
}
```

约束：payload 必须是 JSON/MessagePack map，编码后不超过 2048 bytes；ResourceStore 最多 16 条；持久 snapshot 总计不超过 4096 bytes；TTL 最大 604800 秒。`updated_at`、业务时间戳均为 Unix seconds。数值必须带单位语义，不允许用展示字符串代替数据，例如应传 `duration_minutes:300`，而非 `duration:"5 小时"`。

建议每个业务 payload 包含 `source_status`，其枚举由 schema 自己定义。凭据、refresh token、cookie、设备地址、Agent 本地路径不得进入 payload。

## 4. Page、Slot 与状态

公共类型位于 `include/toolkit/app.h`。Page manifest 是固定数组：

```cpp
const PageSlot kSlots[] = {
    {"example", "example.card", 1, true, SlotStatus::kActive},
    {"future", nullptr, 0, false, SlotStatus::kReserved},
};

const TimedRegion kRegions[] = {
    {"clock", {8, 0, 234, 30}, 60},
};

const PageManifest kManifest{
    "example.dashboard", "Example Dashboard",
    kSlots, 2, kRegions, 1,
};
```

规则：

- active slot 必须声明精确 `schema_id/schema_version`；
- required active slot 必须有 binding；资源本体可以暂时不存在；
- optional active slot 可以不绑定；
- reserved slot 不得声明 schema，也不可绑定，只用于固定“未配置”占位；
- 每个 Page 最多 8 个 binding；
- Page 不遍历 `ResourceStore`，只调用 `context.resources.get("slot")`；
- Page 不根据 resource key 猜 schema。

`SlotResource.state` 的统一含义：

| state | Page 行为 |
|---|---|
| `missing` | binding 缺失或目标 Resource 尚不存在；显示稳定空态 |
| `invalid` | 已存在 Resource 与 slot schema/version 不匹配；显示数据异常 |
| `stale` | schema 正确但 TTL 已过；可显示最后值并明确过期 |
| `fresh` | schema 正确且未过期 |

Model 还可根据 payload 的 `source_status` 映射认证失败、服务离线等业务错误。不得把 transport error 伪装成 fresh 值。

## 5. C++ Model、Widget、Page 模板

头文件：

```cpp
#pragma once

#include "toolkit/app.h"

namespace epd {

struct ExampleModel {
  ResourceState resource_state = ResourceState::kMissing;
  String title = "--";
  int32_t value = 0;

  static ExampleModel fromSlot(const SlotResource& slot);
};

class ExampleCompactWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const ExampleModel& model);
};

class ExamplePage : public IPage {
 public:
  const PageManifest& manifest() const override;
  void buildUi(lv_obj_t* root, const PageContext& context) override;
  void buildTimedRegion(const char*, lv_obj_t*,
                        const RuntimeContext&) override {}
};

}  // namespace epd
```

实现骨架：

```cpp
namespace epd {
namespace {

const PageSlot kSlots[] = {
    {"example", "example.card", 1, true, SlotStatus::kActive},
};
const PageManifest kManifest{
    "example.page", "Example", kSlots, 1, nullptr, 0,
};

}  // namespace

ExampleModel ExampleModel::fromSlot(const SlotResource& slot) {
  ExampleModel model;
  model.resource_state = slot.state;
  if (slot.resource == nullptr || slot.state == ResourceState::kInvalid) {
    return model;
  }
  JsonVariantConst payload = slot.resource->payload.as<JsonVariantConst>();
  if (!payload["title"].is<const char*>() || !payload["value"].is<int32_t>()) {
    model.resource_state = ResourceState::kInvalid;
    return model;
  }
  model.title = payload["title"].as<const char*>();
  model.value = payload["value"].as<int32_t>();
  return model;
}

void ExampleCompactWidget::build(lv_obj_t* parent, const Rect& bounds,
                                 const ExampleModel& model) {
  lv_obj_t* root = lv_obj_create(parent);
  lv_obj_set_pos(root, bounds.x, bounds.y);
  lv_obj_set_size(root, bounds.width, bounds.height);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* label = lv_label_create(root);
  lv_label_set_text_fmt(label, "%s: %ld", model.title.c_str(),
                        static_cast<long>(model.value));
}

const PageManifest& ExamplePage::manifest() const { return kManifest; }

void ExamplePage::buildUi(lv_obj_t* root, const PageContext& context) {
  const ExampleModel model =
      ExampleModel::fromSlot(context.resources.get("example"));
  ExampleCompactWidget::build(root, {8, 8, 234, 106}, model);
}

}  // namespace epd
```

Widget 契约：

- 输入只能是父节点、稳定 `Rect` 和已解析 model；
- 不读取 ResourceStore，不接收 resource key，不切换 Page；
- 不调用 `present()`，不创建 BLE 请求，不安排定时器；
- 不修改全局 screen 样式；
- bounds 必须固定，动态文字不得改变外围布局；
- 业务状态必须在同一 bounds 内稳定显示。

## 6. Timed Region

Timed Region 用于时钟等无需 Resource 的局部内容：

```cpp
void ExamplePage::buildTimedRegion(const char* id, lv_obj_t* root,
                                   const RuntimeContext& context) {
  if (String(id) != "clock") return;
  buildClock(root, {0, 0, 234, 30}, context);
}
```

强制规则：

- 只能读取 `RuntimeContext`；不得捕获 PageResources、ResourceRecord 或 snapshot；
- region render 的 root 坐标原点是 `(0,0)`，不是全屏坐标；
- full `buildUi` 必须用 manifest 中相同 bounds 绘制同一内容；
- bounds 必须在 250x122 画布内，不能重叠不相关动态内容；
- `interval_sec` 大于 0；当前调度器使用 Page 的第一个 timed region；
- 增加多个 region 前必须先扩展调度器为逐 region deadline，不能假设现有代码已支持多 deadline。

电池快路径会验证 RTC frame CRC 与 PageSettings identity hash；失败会回到完整 BLE 启动。不要为快路径增加 NVS 或网络依赖。

## 7. 固件注册

在 `src/main.cpp` 完成全部步骤：

```cpp
#include "toolkit/example_app.h"

epd::ExamplePage g_example_page;

void registerPages() {
  String error;
  if (!g_pages.add(g_home_page, error) ||
      !g_pages.add(g_codex_page, error) ||
      !g_pages.add(g_example_page, error)) {
    TOOLKIT_LOG("page", String("registry error: ") + error);
  }
}
```

`PageRegistry` 容量为 8，并拒绝重复 Page/slot/region ID、无 schema 的 active slot、声明 schema 的 reserved slot 和无效 timed region。不要在 `find()` 里加业务分支，不要把未知 Page 回退为已有 Page。注册后 `capabilities.get.pages` 自动暴露 manifest，Web 表单自动出现新 Page。

若新 Page 要成为默认值，修改 `PageSettings` 默认构造值与 binding；这是破坏默认配置的产品决策，不应由功能模块私自修改。不得创建 migration。

## 8. Producer 契约

公共类型位于 Agent 的 `producer.rs`、`publisher.rs`：

- `ProducerManifest`：静态 ID、标题、resource keys、是否参与 battery auto sync；
- `ProducerContext`：共享状态和 `ResourcePublisher`；
- `ProducerControl`：manual/cycle trigger channel；
- `ProducerRegistry`：拒绝重复 ID、按 ID 刷新；
- `SemanticResource`：不含 revision/updated_at 的业务发布对象；
- `ResourcePublisher`：唯一拥有 revision、payload hash、300 秒 heartbeat、重连 reconcile、BLE 串行写；
- `SyncCoordinator`：唯一拥有 battery cycle 和 `system.sync.complete`。

Producer 不得直接调用 `resource.put`、`resource.list`、`system.sync.complete`，不得管理 BLE 重连，也不得自行生成 revision。

## 9. Rust Producer 模板

```rust
use std::sync::Arc;

use anyhow::{Result, anyhow};
use serde_json::json;
use tokio::sync::mpsc;

use crate::{
    producer::{ProducerContext, ProducerControl, ProducerManifest, ProducerTrigger},
    publisher::SemanticResource,
};

pub static MANIFEST: ProducerManifest = ProducerManifest {
    id: "example.card",
    title: "Example Card",
    resource_keys: &["example/default"],
    auto_sync: true,
};

pub struct ExampleControl {
    trigger: mpsc::Sender<ProducerTrigger>,
}

impl ExampleControl {
    pub fn spawn(context: ProducerContext) -> Self {
        let (trigger, receiver) = mpsc::channel(8);
        tokio::spawn(run(context, receiver));
        Self { trigger }
    }

    pub fn control(&self) -> ProducerControl {
        ProducerControl::new(&MANIFEST, self.trigger.clone())
    }
}

async fn run(
    context: ProducerContext,
    mut triggers: mpsc::Receiver<ProducerTrigger>,
) {
    while let Some(trigger) = triggers.recv().await {
        let cycle_id = match trigger {
            ProducerTrigger::Manual => None,
            ProducerTrigger::SyncCycle(id) => Some(id),
        };
        let result = collect_and_publish(&context).await;
        if let Some(cycle_id) = cycle_id {
            let _ = context.publisher
                .complete_cycle(cycle_id, MANIFEST.id, result.is_ok())
                .await;
        }
    }
}

async fn collect_and_publish(context: &ProducerContext) -> Result<()> {
    // 数据源客户端、认证与字段投影都留在本模块。
    let payload = json!({
        "source_status": "ok",
        "title": "Example",
        "value": 42,
    });
    context.publisher.publish(SemanticResource {
        producer_id: MANIFEST.id,
        key: "example/default",
        schema_id: "example.card",
        schema_version: 1,
        ttl_sec: 600,
        persistence: "snapshot",
        payload,
    }).await?;
    Ok(())
}
```

实际长期运行 Producer 还应像 Codex 一样实现启动采集、正常 poll、数据源通知触发与指数 backoff。无论成功失败，收到 `SyncCycle(id)` 后都必须最终调用一次 `complete_cycle`，否则电池设备会等到 120 秒超时。

## 10. Agent 注册

1. 在 `agent/src/main.rs` 添加 `mod example;`。
2. 用同一个 Publisher 创建 `ProducerContext` 并 spawn：

```rust
let example = example::ExampleControl::spawn(producer::ProducerContext {
    state: state.clone(),
    publisher: publisher.clone(),
});
```

3. 加入唯一 registry 构造：

```rust
let producers = producer::ProducerRegistry::new(
    &state,
    vec![codex.control(), example.control()],
).await?;
```

4. 不修改 `ResourcePublisher` 和 `SyncCoordinator` 的业务分支。

注册后 snapshot 的 `producers[]`、`POST /api/v1/producers/{id}/refresh` 和 Web Producer 列表自动生效。

## 11. Web 扩展

通用管理界面从设备/Agent 动态读取：

- `capabilities.pages` 生成 Page 选项和 slots；
- active slot 只列出 schema ID/version 精确兼容的 Resource；
- optional slot 可空，required slot 缺失时禁止提交；
- reserved slot 禁用并显示“未配置”；
- `producers[]` 生成状态、details JSON 和刷新按钮；
- Resource JSON 编辑器通过 owner-only `PUT /api/v1/device/resource` 发布任意 schema。

因此新增 Page 或 Producer 通常不修改 React。只有需要专用交互（例如 OAuth 登录入口）时才增加 details UI；专用 UI 仍调用 Producer 自己的 Agent API，不得把凭据返回浏览器。通用类型变化必须先改协议文档和 `src/lib/agent.ts`。

## 12. Codex 与 Home 对照

Codex：

- schema：`codex.rate_limits/v1`；
- Producer：`codex.usage`，采集 app-server 并发布 `codex/default` 与 `codex/metrics`；
- Model：`CodexUsageModel::fromSlot`；
- Widget：`CodexUsageFullWidget` 与 `CodexUsageCompactWidget`；
- Page：`codex.usage`，required active slot `codex`。

Home：

- Page ID：`home` 是 v4 默认双组件 Page，`home.three` 是三组件布局；
- `home` 的两个 slot 支持 Codex 双窗口和 `generic.metrics/v1` 通用组件；
- `home.three` 的三个 slot 只暴露单数据项的数值、条形进度和环形进度组件；
- `generic.metrics/v1` 的组件通过 Widget ID 后缀 `.1` 到 `.4` 选择数据项；
- `clock` timed region 为 `{8,0,234,30}` / 60 秒；
- 时钟只读 `RuntimeContext`；slot 未绑定或资源缺失时显示稳定等待态。

这说明 Page 与 Producer 不是一一对应：Home 可同时组合 Codex 与任意 CLI 资源，而 Codex 完整页继续复用专用 Resource/Model。

## 13. 错误与降级

- Producer 采集失败：保留旧 Resource，更新自身 `phase/last_error`，按 backoff 重试；
- BLE 断开：Publisher 缓存最新 semantic payload，重连 reconcile；
- Resource missing：Widget 显示空态，不崩溃；
- Resource invalid：Widget 显示数据异常，不尝试跨版本解析；
- Resource stale：保留最后值并明确过期；
- 未知 Page：固件诊断页；
- registry 重复：启动日志报错，必须修正代码；
- Page binding 校验失败：RPC 返回 `invalid_args`，Web 不应本地伪造成功。

## 14. 完成清单

Resource/schema：

- schema 文档完整，示例不含凭据；
- payload 小于 4096 bytes，字段/单位/空值明确；
- version 与 PageSlot 精确一致；TTL 与 heartbeat 有余量。

固件：

- Model 覆盖 missing/invalid/stale/fresh 和业务错误；
- Widget 只接收 model/parent/bounds；
- Page 只按 slot 取资源；required/optional/reserved 正确；
- Page 已在 `registerPages()` 注册，无重复 ID；
- Timed Region 不读取 Resource；
- 无关 Resource 更新不触发屏幕 IO。

Agent/Web：

- Producer 通过 `SemanticResource` 发布，不直接发 BLE RPC；
- cycle 成功失败都上报 complete；
- Producer 已加入唯一 registry；
- snapshot 和通用刷新接口可见；
- Page 表单能按 schema 过滤；JSON PUT 可用于联调；
- 新功能没有把云凭据带入浏览器或 ESP32。

构建：

```bash
pio run -e esp32_2_13
pio run -e esp32_2_13_release
pio run -e esp32_4_2
pio run -e esp32_4_2_release
cd ../esp32-epd-kit-web/agent && cargo check
cd .. && bun run build
```

不新增独立测试脚本，不创建或手工修改 migration。
