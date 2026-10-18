"""Plays the packaged Shipping build with real input (no console in Shipping) and grabs the screen.

usage: smoke_shipping.py <steps>   steps: comma separated  wait:<s> | key:<vk hex> | down:<vk hex> | up:<vk hex> |
       click:<x>:<y> | look:<dx> | lmb:down|up | rmb:down|up | grab:<name>
"""
import ctypes
import os
import subprocess
import sys
import time
from PIL import ImageGrab

EXE = r'E:\CampusOps_Release\Windows\CampusOps.exe'
OUT = r'E:\game-project\Saved\AutoShots\small'
user32 = ctypes.windll.user32
user32.SetProcessDPIAware()


def key(vk):
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.05)
    user32.keybd_event(vk, 0, 2, 0)


def click(x, y):
    user32.SetCursorPos(x, y)
    time.sleep(0.1)
    user32.mouse_event(2, 0, 0, 0, 0)
    time.sleep(0.05)
    user32.mouse_event(4, 0, 0, 0, 0)


def look(dx):
    for _ in range(20):
        user32.mouse_event(1, int(dx / 20), 0, 0, 0)
        time.sleep(0.02)


proc = subprocess.Popen([EXE, '-windowed', '-ResX=1280', '-ResY=720', '-WinX=0', '-WinY=0'])
start = time.time()
for step in sys.argv[1].split(','):
    kind, _, arg = step.partition(':')
    if proc.poll() is not None:
        print('EXITED early with', proc.returncode, 'at', round(time.time() - start, 1))
        sys.exit(1)
    if kind == 'wait':
        time.sleep(float(arg))
    elif kind == 'key':
        key(int(arg, 16))
    elif kind == 'click':
        x, y = arg.split(':')
        click(int(x), int(y))
    elif kind == 'look':
        look(int(arg))
    elif kind == 'down':
        user32.keybd_event(int(arg, 16), 0, 0, 0)
    elif kind == 'up':
        user32.keybd_event(int(arg, 16), 0, 2, 0)
    elif kind == 'lmb':
        user32.mouse_event(2 if arg == 'down' else 4, 0, 0, 0, 0)
    elif kind == 'rmb':
        user32.mouse_event(8 if arg == 'down' else 16, 0, 0, 0, 0)
    elif kind == 'grab':
        ImageGrab.grab(bbox=(0, 0, 1290, 760)).save(os.path.join(OUT, arg + '.png'))
        print('grab', arg, round(time.time() - start, 1))
print('alive:', proc.poll() is None)
# CampusOps.exe only launches CampusOps-Win64-Shipping.exe: take the whole tree down, or instances pile up.
subprocess.run(['taskkill', '/F', '/T', '/PID', str(proc.pid)], capture_output=True)
subprocess.run(['taskkill', '/F', '/IM', 'CampusOps-Win64-Shipping.exe'], capture_output=True)
