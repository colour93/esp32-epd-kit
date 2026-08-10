# Codex Rate Limits via Local Agent

> Resource: `codex.rate_limits/v1`<br>
> Transport: Codex app-server stdio + BLE v3

## Boundary

```text
React workbench -> loopback HTTP/SSE -> Rust Agent -> BLE v3 -> ESP32
                                         |
                                         +-> codex app-server over JSONL stdio
```

The Agent reads the existing local Codex login through the official app-server interface. It does not implement login, logout, OAuth, token refresh, or direct ChatGPT HTTP calls. The ESP32 and browser never receive Codex credentials; the browser also never owns the BLE connection.

## Lifecycle

The Agent locates `codex` from `EPD_CODEX_PATH`, `PATH`, or common Homebrew locations and starts `codex app-server --listen stdio://`. Each process is initialized once before account requests. Process exit or protocol failure triggers exponential restart backoff capped at 60 seconds.

Only these account reads are used:

- `account/read` with `refreshToken:false`;
- `account/rateLimits/read`;
- `account/rateLimits/updated` as a refresh trigger, not an authoritative snapshot.

Supported account types are `chatgpt` and `chatgptAuthTokens`. Missing login, API-key-only login, and unsupported modes are surfaced to the workbench without starting a login flow.

## Refresh and BLE sync

An immediate rate-limit read occurs at startup, BLE reconnect, manual refresh, `input.key`, and `account/rateLimits/updated`. Normal polling is every 60 seconds. Read failures back off from 60 to 900 seconds while the last device snapshot remains available.

The Agent projects app-server camelCase fields into `codex.rate_limits/v1`, preferring `rateLimitsByLimitId["codex"]` and falling back to `rateLimits`. Other limit buckets remain in `payload.limits`; the renderer uses the selected Codex bucket.

| app-server | resource payload |
|---|---|
| `limitId`, `limitName` | `limit_id`, `limit_name` |
| `planType` | `plan_type` |
| `usedPercent` | `used_percent` |
| `windowDurationMins` | `window_duration_mins` |
| `resetsAt` | `resets_at` |
| `rateLimitReachedType` | `rate_limit_reached_type` |

Synchronization rules:

- changed semantic payload: write immediately;
- unchanged payload: heartbeat write after 300 seconds;
- resource TTL: 600 seconds;
- disconnected BLE: keep current Codex state in Agent memory;
- reconnect: clear the sent hash/timestamp and push immediately;
- failed `resource.put`: do not advance sent state, so it retries later.

Revision is monotonic and always greater than the revision reported by the device. Each successful write refreshes `updated_at`; unchanged logical pixels do not cause e-paper IO.

For an automatic connection to a battery-profile device, the Agent calls `system.sync.complete` after a successful resource write. Firmware delivers pending indication responses and rendering first, then enters deep sleep. Manual connections stay open for management and end through the firmware's 120-second battery timeout.

## Failure behavior

| State | Meaning |
|---|---|
| `missing` | Codex executable not found |
| `auth_required` | no supported ChatGPT login |
| `unavailable` | app-server cannot start or exited |
| `degraded` | account/rate-limit read failed; retrying |
| `ready` | current snapshot is available |

Source failure does not erase the last resource. The device marks it stale only after TTL expires.

## Local security

The Agent listens on loopback only. The management page exchanges an installation secret from the URL fragment for an `HttpOnly; SameSite=Strict` cookie. Secret files are private on Unix, mutation routes validate loopback Origin, and CORS is disabled.

The complete BLE wire format, operation permissions, resource envelope, and battery wake sequence are in [BLE Protocol v3](ble_protocol_v3.md).
