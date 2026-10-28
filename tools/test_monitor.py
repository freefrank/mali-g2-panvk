import select
import socket
import struct
import unittest

from driver_monitor import create_receivers


class MonitorTests(unittest.TestCase):
    def test_delayed_reader_retains_compiler_burst(self):
        receivers = create_receivers(0, 16)
        for receiver in receivers:
            self.addCleanup(receiver.close)
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(sender.close)
        destination = receivers[0].getsockname()
        for index in range(21024):
            payload = b'T' * 124 if index < 20000 else b'B' * 4096
            sender.sendto(struct.pack('I', index) + payload, destination)
        received = set()
        used = set()
        while True:
            ready, _, _ = select.select(receivers, [], [], 0.1)
            if not ready:
                break
            for receiver in ready:
                data, _ = receiver.recvfrom(8192)
                index, = struct.unpack('I', data[:4])
                self.assertEqual(len(data), 128 if index < 20000 else 4100)
                received.add(index)
                used.add(receiver)
        self.assertEqual(received, set(range(21024)))
        self.assertEqual(len(used), 16)
        drops = sum(struct.unpack('9I', receiver.getsockopt(
            socket.SOL_SOCKET, 55, 36))[8] for receiver in receivers)
        self.assertEqual(drops, 0)

    def test_tail_drops_visible_without_later_packet(self):
        receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(receiver.close)
        receiver.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
        receiver.bind(('127.0.0.1', 0))
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(sender.close)
        for _ in range(512):
            sender.sendto(b'X' * 128, receiver.getsockname())
        memory = struct.unpack('9I', receiver.getsockopt(socket.SOL_SOCKET, 55, 36))
        self.assertGreater(memory[8], 0)


if __name__ == '__main__':
    unittest.main()
