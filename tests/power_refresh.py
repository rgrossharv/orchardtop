#!/usr/bin/env python3
"""Real-terminal regression: battery redraws between slow full collections."""
import fcntl
import os
from pathlib import Path
import platform
import pty
import re
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time

if (platform.system(), platform.machine()) != ('Darwin', 'arm64'):
    print('Apple battery cadence test skipped on this platform')
    raise SystemExit(0)
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    config = Path(tmp) / 'orchardtop.conf'
    config.write_text('shown_boxes = "cpu"\nupdate_ms = 4000\n')
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 160, 0, 0))
    process = subprocess.Popen([str(root / 'otop'), '-c', str(config), '--no-tty'],
        stdin=slave, stdout=slave, stderr=slave,
        env=dict(os.environ, TERM='xterm-256color', LC_ALL='en_US.UTF-8'))
    os.close(slave)
    frames, pending, output = [], b'', b''
    matched_locations = 0
    start = time.monotonic()
    try:
        while time.monotonic() - start < 12 and process.poll() is None:
            if not select.select([master], [], [], 0.1)[0]:
                continue
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            output += chunk
            pending += chunk
            while b'\x1b[?2026l' in pending:
                frame, pending = pending.split(b'\x1b[?2026l', 1)
                if b'BAT ' in frame:
                    frames.append(time.monotonic() - start)
                    plain = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', frame.decode(errors='replace'))
                    corner = re.search(r'BAT[▲▼■○].*?(\d+\.\d{2})W', plain)
                    summary = re.search(r'BAT (?:OUT|IN|IDLE) (\d+(?:\.\d+)?)W', plain)
                    if corner and summary:
                        assert abs(float(corner[1]) - float(summary[1])) <= 0.061, plain
                        matched_locations += 1
        os.write(master, b'q')
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=5)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
    text = output.decode(errors='replace')
    assert process.returncode in (0, -signal.SIGTERM), text[-2000:]
    assert 'ERROR' not in text, text[-2000:]
    if not re.search(r'BAT (OUT|IN|IDLE) [0-9.]+W', text):
        print('No battery sensor available; cadence assertion skipped')
    else:
        assert matched_locations > 0, 'No matching corner/summary battery readings'
        assert len(frames) >= 7, f'Battery redraws too slow: {frames}'
        gaps = [b-a for a, b in zip(frames, frames[1:])]
        assert sum(gap < 1.8 for gap in gaps) >= 5, gaps
        print('Battery refresh between 4000ms full updates passed:', [round(t, 2) for t in frames])
