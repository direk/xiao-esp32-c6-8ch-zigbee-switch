# XIAO-C6-8CH firmware 2.3.1 / serial protocol 3

24 endpoints (1–24), eight physical inputs, profile 0x0104, manufacturer DIY-XIAO, model XIAO-C6-8CH.
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
| 0x05 | uint8 | RW | switchType: 0–13 (13=scene_cycle), modes in README |
| 0x06 | uint8 | RW | switchAction: 0 off, 1 on, 2 toggle, 3 on_off (2.2.1) |
| 0x07 / 0x08 | uint8 | RW | minLevel / maxLevel, 1–254 |
| 0x09 | uint16 | RW | transitionTime, deciseconds, 0–600 |
| 0x0a / 0x0b | uint16 | RW | colorMin / colorMax, mireds, 1–1000 |
| 0x0c | uint8 | RW | sceneId |
| 0x0d | uint16 | RW | sceneGroup, 0 for binding, 1–0xfff7 direct group |
| 0x0e | bool | RW | bindingPriority: 1 ON (default), 0 OFF, per endpoint |
| 0x0f | char string | RW | sceneList: empty or up to 16 distinct decimal scene IDs, comma separated; max 63 characters |
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
96 source entries are shared by all endpoints/clusters. Unknown startup state waits
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
Rocker is valid only for toggle/momentary. With function=toggle, each stable
edge sends genOnOff Toggle (0x02), irrespective of contact position or lamp state.
With function=momentary, closed sends ON and open sends OFF. switch_actions
applies only to push. No command is sent just because the device boots with a
closed/open contact. Advanced
functions require push. Min/max pairs and other ranges are validated before writes.
Configuration is committed after one second without another write.

Version 2.2.1 adds switchAction=3 (`on_off`): push/toggle emits explicit On
(0x01), Off (0x00), repeating on debounced presses. Push/momentary advances on
press and release. Phase is independent per channel, shared by physical/UART/USB
inputs, and starts at On after boot or a switchAction write. Other setting writes
preserve phase. External bulb state is not tracked. Existing action IDs remain
unchanged: reports use off/on, not a new action ID. Rocker and advanced functions
ignore this selector, as with other switchAction values.
The choice persists through a separate NVS uint8 mask `onoff_action`; config2 stores
Toggle for these channels so rollback firmware can retain its existing settings.
The mask restores only channels whose legacy selector is Toggle. Phase is RAM-only.


No application-level Zigbee retransmission/replay or battery emulation.
Zigbee OTA is available from 2.2.0; see OTA.md. Zigbee routing/delivery and APS handling remain in SDK.

### Request IDs and retry rules (UART and USB, protocol 2+)

The optional prefix is `@<decimal uint32> `, with IDs in **1..4294967295**.
An ID identifies a request, not a channel, endpoint or scene. Every wire line
ends in LF. For example, for an endpoint accepting CLICK:

```text
request: @42 CLICK EP2\n
reply:   @42 OK CLICK EP2\n
retry:   @42 CLICK EP2\n
reply:   @42 OK CLICK EP2\n
next:    @43 CLICK EP2\n
```

The retry returns the cached reply without executing a second click. In contrast,
`CLICK EP2` without an ID remains valid but has no duplicate suppression.

- Allocate a strictly increasing ID for each new request, across all commands
  and endpoints on the same port. Gaps are allowed; do not wrap at UINT32_MAX.
- On a missing reply, resend the same command with the same ID. Using a new ID
  creates a new request and can repeat the action. Match replies by their ID;
  use one outstanding request at a time and bounded retries. Ignore `#LOG ` lines.
- The last **16 session-executed numbered requests** retain their full replies,
  including errors such as `ERR NOT_READY`. Repeating such an ID returns the
  original error even if conditions have changed. Once the cause is resolved,
  a deliberate new attempt needs a new ID. Numbered INFO/STATUS also occupy
  cache entries; unnumbered requests do not consume IDs or cache entries.
- Reusing a cached ID with a different parsed command returns
  `@42 ERR ID_CONFLICT\n`, with no execution. Comparison includes operation,
  physical-vs-endpoint target, channel/endpoint and duration. Thus `CLICK 2`
  and `CLICK EP2` are distinct, even when their effects happen to coincide.
- An ID at or below the highest accepted ID, no longer present in the cache,
  returns `ERR ID_EXPIRED` and is not executed. Do not automatically replace
  it with a new ID: its previous effect is uncertain and may already have occurred.
- History and the highest ID are RAM-only. Read unnumbered `INFO` at client
  startup and after communication loss; `boot_id` identifies the current XIAO
  boot. A changed boot_id allows starting from 1, but old unacknowledged actions
  must not be replayed automatically across the reboot, including RESET itself.
- A controller restart does not reset XIAO. Persist the allocated ID and pending
  request before transmission, associated with boot_id and port. If boot_id is
  unchanged, resume that session. If local history was lost, do not blindly
  restart at 1 or issue a fresh action for an uncertain previous request;
  application-level recovery is required. INFO does not expose the highest ID.
- UART and USB have independent histories and counters. Retrying via the other
  port can execute again. Multiple senders on one port must share one allocator.
- OK acknowledges local command handling, not lamp response or Zigbee delivery.
  A valid gesture may have no effect in the current endpoint mode (see GET EPn).
  This is duplicate suppression within a boot, not an exactly-once lamp guarantee.
- Parser/transport failures may be untagged and are not cached session replies.
  Do not interpret an untagged error as an acknowledgement for another ID.

User-oriented rules and examples are also in README, “Opcjonalny numer komendy
@ID — zasady i ponawianie”.

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


## Scene cycle (2.2.1)

Function ID 13 appends to the existing mode IDs; previous IDs and 19 action IDs
are unchanged. It requires push input. Each debounced press recalls the next ID
from sceneList, with group_id 1..0xfff7, using cluster 0x0005 command 0x05,
payload uint16 group (little endian), uint8 scene. Release/hold do nothing.
Empty list or group_id=0 emits no command or report. Scene existence is not queried.
Group-addressed commands follow bindingPriority exactly like the existing scene mode.
List/group/function writes reset the index; unrelated writes preserve it through
input rebase. Boot starts at index zero. Index is RAM-only and per-channel.

The wire string is length-prefixed, up to 63 ASCII digits/commas (16 IDs, 0..255).
No ZCL invalid-string sentinel, duplicates, signs, empty elements or overlong IDs
are accepted. Z2M normalizes whitespace and leading zeros before writing.
GET returns the same scene_list as a JSON string.

config2 retains its original 130-byte layout (version byte, padding, 8x16-byte
channels). New scene lists are stored separately in scene_lists (8x64 bytes),
and scene_cycle holds an endpoint bitmask. On export, function 13 is represented
as legacy function 9; on import it is restored only for push/scene channels.
Old configs default to empty lists; previous settings and binding priority remain.
Rollback firmware sees the former single-scene mode using scene_id, not cycling.
All keys are written before one nvs_commit; factory reset clears these keys too.


## Identify and device diagnostics (2.2.2, converter 0.0.9)

Identify 0x0003 server exists on all eight endpoints. IdentifyTime is managed by
the SDK; a nonblocking 10 ms application poll drives the shared active-low GPIO15
LED with 250 ms half-period while ANY endpoint has nonzero IdentifyTime. It
turns off as soon as all endpoints stop. The converter sends a standard Identify
command to EP1; timeout defaults to 3 s, with identify_timeout 1..30 s. Identify
with time zero is supported at the protocol level. TriggerEffect-specific LED
effects are not implemented.

Diagnostics 0x0B05 server is on EP1 only, with NumberOfResets 0x0000 uint16,
read-only/reportable. Counter starts at zero on its first firmware boot,
increments at each subsequent boot and saturates at 65535. Saved once per boot
in separate NVS namespace xiao_diag, key resets; RESET network and BOOT config
reset retain it. Full flash erase removes it. Values before first install are unknown.

Extra device-specific diagnostics stay in 0xFC00 on EP1 (read-only/reportable):

| Attribute | Type | Z2M property | Meaning |
|---|---|---|---|
| 0x0030 | uint32 | uptime | Seconds since boot, saturating uint32 |
| 0x0031 | uint8 | reset_reason | esp_reset_reason_t numeric value, mapped to names by converter |
| 0x0032 | uint32 | zigbee_tx_request_errors | Saturating counter of direct-control esp_zb_aps_data_request calls returning an immediate error |

These are device-wide properties, without channel suffixes. They do not replace
standard MAC/APS metrics. No assertion is made about end-to-end delivery, group
ACKs, radio retransmissions, forwarded traffic, action reports or OTA traffic.
Diagnostics are explicitly read during converter configuration and reported to
coordinator endpoint 1 one second after join and every 60 seconds. Offline does
not generate reports. Metrics are readable between reports; uptime updates once
per second. Error counter/uptime reset on boot. Only reset count is persisted.


## Endpoint routing and physical sources (2.3.0)

There are 8 physical inputs and 24 independently configured endpoints, named
ch1..ch24 in Z2M. Manufacturer/model, profile, OTA IDs and partition map stay
unchanged. All outgoing command clusters and custom control server exist on all
24 endpoints. Identify accepts all 24; OTA and device diagnostics stay on EP1.
Binding snapshots use uint32 masks and an SDK source table of 96 entries.

Additional RW attributes in 0xfc00 on EVERY endpoint:

| Attribute | Type | Values |
|---|---|---|
| 0x10 | uint8 | inputSource: 0 none, 1 D0, 2 D1, 3 D2, 4 D3, 5 D4, 6 D5, 7 D8, 8 D9 |
| 0x11 | uint8 | inputGesture: 0 legacy/raw contact, 1 single, 2 double, 3 hold |

Shared physical settings on EP1..8 (storage endpoints correspond to fixed pins
D0/D1/D2/D3/D4/D5/D8/D9, irrespective of logical inputSource mappings):

| Attribute | Type | Values |
|---|---|---|
| 0x40 | uint8 | pinMode: 0 push, 1 rocker |
| 0x41 | bool | pinInvert |
| 0x42 | uint16 | pinHoldTime: 250..5000 ms, default 500 |
| 0x43 | uint16 | pinMultiTime: 150..1000 ms, default 500 |

Endpoint inputMode/inverted mirror the assigned pin; changing either through
an endpoint updates the shared pin and all endpoint aliases. With source=none,
inputMode configures only the virtual endpoint; invert has no serial effect.
Rocker accepts legacy/raw mappings only. A pin may not mix legacy with decoded
mappings. Invalid combinations are rejected without applying partial changes.

The physical classifier runs once per pin. single waits for the multi-click
window only when at least one double endpoint is assigned. Hold suppresses
clicks, dispatches a sustained virtual activation, then releases with the pin.
Decoded single/double routes pulse the logical endpoint once for 100 ms;
delayed scheduling retains the pulse through at least endpoint debounce.
The endpoint's own switch_type determines the command. Logical holds do not
wait a second full 500 ms after physical hold recognition. Existing advanced
short-click classification inside a logical endpoint still uses 500 ms.
Legacy/raw routing retains the previous planner. UART bare-channel virtual
inputs are ORed with physical pin levels before routing; EP-prefixed states
are ORed at the selected endpoint after routing. Each endpoint has independent
ON/OFF/scene/direction state. Multiple endpoints may share the same pin+gesture.

NVS config3 stores the full v3 struct (24 endpoint configs/priorities and 8 pin
configs). Existing config2, bind_priority, onoff_action and scene_lists are read
for migration and remain first-eight exports for rollback. On first migration,
EP1..8 retain their original pin/legacy behavior; EP9..24 default to source=none.
No extra physical actions are enabled by OTA alone. Full config reset clears
config3; network RESET retains it. Rollback firmware cannot use new mappings.
Configuration changes cancel affected endpoint holds before replacing their
settings, then rebase the affected physical pins to suppress synthetic events.
Binding priority writes alone do not interrupt operation.


## Configuration write-hook reentrancy (2.3.1 fix)

In SDK 1.6.9, zb_zcl_set_attr_val_manuf_internal calls zb_zcl_write_attr_hook
before copying the value even when check_access=false. A user inputGesture
write in 2.3.0 synchronized all inputMode/invert/pin aliases via the same SDK
setter; that setter re-entered write_attr and recursively synchronized aliases
again, preventing a Write Attributes Response and causing a Z2M timeout.

config_write_guard now scopes the whole accepted apply operation. Internal
validation returns success without replacing the pending candidate; internal
write hooks return immediately, leaving the SDK to copy their values. The guard
is released after the outer operation, so subsequent external writes continue.
No reporting/routing/persistence code runs a second time for local alias writes.
Config3 and all wire attributes remain compatible. Converter 0.1.2 additionally
filters the exposed configuration fields by endpoint mode/type; this does not
change wire attributes or stored values.
