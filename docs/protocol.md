# Protocol Reference

All three protocol modes share the same transport framing: **newline-terminated (`\n`) UTF-8 JSON** sent over BLE NUS, WebSocket, or USB CDC. Each line is one complete message. The device processes each line independently; partial lines are buffered until the newline arrives.

Maximum frame size: 4096 bytes (defined by `TRANSPORT_MAX_FRAME_SIZE`).

---

## Claude Buddy

Wire-compatible with [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy). Select in menuconfig: **Buddy Protocol Mode → Claude Buddy**.

### Agent → Device

#### Heartbeat / session snapshot

Sent periodically (typically every few seconds) to reflect the agent's current state.

```json
{"running": 2, "waiting": 1, "tokens": 8500}
```

| Field | Type | Description |
|-------|------|-------------|
| `running` | integer | Number of tasks currently executing |
| `waiting` | integer | Number of tasks waiting (paused for approval, etc.) |
| `tokens` | integer | Cumulative token count for the current session |

The device transitions to **BUSY** when `running > 0`, and back to **IDLE** when `running == 0`. A `tokens` value crossing the milestone threshold (default 50 000, configurable in menuconfig) triggers the **CELEBRATE** state.

#### Tool-use permission request

Sent when the agent needs user approval before calling a tool.

```json
{"id": "a1b2c3", "tool": "bash", "description": "Run: ls -la /home"}
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Unique request identifier (echoed back in the response) |
| `tool` | string | Tool name shown on the approval screen |
| `description` | string | Human-readable description shown as the approval hint |

The device transitions to **ATTENTION**, displays the tool name and description, and starts a 30-second countdown. After the countdown, the request is automatically denied.

### Device → Agent

#### Permission response

Sent immediately after the user touches Approve or Deny (or after the timeout).

```json
{"decision": "allow_once", "id": "a1b2c3"}
```

```json
{"decision": "deny", "id": "a1b2c3"}
```

| Field | Value | Description |
|-------|-------|-------------|
| `decision` | `"allow_once"` | User approved — allow this one call |
| `decision` | `"deny"` | User denied (or timed out) |
| `id` | string | Echoes the `id` from the request |

#### Heartbeat acknowledgement

Sent periodically (every 10 s) as a keepalive.

```json
{"type": "ack"}
```

---

## OpenClaw

Select in menuconfig: **Buddy Protocol Mode → OpenClaw**.

### Agent → Device

#### Heartbeat / session snapshot

```json
{"total": 5, "running": 2, "tokens": 8500}
```

| Field | Type | Description |
|-------|------|-------------|
| `total` | integer | Total tasks in the session |
| `running` | integer | Currently executing tasks |
| `tokens` | integer | Cumulative token count |

#### Tool-use permission request

The approval prompt is nested under a `"prompt"` key.

```json
{
  "prompt": {
    "id": "a1b2c3",
    "tool": "bash",
    "hint": "Run: ls -la /home"
  }
}
```

### Device → Agent

#### Permission response

```json
{"cmd": "permission", "id": "a1b2c3", "decision": "once"}
```

```json
{"cmd": "permission", "id": "a1b2c3", "decision": "deny"}
```

| Field | Value | Description |
|-------|-------|-------------|
| `cmd` | `"permission"` | Fixed discriminator |
| `decision` | `"once"` | Approved for this call |
| `decision` | `"deny"` | Denied |
| `id` | string | Echoes the request `id` |

#### Heartbeat acknowledgement

```json
{"cmd": "ack"}
```

---

## Hermes

Select in menuconfig: **Buddy Protocol Mode → Hermes**.

The Hermes adapter maps incoming JSON fields to `agent_event_t` via a `proto_hermes_cfg_t` configuration struct. This allows adapting to different Hermes-based frameworks without changing source code — only the configuration passed to `proto_hermes_create()` differs.

### Configuration struct

```c
typedef struct {
    /* Heartbeat field names */
    const char *field_running;   // default: "running"
    const char *field_tokens;    // default: "tokens"
    const char *field_total;     // default: "total"  (optional, set NULL to ignore)

    /* Approval request field names */
    const char *field_req_id;    // default: "id"
    const char *field_req_tool;  // default: "tool"
    const char *field_req_hint;  // default: "hint"

    /* Approval response field names */
    const char *field_resp_decision;  // default: "decision"
    const char *field_resp_id;        // default: "id"

    /* Approval response values */
    const char *value_allow;  // default: "allow_once"
    const char *value_deny;   // default: "deny"

    /* Heartbeat ack */
    const char *ack_json;     // default: "{\"type\":\"ack\"}\n"
} proto_hermes_cfg_t;
```

Pass a populated struct to `proto_hermes_create(&proto, &cfg)`. Pass `NULL` to use all defaults (which match the Claude Buddy wire format).

### Default wire format (with NULL config)

The Hermes default intentionally matches Claude Buddy, so a `NULL` config works as a Claude Buddy adapter with the Hermes code path.

---

## Choosing a Protocol

| Scenario | Recommended mode |
|----------|-----------------|
| Using claude-desktop-buddy or a compatible Claude agent | **Claude Buddy** |
| Using the OpenClaw agent framework | **OpenClaw** |
| Using a Hermes-based framework with custom field names | **Hermes** (configure field names in `main.c`) |
| Building your own agent from scratch | **Claude Buddy** — simplest wire format |

---

## Testing with the Command Line

Any terminal that can write to a serial port or WebSocket can drive the device.

### USB

```bash
# Linux / macOS
stty -F /dev/ttyACM0 raw && cat /dev/ttyACM0 &
echo '{"running":1,"waiting":0,"tokens":1234}' > /dev/ttyACM0
```

### WebSocket (requires `websocat`)

```bash
websocat ws://192.168.1.50:8080/buddy
# Then type JSON frames and press Enter
```

### BLE (requires `bleak` Python library)

```python
import asyncio
from bleak import BleakClient

NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

async def main():
    async with BleakClient("SmartBuddy") as c:
        await c.start_notify(NUS_TX, lambda h, d: print("RX:", d.decode()))
        frame = b'{"running":1,"waiting":0,"tokens":999}\n'
        await c.write_gatt_char(NUS_RX, frame)
        await asyncio.sleep(5)

asyncio.run(main())
```
