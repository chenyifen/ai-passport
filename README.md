[简体中文](README.zh_CN.md)

# Swipe Remote

![Swipe Remote illustration](assets/images/swipe-remote-cover.png)

Turn AI Passport into a wireless iPhone short-video remote. Change videos and adjust volume with physical buttons. This standalone firmware starts directly in the phone remote and contains no Mac connection service, desktop bridge or Codex companion.

## Start

1. Install the verified merged firmware at `0x0`. This replaces the installed application and can reset stored pairing/settings; no full-chip erase is needed.
2. On iPhone, open Settings > Accessibility > Touch > AssistiveTouch. Enable AssistiveTouch and disable Drag Lock.
3. Under AssistiveTouch > Devices > Bluetooth Devices, select **Passport Remote**. Enter the six-digit code currently displayed on the device.
4. Wait for **CONNECTED**, then open Douyin or another short-video app in portrait orientation on the unlocked phone.

| Button | Action |
| --- | --- |
| UP click | Swipe down / previous video |
| DOWN click | Swipe up / next video |
| Hold UP | Increase volume once |
| Hold DOWN | Decrease volume once |
| OK click | Recenter the pointer without clicking |
| OK double click | Ask to forget pairing; UP confirms, DOWN cancels |
| Hold OK | Open/close the setup guide |

Buttons do not control the phone while the guide is open. After forgetting pairing locally, forget Passport Remote on iPhone too and pair again. The device saves one bonded peer, resumes advertising on disconnect and starts advertising after reboot. iOS controls when reconnection completes; first pairing and accessibility permissions require the user. No Wi-Fi, account, phone app or computer is required during use.

## Compatibility and validation

The creator has confirmed manual device testing passed. Tracking speed, orientation, iOS settings and the target app can affect drag gestures.

Build and host checks use `./tools/validate.sh` with ESP-IDF 5.5.3. The target is the existing ESP32-C3, 8 MB AI Passport board. Firmware and matching debug artifacts are generated under ignored `build/`; do not commit them. Use the isolated gate instead of cleaning a local toolchain directory.

## Architecture

- `main/phone_remote_main.c`: dedicated remote screen, pairing code, guide, confirmation and non-blocking input queue.
- `main/phone_remote_model.c`: pure button mapping and complete relative mouse drag/release reports.
- `main/phone_remote_ble.c`: authenticated BLE HID mouse and volume reports, persistent bond, reconnect advertising and stale-connection protection. No music transport commands or desktop service.
- `components/bsp`: unchanged board support.

The inherited baseline demo sources remain available as references but are not built by this application's `main/CMakeLists.txt`. The partition layout and shared BSP remain unchanged. Firmware publishing uses the official AI Passport community workflow. Cover artwork, when shown, is an illustration rather than a device screenshot.

## Build checkpoint — 2026-10-29

Build: PASS. Host tests: PASS. Device tests: PASS (creator-reported manual testing). Merged firmware: 1,043,536 bytes, SHA-256 `cfd3e64f160d015a05c48c2ac167c7ebf0be49083812ea486ee16987a1461221`. The gate-verified bundle is `build/firmware/<that-hash>/`. No new firmware has been flashed during this extraction.
