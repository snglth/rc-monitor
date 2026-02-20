# Ghidra Guide for DJI SDK Reverse Engineering

Guide to navigating the DJI Mobile SDK V5 native libraries in Ghidra, using the Ghidra MCP bridge. Documents the methodology used to reverse-engineer the DUML protocol and RC push payload format for the rc-monitor project.

## 1. Target Binaries

Three `.so` files from the DJI Mobile SDK V5 5.17.0 AAR (`jni/arm64-v8a/`):

| Binary | Size | Role |
|--------|------|------|
| `libdjisdk_jni.so` | ~65 MB | SDK logic, JNI registration, RC push handler (`OnButtonPhysicalStatusPush`) |
| `libDJICSDKCommon.so` | ~175 KB | `PackObserver` command routing (cmd_set/cmd_id dispatch) |
| `libdjibase.so` | ~25 MB | USB transport, DUML framing, CRC computation |

All are AARCH64:LE:64:v8A ELF shared libraries. When loaded in Ghidra, use the `program_name` parameter to target a specific binary when multiple are open.

## 2. Ghidra MCP Tools Reference

Key tools used in this project:

| Tool | Purpose |
|------|---------|
| `list_programs` | See which `.so` files are loaded and which is active |
| `list_functions` | Find functions by name pattern (e.g. `OnButtonPhysicalStatusPush`) |
| `get_function_info` | Get signature, entry point, parameter count |
| `get_code` | Decompile a function (`decompiler`), or view `disassembly` or `pcode` |
| `search_bytes` | Find known byte patterns (CRC tables, magic values) |
| `get_hexdump` | Examine raw memory at an address |
| `xrefs` | Find cross-references to/from an address or function |
| `list_strings` | Search for string constants (SDK key names, error messages) |
| `list_exports` | List exported symbols |
| `list_imports` | List imported symbols |

When multiple programs are open, pass `program_name` (e.g. `"libdjibase.so"`) to target a specific binary. Without it, the active program is used.

## 3. Validating CRC Tables (libdjibase.so)

### Locating the CRC8 table

The rc-monitor CRC8 table starts with `00 5e bc e2 61 3f dd 83`. Search for this pattern:

```
search_bytes: pattern="005ebce2613fdd83"
              program_name="libdjibase.so"
```

Result: table found at **0x015d8946**.

Hexdump confirms the first 64 bytes match rc-monitor's `crc8_table`:

```
15d8946  00 5e bc e2 61 3f dd 83  c2 9c 7e 20 a3 fd 1f 41
15d8956  9d c3 21 7f fc a2 40 1e  5f 01 e3 bd 3e 60 82 dc
15d8966  23 7d 9f c1 42 1c fe a0  e1 bf 5d 03 80 de 3c 62
15d8976  be e0 02 5c df 81 63 3d  7c 22 c0 9e 1d 43 a1 ff
```

### Locating the CRC16 table

The CRC16 table starts with `0000 8911 1223 9b32` (little-endian uint16 entries). Found at **0x015d8a46**:

```
15d8a46  00 00 89 11 12 23 9b 32  24 46 ad 57 36 65 bf 74
15d8a56  48 8c c1 9d 5a af d3 be  6c ca e5 db 7e e9 f7 f8
```

### Finding the CRC functions

Use `xrefs` on the table addresses to find the functions that reference them, or decompile directly:

- **calc_crc8** at `0x00a085ec`:

```c
uint calc_crc8(byte *param_1, ushort param_2) {
    uint uVar1;
    ulong uVar2;
    if (param_2 != 0) {
        uVar2 = (ulong)param_2;
        uVar1 = 0x77;                                    // <-- seed
        do {
            uVar2 = uVar2 - 1;
            uVar1 = (uint)(byte)(&DAT_015d8946)[*param_1 ^ uVar1];  // <-- table
            param_1 = param_1 + 1;
        } while (uVar2 != 0);
        return uVar1;
    }
    return 0x77;
}
```

- **calc_crc16** at `0x00a0863c`:

```c
ulong calc_crc16(byte *param_1, ushort param_2) {
    ulong uVar1;
    ulong uVar2;
    uVar1 = (ulong)*DAT_01a06d20;            // seed via global pointer
    if (param_2 != 0) {
        uVar2 = (ulong)param_2;
        do {
            uVar2 = uVar2 - 1;
            uVar1 = (ulong)(
                (uint)*(ushort *)(&DAT_015d8a46 + ((*param_1 ^ uVar1) & 0xff) * 2)
                ^ (uint)uVar1 >> 8
            );
            param_1 = param_1 + 1;
        } while (uVar2 != 0);
    }
    return uVar1;
}
```

### Confirming the CRC16 seed

The seed is loaded indirectly through a global pointer at `0x01a06d20`. The actual seed value lives at `0x01a17728`:

```
1a17728  92 36 ...
```

Little-endian uint16: **0x3692**. This matches rc-monitor's `CRC16_SEED`.

## 4. Analyzing OnButtonPhysicalStatusPush (libdjisdk_jni.so)

### Finding the function

```
list_functions: pattern="OnButtonPhysicalStatusPush"
                program_name="libdjisdk_jni.so"
```

Returns four variants, one per RC abstraction class:

| Address | Class | RC Model |
|---------|-------|----------|
| `0x023866e4` | `FlightControllerAbstraction` | FC (flight controller) |
| `0x028021c0` | `RC701RemoteControllerAbstraction` | DJI RC Pro |
| `0x028576a8` | `RM510RCAbstraction` | RC-N1/N2/DJI RC |
| `0x028624a0` | `RM700RemoteControllerAbstraction` | RC-Plus |

The **RM510** variant at `0x028576a8` is the primary target. All variants share the same payload layout.

### Reading the decompiled output

The full function signature:

```
dji::sdk::RM510RCAbstraction::OnButtonPhysicalStatusPush(dji_cmd_rsp const*)
```

The decompiler output is long (~500 lines) due to C++ smart pointer boilerplate. The important pattern is:

1. **Payload access**: `puVar28 = *(uint5 **)(param_1 + 0x28)` -- the payload pointer is at offset 0x28 in the `dji_cmd_rsp` struct.

2. **Button bitfield extraction** uses a `uint5` (40-bit / 5-byte) little-endian read: `uVar15 = *puVar28` loads bytes 0-4 as a single value, then individual bits are extracted with shifts.

3. **Stick values** are read as `ushort` at byte offsets from the payload pointer, then centered by subtracting `0x400`.

### Field verification table

Every field extracted in the decompiler output matches rc-monitor's implementation:

| Field | Ghidra expression | Bit position | rc-monitor equivalent |
|-------|-------------------|--------------|----------------------|
| Pause | `(uVar15 >> 4) & 1` | byte0 bit4 | `payload[0] & 0x10` |
| GoHome | `(uVar15 >> 5) & 1` | byte0 bit5 | `payload[0] & 0x20` |
| Shutter | `(uVar15 >> 6) & 1` | byte0 bit6 | `payload[0] & 0x40` |
| Record | `(uVar15 >> 8) & 1` | byte1 bit0 | `payload[1] & 0x01` |
| 5D Right | `(uVar15 >> 0xb) & 1` | byte1 bit3 | `payload[1] & 0x08` |
| 5D Up | `(uVar15 >> 0xc) & 1` | byte1 bit4 | `payload[1] & 0x10` |
| 5D Down | `(uVar15 >> 0xd) & 1` | byte1 bit5 | `payload[1] & 0x20` |
| 5D Left | `(uVar15 >> 0xe) & 1` | byte1 bit6 | `payload[1] & 0x40` |
| 5D Center | `(uVar15 >> 0xf) & 1` | byte1 bit7 | `payload[1] & 0x80` |
| Flight mode | `(uVar15 >> 0x10) & 3` | byte2 bits[1:0] | `payload[2] & 0x03` |
| Custom1 | `(uVar15 >> 0x12) & 1` | byte2 bit2 | `payload[2] & 0x04` |
| Custom2 | `(uVar15 >> 0x13) & 1` | byte2 bit3 | `payload[2] & 0x08` |
| Custom3 | `(uVar15 >> 0x14) & 1` | byte2 bit4 | `payload[2] & 0x10` |
| Right stick H | `*(ushort *)(puVar28 + 5) - 0x400` | bytes 5-6 | `u16le(payload+5) - 0x400` |
| Right stick V | `*(ushort *)(puVar28 + 7) - 0x400` | bytes 7-8 | `u16le(payload+7) - 0x400` |
| Left stick V | `*(ushort *)(puVar28 + 9) - 0x400` | bytes 9-10 | `u16le(payload+9) - 0x400` |
| Left stick H | `*(ushort *)(puVar28 + 0xb) - 0x400` | bytes 11-12 | `u16le(payload+11) - 0x400` |
| Left wheel | `*(ushort *)(puVar28 + 0xd) - 0x400` | bytes 13-14 | `u16le(payload+13) - 0x400` |
| Right wheel | `*(ushort *)(puVar28 + 0xf) - 0x400` | bytes 15-16 | `u16le(payload+15) - 0x400` |
| R wheel delta | `(byte4 >> 1 & 0x1f) * sign` | byte4 bits[5:1] + bit6 | `(payload[4]>>1 & 0x1F) * sign` |

### Right wheel delta sign convention

The sign is determined by testing bit 38 of the 40-bit value (bit 6 of byte 4):

```c
iVar4 = 1;
if ((uVar15 & 0x4000000000) == 0) {
    iVar4 = -1;
}
// result = (byte4 >> 1 & 0x1f) * iVar4
```

`0x4000000000` = bit 38 = byte4 bit6. When this bit is **set**, the sign multiplier is +1 (positive). When **clear**, the multiplier is -1 (negative). This matches rc-monitor's implementation where bit6=1 means positive.

### SDK key string constants

Each field is published via an SDK key string. These serve as useful landmarks when navigating the decompiler output:

| String constant | Field |
|----------------|-------|
| `RCPauseButtonDown` | Pause button |
| `RCGohomeButtonDown` | GoHome button |
| `RCShutterButtonDown` | Shutter button |
| `RCRecordButtonDown` | Record button |
| `RCCustomButton1Down` | Custom C1 |
| `RCCustomButton2Down` | Custom C2 |
| `RCCustomButton3Down` | Custom C3 |
| `RCFlightModeSwitchState` | Flight mode switch |
| `RCStickRightHorizontal` | Right stick H |
| `RCStickRightVertical` | Right stick V |
| `RCStickLeftVertical` | Left stick V |
| `RCStickLeftHorizontal` | Left stick H |
| `RCLeftWheel` | Left wheel |
| `RCRightWheel` | Right wheel |
| `RCRightNewWheel` | Right wheel delta |
| `RcFiveDimensionPressedStatus` | 5D joystick state |
| `MockRcHardwareState` | Combined hardware state |

## 5. Reverse Engineering Techniques

### Understanding ARM64 decompiler output

Ghidra's AARCH64 decompiler produces several artifacts that can be safely ignored:

- **ExclusiveMonitorPass / ExclusiveMonitorsStatus**: These correspond to ARM64 `ldxr`/`stxr` (load/store exclusive) instructions used for atomic operations. They implement `std::shared_ptr` reference counting. Ignore the surrounding do/while loops.

- **CONCAT / SUB macros**: `CONCAT17`, `CONCAT31`, `SUB81`, etc. are Ghidra's way of expressing byte-level concatenation and extraction when the decompiler can't recover the original data types. `CONCAT17(a,b)` concatenates 1 byte `a` with 7 bytes `b`. `SUB81(x,n)` extracts byte `n` from an 8-byte value `x`.

- **`__shared_weak_count`**: C++ smart pointer reference count type. The repetitive blocks that test `lVar27 == 0` then call a virtual destructor are shared_ptr release sequences.

### String constants as landmarks

SDK key names (e.g. `s_RCPauseButtonDown_0380b6f3`) are the most reliable way to identify which field is being processed in a long function. Search for these strings first, then examine the code around each reference.

### Dynamic JNI registration

`libdjisdk_jni.so` uses dynamic JNI registration via `JNI_OnLoad` (at `0x01984a04`) rather than static `Java_`-prefixed function names. This means `list_functions` with pattern `Java_` won't find the native method implementations. Instead, trace from `JNI_OnLoad` → `RegisterNatives` calls.

### Navigating C++ vtable dispatch

Most method calls in the decompiled output appear as:
```c
(**(code **)(*(long *)this + 0x58))(this, ...)
```

This is a virtual method call through a vtable. The `0x58` is the vtable offset. To find the actual target, examine the vtable pointer stored at the object's base address.

## 6. Workflow Recipes

### Finding functions by name pattern

```
list_functions: pattern="OnButton"
                program_name="libdjisdk_jni.so"
```

Supports glob-style matching. Case-sensitive by default; add `case_sensitive: false` for case-insensitive.

### Finding data by known byte content

```
search_bytes: pattern="005ebce2613fdd83"
              program_name="libdjibase.so"
```

Hex string, no spaces. Use `??` for wildcard bytes (e.g. `55??04` matches SOF frames with any length byte).

### Tracing callers/callees

```
xrefs: address="0x00a085ec"
       direction="to"
       program_name="libdjibase.so"
```

- `direction="to"`: who calls this address
- `direction="from"`: what this address calls
- `direction="both"`: both directions

For functions: `xrefs: function="calc_crc8"` works by name.

### Examining raw memory

```
get_hexdump: address="0x015d8946"
             len=256
             program_name="libdjibase.so"
```

Standard hex+ASCII output. Useful for examining `.rodata`, `.data`, and `.bss` sections.

### Decompiling functions

```
get_code: function="OnButtonPhysicalStatusPush"
          format="decompiler"
          program_name="libdjisdk_jni.so"
```

Three output formats:
- `decompiler`: C-like pseudocode (most readable)
- `disassembly`: ARM64 assembly with comments
- `pcode`: Ghidra's intermediate representation (for advanced analysis)

Functions can be specified by name or address (e.g. `"0x028576a8"`).

### Cross-library navigation

When analyzing code that spans multiple `.so` files, use `program_name` to switch context:

```
# Check CRC implementation in libdjibase.so
get_code: function="calc_crc8", program_name="libdjibase.so"

# Then examine how it's called from libdjisdk_jni.so
list_functions: pattern="crc", program_name="libdjisdk_jni.so"
```

## 7. Key Addresses Reference

All addresses are specific to **DJI Mobile SDK V5 5.17.0** (`arm64-v8a`).

### libdjibase.so

| Address | Symbol | Description |
|---------|--------|-------------|
| `0x00a085ec` | `calc_crc8` | CRC8 computation (seed 0x77) |
| `0x00a0863c` | `calc_crc16` | CRC16 computation (seed 0x3692) |
| `0x015d8946` | CRC8 table | 256-byte lookup table |
| `0x015d8a46` | CRC16 table | 256 x uint16 lookup table (512 bytes) |
| `0x01a17728` | CRC16 seed | Global: `0x3692` (little-endian) |

### libdjisdk_jni.so

| Address | Symbol | Description |
|---------|--------|-------------|
| `0x01984a04` | `JNI_OnLoad` | Dynamic JNI registration entry point |
| `0x023866e4` | `FlightControllerAbstraction::OnButtonPhysicalStatusPush` | FC variant |
| `0x028021c0` | `RC701RemoteControllerAbstraction::OnButtonPhysicalStatusPush` | DJI RC Pro variant |
| `0x028576a8` | `RM510RCAbstraction::OnButtonPhysicalStatusPush` | RC-N1/N2/DJI RC variant |
| `0x028624a0` | `RM700RemoteControllerAbstraction::OnButtonPhysicalStatusPush` | RC-Plus variant |

All `OnButtonPhysicalStatusPush` variants share the same 17-byte payload layout and `- 0x400` centering logic. The RC701 variant includes an additional `RCAuthLedButtonDown` field (byte1 bit2, shift 0xa) not present in the RM510 variant.
