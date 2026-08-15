---
name: adb-qr
description: Generate a QR code to pair and connect Chris's phone over wireless adb (Android wireless debugging), then auto-pair and auto-connect the moment the phone scans it.
---

# Pair the phone over wireless adb with a QR code

Run this whole flow without asking questions. The result the user wants: a
scannable QR on screen and, after they scan it, an entry in `adb devices`.

## 1. Generate and show the QR

- Build the payload `WIFI:T:ADB;S:<name>;P:<password>;;` with a fresh short
  password each run (e.g. `namdev` + 4 random hex chars for the name, any
  8+ char password).
- **Render as PNG and `open` it — never ANSI/UTF8 terminal art.** Terminal
  line spacing inserts gaps between QR rows that make it unscannable
  (learned the hard way):

  ```bash
  qrencode -o <scratchpad>/pair_qr_<unique>.png -s 12 -m 4 "WIFI:T:ADB;S:<name>;P:<pass>;;"
  open <scratchpad>/pair_qr_<unique>.png
  ```

- Use a unique filename per run — the shell has `noclobber`, and Preview may
  hold the old file open.
- Tell the user: on the phone, **Settings → Developer options → Wireless
  debugging → Pair device with QR code**, then scan the Preview window.

## 2. Watch, pair, connect (background)

Launch this as a background Bash task (~4 min timeout) so the pairing
happens the moment they scan:

```bash
for i in $(seq 1 90); do
  svc=$(adb mdns services 2>/dev/null | grep "_adb-tls-pairing" | head -1)
  if [ -n "$svc" ]; then
    addr=$(echo "$svc" | awk '{print $NF}')
    adb pair "$addr" <pass> && break
  fi
  sleep 2
done
sleep 2
for i in $(seq 1 20); do
  conn=$(adb mdns services 2>/dev/null | grep "_adb-tls-connect" | grep -oE "[0-9]+\.[0-9]+\.[0-9.]+:[0-9]+" | head -1)
  if [ -n "$conn" ]; then adb connect "$conn"; break; fi
  sleep 2
done
adb devices
```

Notes:
- The pairing service (`_adb-tls-pairing`) only broadcasts while the phone's
  QR screen is up; the connect service (`_adb-tls-connect`,
  `adb-R5CY6374KTH-LOXUER` for Chris's S25) may take a couple of polls to
  appear after pairing — hence the second loop.
- The connect **port changes every session**. After connecting, report the
  fresh `IP:port` serial and use `adb -s <that serial>` for everything after
  (old serials in scrollback are stale).
- If the watcher times out because the user hadn't scanned yet, just
  relaunch the watcher loop — the QR stays valid while the phone's pairing
  screen shows it.
- **Don't assume a `192.168.x` subnet** — Chris's S25 has shown up on
  `10.24.248.x`. Match any IPv4 (the loop above does); a hardcoded
  `192\.168\.` grep silently never matches and looks like "pairing failed".
- **If mDNS discovery finds nothing but the phone says it paired, restart
  the adb server** (`adb kill-server && adb start-server`): a pairing that
  already succeeded then shows up directly in `adb devices` as
  `adb-<SERIAL>-XXXXXX._adb-tls-connect._tcp`, which is usable as the `-s`
  serial as-is. Verified 2026-08-15 — the pairing had worked all along and
  only the discovery was stale.
- Fallback if QR scanning fails: the phone's **Pair device with pairing
  code** screen shows `IP:port` + a 6-digit code; run
  `adb pair <IP:port> <code>` directly.
