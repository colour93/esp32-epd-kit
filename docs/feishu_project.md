# 飞书项目凭据登录与数据查询

## 1. 凭据登录

ESP32 不需要内置 `plugin_secret` 或固定 `client_secret`。

飞书项目支持：

```text
OAuth Discovery
→ Dynamic Client Registration
→ Device Code 登录
→ access_token / refresh_token
```

---

### 1.1 获取 OAuth 配置

请求：

```http
GET https://project.feishu.cn/.well-known/oauth-authorization-server
```

需要读取以下字段：

```json
{
  "token_endpoint": "...",
  "registration_endpoint": "...",
  "device_authorization_endpoint": "..."
}
```

---

### 1.2 注册 OAuth Client

请求：

```http
POST <registration_endpoint>
Content-Type: application/json
```

Body：

```json
{
  "client_name": "esp32-feishu-project",
  "grant_types": [
    "urn:ietf:params:oauth:grant-type:device_code",
    "refresh_token"
  ],
  "token_endpoint_auth_method": "none"
}
```

响应：

```json
{
  "client_id": "xxxxxxxx"
}
```

部分响应可能包在 `data` 内：

```json
{
  "code": 0,
  "data": {
    "client_id": "xxxxxxxx"
  }
}
```

需要保存：

```text
client_id
```

---

### 1.3 获取 Device Code

请求：

```http
POST <device_authorization_endpoint>
Content-Type: application/x-www-form-urlencoded
```

Body：

```text
client_id=<client_id>
```

响应：

```json
{
  "device_code": "xxxxxxxx",
  "user_code": "ABCD-EFGH",
  "verification_uri": "https://...",
  "verification_uri_complete": "https://...",
  "expires_in": 600,
  "interval": 5
}
```

将：

```text
verification_uri_complete
```

提供给用户打开或生成二维码。

用户在手机浏览器中完成飞书授权。

---

### 1.4 轮询授权结果

每隔响应中的：

```text
interval
```

秒请求：

```http
POST <token_endpoint>
Content-Type: application/x-www-form-urlencoded
```

Body：

```text
grant_type=urn:ietf:params:oauth:grant-type:device_code
&device_code=<device_code>
&client_id=<client_id>
```

尚未授权：

```json
{
  "error": "authorization_pending"
}
```

继续轮询。

如果返回：

```json
{
  "error": "slow_down"
}
```

增加轮询间隔，例如：

```text
interval += 5
```

如果返回：

```json
{
  "error": "expired_token"
}
```

Device Code 已过期，需要重新开始授权。

授权成功：

```json
{
  "access_token": "xxxxxxxx",
  "refresh_token": "xxxxxxxx",
  "expires_in": 3600
}
```

设备需要持久化：

```text
client_id
access_token
refresh_token
expires_at
```

其中：

```text
expires_at = 当前时间 + expires_in
```

---

## 2. 刷新凭据

`access_token` 过期前，使用 `refresh_token` 获取新的 Token。

请求：

```http
POST <token_endpoint>
Content-Type: application/x-www-form-urlencoded
```

Body：

```text
grant_type=refresh_token
&refresh_token=<refresh_token>
&client_id=<client_id>
```

不需要：

```text
client_secret
```

响应：

```json
{
  "access_token": "new-access-token",
  "refresh_token": "new-refresh-token",
  "expires_in": 3600
}
```

更新：

```text
access_token
expires_at
```

如果返回了新的：

```text
refresh_token
```

则同时覆盖旧值。

如果没有返回新的 `refresh_token`，继续保留旧值。

建议 Token 使用策略：

```text
调用接口
  ↓
HTTP 200
  ↓
正常处理
```

如果返回：

```text
401 Unauthorized
```

则：

```text
刷新 access_token
→ 重试原请求一次
```

第二次仍然 `401` 时，认为登录凭据已失效，需要重新执行 Device Code 授权。

---

# 3. 数据查询

飞书项目 MCP 地址：

```text
https://project.feishu.cn/mcp_server/v1
```

使用：

```http
Authorization: Bearer <access_token>
```

进行认证。

MCP 当前可以直接通过 HTTP POST + JSON-RPC 调用。

---

## 3.1 查询 MCP Tool

请求：

```http
POST https://project.feishu.cn/mcp_server/v1
Authorization: Bearer <access_token>
Content-Type: application/json
```

Body：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "search_by_mql",
    "arguments": {
      "...": "..."
    }
  }
}
```

其中：

```text
search_by_mql
```

用于执行飞书项目 MQL 查询。

---

## 3.2 获取 search_by_mql 参数

开发阶段可以调用：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/list"
}
```

完整请求：

```http
POST https://project.feishu.cn/mcp_server/v1
Authorization: Bearer <access_token>
Content-Type: application/json
```

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/list"
}
```

在返回的 Tool 列表中找到：

```text
search_by_mql
```

并读取：

```text
inputSchema
```

确认实际参数后，可以直接把参数结构固化进 ESP32。

正式运行时不需要重复调用 `tools/list`。

---

## 3.3 查询数量

例如业务目标：

```text
查询某个分类下有多少工作项
```

ESP32 最终只需要发送：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "search_by_mql",
    "arguments": {
      "MQL相关参数": "..."
    }
  }
}
```

MQL 中加入对应分类筛选条件。

返回结果通常类似：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"list\":[],\"total\":37}"
      }
    ],
    "isError": false
  }
}
```

需要进行两次 JSON 解析。

第一次获取：

```text
result.content[0].text
```

得到：

```json
{
  "list": [],
  "total": 37
}
```

第二次解析其中的：

```text
total
```

即可得到：

```text
37
```

---

# 4. 最终保存的凭据

ESP32 最终只需要保存：

```json
{
  "client_id": "...",
  "access_token": "...",
  "refresh_token": "...",
  "expires_at": 1234567890
}
```

其中最关键的是：

```text
client_id
refresh_token
```

`access_token` 可以随时通过 `refresh_token` 重新获取。

推荐将这些数据保存到：

```text
NVS / Preferences
```

正式设备建议启用 NVS Encryption 或 Flash Encryption。

---

# 5. 最终调用流程

首次绑定：

```text
OAuth Discovery
      ↓
注册 client_id
      ↓
申请 Device Code
      ↓
用户扫码授权
      ↓
轮询 token
      ↓
保存 client_id
access_token
refresh_token
```

正常查询：

```text
检查 access_token
      ↓
必要时 refresh
      ↓
POST /mcp_server/v1
      ↓
tools/call
      ↓
search_by_mql
      ↓
解析 total
```

整个流程不需要：

```text
Plugin ID
Plugin Secret
独立服务端
Cloudflare Worker
```
