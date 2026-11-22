"""Run against real loopback sockets. Executables are built by run_world_session.cmd."""
import contextlib
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path


HEARTBEAT, JOIN, READY, PUBLISH, STATE, ACK = 6, 32, 33, 34, 35, 36
HEADER = struct.Struct("!IH")
NUMBER = struct.Struct("!I")
POSE = struct.Struct("!Q6f")


def frame(kind, body=b""):
    return HEADER.pack(len(body), kind) + body


def receive_bytes(peer, count):
    data = bytearray()
    while len(data) < count:
        part = peer.recv(count - len(data))
        if not part:
            raise EOFError("peer disconnected")
        data.extend(part)
    return bytes(data)


def receive(peer):
    length, kind = HEADER.unpack(receive_bytes(peer, HEADER.size))
    return kind, receive_bytes(peer, length)


def expect(peer, kind):
    actual, body = receive(peer)
    assert kind == actual, (kind, actual)
    return body


def send(peer, kind, body=b""):
    peer.sendall(frame(kind, body))


@contextlib.contextmanager
def server(executable, retry_ms, timeout_ms):
    process = subprocess.Popen([executable, "0", str(retry_ms), str(timeout_ms)],
                               stdout=subprocess.PIPE, text=True)
    try:
        line = process.stdout.readline()
        assert line.startswith("LISTEN "), line
        yield int(line.split()[1])
    finally:
        process.terminate()
        process.wait(timeout=5)
        process.stdout.close()


@contextlib.contextmanager
def connect(port):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as peer:
        yield peer


def join(peer, role):
    send(peer, JOIN, bytes([role]))
    assert expect(peer, READY) == b""


def expect_closed(peer):
    try:
        while peer.recv(8192):
            pass
    except ConnectionResetError:
        pass


def wire_checks(executable):
    with server(executable, 100, 3000) as port, connect(port) as host, connect(port) as viewer:
        # Split the length field over two writes; the server must wait for the full header.
        request = frame(JOIN, b"\x01")
        host.sendall(request[:2])
        time.sleep(.04)
        host.sendall(request[2:])
        assert expect(host, READY) == b""
        join(viewer, 0)
        initial = expect(viewer, STATE)
        send(viewer, ACK, initial[:4])
        assert initial[4:] == b"\x00\x00"

        entity = struct.pack("!H", 1) + POSE.pack(0x12345678ABCD, 1, 2, 3, .25, .5, -.25)
        # Sequence 2 arrived before 1. The server still expects 1 and commits only that message.
        host.sendall(frame(PUBLISH, NUMBER.pack(2) + entity) + frame(PUBLISH, NUMBER.pack(1) + entity))
        assert expect(host, ACK) == NUMBER.pack(0)
        assert expect(host, ACK) == NUMBER.pack(1)
        first = expect(viewer, STATE)
        assert first[4:] == entity
        send(host, PUBLISH, NUMBER.pack(1) + entity)
        assert expect(host, ACK) == NUMBER.pack(1)
        send(host, PUBLISH, NUMBER.pack(2) + b"\x00\x00")
        assert expect(host, ACK) == NUMBER.pack(2)
        # Viewer deliberately withheld its acknowledgement. The retry retains old poses.
        repeated = expect(viewer, STATE)
        assert repeated == first
        send(viewer, ACK, first[:4])
        latest = expect(viewer, STATE)
        assert latest[4:] == b"\x00\x00" and NUMBER.unpack(latest[:4])[0] == NUMBER.unpack(first[:4])[0] + 1
        send(viewer, ACK, latest[:4])

        with connect(port) as late:
            join(late, 0)
            assert expect(late, STATE) == latest
        with connect(port) as other_host:
            send(other_host, JOIN, b"\x01")
            expect_closed(other_host)
        host.shutdown(socket.SHUT_RDWR)
        cleared = expect(viewer, STATE)
        assert cleared == NUMBER.pack(NUMBER.unpack(latest[:4])[0] + 1) + b"\x00\x00"

    with server(executable, 60, 320) as port, connect(port) as slow:
        join(slow, 0)
        original = expect(slow, STATE)
        start = time.monotonic()
        repeats = 0
        while True:
            try:
                kind, body = receive(slow)
                if kind == STATE:
                    assert body == original
                    repeats += 1
                    send(slow, HEARTBEAT)  # Alive, but did not accept the snapshot.
            except (EOFError, ConnectionResetError, BrokenPipeError):
                break
        elapsed = time.monotonic() - start
        assert repeats and .20 < elapsed < 2, (repeats, elapsed)
    with server(executable, 60, 320) as port, connect(port) as idle:
        join(idle, 1)
        start = time.monotonic()
        expect_closed(idle)
        idle_elapsed = time.monotonic() - start
        assert .20 < idle_elapsed < 2
    print(f"PASS wire: split header, two frames per write, sequence gap, duplicate publish, identical state retry, "
          f"late join, single host, host departure, missing viewer ACK timeout={elapsed:.3f}s retries={repeats}, "
          f"idle host timeout={idle_elapsed:.3f}s")


class HostProxy:
    """Drop the first completed-publish ACK to exercise the real engine client's retry."""
    def __init__(self, port):
        self.port = port
        self.listener = socket.create_server(("127.0.0.1", 0))
        self.listener.settimeout(8)
        self.address = self.listener.getsockname()[1]
        self.published = []
        self.dropped = False
        self.worker = threading.Thread(target=self.run, daemon=True)
        self.worker.start()

    def run(self):
        with self.listener, self.listener.accept()[0] as host, socket.create_connection(("127.0.0.1", self.port)) as upstream:
            def forward(source, destination, from_host):
                try:
                    while True:
                        kind, body = receive(source)
                        if from_host and kind == PUBLISH:
                            self.published.append(body)
                        if not from_host and kind == ACK and not self.dropped:
                            self.dropped = True
                            continue
                        send(destination, kind, body)
                except (EOFError, OSError):
                    with contextlib.suppress(OSError):
                        destination.shutdown(socket.SHUT_RDWR)

            worker = threading.Thread(target=forward, args=(host, upstream, True), daemon=True)
            worker.start()
            forward(upstream, host, False)
            worker.join(timeout=3)


def main():
    executable = str(Path(sys.argv[1]).resolve())
    wire_checks(executable)
    if len(sys.argv) > 2:
        with server(executable, 100, 8000) as port:
            proxy = HostProxy(port)
            subprocess.run([str(Path(sys.argv[2]).resolve()), str(port), str(proxy.address)], check=True, timeout=20)
            proxy.worker.join(timeout=4)
            assert not proxy.worker.is_alive() and proxy.dropped
            sequence = [NUMBER.unpack(p[:4])[0] for p in proxy.published]
            assert sequence == [1, 1, 2, 3] and proxy.published[0] == proxy.published[1], sequence
            print(f"PASS engine retransmitted the identical first publish after its ACK was withheld: {sequence}")


if __name__ == "__main__":
    main()
