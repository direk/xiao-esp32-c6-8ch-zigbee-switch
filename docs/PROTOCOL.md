# XIAO-C6-8CH protocol 2.1.0

Eight endpoints (1–8), profile 0x0104, manufacturer DIY-XIAO, model XIAO-C6-8CH.
Every endpoint advertises outgoing On/Off (0x0006), Level Control (0x0008),
Color Control (0x0300) and Scenes (0x0005), plus custom server cluster 0xfc00.
There is no battery reporting, sleep or heartbeat. Power is continuous and the
node remains a Zigbee router. External antenna: GPIO3=0, GPIO14=1.
CH1–CH8 use GPIO0/1/2/21/22/23/19/20 (D0–D5, D8, D9).
UART1 TX/RX GPIO16/17 (D6/D7) accepts ASCII button commands at 115200 8N1.
The protocol, timing, safety timeout and responses are specified in [UART.md](UART.md).
Serial button states are ORed with physical inputs before the same gesture planner.
Native USB-C Serial/JTAG accepts the same commands concurrently.
Each port has its own priority-4 task, parser, request/response/log queues and ID session. Only typed commands and
replies cross bounded FreeRTOS queues (8 entries each). Zigbee handles at most
one command total per tick using fair round-robin, zero-timeout queue operations and reserved reply space
before execution. All channel state/config access remains in Zigbee context.
GET 1–8 returns a single JSON line with every channel setting (including binding
priority), mode-permitted commands and effective gestures. It is read-only and
works regardless of network readiness or channel BUSY state. Configuration is
snapshotted when executed in Zigbee context, with UART-task-only transmission.
Replies use 2048-byte payload buffers plus 16 bytes for optional ID framing; oversized JSON produces ERR RESPONSE_SIZE instead
of a truncated object. See UART.md for fields and gesture semantics.

| Attribute | Type | Access | Meaning |
|---|---|---|---|
| 0x01 | uint8 | RW | inputMode: 0 push, 1 rocker |
| 0x02 | bool | RW | inverted |
| 0x05 | uint8 | RW | switchType: 0–12, modes in README |
| 0x06 | uint8 | RW | switchAction: 0 off, 1 on, 2 toggle |
| 0x07 / 0x08 | uint8 | RW | minLevel / maxLevel, 1–254 |
| 0x09 | uint16 | RW | transitionTime, deciseconds, 0–600 |
| 0x0a / 0x0b | uint16 | RW | colorMin / colorMax, mireds, 1–1000 |
| 0x0c | uint8 | RW | sceneId |
| 0x0d | uint16 | RW | sceneGroup, 0 for binding, 1–0xfff7 direct group |
| 0x0e | bool | RW | bindingPriority: 1 ON (default), 0 OFF, per endpoint |
| 0x12 | uint8 | R/report | channelRoute: 0 binding, 1 action, 2 mixed |
| 0x13 | uint8 | R/report | bindingStatus: 0 unknown, 1 unbound, 2 bound |
| 0x14 | uint8 | R/report | bindingClusters bits: 1 On/Off, 2 Level, 4 Color, 8 Scenes |
| 0x20 | uint32 | R/report | event: 24-bit counter and 8-bit action ID |

Action IDs 0–18: off, on, toggle, single, double, triple, hold, release,
brightness_move_up, brightness_move_down, brightness_step_up, brightness_step_down,
brightness_stop, color_temperature_move_up, color_temperature_move_down,
color_temperature_step_up, color_temperature_step_down, color_temperature_stop, scene.
The converter prefixes the source channel, e.g. ch8_brightness_step_up.
Only live event reports produce actions; reading an event never replays it.

Each command checks bindings for its own source endpoint AND cluster. A matching
binding selects direct APS transmission. bindingPriority=1 suppresses its action;
bindingPriority=0 also sends an action without suppressing direct control.
No match always selects an action report to
coordinator address 0, endpoint 1. Multifunction gestures always report actions.
A configured scene group selects direct group transmission without requiring a bind;
it also emits an action when bindingPriority=0. Report-only gestures are sent once.
channelRoute describes direct control availability, irrespective of extra actions.
No-onoff modes intentionally ignore single clicks; alternating no-onoff also ignores
double clicks. They never manufacture a fallback On/Off action for an ignored gesture.

Binding snapshots come from local ZDO Mgmt_Bind, with all pages committed atomically.
32 source entries are shared by all endpoints/clusters. Unknown startup state waits
for the first complete local snapshot. Invalid reads retain the last complete mask.
Self and known coordinator destinations are excluded. Refresh is once per second
and shortly after incoming Bind/Unbind.

The gesture/packet planner in control.c is independent of ESP-IDF and unit-tested.
Direct frames use ZCL cluster-specific client-to-server header 0x11. Level modes
10/11/12 use commands 0x00, 0x02, 0x03 with optionsMask=1, optionsOverride=0.
This explicitly clears ExecuteIfOff for the command. Ordinary upward movement
uses command 0x04 and ordinary steps 0x06. Steps are 25 brightness units.
Hold targets min/max over transitionTime; release sends Stop. Direction alternates
between successive holds in the alternating modes. Color uses 0x0a, 0x4c and 0x47.
Raw frames are decoded in host tests using real zigbee-herdsman definitions.
APS transmission uses the stack binding table; its network delivery still needs
acceptance testing with actual lamps. Lamp support determines ExecuteIfOff behavior.

Input configuration uses NVS key config2 in the existing xiao4 namespace.
The separate uint8 NVS key bind_priority holds one bit per endpoint (bit 0 = CH1).
Absent key defaults to 0xff, so both new and upgraded devices start with all ON.
The config2 blob layout is unchanged. Both keys are written before the same nvs_commit call
after one second of inactivity; factory reset erases both.
First migration reads the 12-byte v1 config and retains input/invert on endpoints
1–4; endpoints 5–8 use push/toggle defaults. The old key is retained. The Zigbee
partitions and application offset 0x10000 are unchanged. The Z2M endpoint database
must be refreshed through interview after the 4-to-8 endpoint upgrade.

Changing bindingPriority applies to the next command without interrupting a hold.
Changing other configuration cancels a held operation and rebases the input, preventing
synthetic presses. A held input at boot does not trigger an automatic hold action.
Rocker is valid only for toggle/momentary and uses closed=ON, open=OFF. Advanced
functions require push. Min/max pairs and other ranges are validated before writes.
Configuration is committed after one second without another write.

No application-level Zigbee retransmission/replay, battery emulation or Zigbee OTA updater.
Use USB for firmware updates. Zigbee routing/delivery and APS handling remain in SDK.

UART protocol v2 adds INFO/STATUS JSON, RESET and optional `@uint32` IDs.
IDs increase within a boot; the last 16 full responses are replayed without
execution. Older IDs outside the cache fail with ERR ID_EXPIRED instead of
executing again; reusing a cached ID for a different command fails with
ERR ID_CONFLICT. The boot_id in INFO/STATUS changes on restart; request history
is RAM-only. Untagged legacy commands retain their behavior.

UART receive error events invalidate the current line, flush input and discard
through the next LF. Diagnostics use atomic UART counters; only the UART task
performs UART I/O. PING without an ID is answered locally; all other valid
requests go through the command queue, including tagged PING.

RESET saves pending xiao4 channel settings, stops active holds, replies, then
uses esp_zb_factory_reset to erase SDK zb_storage and restart. The unchanged
startup handler automatically starts network steering when factory-new.
The app NVS configuration is retained. ACK drain is signaled atomically by the
UART task; reset waits at least 200ms, with a 2s fallback for failed TX. BOOT's
existing full factory reset still additionally erases channel configuration.
RESET is tested via mocked callbacks; no destructive reset is performed by tests.

LOG 0 / LOG 1 toggle normal ESP_LOG output only on the request's port, with no
NVS persistence. Both ports start OFF on every boot. The logging callback only
formats/enqueues bounded single-line #LOG records (8 entries/port); device I/O
is serialized by each port task and replies get priority. Generation tags discard
stale logs after toggling. INFO/STATUS include port and logs_enabled; log_dropped
counts queue overflow. UART counters and ID history are independent of USB.
Physical/virtual channel state and RESET remain global to the device.
Standard primary/secondary consoles and bootloader logs are disabled in build
configuration; ROM/panic/previous bootloader output is outside the runtime LOG router.
