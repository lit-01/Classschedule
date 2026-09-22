"""把一份课表 PDF 的底细翻出来：页数、字体、有没有表格线、文字都落在哪。

写这个是为了搞清楚「课表 PDF 到底是怎么排的」，再决定怎么解析——
是规规矩矩的表格，还是一堆按坐标摆的文本框，直接决定了解析思路。

用法：
    python tools/inspect_pdf.py <课表.pdf>

依赖：pdfplumber（装在 ~/.workbuddy/binaries/python/envs/default 里）
"""

import collections
import sys

import pdfplumber


def dump_page(page, index):
    print(f"\n{'=' * 60}")
    print(f"第 {index + 1} 页   {page.width:.1f} x {page.height:.1f} pt")
    print("=" * 60)

    # 字体和字号：能看出是不是嵌入了中文子集字体、有没有 ToUnicode
    fonts = collections.Counter()
    for ch in page.chars:
        fonts[(ch.get("fontname", "?"), round(ch.get("size", 0), 1))] += 1
    print(f"用到的字体（共 {len(fonts)} 种）：")
    for (name, size), n in fonts.most_common(12):
        print(f"    {name:<34} {size:>5} pt   x{n}")

    print(f"矢量线：横竖线 {len(page.lines)} 条，矩形 {len(page.rects)} 个，"
          f"曲线 {len(page.curves)} 条")

    # 表格：有的话直接抽出来看
    try:
        tables = page.extract_tables()
    except Exception as exc:  # noqa: BLE001
        tables = []
        print(f"    （抽表格失败：{exc}）")
    if tables:
        print(f"\n识别到 {len(tables)} 个表格：")
        for ti, table in enumerate(tables):
            print(f"  ── 表 {ti + 1}：{len(table)} 行 × "
                  f"{max(len(r) for r in table)} 列")
            for row in table[:14]:
                cells = [(c or "").replace("\n", "⏎") for c in row]
                print("     | " + " | ".join(cells))
            if len(table) > 14:
                print(f"     … 还有 {len(table) - 14} 行")
    else:
        print("\n没识别到表格结构 → 多半是用文本框 / 线条拼出来的，"
              "得按坐标聚类")

    # 按 y 分组成行，打印文字和它的 x 坐标
    words = page.extract_words(use_text_flow=False, keep_blank_chars=False)
    if not words:
        print("\n这一页一个字都没有 —— 可能是扫描件（图片），那得走 OCR")
        return

    rows = collections.defaultdict(list)
    for w in words:
        rows[round(w["top"] / 4)].append(w)

    print(f"\n文字块 {len(words)} 个，归成 {len(rows)} 行（y 从上到下）：")
    for key in sorted(rows):
        line = sorted(rows[key], key=lambda w: w["x0"])
        y = line[0]["top"]
        cells = "  ".join(f"{w['text']}@{w['x0']:.0f}" for w in line)
        print(f"  y={y:>6.0f} | {cells}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    with pdfplumber.open(path) as pdf:
        print(f"文件：{path}")
        print(f"页数：{len(pdf.pages)}")
        meta = pdf.metadata or {}
        for key in ("Title", "Author", "Producer", "Creator"):
            if meta.get(key):
                print(f"{key}：{meta[key]}")
        for i, page in enumerate(pdf.pages):
            dump_page(page, i)
    return 0


if __name__ == "__main__":
    sys.exit(main())
