"""Deterministic SVG charts for tools/bench/report.py: no ids, no randomness, no wall clock.

Coordinates are printed with one decimal, so the same data gives identical bytes.
"""
import math
from xml.sax.saxutils import escape

API_COLOURS = {"d3d12": "#2a78d6", "vulkan": "#eb6834"}
API_NAMES = {"d3d12": "D3D12", "vulkan": "Vulkan"}
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
SECONDARY = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
BAND = "#f0efec"
FONT = "system-ui, -apple-system, 'Segoe UI', sans-serif"
MAX_POINTS = 960
HOVER_COLUMNS = 120
STATISTIC_LABELS = (("average", "avg"), ("median", "median"), ("p95", "p95"), ("p99", "p99"), ("worst_1_percent", "worst 1%"),
                    ("max", "max"))
VARIANT_METRICS = ("GPU frame avg", "GPU frame p95", "CPU frame median")


def number(value):
    text = f"{value:.1f}"
    return "0.0" if text == "-0.0" else text


def text_escape(content):
    return escape(str(content), {'"': "&quot;"})


def points_text(points):
    return " ".join(f"{number(x)},{number(y)}" for x, y in points)


def ms(value):
    return "-" if value is None else f"{value:.3f} ms"


def axis_ticks(maximum, count=5):
    """Evenly spaced round ticks (steps of 1, 2, 2.5 or 5 x 10^k) from 0 to at least `maximum`; (ticks, step)."""
    if not maximum or maximum <= 0:
        maximum = 1.0
    raw = maximum / count
    magnitude = 10 ** math.floor(math.log10(raw))
    step = next(factor * magnitude for factor in (1, 2, 2.5, 5, 10) if factor * magnitude >= raw * (1 - 1e-9))
    last = max(math.ceil(maximum / step - 1e-9), 1)
    return [index * step for index in range(last + 1)], step


def tick_label(value, step, suffix=""):
    decimals = 0
    while decimals < 6 and abs(step * 10 ** decimals - round(step * 10 ** decimals)) > 1e-6:
        decimals += 1
    text = f"{value:,.{decimals}f}"
    return ("0" if text.strip("-0.,") == "" else text) + suffix


def bar_path(start, end, top, height):
    """A horizontal bar from `start` (the baseline, square) to `end` (the data end, rounded 4 px)."""
    length = abs(end - start)
    radius = min(4.0, length, height / 2)
    if radius < 0.5:
        return f"M{number(min(start, end))},{number(top)}h{number(length)}v{number(height)}h{number(-length)}Z"
    sign = 1 if end >= start else -1
    sweep = 1 if sign > 0 else 0
    return (f"M{number(start)},{number(top)}H{number(end - sign * radius)}"
            f"A{number(radius)},{number(radius)} 0 0 {sweep} {number(end)},{number(top + radius)}"
            f"V{number(top + height - radius)}"
            f"A{number(radius)},{number(radius)} 0 0 {sweep} {number(end - sign * radius)},{number(top + height)}"
            f"H{number(start)}Z")


class Canvas:
    def __init__(self, width, height, title, subtitle):
        self.width, self.height, self.title, self.subtitle = width, height, title, subtitle
        self.parts = []

    def add(self, markup):
        self.parts.append(markup)

    def text(self, x, y, content, size=11, fill=SECONDARY, anchor="start", weight=None):
        weight_attribute = f' font-weight="{weight}"' if weight else ""
        self.add(f'<text x="{number(x)}" y="{number(y)}" font-size="{size}" fill="{fill}" text-anchor="{anchor}"{weight_attribute}>'
                 f"{text_escape(content)}</text>")

    def line(self, x1, y1, x2, y2, stroke=GRID, width=1):
        self.add(f'<line x1="{number(x1)}" y1="{number(y1)}" x2="{number(x2)}" y2="{number(y2)}" stroke="{stroke}" stroke-width="{width}"/>')

    def rect(self, x, y, width, height, fill, tooltip=None, opacity=None):
        opacity_attribute = f' fill-opacity="{opacity}"' if opacity is not None else ""
        body = f"<title>{text_escape(tooltip)}</title>" if tooltip else ""
        start = (f'<rect x="{number(x)}" y="{number(y)}" width="{number(max(width, 0))}" height="{number(max(height, 0))}" '
                 f'fill="{fill}"{opacity_attribute}')
        self.add(f"{start}>{body}</rect>" if body else f"{start}/>")

    def polyline(self, points, stroke, width=1.5, tooltip=None):
        if len(points) < 2:
            return
        body = f"<title>{text_escape(tooltip)}</title>" if tooltip else ""
        self.add(f'<polyline points="{points_text(points)}" fill="none" stroke="{stroke}" stroke-width="{width}" '
                 f'stroke-linejoin="round" stroke-linecap="round">{body}</polyline>')

    def polygon(self, points, fill, opacity):
        if len(points) >= 3:
            self.add(f'<polygon points="{points_text(points)}" fill="{fill}" fill-opacity="{opacity}"/>')

    def dot(self, x, y, fill, tooltip):
        self.add(f'<circle cx="{number(x)}" cy="{number(y)}" r="4" fill="{fill}" stroke="{SURFACE}" stroke-width="2">'
                 f"<title>{text_escape(tooltip)}</title></circle>")

    def bar(self, start, end, top, height, fill, tooltip, opacity=None):
        opacity_attribute = f' fill-opacity="{opacity}"' if opacity is not None else ""
        self.add(f'<path d="{bar_path(start, end, top, height)}" fill="{fill}"{opacity_attribute}><title>{text_escape(tooltip)}</title></path>')

    def hover(self, x, y, width, height, tooltip):
        self.add(f'<rect x="{number(x)}" y="{number(y)}" width="{number(width)}" height="{number(height)}" fill="#000" '
                 f'fill-opacity="0"><title>{text_escape(tooltip)}</title></rect>')

    def legend(self, x, y, apis, extra=()):
        """API swatches (a short line and the name), then (label, kind) entries: kind band, faded, whisker or ink."""
        for api in apis:
            self.line(x, y - 4, x + 18, y - 4, API_COLOURS[api], 3)
            self.text(x + 24, y, API_NAMES[api], 12, INK)
            x += 24 + 8 * len(API_NAMES[api]) + 24
        for label, kind in extra:
            if kind == "band":
                self.rect(x, y - 10, 18, 10, MUTED, opacity=0.25)
            elif kind == "faded":
                self.rect(x, y - 10, 18, 10, MUTED, opacity=0.35)
            elif kind == "whisker":
                self.line(x + 9, y - 11, x + 9, y + 1, SECONDARY, 1.5)
            else:
                self.line(x, y - 4, x + 18, y - 4, SECONDARY, 2)
            self.text(x + 24, y, label, 12, SECONDARY)
            x += 24 + 6.5 * len(label) + 24

    def render(self):
        head = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {self.width} {self.height}" width="{self.width}" '
                f'height="{self.height}" font-family="{FONT}" role="img">',
                f"<title>{text_escape(self.title)}</title>",
                f"<desc>{text_escape(self.subtitle)}</desc>",
                f'<rect x="0" y="0" width="{self.width}" height="{self.height}" fill="{SURFACE}"/>',
                f'<text x="24" y="32" font-size="17" font-weight="600" fill="{INK}">{text_escape(self.title)}</text>',
                f'<text x="24" y="52" font-size="12" fill="{SECONDARY}">{text_escape(self.subtitle)}</text>']
        return "\n".join(head + self.parts + ["</svg>"]) + "\n"


def y_axis(canvas, left, top, width, height, ticks, step, suffix=""):
    """Grid lines and labels for a vertical value scale from ticks[0] (bottom) to ticks[-1] (top)."""
    low, high = ticks[0], ticks[-1]
    for tick in ticks:
        y = top + height - (tick - low) / (high - low) * height
        canvas.line(left, y, left + width, y, AXIS if tick == 0 else GRID)
        canvas.text(left - 8, y + 4, tick_label(tick, step, suffix), 10, MUTED, "end")
    return lambda value: top + height - (value - low) / (high - low) * height


def x_axis_values(canvas, left, top, width, ticks, step, scale=1, suffix=""):
    """Labels for a horizontal value scale (bars) under the panel at `top`; values are shown times `scale`."""
    low, high = ticks[0], ticks[-1]
    for tick in ticks:
        x = left + (tick - low) / (high - low) * width
        canvas.text(x, top, tick_label(tick * scale, step * scale, suffix), 10, MUTED, "middle")
    return lambda value: left + (value - low) / (high - low) * width


def vertical_grid(canvas, left, top, width, height, ticks):
    low, high = ticks[0], ticks[-1]
    for tick in ticks:
        x = left + (tick - low) / (high - low) * width
        canvas.line(x, top, x, top + height, AXIS if tick == 0 else GRID)


def present_apis(keys):
    return [api for api in API_COLOURS if api in keys]


def buckets(points, key, limit=MAX_POINTS):
    """[(tick, line, low, high)]: the points that have `key`, at most `limit`, each the bucket's mean tick, mean of
    medians, min of mins and max of maxes."""
    usable = [(point["tick"], point[key]) for point in points if point.get(key) is not None]
    if not usable:
        return []
    size = math.ceil(len(usable) / limit)
    result = []
    for start in range(0, len(usable), size):
        chunk = usable[start:start + size]
        total_tick = 0.0
        total_line = 0.0
        for tick, values in chunk:
            total_tick += tick
            total_line += values[0]
        result.append((total_tick / len(chunk), total_line / len(chunk), min(values[1] for _, values in chunk),
                       max(values[2] for _, values in chunk)))
    return result


def timeline(benchmark, traces, variant):
    """traces: {api: [{"tick", "active", "alive", "gpu_frame": (median, min, max) or None, "frame": ...}]}."""
    apis = present_apis(traces)
    left, plot_width, panel_height, gap, top = 84, 960, 160, 70, 128
    width, height = left + plot_width + 28, top + 3 * panel_height + 2 * gap + 56
    every_tick = sorted({point["tick"] for api in apis for point in traces[api]})
    first_tick, last_tick = every_tick[0], every_tick[-1]
    count = len(every_tick)
    bucketed = count > MAX_POINTS
    subtitle = (f"Variant {variant}. Median across runs per tick, with the min-max envelope across runs; {count:,} measured ticks"
                + (f", bucketed to at most {MAX_POINTS} points (bucket mean line, bucket min-max envelope)." if bucketed else "."))
    canvas = Canvas(width, height, f"{benchmark}: frame times per tick", subtitle)
    alive = {api: [(point["tick"], (point["alive"],) * 3) for point in traces[api] if point.get("alive") is not None] for api in apis}
    same_alive = len(apis) > 1 and len({tuple(alive[api]) for api in apis}) == 1
    canvas.legend(left, 80, apis, [("min-max across runs", "band")] + ([("alive particles (both APIs)", "ink")] if same_alive else []))
    x_of = lambda tick: left + (tick - first_tick) / max(last_tick - first_tick, 1) * plot_width
    tick_values, tick_step = axis_ticks(last_tick, 10)
    tick_values = [tick for tick in tick_values if first_tick <= tick <= last_tick] or [first_tick, last_tick]
    panels = (("gpu_frame", "GPU frame (ms): first to last GPU timestamp"), ("frame", "CPU frame (ms): loop period, frame start to next frame start"),
              ("alive", "Alive particles (counter records)"))
    for index, (key, label) in enumerate(panels):
        panel_top = top + index * (panel_height + gap)
        canvas.text(left, panel_top - 12, label, 12, INK, weight="600")
        if key == "alive":
            series = {api: buckets([{"tick": tick, "alive": values} for tick, values in alive[api]], "alive") for api in apis}
        else:
            series = {api: buckets(traces[api], key) for api in apis}
        maximum = max((high for api in apis for _, _, _, high in series[api]), default=1.0)
        ticks, step = axis_ticks(maximum)
        y_of = y_axis(canvas, left, panel_top, plot_width, panel_height, ticks, step)
        for tick in tick_values:
            canvas.text(x_of(tick), panel_top + panel_height + 16, tick_label(tick, tick_step), 10, MUTED, "middle")
        drawn = apis[:1] if key == "alive" and same_alive else apis
        for api in drawn:
            colour = SECONDARY if key == "alive" and same_alive else API_COLOURS[api]
            points = series[api]
            if key != "alive":
                envelope = [(x_of(tick), y_of(high)) for tick, _, _, high in points]
                envelope += [(x_of(tick), y_of(low)) for tick, _, low, _ in reversed(points)]
                canvas.polygon(envelope, colour, 0.16)
            name = "alive particles" if key == "alive" and same_alive else API_NAMES[api]
            canvas.polyline([(x_of(tick), y_of(line)) for tick, line, _, _ in points], colour,
                            tooltip=f"{name}: {label.split(' (')[0]}")
        columns = min(HOVER_COLUMNS, count)
        column_width = plot_width / columns
        for column in range(columns):
            low_tick = every_tick[column * count // columns]
            high_tick = every_tick[(column + 1) * count // columns - 1]
            lines = [f"ticks {low_tick}-{high_tick}" if high_tick != low_tick else f"tick {low_tick}"]
            for api in drawn:
                if key == "alive":
                    values = [values[0] for tick, values in alive[api] if low_tick <= tick <= high_tick]
                    if values:
                        lines.append(f"{'alive' if same_alive else API_NAMES[api]}: {min(values):,}-{max(values):,} particles")
                    continue
                values = [point[key] for point in traces[api] if low_tick <= point["tick"] <= high_tick and point.get(key)]
                if values:
                    total = 0.0
                    for median, _, _ in values:
                        total += median
                    lines.append(f"{API_NAMES[api]}: median {total / len(values):.3f} ms, max {max(high for _, _, high in values):.3f} ms")
            canvas.hover(left + column * column_width, panel_top, column_width, panel_height, "\n".join(lines))
    canvas.text(left + plot_width / 2, height - 18, "Simulation tick (one tick per frame)", 11, SECONDARY, "middle")
    return canvas.render()


def percentiles(label, panels, variant):
    """panels: [(benchmark, {api: {statistic: (median, min, max) or None}})]."""
    apis = present_apis({api for _, per_api in panels for api in per_api})
    columns = 3
    panel_width, panel_height, plot_left, plot_width, plot_height = 340, 230, 52, 268, 150
    rows = math.ceil(len(panels) / columns)
    width, height = 24 + columns * panel_width + 16, 100 + rows * panel_height
    canvas = Canvas(width, height, f"{label}: per-run statistics",
                    f"Variant {variant}. Each point is the median across runs of the per-run statistic (ms); the whisker spans the runs' min-max.")
    canvas.legend(24, 80, apis, [("min-max across runs", "whisker")])
    names = [name for _, name in STATISTIC_LABELS]
    for index, (benchmark, per_api) in enumerate(panels):
        origin_x = 24 + (index % columns) * panel_width
        origin_y = 112 + (index // columns) * panel_height
        left, top = origin_x + plot_left, origin_y + 22
        canvas.text(origin_x, origin_y + 6, f"{benchmark} (ms)", 12, INK, weight="600")
        maximum = max((value[2] for statistics in per_api.values() for value in statistics.values() if value), default=1.0)
        ticks, step = axis_ticks(maximum, 4)
        y_of = y_axis(canvas, left, top, plot_width, plot_height, ticks, step)
        slot = plot_width / len(names)
        for position, name in enumerate(names):
            canvas.text(left + slot * (position + 0.5), top + plot_height + 16, name, 10, MUTED, "middle")
        for offset, api in zip((-5, 5) if len(apis) > 1 else (0,), apis):
            statistics = per_api.get(api)
            if not statistics:
                continue
            placed = []
            for position, (key, name) in enumerate(STATISTIC_LABELS):
                entry = statistics.get(key)
                if entry:
                    placed.append((left + slot * (position + 0.5) + offset, entry, name))
            colour = API_COLOURS[api]
            canvas.polyline([(x, y_of(entry[0])) for x, entry, _ in placed], colour)
            for x, entry, _ in placed:
                canvas.line(x, y_of(entry[1]), x, y_of(entry[2]), colour, 1)
            for x, entry, name in placed:
                canvas.dot(x, y_of(entry[0]), colour, f"{benchmark}, {API_NAMES[api]}, {name}: {ms(entry[0])} "
                                                      f"(runs {entry[1]:.3f}-{entry[2]:.3f} ms)")
    return canvas.render()


def scaling(points, variant):
    """points: {api: [(effects, gpu_frame average (median, min, max), frame median (median, min, max))]}."""
    apis = present_apis(points)
    panel_width, plot_left, plot_width, plot_height, top = 500, 60, 400, 260, 118
    width, height = 24 + 2 * panel_width + 16, top + plot_height + 70
    canvas = Canvas(width, height, "Scaling with simultaneous effects",
                    f"Variant {variant}. single and simultaneous-10/50/100 (small-he, overview); median across runs with the runs' min-max.")
    canvas.legend(24, 80, apis, [("min-max across runs", "whisker")])
    effects = sorted({count for api in apis for count, _, _ in points[api]})
    span = math.log10(max(effects[-1], 2))
    x_of = lambda left, count: left + 20 + math.log10(count) / span * (plot_width - 40)
    for panel, (position, label) in enumerate(((1, "GPU frame average (ms)"), (2, "CPU frame median (ms)"))):
        left = 24 + panel * panel_width + plot_left
        canvas.text(24 + panel * panel_width, top - 14, label, 12, INK, weight="600")
        maximum = max((entry[position][2] for api in apis for entry in points[api] if entry[position]), default=1.0)
        ticks, step = axis_ticks(maximum)
        y_of = y_axis(canvas, left, top, plot_width, plot_height, ticks, step)
        for count in effects:
            canvas.text(x_of(left, count), top + plot_height + 16, f"{count}", 10, MUTED, "middle")
        canvas.text(left + plot_width / 2, top + plot_height + 38, "Simultaneous effects (log scale)", 11, SECONDARY, "middle")
        for api in apis:
            colour = API_COLOURS[api]
            placed = [(x_of(left, entry[0]), entry[position], entry[0]) for entry in points[api] if entry[position]]
            canvas.polyline([(x, y_of(value[0])) for x, value, _ in placed], colour)
            for x, value, _ in placed:
                canvas.line(x, y_of(value[1]), x, y_of(value[2]), colour, 1)
            for x, value, count in placed:
                canvas.dot(x, y_of(value[0]), colour, f"{API_NAMES[api]}, {count} effect(s), {label}: {ms(value[0])} "
                                                      f"(runs {value[1]:.3f}-{value[2]:.3f} ms)")
    return canvas.render()


def bar_panels(canvas, top, label_width, rows, panels, symmetric=False, panel_width=220, gap=28):
    """rows: [(row label, {api: {panel: (value, faded, tooltip)}})]; panels: [(key, title)], each scaled from 0,
    or around 0 in percent when `symmetric`. Returns the bottom y."""
    apis = present_apis({api for _, per_api in rows for api in per_api})
    bar, inner, row_gap = 10, 2, 14
    row_height = len(apis) * bar + (len(apis) - 1) * inner + row_gap
    plot_height = len(rows) * row_height
    for index, (key, title) in enumerate(panels):
        left = 24 + label_width + index * (panel_width + gap)
        canvas.text(left, top - 10, title, 12, INK, weight="600")
        values = [entry[0] for _, per_api in rows for parts in per_api.values() for name, entry in parts.items() if name == key and entry]
        if symmetric:
            positive, step = axis_ticks(max([abs(value) for value in values] + [0.02]), 2)
            ticks = [-tick for tick in reversed(positive[1:])] + positive
        else:
            ticks, step = axis_ticks(max(values + [1e-3]), 3)
        vertical_grid(canvas, left, top, panel_width, plot_height, ticks)
        x_of = x_axis_values(canvas, left, top + plot_height + 14, panel_width, ticks, step, 100 if symmetric else 1, "%" if symmetric else "")
        baseline = x_of(0)
        for row_index, (_, per_api) in enumerate(rows):
            for api_index, api in enumerate(apis):
                entry = (per_api.get(api) or {}).get(key)
                if not entry:
                    continue
                value, faded, tooltip = entry
                y = top + row_index * row_height + row_gap / 2 + api_index * (bar + inner)
                end = x_of(value)
                canvas.bar(baseline, end, y, bar, API_COLOURS[api], tooltip, 0.35 if faded else None)
                text = f"{value * 100:+.1f}%" if symmetric else f"{value:.3f}"
                if symmetric and value < 0:
                    canvas.text(end - 4, y + bar - 1, text, 9, MUTED, "end")
                else:
                    canvas.text(end + 4, y + bar - 1, text, 9, MUTED)
    for row_index, (label, _) in enumerate(rows):
        canvas.text(24 + label_width - 12, top + row_index * row_height + row_height / 2 + 4, label, 11, INK, "end")
    return top + plot_height + 14


def cpu_breakdown(rows, variant):
    """rows: [(benchmark, {api: {"record", "wait", "present", "wait_present": median ms or None}})]."""
    apis = present_apis({api for _, per_api in rows for api in per_api})
    panels = (("record", "Record (ms)"), ("wait", "Wait (ms)"), ("present", "Present call (ms)"), ("wait_present", "Wait + present (ms)"))
    entries = []
    for benchmark, per_api in rows:
        entries.append((benchmark, {api: {key: (parts[key], False, f"{benchmark}, {API_NAMES[api]}, {title.split(' (')[0]}: {ms(parts[key])} (median)")
                                          for key, title in panels if parts.get(key) is not None}
                                    for api, parts in per_api.items()}))
    label_width, panel_width, gap = 150, 210, 28
    row_height = len(apis) * 10 + (len(apis) - 1) * 2 + 14
    width = 24 + label_width + len(panels) * (panel_width + gap) + 8
    height = 130 + len(rows) * row_height + 50
    canvas = Canvas(width, height, "CPU frame breakdown",
                    f"Variant {variant}. Medians across runs of the per-run medians. Record = submit - wait - present; "
                    "compare wait + present across APIs, not each part.")
    canvas.legend(24, 80, apis)
    bottom = bar_panels(canvas, 126, label_width, entries, panels, panel_width=panel_width, gap=gap)
    canvas.text(24 + label_width, bottom + 24, "Milliseconds; each panel has its own scale.", 10, MUTED)
    return canvas.render()


def variants(sections, headline):
    """sections: [(variant, [(benchmark, {api: {metric label: (relative difference, neutral)}})])]."""
    apis = present_apis({api for _, rows in sections for _, per_api in rows for api in per_api})
    label_width, panel_width, gap = 150, 250, 56
    row_height = len(apis) * 10 + (len(apis) - 1) * 2 + 14
    width = 24 + label_width + len(VARIANT_METRICS) * (panel_width + gap) + 8
    height = 118 + sum(84 + len(rows) * row_height for _, rows in sections)
    canvas = Canvas(width, height, f"Variants against {headline}",
                    "Relative difference of the medians across runs. A faded bar is neutral: within max(1%, 3 x the larger relative MAD).")
    canvas.legend(24, 80, apis, [("neutral", "faded")])
    top = 118
    for variant, rows in sections:
        canvas.text(24, top, f"{variant} against {headline}", 13, INK, weight="600")
        entries = [(benchmark, {api: {metric: (difference, neutral, f"{benchmark}, {API_NAMES[api]}, {metric}: {difference * 100:+.2f}% "
                                                                   f"({'neutral' if neutral else 'CHANGED'})")
                                      for metric, (difference, neutral) in parts.items()}
                                for api, parts in per_api.items()}) for benchmark, per_api in rows]
        bottom = bar_panels(canvas, top + 34, label_width, entries, [(metric, metric) for metric in VARIANT_METRICS],
                            symmetric=True, panel_width=panel_width, gap=gap)
        top = bottom + 36
    return canvas.render()


def attribution(panels):
    """panels: [(title, [(region label, {api: average ms})])]."""
    apis = present_apis({api for _, regions in panels for _, per_api in regions for api in per_api})
    label_width, panel_width = 240, 520
    row_height = len(apis) * 10 + (len(apis) - 1) * 2 + 14
    width = 24 + label_width + panel_width + 80
    height = 118 + sum(78 + len(regions) * row_height for _, regions in panels)
    canvas = Canvas(width, height, "GPU regions (R)",
                    "--gpu-regions timestamps, attribution only: the average ms of each region over the frames that had it "
                    "(median across runs). Vulkan regions serialize the GPU.")
    canvas.legend(24, 80, apis)
    top = 118
    for title, regions in panels:
        canvas.text(24, top, title, 13, INK, weight="600")
        entries = [(label, {api: {"ms": (average, False, f"{title}, {API_NAMES[api]}, {label}: {ms(average)}")}
                            for api, average in per_api.items()}) for label, per_api in regions]
        top = bar_panels(canvas, top + 30, label_width, entries, [("ms", "Average (ms)")], panel_width=panel_width) + 34
    return canvas.render()
