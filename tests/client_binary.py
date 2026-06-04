#!/usr/bin/env python3
"""
Binary protocol broadcast chat test client.

Connects to the gameserver on port 9999 using the custom binary protocol,
sends chat messages, and prints broadcast responses from all connected clients.

Usage:
    python3 client_binary.py [host] [port] [num_clients] [messages]
"""

import socket
import struct
import threading
import time
import sys
import binascii

PROTOCOL_VERSION = 1
MSG_TYPE_CHAT = 800
MSG_TYPE_HEARTBEAT = 100
MSG_TYPE_PROTOCOL_NEGOTIATION = 2
MSG_TYPE_SUCCESS = 3

HEADER_SIZE = 20  # 1+1+2+4+4+4+4


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def encode_string(s: str) -> bytes:
    encoded = s.encode("utf-8")
    return struct.pack("!H", len(encoded)) + encoded


def decode_string(data: bytes, offset: int) -> tuple:
    length = struct.unpack_from("!H", data, offset)[0]
    offset += 2
    s = data[offset:offset + length].decode("utf-8")
    return s, offset + length


def build_header(msg_type: int, seq: int, payload_len: int, checksum: int) -> bytes:
    return struct.pack(
        "!BBHIII I",
        PROTOCOL_VERSION,  # version
        0,                 # flags
        msg_type,          # message_type
        seq,               # sequence
        int(time.time() * 1000) & 0xFFFFFFFF,  # timestamp
        payload_len,       # length
        checksum,          # checksum
    )


def build_chat_message(sender: str, message: str, seq: int) -> bytes:
    body = encode_string(sender) + encode_string(message) + struct.pack("!Q", int(time.time() * 1000))
    checksum = crc32(body)
    header = build_header(MSG_TYPE_CHAT, seq, len(body), checksum)
    return header + body


def build_protocol_negotiation(seq: int) -> bytes:
    body = b"\x01"  # version=1
    body += b"\x01"  # supports_compression=true
    body += b"\x00"  # supports_encryption=false
    body += struct.pack("!I", 10 * 1024 * 1024)  # max_message_size
    body += struct.pack("!H", 10)  # supported message types count
    for i in range(1, 11):
        body += struct.pack("!H", i * 100)
    checksum = crc32(body)
    header = build_header(MSG_TYPE_PROTOCOL_NEGOTIATION, seq, len(body), checksum)
    return header + body


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Connection closed")
        buf += chunk
    return buf


def recv_message(sock: socket.socket) -> tuple:
    raw_len = recv_exact(sock, 4)
    msg_len = struct.unpack("!I", raw_len)[0]
    if msg_len == 0 or msg_len > 10 * 1024 * 1024:
        return None, None
    data = recv_exact(sock, msg_len)
    if len(data) < HEADER_SIZE:
        return None, None
    version = data[0]
    flags = data[1]
    msg_type = struct.unpack_from("!H", data, 2)[0]
    seq = struct.unpack_from("!I", data, 4)[0]
    ts = struct.unpack_from("!I", data, 8)[0]
    length = struct.unpack_from("!I", data, 12)[0]
    checksum = struct.unpack_from("!I", data, 16)[0]
    body = data[HEADER_SIZE:]
    return msg_type, body


def client_worker(host: str, port: int, client_id: int, messages: list, ready_event: threading.Event, all_ready: threading.Event):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.connect((host, port))
        print(f"[Client {client_id}] Connected to {host}:{port}")

        seq = 1

        # Protocol negotiation
        neg = build_protocol_negotiation(seq)
        sock.sendall(struct.pack("!I", len(neg)) + neg)
        seq += 1

        # Read negotiation response
        try:
            msg_type, body = recv_message(sock)
            if msg_type is not None:
                print(f"[Client {client_id}] Protocol negotiation response: type={msg_type}")
        except Exception:
            pass

        ready_event.set()
        all_ready.wait()

        time.sleep(0.1 * client_id)

        for msg_text in messages:
            full_msg = f"Client{client_id}: {msg_text}"
            packet = build_chat_message(f"User{client_id}", full_msg, seq)
            sock.sendall(struct.pack("!I", len(packet)) + packet)
            print(f"[Client {client_id}] Sent: {full_msg}")
            seq += 1
            time.sleep(0.3)

        print(f"[Client {client_id}] All messages sent, listening for broadcasts...")
        while True:
            try:
                msg_type, body = recv_message(sock)
                if msg_type is None:
                    break
                if msg_type == MSG_TYPE_CHAT:
                    sender, off = decode_string(body, 0)
                    message, off = decode_string(body, off)
                    print(f"[Client {client_id}] Broadcast received: <{sender}> {message}")
                elif msg_type == MSG_TYPE_HEARTBEAT:
                    pass
            except (ConnectionError, OSError):
                break

    except Exception as e:
        print(f"[Client {client_id}] Error: {e}")
    finally:
        try:
            sock.close()
        except Exception:
            pass
        print(f"[Client {client_id}] Disconnected")


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9999
    num_clients = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    num_messages = int(sys.argv[4]) if len(sys.argv) > 4 else 2

    all_messages = [f"Hello from everyone! Round {r+1}" for r in range(num_messages)]

    print(f"Binary chat test: {num_clients} clients, {num_messages} messages each")
    print(f"Connecting to {host}:{port}")
    print("-" * 60)

    ready_events = [threading.Event() for _ in range(num_clients)]
    all_ready = threading.Event()

    threads = []
    for i in range(num_clients):
        t = threading.Thread(
            target=client_worker,
            args=(host, port, i, all_messages, ready_events[i], all_ready),
            daemon=True,
        )
        t.start()
        threads.append(t)

    for e in ready_events:
        e.wait(timeout=5)

    print("-" * 60)
    print("All clients connected. Starting broadcast test...")
    print("-" * 60)
    all_ready.set()

    try:
        time.sleep(3 + num_messages * 0.5)
    except KeyboardInterrupt:
        pass

    print("-" * 60)
    print("Test complete.")


if __name__ == "__main__":
    main()
