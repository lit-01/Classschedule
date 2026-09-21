"""把界面几种状态各截一张图，不用开窗口、不拍桌面。

用法：python tools/screenshot.py
输出：CourseTable/shots/*.png
"""

import os
import subprocess

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(BASE, "build", "CourseTable.exe")
SHOTS = os.path.join(BASE, "shots")

CASES = [
    ("a-normal.png", {}),
    ("b-edit.png", {"COURSETABLE_SHOT_EDIT": "1"}),
    ("c-dialog.png", {"COURSETABLE_SHOT_DIALOG": "1"}),
    ("f-import.png", {"COURSETABLE_SHOT_ACTION": "importDialog"}),
]


def main():
    os.makedirs(SHOTS, exist_ok=True)

    for name, extra in CASES:
        path = os.path.join(SHOTS, name)
        before = os.path.getmtime(path) if os.path.exists(path) else None

        env = os.environ.copy()
        env["COURSETABLE_SHOT"] = path
        env["QT_FORCE_STDERR_LOGGING"] = "1"  # GUI 程序默认不吐日志
        env.update(extra)

        proc = subprocess.run([EXE], cwd=os.path.join(BASE, "build"),
                              env=env, capture_output=True)
        output = (proc.stdout + proc.stderr).decode("utf-8", "replace").strip()

        size = os.path.getsize(path) if os.path.exists(path) else 0
        fresh = os.path.exists(path) and os.path.getmtime(path) != before
        flag = "" if fresh else "  <-- 没写出新图！grabWindow().save() 偶发静默失败，重跑一次"
        print(f"[{name}] rc={proc.returncode} size={size}{flag}")
        if output:
            print(output[:2000])


if __name__ == "__main__":
    main()
