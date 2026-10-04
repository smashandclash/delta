#!/usr/bin/env python3
"""Plays a real online game of Smash&Clash on the DS ROM, headless, in melonDS DS
(the melonDS core, as a libretro core): it boots the ROM, lets it join Wi-Fi through
melonDS's built-in access point, starts a game, and plays it by tapping the touch
screen like a person would, reading the ROM's debug log to see what is on screen.

  python3 tests/ds_play.py ROM.nds OUT_DIR [--dns 172.104.88.237] [--turns N] [--scenario game|lobby]

Needs: pip install libretro.py pillow, and a melonDS DS core
(melondsds_libretro.so, https://github.com/JesseTG/melonds-ds/releases); set
MELONDS_CORE to its path. No BIOS files: the core's built-in BIOS, as in Delta.
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import time

from libretro import JoypadState, Pointer, Session, TempDirPathDriver, UnformattedLogDriver
from libretro.drivers.user import DefaultUserDriver
from libretro.drivers import ArrayAudioDriver, ArrayVideoDriver, DictOptionDriver, IterableInputDriver, StandardContentDriver
from PIL import Image

from recorder import Recorder, hold

# the DS layout (ds/source/view.c)
TW, TH, BX, BY, GX, GY, HAND_Y, RX, RW = 32, 44, 5, 4, 2, 3, 147, 180, 72
SW_RIGHT_BTN = 256 - 6 - 119 + 60  # the right column of lobby buttons

STATE = re.compile(r'\[snc\] (.*)')
FIELD = re.compile(r'(\w+)=("([^"]*)"|\S+)')


def touch(x, y):
    """DS touch-screen pixel -> libretro pointer (the bottom half of the 256x384 frame)."""
    return Pointer(int((((x / 255) - 0.5) * 2) * 0x7FFF), int((y / 191) * 0x7FFF), True)


def cell_xy(cell, seat):
    col, row = cell % 5, cell // 5
    flip = seat == 'B'
    vx = 4 - col if flip else col
    vy = row if flip else 2 - row
    return BX + vx * (TW + GX) + TW // 2, BY + vy * (TH + GY) + TH // 2


class Driver:
    def __init__(self, rom, out, options, name='Ada'):
        self.out = out
        os.makedirs(out, exist_ok=True)
        self.queue = collections.deque()
        self.log = UnformattedLogDriver()
        self.seen = 0
        self.state = {}
        self.lines = []
        self.shots = 0
        self.rec = Recorder()
        core = os.environ.get('MELONDS_CORE') or os.path.expanduser('~/retro/blobs/melondsds_libretro-linux-x86_64-Release/cores/melondsds_libretro.so')
        self.session = Session(core=core, game=rom, content=StandardContentDriver(), audio=ArrayAudioDriver(),
                               input=IterableInputDriver(self.inputs()), video=ArrayVideoDriver(),
                               options=DictOptionDriver(variables=options), path=TempDirPathDriver(core, 'libretro'), log=self.log,
                               user=DefaultUserDriver(username=name))

    def inputs(self):
        while True:
            yield self.queue.popleft() if self.queue else 0

    def __enter__(self):
        self.session.__enter__()
        return self

    def __exit__(self, *a):
        return self.session.__exit__(*a)

    def frames(self, n):
        for _ in range(n):
            self.session.run()
            self.rec.frame(self.session)
            self.read_log()

    def read_log(self):
        recs = self.log.records
        while self.seen < len(recs):
            msg = recs[self.seen].message
            self.seen += 1
            for line in msg.splitlines():
                m = STATE.search(line)
                if not m:
                    continue
                self.lines.append(line)
                st = {k: (q if q is not None and v.startswith('"') else v) for k, v, q in FIELD.findall(m.group(1))}
                if 'scr' in st:
                    self.state = st
                    print('   ', m.group(1)[:200], flush=True)
                else:
                    print('   ', m.group(1), flush=True)

    def wait_for(self, cond, seconds=60):
        end = time.time() + seconds
        while time.time() < end:
            self.frames(6)
            if self.state and cond(self.state):
                return True
        return False

    def tap(self, x, y, frames=6):
        h, rest = hold(frames)
        self.queue.extend([touch(x, y)] * h + [0] * (rest or 8))
        self.frames(h + (rest or 8) + 2)

    def press(self, **buttons):
        h, rest = hold(6)
        self.queue.extend([JoypadState(**buttons)] * h + [0] * (rest or 8))
        self.frames(h + (rest or 8) + 2)

    def shot(self, name):
        self.frames(8)  # let the frame drawn last make it to the screen
        img = Image.frombytes('RGBA', (256, 384), self.session.video.screenshot().data.obj).convert('RGB')
        path = os.path.join(self.out, f'{self.shots:02d}-{name}.png')
        img.resize((512, 768), Image.NEAREST).save(path)
        self.shots += 1
        print('shot', path, flush=True)
        return img


def s(st, k, default='0'):
    return st.get(k, default)


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
    seat = s(st, 'seat', 'A')
    targets = int(s(st, 'targets'), 16)
    if s(st, 'hop') == '1':
        cells = [c for c in range(15) if targets & (1 << c)]
        if cells:
            d.tap(*cell_xy(cells[0], seat))
        else:
            d.tap(RX + RW // 2, 134)  # the action button: Stay
        return
    playable = int(s(st, 'play'), 16)
    hand = int(s(st, 'hand'))
    picks = [i for i in range(hand) if playable & (1 << i)]
    if not picks:
        return
    i = picks[-1]
    d.tap(BX + i * (TW + GX) + TW // 2, HAND_Y + TH // 2)
    for _ in range(3):
        d.frames(10)
        st = d.state
        if s(st, 'busy') == '1' or s(st, 'turn') != '1':
            return
        targets = int(s(st, 'targets'), 16)
        cells = [c for c in range(15) if targets & (1 << c)]
        if cells:
            d.tap(*cell_xy(cells[len(cells) // 2], seat))
            continue
        if s(st, 'action') == '2':  # an effect with no tile: press Play
            d.tap(RX + RW // 2, 134)
        return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('out')
    ap.add_argument('--dns', default='172.104.88.237', help='the WFC DNS the firmware gets (AltWFC: does not resolve the API host)')
    ap.add_argument('--turns', type=int, default=40)
    ap.add_argument("--scenario", default="game", help="game (vs an opponent at your level) | code (vs a second client) | lobby")
    ap.add_argument("--joiner", default="build/host/api_test", help="the second client for --scenario code")
    ap.add_argument("--name", default="Ada", help="the console nickname (the player name)")
    ap.add_argument("--linger", type=float, default=0, help="--scenario lobby: seconds to stay in the lobby (with its music)")
    a = ap.parse_args()
    options = {
        'melonds_console_mode': 'ds',
        'melonds_sysfile_mode': 'builtin',  # no BIOS files, as in Delta
        'melonds_network_mode': 'indirect',  # libslirp: the emulated access point
        'melonds_firmware_wfc_dns': a.dns,
        'melonds_firmware_username': a.name,
        'melonds_homebrew_sdcard': 'enabled',
        'melonds_jit_enable': 'disabled',
        'melonds_show_cursor': 'disabled',
    }
    with Driver(a.rom, a.out, options, a.name) as d:
        if not d.wait_for(lambda st: st.get('scr') == '0' and st.get('busy') == '0', 120):
            d.shot('boot-failed')
            print('FAIL: never reached the lobby')
            sys.exit(1)
        d.shot('lobby')
        if a.scenario == 'lobby':
            d.frames(int(a.linger * 60))  # past the end of the lobby's theme: it must loop
            save_audio(d, os.path.join(a.out, 'session.wav'))
            return
        other = None
        if a.scenario == 'code':
            # host a duel by code on the DS; a second client (the desktop C build) joins with it
            d.tap(SW_RIGHT_BTN, 94 + 23)  # Play by code
            d.shot('code-screen')
            d.tap(159, 169)  # Host
            if not d.wait_for(lambda st: st.get('st') == '0' and st.get('code', '-') != '-', 60):
                d.shot('no-code')
                print('FAIL: no code')
                sys.exit(1)
            code = d.state['code']
            d.shot('hosting')
            print('joining with code', code, flush=True)
            other = subprocess.Popen([a.joiner, code], stdout=open(os.path.join(a.out, 'joiner.log'), 'w'), stderr=subprocess.STDOUT)
        else:
            d.tap(6 + 60, 44 + 23)  # New game
        if not d.wait_for(lambda st: st.get('have') == '1' and st.get('st') != '0' and (st.get('turn') == '1' or st.get('over') == '1'), 90):
            d.shot('no-game')
            print('FAIL: no game')
            sys.exit(1)
        d.shot('game-start')
        turns = 0
        while turns < a.turns:
            if not d.wait_for(lambda st: st.get('busy') == '0' and (st.get('turn') == '1' or st.get('over') == '1'), 90):
                d.shot('stuck')
                print('FAIL: stuck waiting for a turn')
                sys.exit(1)
            if d.state.get('over') == '1':
                break
            turns += 1
            play_turn(d)
            if turns in (1, 3, 6):
                d.shot(f'turn-{turns}')
        d.wait_for(lambda st: st.get('over') == '1', 30)
        d.frames(240)  # the Game Review
        d.shot('game-over')
        last = d.state
        print('RESULT:', last.get('main'), '| score', last.get('score'), '| moves', last.get('moves'))
        if other:
            other.wait(timeout=60)
            print('joiner:', open(os.path.join(a.out, 'joiner.log')).read().strip().splitlines()[-2:])
        save_audio(d, os.path.join(a.out, 'session.wav'))
        print('PASS' if last.get('over') == '1' else 'FAIL')


if __name__ == '__main__':
    main()
