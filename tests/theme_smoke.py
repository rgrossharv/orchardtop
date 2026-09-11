#!/usr/bin/env python3
"""Exercise real terminal startup, default-theme migration, and custom choices."""
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time

root = Path(__file__).resolve().parents[1]
palette = dict(re.findall(r'theme\[(.*?)\]="(.*?)"', (root / 'themes/apple-dark.theme').read_text()))
embedded = (root / 'src/btop_theme.cpp').read_text().split('Apple_dark_theme = {', 1)[1].split('};', 1)[0]
assert palette == dict(re.findall(r'\{ "(.*?)", "(.*?)" \}', embedded)), 'Embedded palette differs from theme file'
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'config').mkdir()
    binary = tmp / 'bin/orchardtop'
    binary.parent.mkdir()
    shutil.copy2(root / 'bin/orchardtop', binary)
    for theme, width in [(None, 80), ('Default', 120), ('TTY', 80)]:
        config = tmp / f'config-{theme}.conf'
        config.write_text('shown_boxes = "cpu mem net proc"\n' + (f'color_theme = "{theme}"\n' if theme else ''))
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 35, width, 0, 0))
        env = dict(os.environ, TERM='xterm-256color', LC_ALL='en_US.UTF-8', XDG_CONFIG_HOME=str(tmp / 'config'))
        process = subprocess.Popen([str(binary), '-c', str(config), '--no-tty', '-u', '500'],
                                   stdin=slave, stdout=slave, stderr=slave, env=env, cwd=tmp)
        os.close(slave)
        output = bytearray()
        deadline = time.monotonic() + 4
        try:
            while time.monotonic() < deadline and process.poll() is None:
                if select.select([master], [], [], 0.1)[0]:
                    try:
                        output.extend(os.read(master, 65536))
                    except OSError:
                        break
            if process.poll() is None:
                try:
                    os.write(master, b'q')
                except OSError:
                    pass
            process.wait(timeout=5)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
        text = output.decode(errors='replace')
        assert process.returncode == 0, text[-2000:]
        assert 'ERROR' not in text, text[-2000:]
        if theme != 'TTY':
            assert '\x1b[38;2;93;156;255m' in text, 'apple-dark CPU outline absent'
            assert 'color_theme = "apple-dark"' in config.read_text(), 'Default not saved/migrated'
        else:
            assert 'color_theme = "TTY"' in config.read_text(), 'Custom choice overwritten'
        readings = re.findall(r'BAT (?:OUT|IN|IDLE) [0-9.]+W', text)
        print(f'Theme {theme or "fresh"}, {width} columns: passed; battery samples: {readings[-2:]}')
print('Terminal startup and theme checks passed')
