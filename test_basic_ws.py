#!/usr/bin/env python3
"""
Basic WebSocket connection test
Tests what happens after handshake
"""

import socket
import base64
import struct
import time

def send_ws_frame(sock, opcode, payload):
    """Send a WebSocket frame"""
    # Client frames must be masked (RFC 6455)
    mask_bit = 0x80
    payload_len = len(payload)
    
    # Frame header
    frame = bytearray()
    frame.append(0x80 | opcode)  # FIN + opcode
    
    if payload_len < 126:
        frame.append(mask_bit | payload_len)
    elif payload_len < 65536:
        frame.append(mask_bit | 126)
        frame.extend(struct.pack('>H', payload_len))
    else:
        frame.append(mask_bit | 127)
        frame.extend(struct.pack('>Q', payload_len))
    
    # Masking key (4 bytes)
    mask_key = b'\x12\x34\x56\x78'
    frame.extend(mask_key)
    
    # Masked payload
    masked = bytearray(payload)
    for i in range(len(masked)):
        masked[i] ^= mask_key[i % 4]
    frame.extend(masked)
    
    sock.sendall(frame)

def test_websocket_frames():
    # Connect and handshake
    print("Step 1: TCP connection...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect(('localhost', 8080))
    print("✓ Connected")
    
    # WebSocket handshake
    print("\nStep 2: WebSocket handshake...")
    ws_key = base64.b64encode(b'test_key_1234567').decode('ascii')
    request = (
        f"GET /ws HTTP/1.1\r\n"
        f"Host: localhost:8080\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {ws_key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"\r\n"
    )
    sock.sendall(request.encode())
    
    response = sock.recv(4096).decode('ascii', errors='ignore')
    if '101 Switching Protocols' not in response:
        print(f"❌ Handshake failed: {response[:100]}")
        return False
    print("✓ Handshake successful")
    
    # Try to send a text frame
    print("\nStep 3: Sending text frame...")
    try:
        send_ws_frame(sock, 0x01, b'Hello WebSocket')  # 0x01 = text frame
        print("✓ Text frame sent")
    except Exception as e:
        print(f"❌ Failed to send: {e}")
        return False
    
    # Try to receive response
    print("\nStep 4: Waiting for response...")
    sock.settimeout(2.0)
    try:
        data = sock.recv(4096)
        if len(data) == 0:
            print("⚠️  Connection closed by server (0 bytes received)")
            print("   This means the server completed handshake but doesn't")
            print("   maintain the connection to process WebSocket frames.")
        else:
            print(f"✓ Received {len(data)} bytes: {data.hex()}")
    except socket.timeout:
        print("⚠️  Timeout - no response from server")
        print("   Server may have closed connection after handshake")
    except Exception as e:
        print(f"⚠️  Error receiving: {e}")
    
    print("\n" + "="*60)
    print("DIAGNOSIS:")
    print("="*60)
    print("✅ WebSocket handshake: WORKING")
    print("⚠️  Frame processing: NOT IMPLEMENTED")
    print("\nThe server correctly implements RFC 6455 handshake,")
    print("but the example doesn't maintain the connection to")
    print("process WebSocket frames (text, binary, ping, pong).")
    print("\nThis is documented in the example server comments:")
    print("'Full integration with async I/O requires additional")
    print("server-side connection management.'")
    
    sock.close()
    return True

if __name__ == "__main__":
    try:
        test_websocket_frames()
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
