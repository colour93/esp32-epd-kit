# BLE Protocol v3

BLE v3 is the only device transport. The ESP32 is a GATT peripheral; the desktop Agent is the central that discovers the saved device, connects, authenticates, and writes semantic resources. Advertising is discovery metadata only and never carries Codex/resource payloads.

## Discovery and connection

- Device name: `EPD-KIT-` plus the last six hexadecimal digits of the device MAC.
- Service: `f0a30000-0451-4000-b000-000000000001`.
- RX: `f0a30001-0451-4000-b000-000000000001`, encrypted/authenticated Write.
- TX: `f0a30002-0451-4000-b000-000000000001`, encrypted/authenticated Indicate.
- Requested ATT MTU: 247; clients must also support MTU 23.

Raw Manufacturer Specific Data is four bytes:

| Offset | Value | Meaning |
|---:|---|---|
| 0-1 | `ff ff` | internal-use Company ID `0xffff`, little-endian |
| 2 | `03` | protocol major |
| 3 | flags | device state below |

Flags are `0x01 owned`, `0x02 battery enabled`, `0x04 IO12 key enabled`, and `0x08 fast advertising`. Reserved bits are zero. APIs such as btleplug expose `0xffff` as the map key and only bytes 2-3 as its value.

Advertising policy:

| State | Interval | Duration |
|---|---:|---|
| `mains` | about 100-150 ms | continuous |
| `battery`, boot/disconnect | about 100-150 ms | first 30 s |
| `battery`, idle | about 900-1100 ms | until connection or sleep |
| critical battery | off | until voltage reaches `recovery_mv` |

Pairing does not make the device undiscoverable. After a paired link disconnects, firmware rebuilds advertising with the owned flag and returns to the fast interval. The Agent persists `{id,name}` after authentication, reconnects by exact platform ID, and uses the stable name only when exactly one matching device exists. Without a saved target it auto-connects only when one candidate is present.

## Security

The link requires LE Secure Connections, MITM, and bonding. The e-paper displays a random six-digit passkey. The first authenticated bond becomes `owner`; later devices can pair only during an owner-opened 120-second enrollment window. At most four bonds are retained.

- `owner`: configuration, resources, view, bond management, restart, and reset.
- `trusted`: status/time, resource synchronization, and display refresh.
- unauthenticated/unknown: no RPC access.

Owner transfer is immediate. The active owner cannot be revoked. Factory reset clears v3 configuration, resources, owner state, and NimBLE bonds.

## Framing

Every RX/TX value begins with an eight-byte little-endian header:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 1 | magic | fixed `0xe3` |
| 1 | 1 | kind/flags | bits 0-1 kind; `0x04 START`; `0x08 END` |
| 2 | 4 | message ID | request correlation or device event ID |
| 6 | 2 | sequence | contiguous, starting at zero |

Kinds are `0 request`, `1 response`, and `2 event`. A START fragment adds:

| Offset | Size | Field |
|---:|---:|---|
| 8 | 2 | total MessagePack length, 1-8192 |
| 10 | 4 | IEEE CRC-32 of the complete MessagePack payload |
| 14 | variable | first payload bytes |

Other fragments start payload at offset 8. One GATT value is limited to `ATT_MTU - 3`. Assembly expires five seconds after START. A bad kind, sequence, length, timeout, or CRC discards the complete assembly.

The Agent subscribes to TX before writing RX. Device TX is strictly serialized: one indication is submitted, its `BLE_HS_EDONE` confirmation is received, then the next fragment is submitted. Immediate submission failure is retried three times before the batch is dropped; the caller then observes a request timeout or disconnect and may retry the operation.

MessagePack envelopes are:

```text
request   {"op":"config.get","args":{}}
success   {"ok":true,"result":{...}}
failure   {"ok":false,"error":{"code":"...","message":"...","retryable":false}}
event     {"name":"input.key","data":{}}
```

The device processes one complete request at a time. Clients must not reuse an in-flight ID and must ignore response fields they do not recognize.

## Operations

| Operation | Trusted | Owner | Purpose |
|---|:---:|:---:|---|
| `system.hello` | yes | yes | protocol, firmware, MTU, role, power profile |
| `system.status`, `diagnostics.get` | yes | yes | runtime summary |
| `system.time.set` | yes | yes | volatile Unix time and UTC offset |
| `system.sync.complete` | yes | yes | finish an automatic battery sync window |
| `system.restart` | no | yes | restart after response delivery |
| `capabilities.get` | yes | yes | renderer and hardware limits |
| `config.get`, `config.discard` | yes | yes | read or discard staged configuration |
| `config.patch`, `config.commit` | no | yes | stage and atomically commit configuration |
| `resource.list`, `resource.get`, `resource.put` | yes | yes | synchronize semantic data |
| `resource.delete` | no | yes | remove a resource |
| `view.get`, `display.refresh` | yes | yes | inspect view or schedule rendering |
| `view.set` | no | yes | select renderer/resource and commit |
| `security.owner.get`, `security.bonds.list` | yes | yes | inspect current security state |
| `security.enrollment.*`, transfer/revoke | no | yes | manage trusted bonds |
| `factory_reset.prepare`, `factory_reset.commit` | no | yes | physical-code reset flow |

Common errors are `invalid_frame`, `invalid_args`, `unauthorized`, `forbidden`, `not_found`, `conflict`, `busy`, `too_large`, and `storage_error`. Automatic retry follows the response's `retryable` field.

## Configuration and resources

Relevant defaults:

```json
{
  "version": 3,
  "hardware": {
    "battery": {"enabled": false, "low_mv": 3550, "critical_mv": 3400, "recovery_mv": 3650},
    "io12": {"mode": "disabled"}
  },
  "power": {"profile": "mains", "wake_interval_sec": 300},
  "view": {"renderer_id": "codex.rate_limits", "resource_key": "codex/default"}
}
```

`wake_interval_sec` accepts 60-86400 seconds. Battery/IO12/profile changes require restart; a wake interval change applies to the next sleep. Configuration uses CRC-protected double slots in `epd_cfg3`; no older namespace is read or migrated.

Resource envelope:

```json
{
  "key": "codex/default",
  "schema_id": "codex.rate_limits",
  "schema_version": 1,
  "revision": 1786291200,
  "updated_at": 1786291200,
  "ttl_sec": 600,
  "persistence": "snapshot",
  "payload": {}
}
```

A greater revision replaces the record. An equal revision with equal canonical CRC is idempotent; an equal revision with different content or a lower revision is a conflict. Snapshot resources use `epd_res3`, with NVS writes rate-limited to reduce flash wear.

The Codex Agent writes immediately when semantic payload changes and sends a freshness heartbeat at most every 300 seconds when it does not. The resource TTL is 600 seconds. Reconnect clears Agent-side sent state so the current resource is pushed immediately. A heartbeat may schedule a logical render, but unchanged pixels cause no panel IO.

## Battery wake cycle

For an owned device using the `battery` profile:

```text
timer / IO12 wake
  -> fast advertising
  -> Agent finds saved target and connects
  -> authenticated prime (time, config, resources)
  -> Agent reads Codex and resource.put
  -> Agent calls system.sync.complete
  -> device finishes response indications and rendering
  -> BLE stops and deep sleep begins
```

The device stays awake for at most 120 seconds after boot or the latest authenticated request. `system.sync.complete` requests earlier sleep but `takeSleepRequest` does not release it until all queued indication confirmations are finished. Automatic Agent sessions call it after a successful resource write; manual management sessions rely on the 120-second timeout.

Deep sleep uses `power.wake_interval_sec`. IO12 key mode also enables low-level ext0 wake. On deep-sleep wake the firmware keeps the retained e-paper image and does not force a full refresh before new data arrives. Before sleeping it performs one logical render so a freshness transition can be shown.

An unowned device remains available for initial pairing instead of entering the owned-device timeout. At critical battery BLE remains off and firmware wakes every 30 minutes only to recheck voltage; normal operation resumes at `recovery_mv`.

## Events

Events are `input.key`, `display.started`, `display.completed`, and `battery.updated`. Display completion is `unchanged`, `partial`, or `full`; `unchanged` performs no panel IO.

The `codex.rate_limits/v1` projection is described in [Codex Rate Limits via Local Agent](openai_codex_usage.md).
