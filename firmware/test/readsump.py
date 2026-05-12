#!/usr/bin/env python3
"""
SUMP protocol initiator.
Sends the standard init sequence to /dev/ttyUSB1, waits for responses,
then arms the trigger with 0x01. Prints all received bytes as hex.
"""

import serial
import time

PORT     = "/dev/ttyUSB1"
BAUD     = 115200          # standard SUMP default; change if your device differs
TIMEOUT  = 2.0             # seconds to wait for each response
ARM_DELAY = 1.0            # seconds to wait before sending ARM (0x01)


def read_available(ser, timeout=TIMEOUT):
    """Read all available bytes within `timeout` seconds."""
    ser.timeout = timeout
    data = b""
    while True:
        chunk = ser.read(256)
        if not chunk:
            break
        data += chunk
    return data


def print_hex(label, data):
    if data:
        hex_str = " ".join(f"0x{b:02X}" for b in data)
        print(f"[{label}] {hex_str}")
    else:
        print(f"[{label}] <no response>")


def main():
    print(f"Opening {PORT} at {BAUD} baud...")
    with serial.Serial(PORT, BAUD, bytesize=8, parity='N', stopbits=1) as ser:
        ser.flushInput()
        ser.flushOutput()

        # --- Step 1: RESET x5 (0x00) ---
        print("\n>> Sending RESET x5 (0x00 × 5)")
        ser.write(bytes([0x00, 0x00, 0x00, 0x00, 0x00]))
        time.sleep(0.1)
        resp = read_available(ser)
        print_hex("RESET response", resp)

        # --- Step 2: ID (0x02) — expect 4 bytes e.g. "SLA0" or "1ALS" ---
        print("\n>> Sending ID (0x02)")
        ser.write(bytes([0x02]))
        resp = read_available(ser)
        print_hex("ID response", resp)
        if resp:
            try:
                print(f"   Decoded: {resp.decode('ascii', errors='replace')!r}")
            except Exception:
                pass

        # --- Step 3: GET METADATA (0x04) ---
        print("\n>> Sending GET METADATA (0x04)")
        ser.write(bytes([0x04]))
        resp = read_available(ser)
        print_hex("METADATA response", resp)

        # --- Delay before arming ---
        print(f"\n   Waiting {ARM_DELAY}s before arming...")
        time.sleep(ARM_DELAY)

        # --- Step 4: ARM / RUN (0x01) ---
        print("\n>> Sending ARM (0x01)")
        ser.write(bytes([0x01]))

        # Stream everything back to console
        print("\n-- Capture output (Ctrl+C to stop) --")
        ser.timeout = 1.0
        try:
            while True:
                chunk = ser.read(256)
                if chunk:
                    hex_str = " ".join(f"0x{b:02X}" for b in chunk)
                    print(hex_str)
        except KeyboardInterrupt:
            print("\n-- Stopped by user --")


if __name__ == "__main__":
    main()
