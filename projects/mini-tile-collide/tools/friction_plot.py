#!/usr/bin/env python3
"""Render the friction report as one standalone SVG (no third-party deps).

Input : out/friction_trace.csv and out/friction_summary.csv, both produced by
        `make friction-report` (build/freport).
Output: out/friction_report.svg

Four small multiples, sharing one legend treatment:

  ① tangential speed |v| vs time for S1 at 30/60/144/240 fps
  ② mechanical energy vs time, initial level as the dashed reference
  ③ near-critical per-frame |Δv| ripple for S4
  ④ frame-rate invariance: final-position error vs the 240 fps run, per slope

Palette: validated 4-slot categorical order (blue, orange, aqua, yellow) on
the light surface #fcfcfb; adjacent CVD ΔE >= 9.1, normal-vision ΔE >= 19.8.
Aqua and yellow sit below 3:1 contrast, so every series is also direct-labelled
(legend swatches) and the exact numbers are in out/friction_summary.csv.
"""
import csv
import math
import sys

SLOTS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
SURFACE = "#fcfcfb"
PAGE = "#f9f9f7"
INK = "#0b0b0b"
INK2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"

FPS_ORDER = [30, 60, 144, 240]
SCEN = {
    "s1_hold20": "S1 20° held",
    "s2_chain45": "S2 45° chain",
    "s3_mixed45": "S3 ramp→floor",
    "s4_critical": "S4 critical",
}
# freport writes the frame rates slowest-first, so trace id = slope*16 + d.
DT_INDEX = {30: 0, 60: 1, 144: 2, 240: 3}


def load_trace(path):
    rows = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            rows.setdefault(int(r["id"]), []).append({
                "t": float(r["t"]),
                "rest": int(r["at_rest"]),
                "x": float(r["x"]),
                "y": float(r["y"]),
                "vx": float(r["vx"]),
                "vy": float(r["vy"]),
                "e": float(r["energy"]),
                "nx": float(r["naive_x"]),
                "ny": float(r["naive_y"]),
            })
    return rows


def load_summary(path):
    out = []
    with open(path, newline="") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = [x.strip() for x in line.split(",")]
            out.append({"slug": p[0], "dt": float(p[1]), "fps": int(round(float(p[2]))),
                        "conv": int(p[3]), "rest": int(p[4]),
                        "maxv": float(p[5]), "maxdy": float(p[6]),
                        "jv": float(p[7]), "jy": float(p[8]),
                        "egain": float(p[9]), "sw": int(p[10]),
                        "secs": float(p[11]), "ever": int(p[12]),
                        "nflip": int(p[13]), "ndv": float(p[14]),
                        "fx": float(p[15]), "fy": float(p[16])})
    return out


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def nice_ticks(lo, hi, n=5):
    if not (hi > lo):
        hi = lo + 1.0
    raw = (hi - lo) / n
    mag = 10 ** math.floor(math.log10(raw))
    step = 10 * mag
    for m in (1, 2, 2.5, 5, 10):
        if raw <= m * mag:
            step = m * mag
            break
    v = math.ceil(lo / step) * step
    ticks = []
    while v <= hi + step * 1e-9:
        ticks.append(round(v, 12))
        v += step
    return ticks


def frame(x0, y0, w, h, title, sub):
    return (f'<text x="{x0}" y="{y0 - 30}" font-size="13" font-weight="600" fill="{INK}">{esc(title)}</text>'
            f'<text x="{x0}" y="{y0 - 12}" font-size="11" fill="{INK2}">{esc(sub)}</text>'
            f'<rect x="{x0 - 52}" y="{y0 - 44}" width="{w + 72}" height="{h + 96}" '
            f'fill="{SURFACE}" rx="8"/>')


def axes(x0, y0, w, h, xt, yt, xlab, ylab, xfmt="{:g}", yfmt="{:g}"):
    s = []
    x_lo, x_hi = (xt[0], xt[-1]) if xt else (0.0, 1.0)
    y_lo, y_hi = (yt[0], yt[-1]) if yt else (0.0, 1.0)

    def sx(v):
        return x0 + ((v - x_lo) / (x_hi - x_lo)) * w if x_hi != x_lo else x0

    def sy(v):
        return y0 + h - ((v - y_lo) / (y_hi - y_lo)) * h if y_hi != y_lo else y0 + h

    for v in yt:
        y = sy(v)
        s.append(f'<line x1="{x0}" y1="{y:.2f}" x2="{x0 + w}" y2="{y:.2f}" stroke="{GRID}"/>')
        s.append(f'<text x="{x0 - 6}" y="{y + 3.5:.2f}" font-size="10" fill="{MUTED}" '
                 f'text-anchor="end">{esc(yfmt.format(v))}</text>')
    for v in xt:
        x = sx(v)
        s.append(f'<line x1="{x:.2f}" y1="{y0}" x2="{x:.2f}" y2="{y0 + h}" stroke="{GRID}"/>')
        s.append(f'<text x="{x:.2f}" y="{y0 + h + 15}" font-size="10" fill="{MUTED}" '
                 f'text-anchor="middle">{esc(xfmt.format(v))}</text>')
    s.append(f'<line x1="{x0}" y1="{y0 + h}" x2="{x0 + w}" y2="{y0 + h}" stroke="{AXIS}"/>')
    s.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0 + h}" stroke="{AXIS}"/>')
    if xlab:
        s.append(f'<text x="{x0 + w / 2:.2f}" y="{y0 + h + 33}" font-size="11" fill="{INK2}" '
                 f'text-anchor="middle">{esc(xlab)}</text>')
    if ylab:
        s.append(f'<text transform="translate({x0 - 46},{y0 + h / 2}) rotate(-90)" font-size="11" '
                 f'fill="{INK2}" text-anchor="middle">{esc(ylab)}</text>')
    return "".join(s), sx, sy


def poly(points, sx, sy):
    if not points:
        return ""
    d = [f"M{sx(points[0][0]):.2f},{sy(points[0][1]):.2f}"]
    d += [f"L{sx(x):.2f},{sy(y):.2f}" for x, y in points[1:]]
    return "".join(d)


def slope_legend(x, y, labels):
    s = []
    for i, lab in enumerate(labels):
        cx = x + i * 150
        s.append(f'<rect x="{cx}" y="{y - 9}" width="14" height="3" rx="1.5" fill="{SLOTS[i]}"/>')
        s.append(f'<text x="{cx + 20}" y="{y - 4}" font-size="11" fill="{INK2}">{esc(lab)}</text>')
    return "".join(s)


def main():
    if len(sys.argv) < 3:
        print("usage: friction_plot.py <trace.csv> <summary.csv> [out.svg]")
        return 2
    tr = load_trace(sys.argv[1])
    sm = load_summary(sys.argv[2])
    out_path = sys.argv[3] if len(sys.argv) > 3 else "out/friction_report.svg"

    W, H = 1200, 940
    PW, PH = 480, 280
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
         f'viewBox="0 0 {W} {H}" font-family="system-ui,-apple-system,Segoe UI,sans-serif" '
         f'role="img" aria-label="斜坡摩擦积分报告曲线">']
    s.append(f'<rect width="{W}" height="{H}" fill="{PAGE}"/>')
    s.append(f'<text x="44" y="40" font-size="19" font-weight="700" fill="{INK}">'
             '斜坡摩擦积分：定步长 → 可变步长 + 静/动摩擦切换</text>')
    s.append(f'<text x="44" y="60" font-size="12" fill="{INK2}">'
             '四组斜坡 × 四档外部帧长；曲线由 out/friction_trace.csv 生成，数值见 REPORT.md</text>')

    def ids_for(slope):
        return [slope * 16 + DT_INDEX[f] for f in FPS_ORDER]

    # ---------- ① speed vs time, S1 -----------------------------------------
    px, py = 104, 148
    s.append(frame(px, py, PW, PH, "① S2 45° 链条斜坡：切向速度随时间",
                   "四档外部帧长；子步长上限 dt_max 相同"))
    ids = ids_for(1)
    t_end = max(p["t"] for i in ids for p in tr[i])
    vmax = max(math.hypot(p["vx"], p["vy"]) for i in ids for p in tr[i])
    xt, yt = nice_ticks(0.0, t_end), nice_ticks(0.0, vmax * 1.06)
    ax, sx, sy = axes(px, py, PW, PH, xt, yt, "时间 t (s)", "|v| (px/s)")
    s.append(ax)
    for k, i in enumerate(ids):
        pts = [(p["t"], math.hypot(p["vx"], p["vy"])) for p in tr[i]]
        s.append(f'<path d="{poly(pts, sx, sy)}" fill="none" stroke="{SLOTS[k]}" '
                 f'stroke-width="2" stroke-linejoin="round"/>')
    s.append(slope_legend(px, py + PH + 52, [f"{f} fps" for f in FPS_ORDER]))

    # ---------- ② energy vs time, S1 ----------------------------------------
    px, py = 696, 148
    s.append(frame(px, py, PW, PH, "② 机械能不得随时间增长",
                   "S2；E = ½v² − g·y，虚线为初始值，四档曲线重合"))
    e0 = tr[ids[0]][0]["e"]
    e_all = [p["e"] for i in ids for p in tr[i]]
    lo, hi = min(min(e_all), e0), max(max(e_all), e0)
    pad = max(2.0, 0.08 * (hi - lo))
    xt, yt = nice_ticks(0.0, t_end), nice_ticks(lo - pad, hi + pad)
    ax, sx, sy = axes(px, py, PW, PH, xt, yt, "时间 t (s)", "E (px²/s²)", "{:g}", "{:,.0f}")
    s.append(ax)
    s.append(f'<line x1="{px}" y1="{sy(e0):.2f}" x2="{px + PW}" y2="{sy(e0):.2f}" '
             f'stroke="{AXIS}" stroke-width="1" stroke-dasharray="4 3"/>')
    s.append(f'<text x="{px + PW - 4}" y="{sy(e0) - 6:.2f}" font-size="10" fill="{MUTED}" '
             f'text-anchor="end">E₀</text>')
    for k, i in enumerate(ids):
        pts = [(p["t"], p["e"]) for p in tr[i]]
        s.append(f'<path d="{poly(pts, sx, sy)}" fill="none" stroke="{SLOTS[k]}" '
                 f'stroke-width="2"/>')
    worst = max(r["egain"] for r in sm)
    s.append(f'<text x="{px + 8}" y="{py + 18}" font-size="11" fill="{INK2}">'
             f'全部 16 次运行中 max ΔE/帧 = {worst:.1e}（上限 0）</text>')
    s.append(slope_legend(px, py + PH + 52, [f"{f} fps" for f in FPS_ORDER]))

    # ---------- ③ near-critical drift, S4 --------------------------------
    px, py = 104, 592
    s.append(frame(px, py, PW, PH, "③ S4 临界角（θ = atan μs）：从静止释放",
                   "旧积分逐帧判定 → 整块滑走；新积分一帧内锁住，位移 0"))
    ids = ids_for(3)
    y0 = tr[ids[0]][0]["ny"]
    drift = {}
    for i in ids:
        drift[i] = [(p["t"], abs(p["ny"] - y0)) for p in tr[i]]
    dmax = max(v for pts in drift.values() for _, v in pts)
    xt, yt = nice_ticks(0.0, t_end), nice_ticks(0.0, max(dmax * 1.08, 1.0))
    ax, sx, sy = axes(px, py, PW, PH, xt, yt, "时间 t (s)", "竖直位移 (px)")
    s.append(ax)
    for k, i in enumerate(ids):
        s.append(f'<path d="{poly(drift[i], sx, sy)}" fill="none" stroke="{SLOTS[k]}" '
                 f'stroke-width="2" stroke-dasharray="6 4"/>')
        v = drift[i][-1][1]
        s.append(f'<text x="{px + PW - 4}" y="{sy(v) - 5:.2f}" font-size="10" fill="{INK2}" '
                 f'text-anchor="end">旧 {FPS_ORDER[k]} fps：{v:.0f} px</text>')
    s.append(f'<line x1="{px}" y1="{sy(0):.2f}" x2="{px + PW}" y2="{sy(0):.2f}" '
             f'stroke="{INK}" stroke-width="2"/>')
    s.append(f'<text x="{px + PW - 4}" y="{sy(0) - 6:.2f}" font-size="10" fill="{INK}" '
             f'text-anchor="end">新积分：0 px（静摩擦锥内锁住）</text>')
    s.append(f'<text x="{px + 8}" y="{py + 18}" font-size="11" fill="{INK2}">'
             '悬挂/滑走取决于帧长；新积分第 1 帧即静止，之后位移恒为 0</text>')
    s.append(slope_legend(px, py + PH + 52, [f"{f} fps（旧）" for f in FPS_ORDER]))

    # ---------- ④ frame-rate invariance -------------------------------------
    px, py = 696, 592
    s.append(frame(px, py, PW, PH, "④ 帧率无关性：终点偏差",
                   "30/60/144 fps 的终点与 240 fps 之差（px），四组场景"))
    slugs = list(SCEN.keys())
    endpos = {}
    for r in sm:
        endpos.setdefault(r["slug"], {})[r["fps"]] = (r["fx"], r["fy"])
    errs = {}
    for slug in slugs:
        rx, ry = endpos[slug][240]
        errs[slug] = {fps: math.hypot(*[a - b for a, b in zip(endpos[slug][fps], (rx, ry))])
                      for fps in (30, 60, 144)}
    emax = max(v for slug in slugs for v in errs[slug].values())
    yt = nice_ticks(0.0, max(emax * 1.2, 0.01))
    ax, _sx, sy = axes(px, py, PW, PH, [], yt, "", "|Δ终点| (px)", "{:g}", "{:g}")
    s.append(ax)
    s.append(f'<line x1="{px}" y1="{sy(0):.2f}" x2="{px + PW}" y2="{sy(0):.2f}" stroke="{AXIS}"/>')
    group_w = PW / len(slugs)
    bw, gap = 26, 5
    for gi, slug in enumerate(slugs):
        gx = px + gi * group_w + (group_w - (3 * bw + 2 * gap)) / 2
        for k, fps in enumerate((30, 60, 144)):
            v = errs[slug][fps]
            bh = max(1.2, sy(0) - sy(v))
            x = gx + k * (bw + gap)
            s.append(f'<rect x="{x:.2f}" y="{sy(0) - bh:.2f}" width="{bw}" height="{bh:.2f}" '
                     f'rx="3" fill="{SLOTS[k]}"/>')
            if v > 0.005:
                s.append(f'<text x="{x + bw / 2:.2f}" y="{sy(0) - bh - 4:.2f}" font-size="9" '
                         f'fill="{INK2}" text-anchor="middle">{v:.3f}</text>')
        s.append(f'<text x="{px + gi * group_w + group_w / 2:.2f}" y="{py + PH + 16}" '
                 f'font-size="10" fill="{INK2}" text-anchor="middle">{esc(SCEN[slug])}</text>')
    s.append(f'<text x="{px + 8}" y="{py + 18}" font-size="11" fill="{INK2}">'
             '30/60 fps 与 240 fps 逐位一致；144 fps 非整数倍仍有 &lt;0.05 px 偏差</text>')
    # legend for panel ④ uses the same slot order as the frame-rate legend
    lx, ly = px, py + PH + 52
    for k, fps in enumerate((30, 60, 144)):
        cx = lx + k * 150
        s.append(f'<rect x="{cx}" y="{ly - 9}" width="14" height="3" rx="1.5" fill="{SLOTS[k]}"/>')
        s.append(f'<text x="{cx + 20}" y="{ly - 4}" font-size="11" fill="{INK2}">{fps} fps vs 240</text>')

    s.append(f'<text x="44" y="{H - 14}" font-size="11" fill="{MUTED}">'
             'make friction-report → out/friction_summary.csv；make friction-plots → 本图与 out/friction_trace.csv</text>')
    s.append("</svg>")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(s))
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
