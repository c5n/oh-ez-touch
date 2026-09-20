# Bluetooth LE beacons

Off by default. With **Scan for BLE beacons** turned on, the panel listens
for BLE advertisements and publishes what it hears to the MQTT broker, under
a `ble/` subtree. The panel is a receiver only. It advertises nothing,
connects to nothing and cannot pair.

Turning the scanner on needs a restart. The Bluetooth controller claims tens
of kilobytes of RAM at startup. Stopping it does not give the RAM back. A
panel that is not scanning must never have started the controller.

## Topics

The topics sit under the MQTT prefix. With the defaults they read
`oheztouch/oheztouch-new/ble/...`. The key is the advertiser's hardware
address, lower case hex, no separators.

| Topic | Published | Value |
| --- | --- | --- |
| `count` | every window | How many advertisers are currently published |
| `dropped` | every window | Reports lost to a full queue since boot. Normally 0. |
| `<addr>/type` | on discovery | `iBeacon`, `Eddystone-UID`, `Eddystone-URL` or `device` |
| `<addr>/id` | on discovery | The beacon's own identity, or empty for a device that has none |
| `<addr>/name` | on discovery | The advertised name, when there is one |
| `<addr>/power` | on discovery | dBm: the power at one metre a beacon declares, or a plain device's transmit power |
| `<addr>/rssi` | every window | dBm, averaged over the window |
| `<addr>/distance` | every window | Metres, estimated. Beacons only. See below. |
| `<addr>/battery` | every window | mV, Eddystone-TLM only |
| `<addr>/temperature` | every window | Degrees Celsius, Eddystone-TLM only |

Three formats are recognized:

- **iBeacon**: the identity is the proximity UUID, the major and the minor.
- **Eddystone-UID**: a namespace and an instance.
- **Eddystone-URL**.

**Eddystone-TLM** is not an identity but telemetry. Beacons interleave it
between their identity frames. Its battery and temperature are merged onto
the entry the identity frames built.

Anything else in range (a phone, a watch, a thermostat) is a `device`. It is
published with its advertised name and transmit power, and only when
**Publish non-beacon devices** is on.

When an advertiser has not been heard for **Forget after** seconds, all of
its topics are cleared with a zero-length retained publish. That is how a
retained message is removed. Without this, a beacon carried out of the
building would stay in the broker at its last RSSI forever.

## Two things to know before wiring it up

**The address is the key, and the identity is a value.** This is the other
way round from how it is usually drawn. It has to be this way. An iBeacon's
UUID is shared on purpose: a shop's hundred tags carry one UUID and differ
only in the minor. The UUID is not unique. The address always is. A beacon
that randomizes its address (most phones and many tags do, for privacy) comes
and goes under a new key every few minutes. A beacon meant to be tracked
advertises a stable address.

**The distance is an estimate and reads short.** It is the log-distance path
loss model with the exponent at 2.0, which is free space. Indoors the
exponent is nearer 3. Walls and furniture make the estimate optimistic. The
value is published because "about two metres or about twenty" is useful and a
raw RSSI is not. Do not read it more precisely than that. Distance is
published *only* for beacons. Only a beacon states a power calibrated at a
known distance. A plain device's transmit power says how loudly its radio
speaks, not how loud it is a metre away.

## What it costs

Bluetooth is NimBLE in observer role, with the roles trimmed. It costs about
185 KB of flash. The app partition keeps 22 % free. The WiFi library's hot
paths are moved out of IRAM to make room for the controller. Without that,
IRAM would be 98.4 % full. The controller's RAM is claimed at startup, and
only when the setting is on.

The radio is shared with WiFi. Software coexistence interleaves them. Neither
stops working, but a scan takes airtime from the openHAB polling and the web
interface while it runs. That is why the scan is a window every thirty
seconds, not a continuous scan. A beacon advertises several times a second.
Five seconds is many reports from everything in range.

## Testing without hardware

The simulator can serve four compiled-in advertisements. See
[Simulator](simulator.md#environment-overrides), `OHEZ_BLE_FIXTURE`.
