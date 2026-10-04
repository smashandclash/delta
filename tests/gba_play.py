#!/usr/bin/env python3
"""Plays a real online game of Smash&Clash on the GBA ROM, headless, in mGBA (as a
libretro core), through the real SDK bridge (gba/bridge/bridge.mjs on @smashandclash/sdk).

The libretro core has no Lua, so this script does what gba/bridge/smashandclash.lua does
in mGBA: it finds the ROM's mailbox, beats its heartbeat, carries each request to the
SDK bridge over its socket and writes the answer back. It plays with the D-pad and A,
like a person, reading where the focus is from the ROM's state line in RAM.

  node gba/bridge/bridge.mjs &      (after npm install in gba/bridge)
  python3 tests/gba_play.py gba/smashandclash.gba OUT_DIR [--turns N]

Needs: pip install libretro.py pillow, and mgba_libretro.so (MGBA_CORE, or ~/retro/blobs).
"""
import argparse
import collections
import ctypes
import os
import re
import socket
import sys
import time

from libretro import JoypadState, Session, TempDirPathDriver, UnformattedLogDriver
from libretro.drivers import ArrayAudioDriver, ArrayVideoDriver, DictOptionDriver, IterableInputDriver, StandardContentDriver
from PIL import Image

EWRAM = 0x02000000
MAGIC = b'SNC-GBA-BRIDGE1'
STATE = b'SNC-STATE: '
REQ_SEQ, REQ_LEN, RESP_SEQ, RESP_LEN, RESP_STATUS, BEAT, CONNECTED, RESP_CAP, RESP_ADDR, REQ = 16, 20, 24, 28, 32, 36, 40, 44, 48, 64
FIELD = re.compile(r'(\w+)=("([^"]*)"|\S+)')
H_HAND, H_CELL, H_NEW, H_ACTION, H_LOBBY = 1, 2, 3, 10, 12


def u32(mem, off):
    return int.from_bytes(bytes(mem[off:off + 4]), 'little')


def put32(mem, off, v):
    mem[off:off + 4] = int(v & 0xFFFFFFFF).to_bytes(4, 'little')


class Relay:
    """gba/bridge/smashandclash.lua, in Python."""

    def __init__(self, host, port):
        self.host, self.port = host, port
        self.box = None
        self.sock = None
        self.sent = 0
        self.buf = b''
        self.frame = 0
        self.next_try = 0

    def step(self, mem):
        self.frame += 1
        if self.box is None or bytes(mem[self.box:self.box + len(MAGIC)]) != MAGIC:
            self.box = None
            if self.frame % 30:
                return
            i = bytes(mem).find(MAGIC)
            if i < 0:
                return
            self.box = i
            seq = u32(mem, i + REQ_SEQ)
            self.sent = seq if u32(mem, i + RESP_SEQ) == seq else -1
        b = self.box
        put32(mem, b + BEAT, self.frame)
        if not self.sock and self.frame >= self.next_try:
            self.next_try = self.frame + 120
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=1)
                self.sock.setblocking(False)
                self.buf = b''
            except OSError:
                self.sock = None
        put32(mem, b + CONNECTED, 1 if self.sock else 0)
        if not self.sock:
            return
        seq = u32(mem, b + REQ_SEQ)
        if seq != self.sent:
            n = min(u32(mem, b + REQ_LEN), 1024)
            req = bytes(mem[b + REQ:b + REQ + n])
            try:
                self.sock.sendall(b'REQ %d %d\n' % (seq, len(req)) + req)
            except OSError:
                self.sock = None
                return
            self.sent = seq
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    self.sock = None
                    return
                self.buf += chunk
        except BlockingIOError:
            pass
        except OSError:
            self.sock = None
            return
        while b'\n' in self.buf:
            head, rest = self.buf.split(b'\n', 1)
            m = re.match(rb'RES (\d+) (\d+) (\d+)$', head)
            if not m:
                self.buf = rest
                continue
            n = int(m.group(3))
            if len(rest) < n:
                return
            body, self.buf = rest[:n], rest[n:]
            seq = int(m.group(1))
            if seq != u32(mem, b + REQ_SEQ):
                continue
            addr, cap = u32(mem, b + RESP_ADDR) - EWRAM, u32(mem, b + RESP_CAP)
            if len(body) <= cap:
                mem[addr:addr + len(body)] = body
            put32(mem, b + RESP_LEN, len(body))
            put32(mem, b + RESP_STATUS, int(m.group(2)))
            put32(mem, b + RESP_SEQ, seq)


class Driver:
    def __init__(self, rom, out, bridge):
        self.out = out
        os.makedirs(out, exist_ok=True)
        self.queue = collections.deque()
        self.state, self.raw, self.addr, self.shots = {}, '', None, 0
        self._mem = None
        self.clock = time.time()
        self.relay = Relay(*bridge)
        core = os.environ.get('MGBA_CORE') or os.path.expanduser('~/retro/blobs/mgba_libretro.so')
        self.session = Session(core=core, game=rom, content=StandardContentDriver(), audio=ArrayAudioDriver(),
                               input=IterableInputDriver(self.inputs()), video=ArrayVideoDriver(),
                               options=DictOptionDriver(variables={'mgba_skip_bios': 'ON'}), path=TempDirPathDriver(core, 'libretro'),
                               log=UnformattedLogDriver())

    def inputs(self):
        while True:
            yield self.queue.popleft() if self.queue else 0

    def __enter__(self):
        self.session.__enter__()
        return self

    def __exit__(self, *a):
        return self.session.__exit__(*a)

    def mem(self):
        # EWRAM, all 256 KB (the core reports a smaller size than the buffer it points at)
        if self._mem is None:
            ptr = self.session.core.get_memory_data(2)  # RETRO_MEMORY_SYSTEM_RAM
            addr = ptr if isinstance(ptr, int) else ctypes.cast(ptr, ctypes.c_void_p).value
            self._mem = memoryview((ctypes.c_ubyte * 0x40000).from_address(addr)).cast('B')
        return self._mem

    def read_state(self, mem):
        buf = bytes(mem)
        if self.addr is None or buf[self.addr:self.addr + len(STATE)] != STATE:
            i = buf.find(STATE)
            self.addr = i if i >= 0 else None
            if self.addr is None:
                return
        end = buf.find(b'\0', self.addr)
        line = buf[self.addr + len(STATE):end].decode('utf-8', 'replace')
        if line != self.raw:
            self.raw = line
            self.state = {k: (q if v.startswith('"') else v) for k, v, q in FIELD.findall(line)}
            short = line.split(' hits=')[0]
            print('   ', short.split(' rating=')[0][-90:], 'draw', self.state.get('draw'), 'focus', self.state.get('focus'), '|', self.state.get('main', ''), '|', self.state.get('notice', ''), flush=True)

    def frames(self, n):
        for _ in range(n):
            self.session.run()
            mem = self.mem()
            self.relay.step(mem)
            # while an SDK call is out, run at the GBA's own speed: its timeouts count frames
            b = self.relay.box
            if b is not None and u32(mem, b + REQ_SEQ) != u32(mem, b + RESP_SEQ):
                self.clock += 1 / 59.73
                lag = self.clock - time.time()
                if lag > 0:
                    time.sleep(lag)
                elif lag < -0.5:
                    self.clock = time.time()
            else:
                self.clock = time.time()
        self.read_state(self.mem())

    def wait_for(self, cond, seconds=60):
        end = time.time() + seconds
        while time.time() < end:
            self.frames(6)
            if self.state and cond(self.state):
                return True
        return False

    def press(self, **buttons):
        self.queue.extend([JoypadState(**buttons)] * 3 + [0] * 4)
        self.frames(8)

    def hits(self):
        out = []
        for part in self.state.get('hits', '').split(';'):
            if part.count(':') == 5:
                out.append(tuple(int(v) for v in part.split(':')))
        return out

    def focus(self):
        f = self.state.get('focus', '0:0').split(':')
        return int(f[0]), int(f[1])

    def go(self, hid, arg=0):
        """Moves the focus to a target with the D-pad (and presses nothing)."""
        for _ in range(30):
            if self.focus() == (hid, arg):
                return True
            hits = self.hits()
            cur = [h for h in hits if (h[0], h[1]) == self.focus()]
            dst = [h for h in hits if (h[0], h[1]) == (hid, arg)]
            if not cur or not dst:
                self.frames(6)
                continue
            c, d = cur[0], dst[0]
            cx, cy, dx, dy = c[2] + c[4] / 2, c[3] + c[5] / 2, d[2] + d[4] / 2, d[3] + d[5] / 2
            if abs(dx - cx) > max(c[4], d[4]) / 2:
                self.press(right=True) if dx > cx else self.press(left=True)
            else:
                self.press(down=True) if dy > cy else self.press(up=True)
        return self.focus() == (hid, arg)

    def choose(self, hid, arg=0):
        if self.go(hid, arg):
            self.press(a=True)
            return True
        i = self.raw.find('focus=')
        print('could not reach', hid, arg, 'focus', self.focus(), '| raw:', self.raw[i:i + 20], flush=True)
        return False

    def shot(self, name):
        self.frames(10)
        frame = self.session.video.screenshot()
        img = Image.frombytes('RGBA', (frame.width, frame.height), frame.data.obj).convert('RGB')
        path = os.path.join(self.out, f'{self.shots:02d}-{name}.png')
        img.resize((img.width * 3, img.height * 3), Image.NEAREST).save(path)
        self.shots += 1
        print('shot', path, flush=True)


def save_audio(d, path):
    """The session's sound as a WAV, and how much of it was sound at all."""
    import wave
    import numpy as np
    buf = np.frombuffer(d.session.audio.buffer, dtype=np.int16)
    info = d.session.audio.system_av_info
    rate = int(round(info.timing.sample_rate)) if info else 32768
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(buf.tobytes())
    mono = buf.reshape(-1, 2).mean(axis=1) / 32768
    secs = [mono[i:i + rate] for i in range(0, len(mono) - rate, rate)]
    loud = [20 * np.log10(np.sqrt(np.mean(x * x)) + 1e-9) for x in secs]
    quiet = sum(1 for v in loud if v < -50)
    print(f'audio: {len(mono) / rate:.1f} s at {rate} Hz, median {np.median(loud):.1f} dB, {quiet} silent seconds -> {path}', flush=True)


def play_turn(d):
    st = d.state
    if st.get('hop') == '1':
        cells = [c for c in range(15) if int(st.get('targets', '0'), 16) & (1 << c)]
        d.choose(H_CELL, cells[0]) if cells else d.choose(H_ACTION)
        return
    playable = int(st.get('play', '0'), 16)
    picks = [i for i in range(int(st.get('hand', '0'))) if playable & (1 << i)]
    if not picks:
        return
    d.choose(H_HAND, picks[-1])
    for _ in range(3):
        d.frames(6)
        st = d.state
        if st.get('busy') == '1' or st.get('turn') != '1':
            return
        cells = [c for c in range(15) if int(st.get('targets', '0'), 16) & (1 << c)]
        if cells:
            d.choose(H_CELL, cells[len(cells) // 2])
            continue
        if st.get('action') == '2':  # an effect with no tile: Play it
            d.choose(H_ACTION)
        return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('out')
    ap.add_argument('--turns', type=int, default=40)
    ap.add_argument('--bridge', default='127.0.0.1:8765')
    ap.add_argument('--linger', type=float, default=0, help='stay in the lobby this many seconds (with its music), then stop')
    a = ap.parse_args()
    host, port = a.bridge.split(':')
    with Driver(a.rom, a.out, (host, int(port))) as d:
        d.frames(60)
        d.shot('boot')
        if not d.wait_for(lambda st: st.get('boot') == '0' and st.get('scr') == '0' and st.get('busy') == '0', 90):
            d.shot('boot-failed')
            print('FAIL: never reached the lobby')
            os._exit(1)
        d.shot('lobby')
        if a.linger:  # past the end of the lobby's theme: it must loop
            d.frames(int(a.linger * 60))
            save_audio(d, os.path.join(a.out, 'session.wav'))
            print('PASS', flush=True)
            os._exit(0)
        d.choose(H_NEW)
        if not d.wait_for(lambda st: st.get('have') == '1' and st.get('st') != '0' and (st.get('turn') == '1' or st.get('over') == '1'), 90):
            d.shot('no-game')
            print('FAIL: no game')
            os._exit(1)
        d.shot('game-start')
        turns = 0
        while turns < a.turns:
            if not d.wait_for(lambda st: st.get('busy') == '0' and (st.get('turn') == '1' or st.get('over') == '1'), 90):
                d.shot('stuck')
                print('FAIL: stuck waiting for a turn')
                os._exit(1)
            if d.state.get('over') == '1':
                break
            turns += 1
            if turns == 2:  # SELECT: the card inspector
                d.go(H_HAND, 0)
                d.press(select=True)
                d.shot('inspect')
                d.press(b=True)
            play_turn(d)
            if turns in (1, 3, 6):
                d.shot(f'turn-{turns}')
        d.wait_for(lambda st: st.get('over') == '1', 30)
        d.frames(300)  # the Game Review
        d.shot('game-over')
        last = d.state
        print('RESULT:', last.get('main'), '| score', last.get('score'), '| moves', last.get('moves'), '| rating', last.get('rating'))
        save_audio(d, os.path.join(a.out, 'session.wav'))
        print('PASS' if last.get('over') == '1' else 'FAIL', flush=True)
    os._exit(0)


if __name__ == '__main__':
    main()
