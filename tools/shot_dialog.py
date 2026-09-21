"""抓「->」导课对话框。

为什么要单独一个脚本：对话框（哪怕关了原生、用 Qt 自己的）也是**独立顶层窗口**，
QQuickWindow::grabWindow() 只抓主窗口，框里是空的。
所以这里从外面按屏幕坐标抓：先 EnumWindows 找到这个进程的对话框窗口，取它的
rect，再 ImageGrab 抓那块。

用法：python tools/shot_dialog.py
输出：CourseTable/shots/f-import.png
"""

import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

from PIL import ImageGrab

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(BASE, "build", "CourseTable.exe")
OUT = os.path.join(BASE, "shots", "f-import.png")
TMP = os.path.join(BASE, "shots", "_main_tmp.png")

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # 跟应用一样按物理像素算
except OSError:
    user32.SetProcessDPIAware()

WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

# GetWindowRect 会把窗口四周的透明阴影也算进去，抓下来边上会多一圈背景。
# DWM 的 extended frame bounds 才是肉眼看到的那个框。
DWMWA_EXTENDED_FRAME_BOUNDS = 9


def visible_rect(hwnd):
    rect = wt.RECT()
    try:
        dwm = ctypes.windll.dwmapi
        hr = dwm.DwmGetWindowAttribute(wt.HWND(hwnd), DWMWA_EXTENDED_FRAME_BOUNDS,
                                       ctypes.byref(rect), ctypes.sizeof(rect))
        if hr == 0 and rect.right > rect.left and rect.bottom > rect.top:
            return rect.left, rect.top, rect.right, rect.bottom
    except OSError:
        pass
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    return rect.left, rect.top, rect.right, rect.bottom


def windows_of(pid):
    found = []

    def cb(hwnd, _):
        wpid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value != pid:
            return True
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        found.append((hwnd, buf.value) + visible_rect(hwnd))
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    env = os.environ.copy()
    env["COURSETABLE_SHOT"] = TMP
    env["COURSETABLE_SHOT_ACTION"] = "importDialog"
    env["COURSETABLE_SHOT_DELAY"] = "9000"  # 留 9 秒给我们从外面抓
    env["QT_FORCE_STDERR_LOGGING"] = "1"

    proc = subprocess.Popen([EXE], cwd=os.path.join(BASE, "build"),
                            env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)

    target = None
    for _ in range(60):  # 最多等 6 秒
        time.sleep(0.1)
        wins = windows_of(proc.pid)
        if len(wins) >= 2:  # 主窗口 + 对话框
            for w in wins:
                print("window:", hex(w[0]), repr(w[1]), w[2:])
            # 对话框是后出来的那个（不是主窗口「课表」）
            cand = [w for w in wins if w[1] != "课表"] or wins[1:]
            if cand:
                target = cand[-1]
                break

    if target is None:
        print("没找到对话框窗口，这个进程只有：")
        for w in windows_of(proc.pid):
            print("  ", hex(w[0]), repr(w[1]), w[2:])
        proc.kill()
        return 1

    hwnd, title = target[0], target[1]
    # 对话框的位置和大小是 Qt 记着的，弹出来之后还会自己挪一下、撑一下。
    # 先等它落定，**再重新取一次边框**——否则拿的是刚出现那一刻的坐标，图会抓偏。
    time.sleep(0.9)
    l, t, r, b = visible_rect(hwnd)
    img = ImageGrab.grab(bbox=(l, t, r - 6, b - 5))
    img.save(OUT)
    print(f"抓到对话框 {title!r}: {(l, t, r, b)} -> {OUT} {img.size}")

    proc.kill()
    proc.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
