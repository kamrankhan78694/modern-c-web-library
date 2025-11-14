#!/usr/bin/env python3
"""
WebSocket ping test client
Tests the server's ping/pong functionality
"""

import asyncio
import websockets
import sys

async def test_ping():
    uri = "ws://localhost:8080/ws"
    
    try:
        print("Connecting to WebSocket server...")
        async with websockets.connect(uri) as websocket:
            print("✓ Connected successfully")
            
            # Test 1: Send a text message
            print("\n[Test 1] Sending text message...")
            await websocket.send("Hello, WebSocket!")
            response = await websocket.recv()
            print(f"✓ Received echo: {response}")
            
            # Test 2: Test ping/pong
            print("\n[Test 2] Testing ping/pong...")
            pong_waiter = await websocket.ping()
            latency = await pong_waiter
            print(f"✓ Pong received! Latency: {latency:.3f}s")
            
            # Test 3: Send another message
            print("\n[Test 3] Sending another message...")
            await websocket.send("Testing after ping")
            response = await websocket.recv()
            print(f"✓ Received echo: {response}")
            
            # Test 4: Multiple pings
            print("\n[Test 4] Sending multiple pings...")
            for i in range(3):
                pong_waiter = await websocket.ping()
                latency = await pong_waiter
                print(f"  Ping {i+1}: {latency:.3f}s")
            print("✓ All pongs received!")
            
            print("\n✅ All tests passed!")
            
    except websockets.exceptions.ConnectionClosed as e:
        print(f"❌ Connection closed: {e}")
        return 1
    except Exception as e:
        print(f"❌ Error: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    exit_code = asyncio.run(test_ping())
    sys.exit(exit_code)
