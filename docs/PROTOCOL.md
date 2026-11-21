# XIAO-C6-4CH-FAIL1, protocol v1.2 (firmware/converter 1.2.0)

Manufacturer: `DIY-XIAO`. Model retained for in-place upgrades.
HA profile `0x0104`, On/Off Switch device `0x0000`, endpoints 1, 2, 3, 4.
Each endpoint exposes a client On/Off cluster (`0x0006`) and a server custom
cluster `0xfc00`. Custom frames have no manufacturer-specific header/code.

| Attribute | Endpoint | ZCL type | Access | Meaning |
|---|---|---|---|---|
| 0x0001 | 1–4 | uint8, 0x20 | R/W | inputMode: 0 push, 1 rocker |
| 0x0002 | 1–4 | bool, 0x10 | R/W | inverted |
| 0x0012 | 1–4 | uint8, 0x20 | R/report | channelRoute: 0 binding, 1 Z2M action |
| 0x0013 | 1–4 | uint8, 0x20 | R/report | bindingStatus: 0 unknown, 1 unbound, 2 bound |
| 0x0020 | 1–4 | uint32, 0x23 | R/report | event: 24-bit counter followed by 8-bit On/Off command |

Attributes 0x0003, 0x0004, 0x0010 and 0x0011 from previous versions have been
removed. No timeout, challenge, heartbeat or online/offline state remains.

One debounced input transition selects exactly one path:

- Bound channel: standard On/Off command through its source endpoint binding
  table. Never produce an actionable event for HA/Z2M.
- Unbound channel: event report to coordinator network address 0, endpoint 1.
  Never issue standard On/Off to another channel's bindings.

The ZDO Mgmt_Bind request targets the device's own short address. Complete
paginated snapshots replace the mask atomically; errors retain the previous mask.
The table is refreshed locally every second and shortly after observed Bind/Unbind.
Only local-source On/Off entries with group or IEEE destinations count. Self and
known coordinator destinations are excluded. Input handling waits for the first
complete snapshot after startup (normally tens of milliseconds).

Binding capacity is 32 entries across all channels. Bind `genOnOff` only to
lamps/groups. Do not bind it to the coordinator. The converter ignores received
standard On/Off commands and does not bind endpoints automatically.

Input configuration persists after 1 second of inactivity. The v1 NVS layout
is retained to preserve rocker/push and invert settings; the former timeout
storage field is reserved and unused. Zigbee network/bindings storage is unchanged.
An application-only update at 0x10000 preserves both partitions.

External antenna is selected: GPIO3 LOW enables the XIAO RF switch, GPIO14 HIGH
selects U.FL. Startup logs read back both pin levels. This confirms switch control,
not antenna gain or RF link quality. D3 is GPIO21 and is unrelated to GPIO3.

No application buffering/replay or exactly-once guarantee; stack APS/network
retries still apply. Direct binding requires powered targets and a viable mesh path.
HA may independently control the same lamps; state reporting depends on lamp support.
