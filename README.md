# ESP32 E-Paper Toolkit

BLE-only firmware for the Waveshare 2.13inch e-Paper Cloud Module V4. It receives versioned semantic resources from a desktop Agent and renders them locally. The first renderer displays Codex rate-limit windows.

## Runtime

```text
Codex app-server ──stdio──> Rust Agent ──BLE v3──> ResourceStore
                                                      │
                                                      └──> RendererRegistry ──> LVGL / GxEPD2
```

The firmware has no Wi-Fi, SNTP, TLS, HTTP client, certificate bundle, OAuth, relay token, or OpenAI credential path.

Core components:

- `ResourceStore`: generic `{key,schema_id,schema_version,revision,...}` records with CRC, TTL, idempotent revisions, and optional NVS snapshots.
- `RendererRegistry`: maps renderer IDs to accepted semantic schemas.
- `CodexUsageRenderer`: renders `codex.rate_limits/v1` using primary/secondary windows, reset time, plan, source status, and freshness.
- `BleProtocolService`: authenticated BLE v3 framing, owner/trusted authorization, configuration, resources, and events.
- `DisplayManager`: logical-frame diffing, dirty rectangles, partial/full policy, and unchanged-frame suppression.

## Hardware

| Function | GPIO |
|---|---:|
| EPD SCK | 13 |
| EPD MOSI | 14 |
| EPD CS | 15 |
| EPD BUSY | 25 |
| EPD RST | 26 |
| EPD DC | 27 |
| optional key | 12 |
| optional VBAT/3 ADC | 36 |

The default panel class is `GxEPD2_213_B74` with a 250x122 logical landscape canvas.

## Safe defaults

- battery disabled;
- IO12 disabled and not configured by firmware;
- `mains` power profile;
- renderer `codex.rate_limits`;
- resource key `codex/default`;
- locale `zh-CN`, timezone `Asia/Shanghai`.

Battery and IO12 are compile-time-capable but runtime-configurable. Hardware and power changes take effect after restart. When battery is disabled, the firmware does not read GPIO36, register Battery Service, or enter low-battery lock. Critical battery disables BLE and holds the screen while timed ADC checks wait for `recovery_mv`.

In the `battery` profile an owned device wakes every `power.wake_interval_sec` (default 300 seconds), advertises, accepts an Agent sync, finishes rendering and indication delivery, then returns to deep sleep. IO12 key mode adds external wake. If no Agent completes synchronization, the awake window ends after 120 seconds. Advertising carries identity/state only; resource data is sent after the Agent connects.

## Security and storage

BLE requires LE Secure Connections, MITM, and bonding. The e-paper shows a six-digit passkey. The first bond becomes owner; owner can enroll up to three trusted hosts for 120 seconds. Trusted hosts may synchronize resources but cannot change hardware, bonds, ownership, or factory state.

v3 uses new NVS namespaces:

- `epd_cfg3`: CRC-protected double-slot configuration;
- `epd_res3`: snapshot resources;
- `epd_sec3`: owner identity.

Older configuration and bond state is not read or migrated. No migration is created.

## Build

```bash
pio run -e esp32dev
pio run -e esp32dev_release
```

The project uses a `huge_app.csv` partition because OTA is not supported. Debug serial output is 115200 baud and uses the `[toolkit]` prefix.

## Serial recovery

The recovery console is available in debug and release firmware at `115200 8N1`.
It does not require BLE, an owner bond, or IO12. Open the monitor and type `help`:

```bash
pio device monitor -b 115200
```

```text
status
io12 disable
restart
factory-reset prepare
factory-reset confirm <code>
```

`io12 disable` preserves resources and security state, then restarts with IO12
left unconfigured. Factory reset uses a six-digit code valid for 30 seconds and
erases `epd_cfg3`, `epd_res3`, `epd_sec3`, and all BLE bonds before restarting.

GPIO12 is also an ESP32 boot-strapping pin. If physical damage holds it high and
the ESP32 cannot boot far enough to print the console banner, firmware recovery
cannot run; repair the circuit or hold GPIO12 low during reset first.

## Documentation

- [BLE Protocol v3](docs/ble_protocol_v3.md)
- [Codex Rate Limits via Local Agent](docs/openai_codex_usage.md)
- [Desktop Agent and workbench](../esp32-epd-kit-web/README.md)
