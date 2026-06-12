#!/usr/bin/env python3
"""On-demand screenshot grabber for the Smart Buddy LVGL debug screenshot server.

Usage:
    python tools/screenshot/screenshot.py <device-ip> [--port 3333] [--out tools/screenshot/screenshots/latest.png]

Connects to the firmware's debug_screenshot TCP server, requests a snapshot
of the current LVGL screen, and saves it as a PNG for inspection.
"""
import argparse
import os
import socket
import struct
import sys

import numpy as np
from PIL import Image

HEADER_FMT = "<HHHHI"  # w, h, stride, reserved, data_len
HEADER_SIZE = struct.calcsize(HEADER_FMT)


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("connection closed before receiving all data")
        buf.extend(chunk)
    return bytes(buf)


def rgb565_to_rgb888(data: bytes, width: int, height: int, stride: int) -> np.ndarray:
    pixels = np.frombuffer(data, dtype="<u2")
    pixels = pixels.reshape(height, stride // 2)[:, :width]

    r = ((pixels >> 11) & 0x1F).astype(np.uint8)
    g = ((pixels >> 5) & 0x3F).astype(np.uint8)
    b = (pixels & 0x1F).astype(np.uint8)

    r = (r << 3) | (r >> 2)
    g = (g << 2) | (g >> 4)
    b = (b << 3) | (b >> 2)

    return np.dstack((r, g, b))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device_ip", help="IP address of the Smart Buddy device")
    parser.add_argument("--port", type=int, default=3333, help="screenshot server TCP port (default 3333)")
    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "screenshots", "latest.png")
    parser.add_argument("--out", default=default_out, help="output PNG path")
    parser.add_argument("--timeout", type=float, default=5.0, help="socket timeout in seconds")
    args = parser.parse_args()

    with socket.create_connection((args.device_ip, args.port), timeout=args.timeout) as sock:
        sock.settimeout(args.timeout)
        sock.sendall(b"S")

        header = recv_exact(sock, HEADER_SIZE)
        w, h, stride, _reserved, data_len = struct.unpack(HEADER_FMT, header)
        pixel_data = recv_exact(sock, data_len)

    rgb = rgb565_to_rgb888(pixel_data, w, h, stride)
    image = Image.fromarray(rgb, "RGB")

    out_path = args.out
    out_dir = os.path.dirname(out_path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    image.save(out_path)

    print(f"saved {w}x{h} screenshot to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
