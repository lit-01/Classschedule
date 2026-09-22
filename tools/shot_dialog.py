"""从外面按屏幕坐标抓一个弹窗，存成 PNG。

用法：
    python tools/shot_dialog.py <actionName> <输出.png> [KEY=VAL ...]

例：
    python tools/shot_dialog.py importNote shots/h-importnote.png ^
        COURSETABLE_AUTOIMPORT="E:\\表格\\蒲亨林(2026-2027-1)课表.pdf"

为什么要单独一个脚本：弹窗（就算是关了原生、用 Qt 自己那套）也是**独立顶层
窗口**，QQuickWindow::grabWindow() 只抓得到主窗口，抓下来框里是空的。
所以这里从外面来：先 EnumWindows 找到这个进程的弹窗，取它真实的可见边框，
再 ImageGrab 抓那块区域。

`actionName` 是 QML 里的 objectName，程序会去调它的 open()。
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

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # 跟应用一样按物理像素算
except OSError:
    user32.SetProcessDPIAware()

WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

# GetWindowRect 会把窗口四周的透明阴影也算进去，抓下来边上多一圈背景。
# DWM 的 extended frame bounds 才是肉眼看到的那个框。
DWMWA_EXTENDED_FRAME_BOUNDS = 9


def visible_rect(hwnd):
    rect = wt.RECT()
    try:
        hr = ctypes.windll.dwmapi.DwmGetWindowAttribute(
            wt.HWND(hwnd), DWMWA_EXTENDED_FRAME_BOUNDS,
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
        if wpid.value != pid or not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        found.append((hwnd, buf.value) + visible_rect(hwnd))
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    action = sys.argv[1]
    out = sys.argv[2]
    if not os.path.isabs(out):
        out = os.path.join(BASE, out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    env = os.environ.copy()
    env["COURSETABLE_SHOT"] = os.path.join(os.path.dirname(out), "_main_tmp.png")
    env["COURSETABLE_SHOT_ACTION"] = action
    env["COURSETABLE_SHOT_DELAY"] = "9000"  # 留 9 秒给我们从外面抓
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    for extra in sys.argv[3:]:
        if "=" in extra:
            key, _, val = extra.partition("=")
            env[key] = val

    proc = subprocess.Popen([EXE], cwd=os.path.join(BASE, "build"), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    target = None
    for _ in range(60):  # 最多等 6 秒
        time.sleep(0.1)
        wins = windows_of(proc.pid)
        if len(wins) >= 2:  # 主窗口 + 弹窗
            for w in wins:
                print("window:", hex(w[0]), repr(w[1]), w[2:])
            cand = [w for w in wins if w[1] != "课表"] or wins[1:]
            if cand:
                target = cand[-1]
                break

    if target is None:
        print("没找到弹窗，这个进程只有：")
        for w in windows_of(proc.pid):
            print("  ", hex(w[0]), repr(w[1]), w[2:])
        proc.kill()
        return 1

    hwnd, title = target[0], target[1]
    # 弹窗的位置和大小是 Qt 记着的，弹出来之后还会自己挪一下、撑一下。
    # 等它落定，**再重新取一次边框** —— 否则拿的是刚出现那一刻的坐标，图会抓偏。
    time.sleep(0.9)
    l, t, r, b = visible_rect(hwnd)
    ImageGrab.grab(bbox=(l, t, r - 6, b - 5)).save(out)
    print(f"抓到弹窗 {title!r}: {(l, t, r, b)} -> {out}")

    proc.kill()
    proc.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
