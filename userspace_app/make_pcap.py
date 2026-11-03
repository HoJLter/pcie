#!/usr/bin/env python3
import struct

def ip_checksum(data: bytes) -> int:
    s = 0
    for i in range(0, len(data), 2):
        s += (data[i] << 8) | (data[i+1] if i+1 < len(data) else 0)
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF

# --- payload ---
payload = b"hello from FPGA!"
udp_len  = 8 + len(payload)
ip_total = 20 + udp_len

# --- IPv4 header ---
src_ip = bytes([10, 0, 0, 1])
dst_ip = bytes([10, 0, 0, 2])
ip_hdr = struct.pack('!BBHHHBBH4s4s',
                     0x45, 0, ip_total, 0, 0x4000, 64, 17, 0, src_ip, dst_ip)
ip_hdr = ip_hdr[:10] + struct.pack('!H', ip_checksum(ip_hdr)) + ip_hdr[12:]

# --- UDP header (checksum 0 — допустимо для IPv4) ---
udp_hdr = struct.pack('!HHHH', 12345, 54321, udp_len, 0)

# --- Ethernet ---
eth = struct.pack('!6s6sH',
                  bytes([0x02,0,0,0,0,1]),
                  bytes([0x02,0,0,0,0,2]),
                  0x0800)
frame = eth + ip_hdr + udp_hdr + payload

# --- PCAP ---
with open('data/traffic.pcap', 'wb') as f:
    # global header (LE)
    f.write(struct.pack('<IHHIIII', 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
    # packet header
    f.write(struct.pack('<IIII', 1, 0, len(frame), len(frame)))
    f.write(frame)

print(f"OK: 1 packet, frame={len(frame)} bytes -> data/traffic.pcap")