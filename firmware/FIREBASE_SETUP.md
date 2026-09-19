# AeroSense — Firebase setup

Everything the device needs from the Firebase console, in order. Budget about
fifteen minutes the first time.

At the end you will have filled in six values in `src/Secrets.h`. Until you do,
the firmware builds and runs exactly as the offline version did — no radio is
started and nothing leaves the device.

---

## 1. Create the project

1. Go to <https://console.firebase.google.com> and sign in with a Google account.
2. **Add project** → name it (e.g. `aerosense`) → Continue.
3. Google Analytics is not used by this device. Turn it off unless you want it.
4. Wait for provisioning, then **Continue**.

You are on the **Spark (free)** plan. It is enough for this device: one unit
writing every 5 seconds uses a few MB of transfer per month against a 10 GB/mo
allowance.

## 2. Create the Realtime Database

> Make sure you pick **Realtime Database**, not **Firestore Database**. They are
> different products and the firmware speaks to the first one.

1. Left sidebar → **Build** → **Realtime Database** → **Create Database**.
2. Choose a location near you (e.g. `asia-southeast1`). This cannot be changed later.
3. Choose **Start in locked mode**. Rules come in step 5.

Copy the URL shown above the empty data tree. It looks like:

```
https://aerosense-1234-default-rtdb.asia-southeast1.firebasedatabase.app
```

→ this is `FIREBASE_DATABASE_URL`. **No trailing slash.**

## 3. Get the Web API key

1. Gear icon (top left, next to Project Overview) → **Project settings**.
2. **General** tab → scroll to **Web API Key**.

→ this is `FIREBASE_API_KEY`.

This key is not a secret in the usual sense — it identifies the project, it does
not grant access. Access is controlled entirely by step 5's rules. It is
gitignored anyway because it sits in the same file as the password.

## 4. Create the device account

The firmware signs in as a normal Firebase user. This is a service identity for
the hardware, not a person.

1. **Build** → **Authentication** → **Get started**.
2. **Sign-in method** tab → **Email/Password** → enable the first toggle
   (leave "Email link / passwordless" off) → **Save**.
3. **Users** tab → **Add user**.
   - Email: something on a domain you control, e.g. `aerosense-01@yourdomain.com`.
     It never receives mail and does not need to be a real inbox.
   - Password: long and random. Generate one; you will paste it once.
4. **Add user**.

→ these are `FIREBASE_USER_EMAIL` and `FIREBASE_USER_PASSWORD`.

Copy the **User UID** from the users table — the rules in the next step need it.

## 5. Security rules

**Build** → **Realtime Database** → **Rules** tab. Replace the contents with the
following, substituting the UID you just copied:

```json
{
  "rules": {
    "devices": {
      "aerosense-01": {
        ".read": "auth != null && auth.uid === 'PASTE_DEVICE_UID_HERE'",
        ".write": "auth != null && auth.uid === 'PASTE_DEVICE_UID_HERE'"
      }
    }
  }
}
```

**Publish.**

This grants the device write access to its own node and nothing else. An
attacker who extracted the credentials from the flash could not read or damage
anything outside `/devices/aerosense-01`.

If you add a second unit, give it its own `DEVICE_ID`, its own user, and its own
block in the rules.

### Letting a dashboard read the data

The rules above deliberately lock *reading* to the device too. To let a web
dashboard or phone app read it, add a second condition — for example, your own
signed-in account:

```json
".read": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'YOUR_UID')"
```

Do not use `".read": true`. That publishes your sensor history to anyone who
guesses the project URL.

## 6. Fill in Secrets.h

```powershell
copy src\Secrets.example.h src\Secrets.h
```

(`src/Secrets.h` already exists in this checkout with empty values — just edit
it.) Fill in all six:

| Value | Where it came from |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | your 2.4 GHz network |
| `FIREBASE_API_KEY` | step 3 |
| `FIREBASE_DATABASE_URL` | step 2 |
| `FIREBASE_USER_EMAIL` / `FIREBASE_USER_PASSWORD` | step 4 |
| `DEVICE_ID` | your choice; must match the rules in step 5 |

`src/Secrets.h` is gitignored. `src/Secrets.example.h` is the committed template
— never put real values in it.

## 7. Flash and watch

```powershell
pio run -t upload
pio device monitor
```

A healthy first boot looks like:

```
[wifi] connecting to "YourNetwork"
[wifi] connected, ip 192.168.1.42, rssi -58 dBm
[cloud] signing in
Token info: type = access token, status = on request
Token info: type = access token, status = ready
```

Then the console's data tree fills in under `devices/aerosense-01/live` and
updates every 5 seconds.

---

## What gets uploaded

`/devices/<DEVICE_ID>/live` is overwritten every `FIREBASE_UPDATE_INTERVAL`
(5 s). `/devices/<DEVICE_ID>/history/<pushId>` is appended every
`FIREBASE_HISTORY_INTERVAL_MS` (60 s). Both carry the same fields:

| Field | Meaning |
|---|---|
| `pm1_0`, `pm2_5`, `pm10` | particulates, µg/m³, rolling-averaged |
| `pmsConnected` | false if the PMS5003 has sent no valid frame in 5 s |
| `mq2`, `mq2Baseline` | gas sensor, smoothed raw ADC counts, and its clean-air reference |
| `mqWarmedUp`, `mqWarmupRemainingSec` | gas readings are unreliable until warm-up finishes |
| `score`, `category` | composite 0–100 environment score and its label |
| `aqi`, `aqiValid` | US EPA AQI; `aqi` is omitted entirely when not valid |
| `uptimeSec`, `rssi` | device health |
| `ts` | server-side timestamp, stamped by Firebase, not the device |

History is append-only and nothing prunes it. At one record a minute that is
~1,440/day, which is fine indefinitely on Spark, but if you want it bounded,
set `FIREBASE_HISTORY_ENABLED` to 0 in `Config.h` or add a scheduled Cloud
Function to trim it.

---

## Wi-Fi provisioning

The device tries, in order:

1. `WIFI_SSID` / `WIFI_PASSWORD` from `Secrets.h`.
2. Credentials stored by a previous SmartConfig session.

When both exist it alternates between them on each failure, so a unit that was
paired by phone still comes back up on its built-in network and vice versa.
After `WIFI_ATTEMPTS_BEFORE_SC` (3) consecutive failures it stops guessing and
listens for a phone for `SMARTCONFIG_TIMEOUT_MS` (3 minutes), cycling ESPTouch
and ESPTouch+AirKiss every 40 s because phone apps differ in which they speak
and rarely say so.

To pair: install an ESPTouch app (Espressif's "EspTouch", or "IoT Espressif"),
put the phone on the 2.4 GHz network you want the device on, and enter the
password. The serial log shows which protocol is listening at any moment.

**ESPTouch v2 is not in the cycle by default.** This ESP32 core forces
encryption on for v2, so an unkeyed v2 listener can never decode. Set
`SMARTCONFIG_V2_KEY` in `Config.h` to a string of *exactly* 16 characters — the
same string entered in the app — and it joins the rotation automatically.

SmartConfig is unreliable on recent Android versions, which restrict the
multicast the protocol depends on. Stored credentials are the dependable path;
treat pairing as the fallback it is.

---

## Troubleshooting

**Never connects, no timeout message.** The SSID is 5 GHz. The ESP32 has no
5 GHz radio. This is the most common cause by a wide margin.

**`[wifi] attempt N timed out` repeatedly.** Wrong password, or the AP has band
steering with one SSID for both bands — split them, or use the 2.4 GHz-only SSID.

**`token is not ready` / `INVALID_LOGIN_CREDENTIALS`.** The email or password in
`Secrets.h` does not match the user in **Authentication → Users**, or
Email/Password sign-in was never enabled in step 4.2.

**`PERMISSION_DENIED`.** The rules in step 5 do not match. Check that the UID in
the rules is the device user's UID and that `DEVICE_ID` in `Secrets.h` matches
the node name in the rules exactly.

**`connection refused` / `ssl handshake failed`.** Usually a captive portal or a
firewall intercepting TLS. Try a phone hotspot to confirm.

**Device reboots when it starts uploading.** Stack overflow in the Firebase
task. Raise `FIREBASE_TASK_STACK` in `Config.h` from 10240 to 12288.

**Nothing at all on serial.** The monitor baud must be 115200 and the port must
be the CP210x one, not a Bluetooth COM port. `pio device list` shows which.

---

## One thing to fix outside the code

`AeroSense_User_Guide.docx` and `AeroSense_Quick_Guide.docx` both currently
state that the device has "no Wi-Fi and no network connection of any kind" and
that "nothing leaves the device." Once a unit ships with credentials in
`Secrets.h` that is no longer true, and the guides need updating — both the
factual claim and, depending on who the users are, a note about what is stored
and where.
