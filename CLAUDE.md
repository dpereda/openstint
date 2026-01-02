# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenStint is a Software-Defined Radio (SDR) based RC lap timing decoder that works with both OpenStint transponders and AMB/RC3 transponders. It receives 5 MHz BPSK modulated transponder signals, decodes them, and publishes timing events via ZeroMQ.

**Key characteristics:**
- Designed to run on constrained hardware (Raspberry Pi 3 Model B+)
- C++20 codebase using liquid-dsp for signal processing
- Real-time signal processing in SDR callback threads
- "Does one thing and one thing well" philosophy

## Build Commands

### Standard Build
```bash
cmake .
make
```

### Clean Build
```bash
rm -rf CMakeCache.txt CMakeFiles src/CMakeFiles
cmake .
make clean
make
```

### Build Options
```bash
# Build without RTL-SDR support
cmake -DUSE_RTLSDR=OFF .

# Build without HackRF support
cmake -DUSE_HACKRF=OFF .

# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug .
make
```

### Running
```bash
# HackRF (default)
./src/openstint

# RTL-SDR
./src/openstint -r

# RTL-SDR Blog V4 with optimal settings
./src/openstint -r -g 60 -m -t 0.67

# See all options
./src/openstint -h
```

### Testing Hardware
```bash
# Test RTL-SDR detection
rtl_test

# Test HackRF detection
hackrf_info

# Test ZeroMQ integration
python3 integrations/subscriber.py localhost 5556
```

## Architecture

### Hardware Abstraction Layer (HAL)

The codebase uses a clean HAL to support multiple SDR backends:

```
┌─────────────────────────────────────────────────────┐
│                    main.cpp                          │
│  (Signal processing, frame detection, decoding)      │
└────────────────────┬────────────────────────────────┘
                     │
                     │ SdrDevice interface
                     │
        ┌────────────▼──────────────┐
        │   sdr_device.hpp/cpp       │
        │   (Factory + interface)    │
        └────────┬──────────┬────────┘
                 │          │
         ┌───────▼──┐   ┌──▼────────┐
         │ HackRF   │   │ RTL-SDR   │
         │ Backend  │   │ Backend   │
         └──────────┘   └───────────┘
```

**Key files:**
- `src/sdr_device.hpp` - Abstract interface (`SdrDevice`) and factory function
- `src/sdr_hackrf.cpp` - HackRF implementation
- `src/sdr_rtlsdr.cpp` - RTL-SDR implementation (includes sample format conversion)
- `src/main.cpp` - Application logic, uses SDR abstraction

**Critical detail:** RTL-SDR produces unsigned 8-bit samples (0-255, centered at 127). These are converted to signed int8 (-128 to +127, centered at 0) in `sdr_rtlsdr.cpp` to match the processing pipeline expectations.

### Signal Processing Pipeline

```
SDR Hardware (5 MHz, 5 MSPS IQ samples)
    │
    ▼
rx_callback (sdr backend)
    │
    ├─> Sample format conversion (RTL-SDR only)
    │
    ▼
FrameDetector::process_baseband()
    │
    ├─> Preamble correlation (OpenStint & Legacy)
    ├─> DC offset & noise variance tracking
    ├─> Threshold detection
    │
    ▼
SymbolReader::read_symbol()
    │
    ├─> Symbol synchronization (liquid-dsp symsync_crcf)
    ├─> BPSK demodulation
    ├─> Soft bit generation (0-255 confidence)
    │
    ▼
decode_openstint() / decode_legacy()
    │
    ├─> FEC decoding (libfec for Legacy)
    ├─> CRC validation
    ├─> Extract transponder ID
    │
    ▼
PassingDetector::append()
    │
    ├─> Temporal clustering of detections
    ├─> RSSI/EVM aggregation
    │
    ▼
ZeroMQ Publisher (:5556)
    │
    └─> Publish "P" (passing), "T" (timesync), "S" (status) messages
```

### Core Components

**Frame Detection (`frame.hpp/cpp`):**
- `FrameDetector` - Correlates incoming samples against known preambles
- `SymbolReader` - Synchronized symbol extraction using liquid-dsp
- Two preambles: OpenStint (16 symbols) and Legacy/AMB (12 symbols)
- Operates at 4 samples per symbol oversampling

**Transponder Support (`transponder.hpp/cpp`):**
- OpenStint protocol: FEC-encoded, 7-digit ID, optional timesync messages
- Legacy/AMB protocol: Convolutional coded, 7-digit ID, compatible with RC3
- Protocol properties defined in `transponder_props()`

**Passing Detection (`passing.hpp/cpp`):**
- Groups multiple frame detections into single "passing" events
- Tracks detections per transponder using `std::map<TransponderKey, vector<Detection>>`
- Temporal clustering identifies when transponder passes antenna
- Thread-safe with mutex protection

**Statistics (`counters.hpp/cpp`):**
- `RxStatistics` tracks frames received/processed, RSSI, noise floor
- Status messages published every second via ZeroMQ

### ZeroMQ Integration

The decoder publishes three message types to port 5556 (configurable):

**Passing ("P"):**
```
P <timestamp> <type> <id> <rssi> <hit_count> <evm>
```
Example: `P 1618706341 OPN 1615544 3.50 64 0.30`

**Timesync ("T"):** (OpenStint only)
```
T <timestamp> <type> <id> <transponder_timecode>
```

**Status ("S"):**
```
S <timestamp> <noise_power> <dc_offset> <frames_rx> <frames_ok>
```

See `docs/decoder-protocol.md` for full protocol specification.

### Thread Model

**Main thread:**
- Initialization, configuration, ZeroMQ publishing
- Periodic status reporting (1 Hz)
- Signal handling (Ctrl-C)

**SDR callback thread:**
- Runs in libhackrf/librtlsdr internal thread
- Calls `rx_callback` lambda with IQ samples
- All signal processing happens here (frame detection, decoding)
- No mutex needed - processing is single-threaded

**Critical:** Signal processing must be efficient to avoid buffer overruns. Target is <50% CPU on Raspberry Pi 3.

## Development Guidelines

### When Adding Features

**Maintain these constraints:**
- Must run on Raspberry Pi 3 (ARMv8, ~1.4 GHz, 1 GB RAM)
- Real-time processing requirement (5 MSPS sustained)
- No dropped samples/frames acceptable
- Simple, well-documented interfaces for 3rd-party integration

**Before modifying signal processing:**
- Profile on target hardware (Raspberry Pi)
- Measure impact on CPU usage
- Test with both HackRF and RTL-SDR backends
- Verify no regression in detection rates

### When Modifying SDR Backends

**If changing `sdr_device.hpp` interface:**
- Update both `sdr_hackrf.cpp` and `sdr_rtlsdr.cpp`
- Maintain backward compatibility or update all call sites
- Document any new configuration parameters

**If changing sample format:**
- Remember: pipeline expects signed int8 complex samples
- RTL-SDR conversion is in `sdr_rtlsdr.cpp:rx_callback_wrapper()`
- HackRF already provides signed int8 (no conversion needed)

**RTL-SDR Blog V4 special handling:**
- Uses built-in upconverter instead of direct sampling
- Offset tuning: tune to 4.75 MHz hardware, digitally mix to 5.0 MHz
- May need 2:1 upsampling if hardware rejects 5.0 MSPS
- IQ inversion flag (`-i`) available if spectrum is mirrored

### When Modifying Frame Detection

**Key parameters:**
- `SAMPLE_RATE = 5000000` - Do not change (transponders transmit at 5 MHz)
- `SYMBOL_RATE = 1250000` - BPSK symbol rate
- `SAMPLES_PER_SYMBOL = 4` - Oversampling ratio
- `detection_threshold` - Correlation threshold (default 0.70, RTL-SDR often needs 0.67)

**Preambles are defined in:**
- `transponder.hpp` - `bpsk_preamble` arrays
- `frame.hpp` - `Preamble<uint16_t>` template instantiations

**If changing threshold or correlation:**
- Test with both weak and strong signals
- Verify false positive rate remains low
- Check EVM values (< 0.40 needed for reliable decode)

### Testing Checklist

After making changes:

1. **Build test:** `cmake . && make` (no warnings)
2. **HackRF regression:** `./src/openstint` (verify existing functionality)
3. **RTL-SDR test:** `./src/openstint -r -g 60 -m` (verify compatibility)
4. **Transponder test:** Place transponder near antenna, verify "P" messages
5. **Long-duration test:** Run 1+ hour, check for memory leaks or stability issues
6. **CPU test on RPi3:** Monitor with `top`, should stay < 50%

## Common Tasks

### Adding a New SDR Backend

1. Create `src/sdr_newdevice.hpp` and `src/sdr_newdevice.cpp`
2. Implement `SdrDevice` interface
3. Add to `SdrBackend` enum in `sdr_device.hpp`
4. Update factory in `sdr_device.cpp`
5. Add CMake option in `src/CMakeLists.txt`
6. Handle sample format conversion if needed (must output signed int8)

### Tuning Detection Parameters

**For gain tuning:**
- Start with `-g 50` for RTL-SDR or default LNA/VGA for HackRF
- Enable monitor mode: `-m`
- Observe RSSI (want -40 to -60 dBm) and EVM (want < 0.30)
- Adjust gain up if RSSI too low, down if saturating

**For threshold tuning:**
- Default: 0.70 for HackRF, 0.67 for RTL-SDR
- Lower threshold = more sensitive but more false positives
- Higher threshold = less sensitive but cleaner
- Check "Max correlation" in debug output

**For antenna issues:**
- Must be parallel wires (NOT a loop)
- Spacing: 25-30 cm apart
- Termination: 330-470Ω resistor (NOT kΩ!)
- Balun: 1:9 HF balun recommended

### Debugging Signal Issues

**Enable monitor mode:** `-m`
- Shows frame detections, RSSI, EVM
- Displays correlation scores and DC offset
- Useful for diagnosing reception problems

**Check status messages:**
- `noise_power` - baseline RF noise level
- `dc_offset_magnitude` - should be < 10, reduce VGA if higher
- `frames_received` vs `frames_processed` - large gap indicates poor SNR

**Common issues:**
- No detections: Increase gain (`-g` or `-l`/`-v`)
- Too many false positives: Increase threshold (`-t`), decrease gain
- High EVM: Check antenna tuning, verify transponder LC circuit
- DC offset issues: Offset tuning enabled for RTL-SDR V4, adjust VGA for HackRF

## Key Constants and Frequencies

```cpp
CENTER_FREQ_HZ = 5000000      // 5 MHz carrier
SAMPLE_RATE = 5000000          // 5 MSPS
SYMBOL_RATE = 1250000          // 1.25 Msymbols/sec BPSK
BB_FILTER_BW = 1750000         // Baseband filter bandwidth
SAMPLES_PER_SYMBOL = 4         // Oversampling
DEFAULT_ZEROMQ_PORT = 5556     // ZeroMQ publisher port
```

## Integration Points

**ZeroMQ clients** should:
- Subscribe to port 5556 (or custom `-p` value)
- Parse space-separated text messages
- Handle "P", "T", "S" message types
- Use `decoder_timestamp` as monotonic counter (resets on decoder restart)
- See `integrations/subscriber.py` for minimal example
- See `integrations/laptimer.py` for working lap timer

**Custom transponders** should:
- Transmit on 5 MHz carrier
- Use 1.25 Msymbols/sec BPSK
- Include proper preamble (see `docs/transponder-protocol.md`)
- Tune for low EVM (< 0.30 typical)
- See https://github.com/zsellera/openstint-transponder for reference

## Dependencies

**Required:**
- CMake 3.27+
- C++20 compiler (GCC 10+, Clang 12+)
- liquid-dsp (signal processing)
- libfec (forward error correction)
- cppzmq (ZeroMQ C++ bindings)

**Optional (SDR backends):**
- libhackrf (HackRF One support)
- librtlsdr (RTL-SDR support)

**Platform-specific:**
- macOS: libfec must be compiled from source (https://github.com/fblomqvi/libfec)
- Linux: All available via apt-get
- Raspberry Pi: May need to blacklist dvb_usb_rtl28xxu kernel module

## Related Documentation

- `README.md` - Project overview, quickstart, contribution guidelines
- `QUICKSTART.md` - RTL-SDR setup guide and optimal V4 settings
- `RTL-SDR_IMPLEMENTATION.md` - Complete RTL-SDR implementation details
- `docs/decoder-protocol.md` - ZeroMQ message format specification
- `docs/transponder-protocol.md` - OpenStint transponder specification

## Project Philosophy

Per README.md contribution guidelines, changes should:
- Support small-scale clubs and friendly gatherings
- Run on Raspberry Pi 3 Model B+
- Maintain simple, well-documented interfaces
- Follow "does one thing and one thing well" philosophy
- Not pursue RC4 support (closed protocol, not aligned with project values)
