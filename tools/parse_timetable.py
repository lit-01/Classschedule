"""把教务系统导出的课表 PDF 解析成课程列表（JSON）。

这是**解析算法原型**：先在 Python 里把逻辑跑通、拿到「标准答案」，
再把同一套逻辑用 C++ 实现进 app。两边的关键思路是一致的：

    1. 按「列」把字捞出来（一个数据列 = 一个星期几），跨页拼接；
    2. 每列里用「学分:数字」当块结束标志，切出一个个课程块；
    3. 每个块里再用正则抠出 节次 / 周次 / 场地 / 教师。

为什么按列而不是按行：格子里文字会换行，按 y 排序会让相邻几天的内容交错在一起；
按列就天然分开了。而且「星期几」只能从位置得到（格子里没写）。

用法：
    python tools/parse_timetable.py <课表.pdf> [输出.json]

依赖：pdfplumber（装在 ~/.workbuddy/binaries/python/envs/default 里）
"""

import json
import re
import sys

import pdfplumber

DAYS = ["星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"]

# 一个课程块从「第一个字」到「学分:x.x」结束
BLOCK_RE = re.compile(r"(.*?学分:[\d.]+)", re.S)
SECTION_RE = re.compile(r"\((\d+)-(\d+)节\)")
# 紧跟在 "(N-M节)" 后面的那段就是周次，形如 1-16周 / 2-16周(双) / 9周 / 4-8周,10-16周
WEEKS_RE = re.compile(r"节\)([^/]*)")
RANGE_RE = re.compile(r"(\d+)(?:\s*-\s*(\d+))?\s*周\s*(?:\(?\s*([双单])\s*\)?)?")
FIELD_RE = {
    "campus": re.compile(r"/校区:([^/]*)"),
    "room": re.compile(r"/场地:([^/]*)"),
    "teacher": re.compile(r"/教师:([^/]*)"),
}
# 课程名两边会挂 ★(理论) ☆(实验) ◆(上机) ■(实践)，抠名字时去掉
DECOR = "★☆◆■"

NUM_RE = re.compile(r"(\d+)")


def group_lines(chars, tol=5.0):
    """把一堆字符按基线归成行（同一行内按 x 排）。"""
    ordered = sorted(chars, key=lambda c: (c["top"], c["x0"]))
    lines = []
    for ch in ordered:
        if lines and abs(ch["top"] - lines[-1][0]["top"]) <= tol:
            lines[-1].append(ch)
        else:
            lines.append([ch])
    return lines


def data_chars(page):
    """丢掉标题行和表头行，只留表格数据区的字。

    标题（「蒲亨林课表」「…学号：…」）和表头（「星期一」…「星期日」）的 x
    都落在数据列里，不过滤的话会被当成课程名的一部分，解析出来就变成
    「星期一概率论与数理统计」这种。
    """
    tables = page.find_tables()
    if not tables:
        return page.chars

    table = tables[0]
    start = 0
    for i, row in enumerate(table.extract()):
        if any(cell and "星期" in str(cell) for cell in row):
            start = i + 1  # 表头行的下一行才是数据
            break
    if start < len(table.rows):
        return [c for c in page.chars if c["top"] >= table.rows[start].bbox[1] - 2]
    return page.chars


def column_text(chars, x0, x1):
    """取某一列里的全部文字，按行拼成一串（中文直接连，不加分隔）。"""
    picked = [c for c in chars if x0 - 3 <= c["x0"] < x1 - 1]
    if not picked:
        return ""

    out = []
    for line in group_lines(picked):
        out.append("".join(c["text"] for c in sorted(line, key=lambda c: c["x0"])))
    return "".join(out)


def parse_weeks(text):
    """'1-16周' / '2-16周(双)' / '9周' / '4-8周,10-16周' → 周次列表。"""
    weeks = []
    for start, end, parity in RANGE_RE.findall(text):
        a = int(start)
        b = int(end) if end else a
        if b < a:
            a, b = b, a
        span = list(range(a, b + 1))
        if parity == "双":
            span = [w for w in span if w % 2 == 0]
        elif parity == "单":
            span = [w for w in span if w % 2 == 1]
        weeks.extend(span)
    return sorted(set(weeks))


def clean_name(raw):
    return raw.strip().strip(DECOR).strip(" 　")


def parse_column(text, day):
    """一列文字 → 若干课程。"""
    courses = []
    for chunk in BLOCK_RE.findall(text):
        m = SECTION_RE.search(chunk)
        if not m:
            continue  # 不是课（比如"其他课程"那行网课）

        name = clean_name(chunk[: m.start()])
        if not name:
            continue

        weeks_text = WEEKS_RE.search(chunk)
        weeks = parse_weeks(weeks_text.group(1)) if weeks_text else []

        course = {
            "name": name,
            "day": day,
            "startSection": int(m.group(1)),
            "endSection": int(m.group(2)),
            "weeks": weeks,
            "weeksLabel": weeks_text.group(1).strip() if weeks_text else "",
        }
        for key, rx in FIELD_RE.items():
            hit = rx.search(chunk)
            course[key] = hit.group(1).strip() if hit else ""
        courses.append(course)
    return courses


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None

    with pdfplumber.open(path) as pdf:
        # 列的左右边界取第一页表格的（后面几页版式一样）
        table = pdf.pages[0].find_tables()[0]
        # 9 列 = 时间段 + 节次 + 7 天，取后 7 列
        col_boxes = [(c.bbox[0], c.bbox[2]) for c in table.columns[2:9]]
        assert len(col_boxes) == 7, f"列数不对：{len(col_boxes)}"

        col_texts = [""] * 7
        for page in pdf.pages:
            chars = data_chars(page)
            for i, (x0, x1) in enumerate(col_boxes):
                col_texts[i] += column_text(chars, x0, x1)

    courses = []
    for i, text in enumerate(col_texts):
        found = parse_column(text, i + 1)
        print(f"\n【{DAYS[i]}】{len(found)} 门")
        for c in found:
            print(f"   {c['startSection']:>2}-{c['endSection']:<2}节  "
                  f"{c['weeksLabel']:<12} {c['name']:<16} "
                  f"{c['room']:<20} {c['teacher']}")
            courses.append(c)

    all_weeks = [w for c in courses for w in c["weeks"]]
    result = {
        "source": path,
        "courseCount": len(courses),
        "maxWeek": max(all_weeks) if all_weeks else 0,
        "courses": courses,
    }
    print(f"\n合计 {len(courses)} 门/次，最大周次 {result['maxWeek']}")

    if out_path:
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(result, fh, ensure_ascii=False, indent=2)
        print(f"已写出 {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
