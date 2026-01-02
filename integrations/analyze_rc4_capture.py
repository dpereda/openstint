#!/usr/bin/env python3
"""
RC4 Transponder Data Analyzer

Analyzes captured RC4 payload data to help reverse-engineer the protocol.
"""

# RC4 payloads captured from the terminal output
# Format: (timestamp_approx, decoded_legacy_id, payload_hex)
captures = [
    (149082, 4008000,  "01 F4 BA 21 50 00 00 00 00 00 00 00 00 00 00 00 00 00"),
    (149297, 12870738, "4B AA 01 15 10 00 00 00 00 00 00 00 00 00 00 00 00 00"),
    (149595, 9732332,  "7A BD 5D 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00"),
    (149909, 2700628,  "97 CE 71 63 30 00 00 00 00 00 00 00 00 00 00 00 00 00"),
]

def hex_to_bytes(hex_str):
    return bytes.fromhex(hex_str.replace(" ", ""))

def bytes_to_bits(data):
    return ''.join(f'{b:08b}' for b in data)

def analyze_payload(payload_hex, legacy_id):
    data = hex_to_bytes(payload_hex)
    bits = bytes_to_bits(data)
    
    print(f"\n{'='*60}")
    print(f"Legacy decode ID: {legacy_id} (0x{legacy_id:06X})")
    print(f"Raw bytes: {payload_hex}")
    print(f"Binary (first 40 bits): {bits[:40]}")
    
    # Try to extract potential ID assuming various bit positions
    # RC4 transponder IDs are typically 7 digits (0-9999999)
    
    # Try direct interpretation of first 24 bits (3 bytes)
    id_24bit = int.from_bytes(data[:3], 'big')
    print(f"First 24 bits as ID (big-endian): {id_24bit}")
    
    id_24bit_le = int.from_bytes(data[:3], 'little')
    print(f"First 24 bits as ID (little-endian): {id_24bit_le}")
    
    # Try bit-reversed
    def reverse_bits_byte(b):
        return int(f'{b:08b}'[::-1], 2)
    
    reversed_bytes = bytes(reverse_bits_byte(b) for b in data[:3])
    id_reversed = int.from_bytes(reversed_bytes, 'big')
    print(f"First 24 bits reversed: {id_reversed}")
    
    # The legacy decoder extracts bits in chunks of 4, with every 4th being status
    # Let's try similar extraction
    msg_bits = bits[:32]
    tid_bits = ""
    status_bits = ""
    for i in range(32):
        if i % 4 != 0:
            tid_bits += msg_bits[31-i]  # reverse order
        else:
            status_bits += msg_bits[31-i]
    
    if len(tid_bits) >= 24:
        tid_extracted = int(tid_bits[:24], 2) if tid_bits[:24] else 0
        print(f"Legacy-style extraction: TID={tid_extracted}, Status=0b{status_bits}")

print("RC4 Transponder Payload Analysis")
print("="*60)

for ts, legacy_id, payload in captures:
    analyze_payload(payload, legacy_id)

# Look for common patterns
print("\n" + "="*60)
print("PATTERN ANALYSIS")
print("="*60)

print("\nByte-by-byte comparison of all captures:")
for i in range(5):  # First 5 bytes
    values = [hex_to_bytes(c[2])[i] for c in captures]
    print(f"Byte {i}: {[f'0x{v:02X}' for v in values]}")

# Group by potential transponder (if same transponder, payloads should have similarities)
print("\n\nNote: If two captures are from the SAME transponder, they should")
print("have identical ID portions but possibly different timestamp/status bytes.")
print("\nPlease tell me the actual IDs of your two RC4 transponders")
print("(usually printed on a label) so we can correlate the data!")
