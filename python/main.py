#!/usr/bin/env python3

import argparse
import socket
import struct
from typing import Iterable


DEFAULT_SOCKET_PATH = "/tmp/frf.sock"
DEFAULT_CMD = 0x0102
DEFAULT_DATA = bytes.fromhex("DE AD BE EF")


def parse_hex_bytes(value: str) -> bytes:
    cleaned = value.replace(" ", "").replace("0x", "").replace(",", "")
    if len(cleaned) % 2 != 0:
        raise argparse.ArgumentTypeError("hex data must contain an even number of digits")
    try:
        return bytes.fromhex(cleaned)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid hex data: {value}") from exc


def build_request(cmd: int, data: bytes) -> bytes:
    if not 0 <= cmd <= 0xFFFF:
        raise ValueError("cmd must fit in uint16")
    if len(data) > 255:
        raise ValueError("data length must be <= 255")
    return struct.pack("!HB", cmd, len(data)) + data


def read_exact(sock: socket.socket, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        chunk = sock.recv(length - len(chunks))
        if not chunk:
            raise RuntimeError("socket closed before full response was received")
        chunks.extend(chunk)
    return bytes(chunks)


def send_request(socket_path: str, cmd: int, data: bytes) -> tuple[int, int]:
    request = build_request(cmd, data)
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
        client.connect(socket_path)
        client.sendall(request)
        response = read_exact(client, 5)

    status = response[0]
    errno_value = struct.unpack("!i", response[1:])[0]
    return status, errno_value


def format_bytes(data: Iterable[int]) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def main() -> int:
    parser = argparse.ArgumentParser(description="External RPMSG socket test client")
    parser.add_argument("--socket", default=DEFAULT_SOCKET_PATH, help="Unix socket path")
    parser.add_argument("--cmd", default=f"0x{DEFAULT_CMD:04X}", help="command id in hex")
    parser.add_argument("--data", type=parse_hex_bytes, default=DEFAULT_DATA, help="payload bytes in hex")
    args = parser.parse_args()

    cmd = int(args.cmd, 16) if isinstance(args.cmd, str) else int(args.cmd)
    status, errno_value = send_request(args.socket, cmd, args.data)

    print(f"socket: {args.socket}")
    print(f"cmd: 0x{cmd:04X}")
    print(f"data: {format_bytes(args.data)}")
    print(f"status: {status}")
    print(f"errno: {errno_value}")

    return 0 if status == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
