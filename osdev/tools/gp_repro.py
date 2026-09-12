#!/usr/bin/env python3
"""gp_repro.py — boot myos.iso headless, hammer process_test menu key '1'
(thread_api_test thread storm) through the QEMU monitor to reproduce the
latent #GP, then quit qemu.  serial.log is captured by the caller.
"""
import os, socket, subprocess, sys, time

ISO = os.path.join(os.path.dirname(__file__), "..", "output", "myos.iso")
SOCK = "/tmp/osdev-mon.sock"
SERIAL = os.path.join(os.path.dirname(__file__), "..", "serial.log")
KEY_INTERVAL = 0.3          # send a '1' every 300 ms
START_DELAY = 9.0           # let grub(3s)+boot+rtc ramp finish + menu up
HAMMER_SECS = 90.0          # how long to keep pressing

def sendkey():
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(SOCK)
        s.sendall(b"sendkey 1\n")
        time.sleep(0.05)    # give qemu a moment to consume
        s.close()
    except OSError as e:
        print("sendkey failed:", e)

def main():
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    if os.path.exists(SERIAL):
        os.unlink(SERIAL)

    qemu = subprocess.Popen([
        "qemu-system-i386", "-cdrom", ISO,
        "-serial", "file:" + SERIAL,
        "-display", "none", "-no-reboot",
        "-monitor", "unix:%s,server,nowait" % SOCK,
    ])

    # wait for the monitor socket to appear
    for _ in range(200):
        if os.path.exists(SOCK):
            break
        time.sleep(0.05)

    time.sleep(START_DELAY)
    end = time.time() + HAMMER_SECS
    n = 0
    while time.time() < end:
        sendkey()
        n += 1
        time.sleep(KEY_INTERVAL)
    print("sent %d keys" % n)

    qemu.terminate()
    try:
        qemu.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu.kill()

if __name__ == "__main__":
    main()
