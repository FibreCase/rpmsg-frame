#!/usr/bin/env python3

import argparse
from multiprocessing import get_context
import socket
import struct
from typing import Iterable


DEFAULT_SOCKET_PATH = "/tmp/frf.sock"
DEFAULT_CMD = 0x0103
DEFAULT_DATA = bytes.fromhex("12 34 56 AA 55 AA 78 90")


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


def worker(
    index: int,
    barrier,
    results,
    socket_path: str,
    cmd: int,
    data: bytes,
) -> None:
    try:
        barrier.wait()
        status, errno_value = send_request(socket_path, cmd, data)
        results.put((index, status, errno_value, None))
    except BaseException as exc:
        results.put((index, None, None, f"{type(exc).__name__}: {exc}"))


def send_request_concurrently(
    socket_path: str,
    cmd: int,
    data: bytes,
    process_count: int,
) -> list[tuple[int, int | None, int | None, str | None]]:
    context = get_context("spawn")
    barrier = context.Barrier(process_count + 1)
    results = context.Queue()

    processes = []
    for index in range(process_count):
        process = context.Process(
            target=worker,
            args=(index, barrier, results, socket_path, cmd, data),
        )
        process.start()
        processes.append(process)

    barrier.wait()

    for process in processes:
        process.join()

    collected = [results.get() for _ in range(process_count)]
    collected.sort(key=lambda item: item[0])
    return collected


def format_bytes(data: Iterable[int]) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def main() -> int:
    parser = argparse.ArgumentParser(description="External RPMSG socket test client")
    parser.add_argument("--socket", default=DEFAULT_SOCKET_PATH, help="Unix socket path")
    parser.add_argument("--cmd", default=f"0x{DEFAULT_CMD:04X}", help="command id in hex")
    parser.add_argument("--data", type=parse_hex_bytes, default=DEFAULT_DATA, help="payload bytes in hex")
    parser.add_argument("--processes", type=int, default=5, help="number of concurrent client processes")
    args = parser.parse_args()

    cmd = int(args.cmd, 16) if isinstance(args.cmd, str) else int(args.cmd)
    if args.processes < 1:
        raise SystemExit("--processes must be at least 1")

    if args.processes == 1:
        results = [(0, *send_request(args.socket, cmd, args.data), None)]
    else:
        results = send_request_concurrently(args.socket, cmd, args.data, args.processes)

    print(f"socket: {args.socket}")
    print(f"cmd: 0x{cmd:04X}")
    print(f"data: {format_bytes(args.data)}")
    print(f"processes: {args.processes}")

    exit_code = 0
    for index, status, errno_value, error in results:
        if error is not None:
            print(f"[{index}] error: {error}")
            exit_code = 1
            continue

        print(f"[{index}] status: {status}")
        print(f"[{index}] errno: {errno_value}")
        if status != 0:
            exit_code = 1

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
