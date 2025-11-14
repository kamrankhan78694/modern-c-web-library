#!/usr/bin/env python3
"""
WebSocket handshake test
Tests that the server properly performs RFC 6455 WebSocket handshake
"""

import socket
import base64
import hashlib

def test_websocket_handshake():
    # Connect to server
    print("Connecting to localhost:8080...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect(('localhost', 8080))
    print("✓ TCP connection established")
    
    # Generate WebSocket key
    ws_key = base64.b64encode(b'test_key_1234567').decode('ascii')
    print(f"\nWebSocket-Key: {ws_key}")
    
    # Send WebSocket upgrade request
    request = (
        f"GET /ws HTTP/1.1\r\n"
        f"Host: localhost:8080\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {ws_key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"\r\n"
    )
    
    print("\nSending handshake request:")
    print(request.replace('\r\n', '\\r\\n\n'))
    
    sock.sendall(request.encode())
    
    # Receive response
    response = sock.recv(4096).decode('ascii')
    print("Received response:")
    print(response.replace('\r\n', '\\r\\n\n'))
    
    # Parse response
    lines = response.split('\r\n')
    status_line = lines[0]
    
    # Check status
    if '101' in status_line and 'Switching Protocols' in status_line:
        print("\n✓ Status: 101 Switching Protocols")
    else:
        print(f"\n❌ Invalid status: {status_line}")
        sock.close()
        return False
    
    # Check headers
    headers = {}
    for line in lines[1:]:
        if ':' in line:
            key, value = line.split(':', 1)
            headers[key.strip().lower()] = value.strip()
    
    # Verify required headers
    if headers.get('upgrade', '').lower() != 'websocket':
        print("❌ Missing or invalid Upgrade header")
        sock.close()
        return False
    print("✓ Upgrade: websocket")
    
    if headers.get('connection', '').lower() != 'upgrade':
        print("❌ Missing or invalid Connection header")
        sock.close()
        return False
    print("✓ Connection: Upgrade")
    
    # Verify Sec-WebSocket-Accept
    if 'sec-websocket-accept' in headers:
        expected_accept = base64.b64encode(
            hashlib.sha1(
                (ws_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()
            ).digest()
        ).decode('ascii')
        
        actual_accept = headers['sec-websocket-accept']
        if actual_accept == expected_accept:
            print(f"✓ Sec-WebSocket-Accept: {actual_accept} (valid)")
        else:
            print(f"❌ Invalid Sec-WebSocket-Accept")
            print(f"   Expected: {expected_accept}")
            print(f"   Actual:   {actual_accept}")
            sock.close()
            return False
    else:
        print("❌ Missing Sec-WebSocket-Accept header")
        sock.close()
        return False
    
    print("\n✅ WebSocket handshake successful!")
    print("   The server correctly implements RFC 6455 WebSocket handshake")
    
    sock.close()
    return True

if __name__ == "__main__":
    try:
        success = test_websocket_handshake()
        exit(0 if success else 1)
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
        exit(1)
