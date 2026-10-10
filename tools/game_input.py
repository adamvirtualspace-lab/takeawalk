"""Scripted input for testing the plugin in the game. Every action is refused unless the
game's own window is in the foreground, so nothing is ever typed into another program.

game_input.py state                      foreground window, game window rectangle, cursor position
game_input.py focus                      bring the game's window to the foreground
game_input.py key <name> [hold_ms]       press and release a key (f9, f12, w, o, p, esc, enter, shift, ...)
game_input.py keys <name+name> <hold_ms> hold several keys together (shift+w)
game_input.py rel <dx> <dy>              move the mouse relatively
game_input.py abs <x> <y>                put the cursor at a screen position
game_input.py click                      left click where the cursor is
game_input.py shot                       press F12 (Steam screenshot) and print the newest file
game_input.py seq <step> ...             several of the above in one go: key:w:2000 wait:500 rel:100:0 shot abs:10:20 click
"""
import ctypes, ctypes.wintypes as wt, glob, os, sys, time

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
user32.SetProcessDPIAware()

SHOTS = r'C:\Program Files (x86)\Steam\userdata\142977176\760\remote\227300\screenshots'
GAME = 'eurotrucks2.exe'

VK = {
    'esc': 0x1B, 'enter': 0x0D, 'space': 0x20, 'shift': 0xA0, 'ctrl': 0xA2, 'tab': 0x09,
    'up': 0x26, 'down': 0x28, 'left': 0x25, 'right': 0x27, 'tilde': 0xC0,
}
for i in range(1, 13):
    VK['f%d' % i] = 0x6F + i
for c in 'abcdefghijklmnopqrstuvwxyz0123456789':
    VK[c] = ord(c.upper())
EXTENDED = {0x26, 0x28, 0x25, 0x27}

class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wt.LONG), ('dy', wt.LONG), ('mouseData', wt.DWORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('dwExtraInfo', ctypes.c_void_p)]
class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wt.WORD), ('wScan', wt.WORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('dwExtraInfo', ctypes.c_void_p)]
class UNION(ctypes.Union):
    _fields_ = [('mi', MOUSEINPUT), ('ki', KEYBDINPUT)]
class INPUT(ctypes.Structure):
    _fields_ = [('type', wt.DWORD), ('u', UNION)]

def send(inp):
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))

def process_name(pid):
    h = kernel32.OpenProcess(0x1000, False, pid)
    if not h:
        return ''
    buf = ctypes.create_unicode_buffer(520)
    size = wt.DWORD(520)
    ok = kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size))
    kernel32.CloseHandle(h)
    return os.path.basename(buf.value).lower() if ok else ''

def window_process(hwnd):
    pid = wt.DWORD(0)
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return process_name(pid.value)

def game_window():
    found = []
    def each(hwnd, _):
        if user32.IsWindowVisible(hwnd) and window_process(hwnd) == GAME:
            r = wt.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            if r.right - r.left > 300:
                found.append((hwnd, r.left, r.top, r.right, r.bottom))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)(each), 0)
    return found[0] if found else None

def game_in_front():
    return window_process(user32.GetForegroundWindow()) == GAME

def require_game():
    if not game_in_front():
        print('REFUSED: the game is not the foreground window (%s is)' % window_process(user32.GetForegroundWindow()))
        sys.exit(2)

def key_event(vk, up):
    scan = user32.MapVirtualKeyW(vk, 0)
    flags = (2 if up else 0) | (1 if vk in EXTENDED else 0)
    i = INPUT(type=1)
    i.u.ki = KEYBDINPUT(vk, scan, flags, 0, None)
    send(i)

def key(names, hold):
    require_game()
    vks = [VK[n] for n in names.lower().split('+')]
    for vk in vks:
        key_event(vk, False)
    time.sleep(hold / 1000.0)
    for vk in reversed(vks):
        key_event(vk, True)

def rel(dx, dy):
    require_game()
    # in steps, so that the game sees a smooth movement
    steps = max(1, int(max(abs(dx), abs(dy)) / 40))
    for s in range(steps):
        i = INPUT(type=0)
        i.u.mi = MOUSEINPUT(int(dx * (s + 1) / steps) - int(dx * s / steps), int(dy * (s + 1) / steps) - int(dy * s / steps), 0, 1, 0, None)
        send(i)
        time.sleep(0.01)

def absolute(x, y):
    require_game()
    user32.SetCursorPos(int(x), int(y))
    # a tiny relative move makes programs which listen to raw input notice
    for d in (1, -1):
        i = INPUT(type=0)
        i.u.mi = MOUSEINPUT(d, 0, 0, 1, 0, None)
        send(i)
        time.sleep(0.02)

def click():
    require_game()
    for flag in (2, 4):
        i = INPUT(type=0)
        i.u.mi = MOUSEINPUT(0, 0, 0, flag, 0, None)
        send(i)
        time.sleep(0.08)

def newest_shot():
    files = glob.glob(os.path.join(SHOTS, '*.jpg'))
    return max(files, key=os.path.getmtime) if files else None

def shot():
    before = newest_shot()
    key('f12', 80)
    for _ in range(40):
        time.sleep(0.25)
        now = newest_shot()
        if now and now != before:
            time.sleep(0.5)
            print('screenshot', now)
            return
    print('no new screenshot appeared')

def state():
    fg = user32.GetForegroundWindow()
    title = ctypes.create_unicode_buffer(200)
    user32.GetWindowTextW(fg, title, 200)
    print('foreground: %s "%s"' % (window_process(fg), title.value))
    print('game window:', game_window())
    p = wt.POINT()
    user32.GetCursorPos(ctypes.byref(p))
    print('cursor: %d %d   screen: %d x %d' % (p.x, p.y, user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)))

def focus():
    w = game_window()
    if not w:
        print('no game window')
        return
    user32.ShowWindow(w[0], 9)
    # Windows only lets a process take the foreground after some input of its own.
    key_event(0x12, False)
    key_event(0x12, True)
    user32.SetForegroundWindow(w[0])
    time.sleep(0.5)
    print('game in front:', game_in_front())

def run(step):
    p = step.split(':')
    if p[0] == 'key':
        key(p[1], int(p[2]) if len(p) > 2 else 80)
    elif p[0] == 'wait':
        time.sleep(int(p[1]) / 1000.0)
    elif p[0] == 'rel':
        rel(int(p[1]), int(p[2]))
    elif p[0] == 'abs':
        absolute(int(p[1]), int(p[2]))
    elif p[0] == 'click':
        click()
    elif p[0] == 'shot':
        shot()
    elif p[0] == 'state':
        state()
    elif p[0] == 'focus':
        focus()
    else:
        print('unknown step', step)
        sys.exit(1)

what = sys.argv[1]
if what == 'seq':
    for step in sys.argv[2:]:
        run(step)
elif what in ('key', 'keys'):
    key(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 80)
elif what == 'rel':
    rel(int(sys.argv[2]), int(sys.argv[3]))
elif what == 'abs':
    absolute(int(sys.argv[2]), int(sys.argv[3]))
else:
    run(what)
