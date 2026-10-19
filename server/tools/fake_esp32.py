"""Simula una maquina expendedora conectandose al servidor TCP.

Uso:
    python tools/fake_esp32.py --host 127.0.0.1 --port 8080 --items 42 3 99
"""

import argparse
import json
import socket


def send(sock: socket.socket, msg: dict) -> None:
    line = json.dumps(msg) + "\n"
    print(f"-> {line.strip()}")
    sock.sendall(line.encode())


def recv(f) -> dict:
    line = f.readline()
    if not line:
        raise ConnectionError("el servidor cerro la conexion")
    print(f"<- {line.strip()}")
    return json.loads(line)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--device-id", default="fake-vm")
    parser.add_argument("--items", type=int, nargs="+", default=[42])
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        f = sock.makefile("r")
        send(sock, {"type": "hello", "device_id": args.device_id})
        recv(f)
        send(sock, {"type": "ping"})
        recv(f)
        for req_id, item in enumerate(args.items, start=1):
            send(sock, {"type": "pay_req", "req_id": req_id, "item_id": item})
            recv(f)


if __name__ == "__main__":
    main()
