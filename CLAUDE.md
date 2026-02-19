# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

rc-monitor is a self-contained C library for parsing DJI remote controller inputs via the DUML protocol over USB. It decodes button/stick state from RM510-series controllers (RC-N1, RC-N2, DJI RC). The protocol was reverse-engineered from `libdjisdk_jni.so` in DJI Mobile SDK V5 5.17.0.

## Build & Test Commands

### Desktop (development/testing)

```bash
mkdir build && cd build
cmake ..
make
./test_rc_monitor
```

The test binary runs 32 unit tests and prints pass/fail for each. All tests must pass with "All tests passed." at the end.

### RC Emulator

```bash
./rc_emulator              # interactive TUI
./rc_emulator -o rec.bin   # record DUML frames to file
```

The emulator exercises the full parsing pipeline without hardware. Keyboard/mouse input drives virtual RC state through `build_payload() → rcm_build_packet() → rcm_feed() → rcm_parse_payload() → callback`. The parsed `rc_state_t` from the callback is what the UI displays.

### Recording verification

```bash
./verify_recording rec.bin
```

Feeds a recorded `.bin` file back through `rcm_feed()` and prints each decoded frame. Confirms the emulator produces valid DUML frames that round-trip through the parser.

### Android (NDK)

The CMakeLists.txt auto-detects Android via `if(ANDROID)` and builds `librc_monitor.so` (shared) instead of the static lib + test binary. This is referenced from an Android app's `build.gradle` via `externalNativeBuild`.

## Architecture

### Data Flow

```
USB bulk read → rcm_feed() → ring buffer → DUML frame parser → rcm_parse_payload() → rc_state_t → callback
```

### C Core (`src/rc_monitor.c`, `include/rc_monitor.h`)

- **Ring buffer DUML parser**: 4096-byte circular buffer with a two-state machine (SCAN_SOF → READ_FRAME) that handles partial USB reads and frame reassembly.
- **Dual CRC validation**: CRC8 (seed 0x77) validates headers; CRC16 (seed 0x3692) validates full frames. Both use precomputed 256-entry lookup tables.
- **Payload decoder**: Extracts 17-byte RC push payloads (cmd_set=0x06, cmd_id=0x05) into `rc_state_t` using bit masks — not C bitfields — to avoid compiler alignment issues.
- **Stick centering**: Raw uint16 LE values are centered by subtracting 0x400 (1024), yielding signed range ~-660 to +660.
- **Packet builder**: Constructs DUML v1 frames with proper CRC, used for enable commands and channel requests.

### JNI Bridge (`src/rc_monitor_jni.c`)

Singleton `jni_ctx_t` holds the parser, JavaVM reference, listener global ref, and cached method ID. The callback attaches the thread to the JVM when invoked from the USB read thread. State is passed as 20 individual parameters (not an object) for speed.

### Java Layer (`java/com/dji/rcmonitor/`)

- **RcMonitor.java**: Wraps native methods. `SimpleListener` adapter packs the 20 callback parameters into an `RcState` object for convenience.
- **UsbRcReader.java**: Full USB lifecycle — device discovery by VID/PID, CDC ACM setup (115200 8N1, DTR+RTS), DUML handshake (enable cmd), background read loop with automatic fallback to polling if push data stops after 2 seconds.

### Tests (`test/test_rc_monitor.c`)

Minimal test harness with `ASSERT`/`ASSERT_EQ` macros — no external test framework. Tests cover payload parsing, DUML frame parsing with CRC verification, packet building with round-trip validation, and edge cases (short payloads, garbage prefixes, null pointers).

### RC Emulator (`emulator/rc_emulator.c`)

Single-file interactive ncurses tool that exercises the full pipeline without hardware. Maps keyboard/mouse input to virtual RC state, constructs 17-byte payloads (inverse of `rcm_parse_payload()`), wraps them in DUML frames via `rcm_build_packet()`, and feeds through `rcm_feed()`. The parsed `rc_state_t` from the callback drives the terminal UI display. Optional `-o <file>` flag records raw DUML frames for replay or fuzzer corpus generation.

### Recording Verifier (`test/verify_recording.c`)

Feeds a `.bin` file of raw DUML frames back through `rcm_feed()` and prints each decoded `rc_state_t`. Used to validate that emulator recordings (or captured USB data) round-trip correctly through the parser.

## Key Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `DUML_SOF` | 0x55 | Start of frame marker |
| `DUML_CMD_SET_RC` | 0x06 | RC command set |
| `DUML_CMD_RC_PUSH` | 0x05 | RC push packet |
| `RC_PUSH_PAYLOAD_LEN` | 17 | Expected payload size |
| `DJI_USB_VID` | 0x2CA3 | DJI vendor ID |
| `DJI_USB_PID_INIT` | 0x0040 | Initial USB PID |
| `DJI_USB_PID_ACTIVE` | 0x1020 | Active USB PID |

## Payload Byte Layout (17 bytes)

Bytes 0–4 are button/switch bitfields. Bytes 5–16 are six uint16 LE analog values (right stick H/V, left stick V/H, left wheel, right wheel). Byte 4 contains the right wheel delta as a 5-bit magnitude + 1-bit sign. Full bit-level documentation is in `RC_MONITORING_SPEC.md`.
