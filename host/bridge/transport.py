from __future__ import annotations
import base64, hashlib, os, socket, struct
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MAX_FRAME_BYTES = 1_048_576

class LocalWebSocket:
    """Small RFC 6455 client for this loopback-only bridge; no external packages or TLS."""

    def __init__(self, sock: socket.socket, buffered: bytes = b"") -> None:
        self.sock = sock
        self.buffer = bytearray(buffered)
        self.fragments = bytearray()
        self.fragment_opcode = 0
        self.closed = False
        self.sock.setblocking(False)

    @classmethod
    def connect(cls, host: str, port: int) -> "LocalWebSocket":
        sock = socket.create_connection((host, port), timeout=4.0)
        sock.settimeout(4.0)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            "GET / HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Origin: http://127.0.0.1\r\n"
            "User-Agent: ETB-Minecraft-Bridge/0.1\r\n\r\n"
        ).encode("ascii")
        sock.sendall(request)

        response = bytearray()
        while b"\r\n\r\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                raise ConnectionError("Minecraft closed the WebSocket handshake")
            response.extend(chunk)
            if len(response) > 16_384:
                raise ConnectionError("Oversized WebSocket handshake")

        end = response.index(b"\r\n\r\n") + 4
        header = bytes(response[:end]).decode("latin-1")
        remainder = bytes(response[end:])
        lines = header.split("\r\n")
        if not lines or " 101 " not in f" {lines[0]} ":
            raise ConnectionError(f"Unexpected WebSocket response: {lines[0] if lines else '<empty>'}")
        headers: dict[str, str] = {}
        for line in lines[1:]:
            if ":" in line:
                name, value = line.split(":", 1)
                headers[name.strip().casefold()] = value.strip()
        expected = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
        if headers.get("sec-websocket-accept") != expected:
            raise ConnectionError("Minecraft WebSocket accept hash did not match")
        return cls(sock, remainder)

    def send_frame(self, opcode: int, payload: bytes) -> None:
        mask = os.urandom(4)
        length = len(payload)
        first = bytes((0x80 | opcode,))
        if length < 126:
            header = first + bytes((0x80 | length,))
        elif length < 65_536:
            header = first + bytes((0x80 | 126,)) + struct.pack("!H", length)
        else:
            header = first + bytes((0x80 | 127,)) + struct.pack("!Q", length)
        masked = bytes(value ^ mask[index & 3] for index, value in enumerate(payload))
        self.sock.setblocking(True)
        self.sock.settimeout(2.0)
        self.sock.sendall(header + mask + masked)
        self.sock.setblocking(False)

    def send_text(self, message: str) -> None:
        self.send_frame(0x1, message.encode("utf-8"))

    def poll_text(self) -> list[str]:
        while True:
            try:
                chunk = self.sock.recv(65_536)
            except BlockingIOError:
                break
            if not chunk:
                raise ConnectionError("Minecraft closed the WebSocket")
            self.buffer.extend(chunk)

        messages: list[str] = []
        while len(self.buffer) >= 2:
            first, second = self.buffer[0], self.buffer[1]
            final = bool(first & 0x80)
            opcode = first & 0x0F
            masked = bool(second & 0x80)
            length = second & 0x7F
            offset = 2

            if length == 126:
                if len(self.buffer) < offset + 2:
                    break
                length = struct.unpack_from("!H", self.buffer, offset)[0]
                offset += 2
            elif length == 127:
                if len(self.buffer) < offset + 8:
                    break
                length = struct.unpack_from("!Q", self.buffer, offset)[0]
                offset += 8
            if length > MAX_FRAME_BYTES:
                raise ConnectionError(f"Rejected oversized WebSocket frame ({length} bytes)")

            mask = b""
            if masked:
                if len(self.buffer) < offset + 4:
                    break
                mask = bytes(self.buffer[offset : offset + 4])
                offset += 4
            if len(self.buffer) < offset + length:
                break

            payload = bytes(self.buffer[offset : offset + length])
            del self.buffer[: offset + length]
            if masked:
                payload = bytes(value ^ mask[index & 3] for index, value in enumerate(payload))

            if opcode == 0x8:
                self.closed = True
                raise ConnectionError("Minecraft sent a WebSocket close frame")
            if opcode == 0x9:
                self.send_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            if opcode == 0x1:
                if final:
                    messages.append(payload.decode("utf-8", errors="replace"))
                else:
                    self.fragments = bytearray(payload)
                    self.fragment_opcode = opcode
                continue
            if opcode == 0x0 and self.fragment_opcode == 0x1:
                self.fragments.extend(payload)
                if len(self.fragments) > MAX_FRAME_BYTES:
                    raise ConnectionError("Fragmented WebSocket message too large")
                if final:
                    messages.append(self.fragments.decode("utf-8", errors="replace"))
                    self.fragments.clear()
                    self.fragment_opcode = 0
        return messages

    def close(self) -> None:
        if self.closed:
            self.sock.close()
            return
        try:
            self.send_frame(0x8, b"")
        except OSError:
            pass
        self.closed = True
        self.sock.close()
