#!/usr/bin/env python3
"""Exercise the real player with a test-only PCM output and an isolated config."""
import array
import math
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import tempfile
import termios
import tty
import time
import fcntl
import wave

ROOT = Path(__file__).resolve().parents[1]


def tone(path, seconds=2, rate=48000, channels=2):
    data = array.array('h')
    for i in range(int(seconds * rate)):
        for ch in range(channels):
            data.append(round(16000 * math.sin(2 * math.pi * (440 + ch * 110) * i / rate)))
    if os.sys.byteorder != 'little':
        data.byteswap()
    with wave.open(str(path), 'wb') as f:
        f.setparams((channels, 2, rate, 0, 'NONE', 'not compressed'))
        f.writeframes(data.tobytes())


class Player:
    def __init__(self, folder, fast=False, autosave=None, rc=''):
        self.folder = folder
        folder.mkdir()
        self.capture = folder / 'audio.raw'
        self.control, control_slave = pty.openpty()
        tty.setraw(control_slave)
        if autosave is not None:
            (folder / 'autosave').write_text(autosave)
        (folder / 'rc').write_text('set output_plugin=speedtest\nset softvol=true\nset softvol_state=100 100\n' + rc)
        env = dict(os.environ, CMUS_HOME=str(folder), CMUS_LIB_DIR=str(ROOT / 'tests/plugins'),
                   CMUS_DATA_DIR=str(ROOT / 'data'), CMUS_TEST_CAPTURE=str(self.capture),
                   TERM='xterm-256color', CMUS_TEST_CONTROL_FD=str(control_slave))
        if fast:
            env['CMUS_TEST_FAST'] = '1'
        self.output = bytearray()
        self.dismissed_mpris = False
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 100, 0, 0))
        self.proc = subprocess.Popen([str(ROOT / 'tests/cmus-speed-test')],
                                     stdin=slave, stdout=slave, stderr=slave, env=env,
                                     pass_fds=(control_slave,))
        os.close(slave)
        os.close(control_slave)
        try:
            self.command('status')
        except BaseException:
            self.proc.kill()
            self.proc.wait()
            os.close(self.master)
            os.close(self.control)
            raise

    def pump(self):
        while select.select([self.master], [], [], 0)[0]:
            try:
                self.output.extend(os.read(self.master, 65536))
            except OSError:
                break
        if not self.dismissed_mpris and b'MPRIS will be disabled.' in self.output and b'Press <enter>' in self.output:
            os.write(self.master, b'\n')
            self.dismissed_mpris = True
        assert self.proc.poll() is None, 'cmus exited unexpectedly: ' + self.output.decode(errors='replace')

    def command(self, cmd):
        self.pump()
        os.write(self.control, cmd.encode() + b'\n')
        result = b''
        end = time.monotonic() + 5
        while result != b'\n' and not result.endswith(b'\n\n'):
            self.pump()
            if time.monotonic() >= end:
                raise AssertionError('no command reply: ' + cmd + repr(result) + self.output.decode(errors='replace'))
            if select.select([self.control], [], [], .02)[0]:
                result += os.read(self.control, 65536)
        return result.decode()

    def status(self):
        return dict(line.split(' ', 1) for line in self.command('status').splitlines() if ' ' in line)

    def wait(self, predicate, timeout=10):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.pump()
            if predicate():
                return
            time.sleep(.02)
        raise AssertionError('timeout waiting for player')

    def key(self, key):
        os.write(self.master, key)

    def close(self):
        try:
            self.command('quit')
            self.proc.wait(timeout=5)
            assert self.proc.returncode == 0
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait()
            os.close(self.master)
            os.close(self.control)


def main():
    # Keep fixtures inside the checkout, also on restricted build hosts.
    with tempfile.TemporaryDirectory(prefix='speed-', dir=ROOT / 'tests') as name:
        base = Path(name)
        audio = base / 'tone.wav'
        tone(audio)
        if os.environ.get('CMUS_TEST_NO_TEMPO') == '1':
            p = Player(base / 'disabled', fast=True)
            try:
                assert 'Rubber Band' in p.command('speed 2')
                assert float(p.status()['speed']) == 1
                p.command(f'player-play {audio}')
                p.wait(lambda: p.status()['status'] == 'stopped')
                assert p.capture.stat().st_size == 96000 * 4
            finally:
                p.close()
            print('player speed: disabled build keeps normal playback')
            return
        for speed in (.25, .5, 1, 1.1, 2, 4):
            print(f'checking speed {speed}', flush=True)
            p = Player(base / ('rate-' + str(speed)), fast=True)
            try:
                assert 'Error' not in p.command(f'speed {speed}')
                p.command(f'player-play {audio}')
                p.wait(lambda: p.status()['status'] == 'stopped')
                got = p.capture.stat().st_size // 4
                want = round(96000 / speed)
                assert abs(got - want) <= 1, (speed, got, want)
                samples = array.array('h', p.capture.read_bytes())
                assert max(samples) > 1000
                if speed == 1:
                    with wave.open(str(audio), 'rb') as source:
                        assert p.capture.read_bytes() == source.readframes(96000)
            finally:
                p.close()

        second = base / 'second.wav'
        tone(second, seconds=1, rate=44100, channels=1)
        p = Player(base / 'transition', fast=True)
        try:
            p.command('speed 2')
            p.command(f'add -q {second}')
            time.sleep(.1)
            p.command(f'player-play {audio}')
            p.wait(lambda: p.status()['status'] == 'stopped')
            # Mono input is converted to stereo by the existing input path.
            assert p.capture.stat().st_size == (96000 + 44100) // 2 * 4
            assert float(p.status()['speed']) == 2
        finally:
            p.close()

        old = (ROOT / 'data/rc').read_text().replace('speed /1.1', 'vol +1% +0').replace('speed *1.1', 'vol +0 +1%')
        old = old.replace('bind common backspace speed 1\n', '')
        p = Player(base / 'controls', autosave=old)
        try:
            p.key(b']')
            p.wait(lambda: abs(float(p.status()['speed']) - 1.1) < 1e-6)
            p.key(b'[')
            p.wait(lambda: float(p.status()['speed']) == 1)
            for bad in ('0', '-nan', 'nan', 'inf', '5', '/0', '*0', '1junk'):
                p.command('speed ' + bad)
                assert float(p.status()['speed']) == 1, bad
            p.command('speed *100')
            assert float(p.status()['speed']) == 4
            p.command('speed /100')
            assert float(p.status()['speed']) == .25
            p.key(b'\x7f')
            p.wait(lambda: float(p.status()['speed']) == 1)
            tone(audio, seconds=12)
            p.command('speed 2')
            p.command(f'player-play {audio}')
            p.wait(lambda: int(p.status().get('position', '0')) >= 2)
            p.command('player-pause')
            pos = p.status()['position']
            time.sleep(.15)
            assert p.status()['position'] == pos
            p.command('seek 6')
            p.wait(lambda: int(p.status()['position']) == 6)
            assert p.status()['status'] == 'paused'
            p.command('speed 0.5')
            p.command('player-pause')
            p.wait(lambda: int(p.status()['position']) >= 7, timeout=4)
            p.command('speed 2')
            p.wait(lambda: p.status()['status'] == 'stopped', timeout=5)
        finally:
            p.close()

        p = Player(base / 'custom', autosave=old.replace('bind common [ vol +1% +0', 'bind common [ speed 0.75'),
                   rc='bind -f common ] speed 1.5\n')
        try:
            p.key(b'[')
            p.wait(lambda: float(p.status()['speed']) == .75)
            p.key(b']')
            p.wait(lambda: float(p.status()['speed']) == 1.5)
        finally:
            p.close()
    print('player speed: duration, bindings, migration, validation, pause, seek and live changes passed')


if __name__ == '__main__':
    main()
