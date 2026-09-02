"""RFCOMM echo test against the BluetoothSPP example. No root needed.

The example assembles a line in a 128-byte buffer and flushes when it fills,
so payloads are kept under that; >127 legitimately arrives as two echoes.
"""
import socket, sys, time

addr, chan = sys.argv[1], int(sys.argv[2])
s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
s.settimeout(20)
s.connect((addr, chan))
print("  connected to %s ch%d" % (addr, chan))
time.sleep(0.5)

def drain():
    s.settimeout(0.4)
    try:
        while s.recv(4096):
            pass
    except socket.timeout:
        pass
    s.settimeout(20)

def roundtrip(n):
    drain()
    payload = bytes((i % 26) + 97 for i in range(n))
    s.sendall(payload + b"\n")
    want = b"echo: " + payload + b"\r\n"
    got = b""
    deadline = time.time() + 10
    while len(got) < len(want) and time.time() < deadline:
        try:
            d = s.recv(4096)
        except socket.timeout:
            break
        if not d:
            break
        got += d
    ok = got == want
    print("  %4dB %s" % (n, "PASS" if ok else "FAIL (got %dB want %dB)" % (len(got), len(want))))
    return ok, want, got

results = [roundtrip(n)[0] for n in (1, 16, 64, 120)]

#  positive control: 期待値を1バイト壊し、比較器が本当に差を見ることを示す
ok, want, got = roundtrip(32)
broken = bytearray(want); broken[7] ^= 0x01
detected = bytes(broken) != got
print("  positive control: %s" % ("PASS (差を検出)" if detected else "FAIL (検出できず)"))
s.close()
n_ok = sum(results) + (1 if ok else 0) + (1 if detected else 0)
print("  --- %d/%d PASS ---" % (n_ok, len(results) + 2))
