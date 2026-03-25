#!/usr/bin/env python3
"""
PDB2026 UART Logger
===============
Reads telemetry from the STM32 power distribution board over UART
and logs it to a CSV file with timestamps.

Format (received): "V:24.3,I:12.1,P:295.5\r\n"

Usage:
    pip install pyserial
    python3 pdb_logger.py

    # Specify port and output file:
    python3 pdb_logger.py --port /dev/tty.usbserial-XXXX --out log.csv
"""

import serial
import csv
import time
import argparse
import sys
from datetime import datetime


def find_serial_port():
    """Try to auto-detect the STM32 serial port on macOS/Linux."""
    import glob
    candidates = (
        glob.glob("/dev/tty.usbserial-*") +
        glob.glob("/dev/tty.usbmodem*") +
        glob.glob("/dev/ttyUSB*") +
        glob.glob("/dev/ttyACM*")
    )
    return candidates[0] if candidates else None


def parse_line(line: str):
    """
    Parses a telemetry line like "V:24.3,I:12.1,P:295.5"
    Returns (voltage, current, power) as floats, or None on error.
    """
    try:
        parts = {}
        for token in line.strip().split(","):
            key, val = token.split(":")
            parts[key.strip()] = float(val.strip())
        return parts.get("V"), parts.get("I"), parts.get("P")
    except Exception:
        return None


def main():
    parser = argparse.ArgumentParser(description="PDB UART Telemetry Logger")
    parser.add_argument("--port", help="Serial port (e.g. /dev/tty.usbserial-XXXX)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    parser.add_argument("--out", default="pdb_log.csv", help="Output CSV file (default pdb_log.csv)")
    parser.add_argument("--print", dest="print_live", action="store_true",
                        help="Print live readings to terminal")
    args = parser.parse_args()

    # Auto-detect port if not specified
    port = args.port or find_serial_port()
    if not port:
        print("ERROR: Could not find a serial port. Connect the STM32 and specify --port")
        print("  Example: python3 pdb_logger.py --port /dev/tty.usbserial-1420")
        sys.exit(1)

    print(f"Connecting to {port} at {args.baud} baud...")
    print(f"Logging to: {args.out}")
    print("Press Ctrl+C to quit/stop process.\n")

    try:
        ser = serial.Serial(port, args.baud, timeout=1.0)
    except serial.SerialException as e:
        print(f"ERROR opening port: {e}")
        sys.exit(1)

    with open(args.out, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["timestamp", "elapsed_s", "voltage_V", "current_A", "power_W"])

        start_time = time.time()
        sample_count = 0
        error_count = 0

        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue

                try:
                    line = raw.decode("ascii", errors="ignore").strip()
                except Exception:
                    continue

                result = parse_line(line)
                if result is None or any(v is None for v in result):
                    error_count += 1
                    continue

                voltage, current, power = result
                now = datetime.now().isoformat(timespec="milliseconds")
                elapsed = round(time.time() - start_time, 3)

                writer.writerow([now, elapsed, voltage, current, power])
                csvfile.flush()  # Write immediately

                sample_count += 1

                if args.print_live or sample_count % 10 == 0:
                    print(f"[{now}]  V: {voltage:6.2f} V  |  I: {current:6.2f} A  |  P: {power:7.2f} W")

        except KeyboardInterrupt:
            print(f"\nStopped. {sample_count} samples logged to {args.out}")
            if error_count:
                print(f"  ({error_count} malformed lines skipped)")
        finally:
            ser.close()


if __name__ == "__main__":
    main()
    
# PLEASE DON'T BLOW UP PLEASE PLEASE PLEASE
