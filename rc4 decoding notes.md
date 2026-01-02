# RC4 Transponder Decoding Notes

## Key Findings

### RC4 Preamble
- RC4 transponders use preamble **`0x7916`** (NOT `0x51e4` which is Legacy/RC3)
- Source: RCHourglass project - "Better RC4 Hybrid detection (preamble detector was 0xF916, now it's 0x7916)"

### Protocol Characteristics
- Same 5 MHz carrier frequency as RC3
- Same 1.25 Msymbols/sec BPSK modulation
- Frame length: 320 bits (vs 80 bits for RC3)
- Data appears to be scrambled with per-transponder PN sequence

### Fingerprint Approach
Since the actual transponder ID is scrambled with an unknown PN sequence, we use a
fingerprint-based identification approach similar to RCHourglass "registration":

1. Each RC4 transponder produces unique raw softbit patterns
2. First 16 bits form a stable fingerprint
3. Transponders can be distinguished without full protocol decode

**Known Fingerprints:**
- T1 (ID 9218321): Pattern `0111110000000000...`
- T2 (ID 5852921): Pattern `1110101101000111...`

## Reference Projects
- RCHourglass: https://github.com/mv4wd/RCHourglass
- RCHourglass Wiki: https://github.com/mv4wd/RCHourglass/wiki
- Forum thread: https://www.rctech.net/forum/radio-electronics/1002584-rchourglass-diy-lap-timing-aka-cano-revised.html

## Implementation Status
- [x] RC4 preamble detection (`0x7916`)
- [x] Fingerprint extraction from raw softbits  
- [ ] Registration system for fingerprint → ID mapping
- [ ] Integration with PassingDetector for lap timing

## Files Modified
- `src/transponder.cpp` - Added RC4 preamble, fingerprint extraction
- `src/frame.hpp` - Added RC4 preamble matcher
- `src/frame.cpp` - Added RC4 detection logic
- `src/main.cpp` - Added RC4 debug output