#!/usr/bin/env python3
"""
WebSocket protocol broadcast chat test client.

Connects to the gameserver on port 8080 using WebSocket,
sends chat messages as JSON, and prints broadcast responses.

Usage:
    python3 client_websocket.py [host] [port] [num_clients] [messages]
"""

import asyncio
import json
import sys
import time

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' package required. Install with:")
    print("  pip install websockets")
    sys.exit(1)


async def client_worker(host: str, port: int, client_id: int, messages: list, ready_event: asyncio.Event, all_ready: asyncio.Event):
    uri = f"ws://{host}:{port}/game"
    try:
        async with websockets.connect(uri) as ws:
            print(f"[Client {client_id}] Connected to {uri}")

            ready_event.set()
            await all_ready.wait()

            await asyncio.sleep(0.1 * client_id)

            for msg_text in messages:
                full_msg = f"Client{client_id}: {msg_text}"
                chat_msg = {
                    "msg": "chat_message",
                    "sender": f"User{client_id}",
                    "message": full_msg,
                    "timestamp": int(time.time() * 1000),
                }
                await ws.send(json.dumps(chat_msg))
                print(f"[Client {client_id}] Sent: {full_msg}")
                await asyncio.sleep(0.3)

            print(f"[Client {client_id}] All messages sent, listening for broadcasts...")

            async for raw in ws:
                try:
                    data = json.loads(raw)
                    msg_type = data.get("msg", "")
                    if msg_type == "chat_message":
                        sender = data.get("sender", "?")
                        message = data.get("message", "?")
                        print(f"[Client {client_id}] Broadcast received: <{sender}> {message}")
                    elif msg_type == "authentication":
                        print(f"[Client {client_id}] Auth response: {data.get('desc', '')}")
                except json.JSONDecodeError:
                    pass

    except websockets.exceptions.ConnectionClosed:
        print(f"[Client {client_id}] Connection closed")
    except Exception as e:
        print(f"[Client {client_id}] Error: {e}")
    finally:
        print(f"[Client {client_id}] Disconnected")


async def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
    num_clients = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    num_messages = int(sys.argv[4]) if len(sys.argv) > 4 else 2

    all_messages = [f"Hello from everyone! Round {r+1}" for r in range(num_messages)]

    print(f"WebSocket chat test: {num_clients} clients, {num_messages} messages each")
    print(f"Connecting to ws://{host}:{port}/game")
    print("-" * 60)

    ready_events = [asyncio.Event() for _ in range(num_clients)]
    all_ready = asyncio.Event()

    tasks = [
        asyncio.create_task(
            client_worker(host, port, i, all_messages, ready_events[i], all_ready)
        )
        for i in range(num_clients)
    ]

    for e in ready_events:
        try:
            await asyncio.wait_for(e.wait(), timeout=5)
        except asyncio.TimeoutError:
            print("WARNING: Not all clients connected in time")

    print("-" * 60)
    print("All clients connected. Starting broadcast test...")
    print("-" * 60)
    all_ready.set()

    try:
        await asyncio.sleep(3 + num_messages * 0.5)
    except KeyboardInterrupt:
        pass

    for t in tasks:
        t.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)

    print("-" * 60)
    print("Test complete.")


if __name__ == "__main__":
    asyncio.run(main())
