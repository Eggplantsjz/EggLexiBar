#!/usr/bin/env python3
"""Build the OCR benchmark corpus.

Every page is plain HTML rendered by headless Edge at 2560x1440 CSS pixels with
a device scale factor of 1.5, which is exactly how a 3840x2160 screen looks at
150% scaling - the setup WordLens is used on most.

Because the content is authored here, the ground truth is exact: the token list
is extracted from the rendered text itself, not from an OCR result.

Usage:
    python tools/bench/make_corpus.py            # render every page
    python tools/bench/make_corpus.py --no-render
"""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import subprocess
import sys
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tools" / "bench"
HTML_DIR = BENCH / "corpus" / "html"
PNG_DIR = BENCH / "corpus" / "png"
TRUTH_DIR = BENCH / "corpus" / "truth"
MANIFEST = BENCH / "corpus" / "manifest.json"

EDGE_CANDIDATES = [
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
]

VIEWPORT = (2560, 1440)
SCALE = 1.5

TOKEN_RE = re.compile(r"[A-Za-z]+(?:['\u2019\-][A-Za-z]+)*")
WORD_RE = re.compile(r"^[a-z]+(?:['\-][a-z]+)*$")


class BodyText(HTMLParser):
    """Collect visible text; skips <script> and <style>."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self._skip = 0

    def handle_starttag(self, tag: str, attrs) -> None:
        if tag in ("script", "style"):
            self._skip += 1

    def handle_endtag(self, tag: str) -> None:
        if tag in ("script", "style") and self._skip:
            self._skip -= 1

    def handle_data(self, data: str) -> None:
        if not self._skip:
            self.parts.append(data)

    def text(self) -> str:
        return " ".join(self.parts)


def truth_tokens(page_html: str) -> list[str]:
    parser = BodyText()
    parser.feed(page_html)
    tokens = []
    for raw in TOKEN_RE.findall(parser.text()):
        token = raw.replace("\u2019", "'").lower()
        if WORD_RE.match(token) and len(token) >= 3:
            tokens.append(token)
    return tokens


BASE_CSS = """
* { box-sizing: border-box; }
html, body { margin: 0; height: 100%; overflow: hidden; }
body { -webkit-font-smoothing: antialiased; text-rendering: optimizeLegibility; }
"""

PAGES: dict[str, str] = {}

# --------------------------------------------------------------------------
# 1. Long-form article: 19px sans, light theme, comfortable measure.
# --------------------------------------------------------------------------
PAGES["article"] = """
<!doctype html><html><head><meta charset="utf-8"><title>article</title><style>
%(base)s
body { background:#fbfbfd; color:#1d2330; font:19px/1.75 'Segoe UI',system-ui,sans-serif; }
.top { display:flex; justify-content:space-between; padding:34px 90px 0; font-size:15px; color:#6b7280; }
.top b { color:#1d2330; letter-spacing:.14em; font-weight:600; }
article { max-width:920px; margin:28px auto 0; padding:0 24px; }
h1 { font-size:52px; line-height:1.12; letter-spacing:-.02em; margin:18px 0 14px; }
.byline { color:#6b7280; font-size:16px; margin-bottom:26px; }
p { margin:0 0 19px; }
.lead { font-size:22px; color:#374151; }
blockquote { margin:24px 0; padding:6px 0 6px 22px; border-left:4px solid #c7d2fe; color:#374151; font-style:italic; }
h2 { font-size:29px; margin:30px 0 10px; letter-spacing:-.01em; }
ul { margin:0 0 18px; padding-left:26px; }
li { margin-bottom:7px; }
code { font:16px 'Cascadia Mono',Consolas,monospace; background:#eef2ff; padding:2px 6px; border-radius:5px; }
</style></head><body>
<div class="top"><b>FIELD NOTES</b><span>Archive &nbsp;&middot;&nbsp; Essays &nbsp;&middot;&nbsp; About</span></div>
<article>
<h1>Notes on Distributed Systems</h1>
<div class="byline">Ada Lin &nbsp;&middot;&nbsp; 12 min read &nbsp;&middot;&nbsp; updated yesterday</div>
<p class="lead">Every distributed system is an exercise in accepting uncertainty. The network drops messages, clocks drift, and machines fail without warning.</p>
<p>When a request arrives, the coordinator must decide whether to replicate it, queue it, or reject it. A careless decision produces duplicated work; a timid decision produces unacceptable latency. The useful mental model is a ledger: each operation has an identity, an ordering, and a durable record of whether it committed.</p>
<h2>Consensus is expensive</h2>
<p>Replication protocols such as Raft and Paxos guarantee agreement, but they pay for it with round trips. Under normal conditions a write needs one round trip to the leader and one to a quorum of followers. When the leader disappears, the survivors must elect a replacement before progress resumes.</p>
<blockquote>The hard part is not reaching agreement. The hard part is knowing that agreement was reached while the evidence is still arriving.</blockquote>
<p>A practical service therefore separates the fast path from the recovery path. The fast path handles the overwhelming majority of operations with a single replication step. The recovery path is rare, expensive, and carefully instrumented.</p>
<h2>Measure the tail</h2>
<p>Average latency hides the behaviour that users actually notice. Report the median, the ninety-fifth percentile, and the maximum over a rolling window. Capture queue depth alongside duration, because a long queue converts a healthy dependency into a cascading failure.</p>
<ul>
<li>Bound every queue; an unbounded queue is a memory leak with extra steps.</li>
<li>Give each request a deadline and propagate it through every call.</li>
<li>Record why a request was retried, not merely that it was retried.</li>
</ul>
<p>Finally, remember that observability is a product feature. A dashboard nobody trusts is worse than no dashboard at all, because it manufactures confidence without evidence.</p>
</article>
</body></html>
"""

# --------------------------------------------------------------------------
# 2. Code editor: 14px monospace, dark theme, syntax colours.
# --------------------------------------------------------------------------
PAGES["code"] = """
<!doctype html><html><head><meta charset="utf-8"><title>code</title><style>
%(base)s
body { background:#0f1218; color:#d7dce7; font:14px/1.55 'Cascadia Mono',Consolas,monospace; }
.tabs { display:flex; align-items:center; gap:2px; background:#151922; padding:0 22px; height:52px; border-bottom:1px solid #232936; }
.tab { padding:10px 18px; color:#9aa4b8; font-size:14px; }
.tab.on { background:#0f1218; color:#e6ebf5; border-top:2px solid #4a6ef5; }
.crumbs { padding:12px 34px 8px; color:#7d879b; font-size:13px; }
.code { padding:4px 0 0 0; }
.row { display:flex; }
.num { width:72px; flex:none; padding-right:20px; text-align:right; color:#4d5668; user-select:none; }
.src { white-space:pre; }
.k { color:#7aa2f7; } .t { color:#7dcfff; } .s { color:#9ece6a; } .c { color:#6b7487; font-style:italic; }
.f { color:#e0af68; } .n { color:#ff9e64; } .m { color:#c0caf5; }
.status { position:absolute; bottom:0; left:0; right:0; height:36px; background:#1b2130; color:#aeb8cc; font-size:13px; display:flex; gap:30px; align-items:center; padding:0 26px; }
</style></head><body>
<div class="tabs"><span class="tab on">PowerState.cc</span><span class="tab">DfgBuilder.h</span><span class="tab">RaftLog.cc</span></div>
<div class="crumbs">thor_core / Power / upf / model / PowerState.cc</div>
<div class="code">
<div class="row"><span class="num">1</span><span class="src"><span class="c">// Build the dependency graph for the power model.</span></span></div>
<div class="row"><span class="num">2</span><span class="src"><span class="k">#include</span> <span class="s">"PowerState.h"</span></span></div>
<div class="row"><span class="num">3</span><span class="src"><span class="k">#include</span> <span class="s">&lt;algorithm&gt;</span></span></div>
<div class="row"><span class="num">4</span><span class="src"></span></div>
<div class="row"><span class="num">5</span><span class="src"><span class="k">namespace</span> thor <span class="m">{</span></span></div>
<div class="row"><span class="num">6</span><span class="src"></span></div>
<div class="row"><span class="num">7</span><span class="src"><span class="k">void</span> <span class="f">PowerState</span>::<span class="f">BuildGraph</span>(<span class="t">const</span> Design& design)</span></div>
<div class="row"><span class="num">8</span><span class="src"><span class="m">{</span></span></div>
<div class="row"><span class="num">9</span><span class="src">    m_nodes.<span class="f">clear</span>();</span></div>
<div class="row"><span class="num">10</span><span class="src">    m_edges.<span class="f">clear</span>();</span></div>
<div class="row"><span class="num">11</span><span class="src"></span></div>
<div class="row"><span class="num">12</span><span class="src">    <span class="k">for</span> (<span class="t">const</span> Cell& cell : design.<span class="f">Cells</span>()) <span class="m">{</span></span></div>
<div class="row"><span class="num">13</span><span class="src">        <span class="k">const</span> <span class="k">auto</span> name = cell.<span class="f">Name</span>();</span></div>
<div class="row"><span class="num">14</span><span class="src">        <span class="k">if</span> (name.<span class="f">starts_with</span>(<span class="s">"buffer_"</span>))</span></div>
<div class="row"><span class="num">15</span><span class="src">            <span class="k">continue</span>;</span></div>
<div class="row"><span class="num">16</span><span class="src">        m_nodes.<span class="f">insert</span>(<span class="m">{</span>name, cell.<span class="f">Area</span>()<span class="m">}</span>);</span></div>
<div class="row"><span class="num">17</span><span class="src">    <span class="m">}</span></span></div>
<div class="row"><span class="num">18</span><span class="src"></span></div>
<div class="row"><span class="num">19</span><span class="src">    <span class="k">for</span> (<span class="t">const</span> Net& net : design.<span class="f">Nets</span>()) <span class="m">{</span></span></div>
<div class="row"><span class="num">20</span><span class="src">        <span class="k">if</span> (net.<span class="f">fanout</span>() &lt; <span class="n">2</span>)</span></div>
<div class="row"><span class="num">21</span><span class="src">            <span class="k">continue</span>;</span></div>
<div class="row"><span class="num">22</span><span class="src">        <span class="k">for</span> (<span class="t">const</span> Pin* pin : net.<span class="f">Pins</span>()) <span class="m">{</span></span></div>
<div class="row"><span class="num">23</span><span class="src">            <span class="k">if</span> (!pin || pin-&gt;<span class="f">IsClock</span>())</span></div>
<div class="row"><span class="num">24</span><span class="src">                <span class="k">continue</span>;</span></div>
<div class="row"><span class="num">25</span><span class="src">            m_edges.<span class="f">push_back</span>(<span class="m">{</span>pin-&gt;<span class="f">Owner</span>(), pin-&gt;<span class="f">Load</span>()<span class="m">}</span>);</span></div>
<div class="row"><span class="num">26</span><span class="src">        <span class="m">}</span></span></div>
<div class="row"><span class="num">27</span><span class="src">    <span class="m">}</span></span></div>
<div class="row"><span class="num">28</span><span class="src"></span></div>
<div class="row"><span class="num">29</span><span class="src">    <span class="f">SortByWeight</span>(m_edges);</span></div>
<div class="row"><span class="num">30</span><span class="src">    <span class="f">VerifyAcyclic</span>(m_edges);</span></div>
<div class="row"><span class="num">31</span><span class="src"><span class="m">}</span></span></div>
<div class="row"><span class="num">32</span><span class="src"></span></div>
<div class="row"><span class="num">33</span><span class="src"><span class="c">// Recompute the critical path after every incremental change.</span></span></div>
<div class="row"><span class="num">34</span><span class="src"><span class="k">float</span> <span class="f">PowerState</span>::<span class="f">CriticalPath</span>() <span class="t">const</span></span></div>
<div class="row"><span class="num">35</span><span class="src"><span class="m">{</span></span></div>
<div class="row"><span class="num">36</span><span class="src">    <span class="k">float</span> worst = <span class="n">0.0f</span>;</span></div>
<div class="row"><span class="num">37</span><span class="src">    <span class="k">for</span> (<span class="t">const</span> Edge& edge : m_edges)</span></div>
<div class="row"><span class="num">38</span><span class="src">        worst = std::<span class="f">max</span>(worst, edge.<span class="f">delay_ns</span>());</span></div>
<div class="row"><span class="num">39</span><span class="src">    <span class="k">return</span> worst;</span></div>
<div class="row"><span class="num">40</span><span class="src"><span class="m">}</span></span></div>
<div class="row"><span class="num">41</span><span class="src"></span></div>
<div class="row"><span class="num">42</span><span class="src"><span class="m">}</span> <span class="c">// namespace thor</span></span></div>
</div>
<div class="status"><span>main*</span><span>Ln 30, Col 17</span><span>UTF-8</span><span>C++</span><span>clangd</span></div>
</body></html>
"""

# --------------------------------------------------------------------------
# 3. Settings dialog: 13px UI labels, dark theme, short lines.
# --------------------------------------------------------------------------
PAGES["ui"] = """
<!doctype html><html><head><meta charset="utf-8"><title>ui</title><style>
%(base)s
body { background:linear-gradient(120deg,#0d1017,#141a26); color:#e6ebf5; font:13px/1.5 'Segoe UI',system-ui,sans-serif;
       display:flex; align-items:center; justify-content:center; }
.win { width:1060px; background:#181d27; border:1px solid #2a3242; border-radius:12px; box-shadow:0 30px 80px rgba(0,0,0,.55); }
.head { padding:22px 30px 16px; border-bottom:1px solid #242c3b; font-size:19px; font-weight:600; }
.body { display:grid; grid-template-columns:300px 1fr; }
.nav { border-right:1px solid #242c3b; padding:16px 12px; }
.nav div { padding:10px 16px; border-radius:8px; color:#9aa4b8; }
.nav .on { background:#232c3d; color:#e6ebf5; }
.form { padding:18px 30px 24px; }
.section { color:#98a3b8; font-size:12px; letter-spacing:.18em; margin:14px 0 10px; }
.row { display:flex; align-items:center; justify-content:space-between; padding:9px 0; gap:24px; }
.row .label { color:#dbe3f0; }
.row .hint { color:#7f8ba1; font-size:12px; margin-top:3px; }
input[type=checkbox] { width:17px; height:17px; accent-color:#5b7cfa; }
select, input[type=number] { background:#111722; color:#dbe3f0; border:1px solid #303a4d; border-radius:6px; padding:6px 10px; font-size:13px; }
.buttons { display:flex; justify-content:flex-end; gap:10px; margin-top:22px; }
button { border:1px solid #303a4d; background:#1f2735; color:#dbe3f0; padding:9px 20px; border-radius:7px; font-size:13px; }
button.primary { background:#4a6ef5; border-color:#4a6ef5; color:#fff; }
table { width:100%%; border-collapse:collapse; margin-top:8px; font-size:13px; }
th { text-align:left; color:#8b97ad; font-weight:500; padding:7px 10px; border-bottom:1px solid #2a3242; }
td { padding:7px 10px; border-bottom:1px solid #202836; color:#cfd8e8; }
.badge { color:#9ece6a; font-size:12px; }
.badge.wait { color:#e0af68; }
</style></head><body>
<div class="win">
<div class="head">Preferences</div>
<div class="body">
  <div class="nav">
    <div class="on">General</div><div>Appearance</div><div>Recognition</div>
    <div>Dictionary</div><div>Shortcuts</div><div>Advanced</div><div>About</div>
  </div>
  <div class="form">
    <div class="section">GENERAL</div>
    <div class="row"><div><div class="label">Launch at startup</div>
      <div class="hint">Start the status bar when you sign in</div></div><input type="checkbox" checked></div>
    <div class="row"><div><div class="label">Keep unhandled words</div>
      <div class="hint">Words stay until you mark them as known or ignored</div></div><input type="checkbox" checked></div>
    <div class="row"><div><div class="label">Recognition interval</div>
      <div class="hint">How often the screen is sampled</div></div><input type="number" value="1.5"></div>
    <div class="row"><div><div class="label">Capture mode</div>
      <div class="hint">Full screen is slower than the active window</div></div>
      <select><option>Current window</option><option>Full screen</option><option>Around cursor</option></select></div>
    <div class="section">DISPLAY</div>
    <div class="row"><div><div class="label">Hide the bar after</div>
      <div class="hint">Only when the pointer is away</div></div><input type="number" value="20"></div>
    <div class="row"><div><div class="label">Automatically snooze repeated words</div>
      <div class="hint">After three scans and twenty seconds without interaction</div></div><input type="checkbox" checked></div>
    <div class="section">RECENT WORDS</div>
    <table><tr><th>Word</th><th>Count</th><th>Status</th></tr>
      <tr><td>boundary</td><td>2</td><td class="badge wait">pending</td></tr>
      <tr><td>conformal</td><td>1</td><td class="badge wait">pending</td></tr>
      <tr><td>decimator</td><td>3</td><td class="badge">known</td></tr>
      <tr><td>waveguide</td><td>1</td><td class="badge">known</td></tr>
    </table>
    <div class="buttons"><button>Cancel</button><button class="primary">Save changes</button></div>
  </div>
</div></div>
</body></html>
"""

# --------------------------------------------------------------------------
# 4. README/documentation: 15px, inline code, hyphenated compounds, links.
# --------------------------------------------------------------------------
PAGES["doc"] = """
<!doctype html><html><head><meta charset="utf-8"><title>doc</title><style>
%(base)s
body { background:#ffffff; color:#24292f; font:15px/1.7 'Segoe UI',system-ui,sans-serif; }
.wrap { max-width:1180px; margin:0 auto; padding:42px 56px; }
h1 { font-size:34px; margin:0 0 6px; letter-spacing:-.01em; }
.sub { color:#57606a; margin-bottom:22px; }
h2 { font-size:23px; margin:26px 0 8px; padding-bottom:6px; border-bottom:1px solid #d8dee4; }
p { margin:0 0 12px; }
ul { margin:0 0 14px; padding-left:26px; } li { margin-bottom:6px; }
code { font:13px 'Cascadia Mono',Consolas,monospace; background:#eff1f3; padding:2px 6px; border-radius:5px; }
pre { background:#f6f8fa; border:1px solid #d8dee4; border-radius:8px; padding:14px 18px; font:13px/1.55 'Cascadia Mono',Consolas,monospace; color:#24292f; }
a { color:#0969da; text-decoration:none; }
table { border-collapse:collapse; width:100%%; margin:6px 0 16px; font-size:14px; }
th, td { border:1px solid #d8dee4; padding:7px 12px; text-align:left; }
th { background:#f6f8fa; }
.tag { display:inline-block; background:#ddf4ff; color:#0969da; border-radius:20px; padding:2px 10px; font-size:12px; }
</style></head><body>
<div class="wrap">
<h1>wordlens-core</h1>
<div class="sub">Offline screen vocabulary for Windows &nbsp;&middot;&nbsp; <span class="tag">v0.2</span></div>
<p>A read-only pipeline that captures the screen, recognises English words and keeps the ones worth learning. Everything runs locally; no request ever leaves the machine.</p>
<h2>Quick start</h2>
<pre>powershell -NoProfile -File scripts\\build.ps1
build\\WordLens.exe
build\\wlocr.exe --define boundary waveguide</pre>
<h2>How it works</h2>
<ul>
<li>The capture layer grabs either the full screen, the active window, or the region around the cursor.</li>
<li>Tesseract runs as a child process so a slow scan never freezes the interface.</li>
<li>The dictionary is memory-mapped, so lookups cost microseconds and startup stays fast.</li>
<li>Words you already know are filtered by a frequency list and a personal blocklist.</li>
</ul>
<p>Compound spellings such as <code>state-of-the-art</code>, <code>read-only</code> and <code>end-to-end</code> are normalised before lookup. Tool names like <code>clang-format</code> are rejected because the offline dictionary has no entry for them.</p>
<h2>Options</h2>
<table>
<tr><th>Option</th><th>Default</th><th>Description</th></tr>
<tr><td>intervalMs</td><td>1500</td><td>Delay between captures in milliseconds</td></tr>
<tr><td>psm</td><td>11</td><td>Tesseract page segmentation mode</td></tr>
<tr><td>minConfidence</td><td>55</td><td>Discard low-confidence tokens</td></tr>
<tr><td>commonWordCount</td><td>3000</td><td>Size of the familiar-word list</td></tr>
</table>
<h2>Troubleshooting</h2>
<p>If the bar stays empty, confirm that the bundled language data exists and that the executable can locate the trained model. On a high-DPI display, run the capture tool once to verify the reported image size. A dense full-screen scan may take several seconds; narrow the region to improve throughput.</p>
<p>See the full documentation at <a href="#">https://example.com/docs/wordlens/quick-start</a> for configuration details and release notes.</p>
</div>
</body></html>
"""

# --------------------------------------------------------------------------
# 5. Terminal log: 13px monospace, dark, timestamps and paths.
# --------------------------------------------------------------------------
PAGES["terminal"] = """
<!doctype html><html><head><meta charset="utf-8"><title>terminal</title><style>
%(base)s
body { background:#0c0f14; color:#c9d1dc; font:13px/1.62 'Cascadia Mono',Consolas,monospace; padding:26px 34px; }
.line { white-space:pre; }
.ts { color:#5b6577; } .ok { color:#9ece6a; } .warn { color:#e0af68; } .err { color:#f7768e; }
.path { color:#7dcfff; } .num { color:#ff9e64; } .dim { color:#7a8496; }
.cmd { color:#e6ebf5; }
</style></head><body>
<div class="line"><span class="ts">[2026-10-28 09:14:02]</span> <span class="cmd">$ cmake --build build --config Release</span></div>
<div class="line"><span class="ts">[09:14:02]</span> <span class="dim">[1/12]</span> Building CXX object wordlens_core/CMakeFiles/wordlens_core.dir/Dictionary.cpp.obj</div>
<div class="line"><span class="ts">[09:14:03]</span> <span class="dim">[2/12]</span> Building CXX object wordlens_core/CMakeFiles/wordlens_core.dir/WordFilter.cpp.obj</div>
<div class="line"><span class="ts">[09:14:03]</span> <span class="dim">[3/12]</span> Building CXX object wordlens_core/CMakeFiles/wordlens_core.dir/WordSession.cpp.obj</div>
<div class="line"><span class="ts">[09:14:04]</span> <span class="dim">[4/12]</span> <span class="ok">Built target wordlens_core</span></div>
<div class="line"><span class="ts">[09:14:04]</span> <span class="dim">[5/12]</span> Building CXX object CMakeFiles/WordLens.dir/src/AppController.cpp.obj</div>
<div class="line"><span class="ts">[09:14:05]</span> <span class="warn">warning:</span> unused parameter 'surface' in CaptureFrame</div>
<div class="line"><span class="ts">[09:14:05]</span> <span class="dim">[6/12]</span> Building CXX object CMakeFiles/WordLens.dir/src/TesseractOcr.cpp.obj</div>
<div class="line"><span class="ts">[09:14:06]</span> <span class="dim">[7/12]</span> Building CXX object CMakeFiles/WordLens.dir/src/OverlayBar.cpp.obj</div>
<div class="line"><span class="ts">[09:14:06]</span> <span class="dim">[8/12]</span> Linking CXX executable WordLens.exe</div>
<div class="line"><span class="ts">[09:14:06]</span> <span class="dim">[9/12]</span> <span class="ok">Built target WordLens</span></div>
<div class="line"></div>
<div class="line"><span class="ts">[09:14:21]</span> <span class="cmd">$ ./run_tests.sh --filter dictionary</span></div>
<div class="line"><span class="ts">[09:14:21]</span> Running 18 checks from <span class="path">tests/dictionary_check.cpp</span></div>
<div class="line"><span class="ts">[09:14:21]</span> <span class="ok">ok</span>    loads the binary dictionary</div>
<div class="line"><span class="ts">[09:14:21]</span> <span class="ok">ok</span>    binary search finds every fixture</div>
<div class="line"><span class="ts">[09:14:21]</span> <span class="warn">FAIL</span>  rejects transliterated personal names</div>
<div class="line"><span class="ts">[09:14:21]</span> <span class="ok">ok</span>    base form fallback handles plurals</div>
<div class="line"><span class="ts">[09:14:22]</span> <span class="dim">18 checks, 1 failure, 0 skipped</span></div>
<div class="line"></div>
<div class="line"><span class="ts">[09:15:03]</span> <span class="cmd">$ wlocr --all --psm 11 screen.png</span></div>
<div class="line"><span class="ts">[09:15:04]</span> engine: <span class="path">D:/tools/tesseract/tesseract.exe</span></div>
<div class="line"><span class="ts">[09:15:04]</span> image: <span class="num">3840x2160</span>  scale=<span class="num">150%%</span>  psm=<span class="num">11</span></div>
<div class="line"><span class="ts">[09:15:10]</span> elapsed: <span class="num">6.12</span>s</div>
<div class="line"><span class="ts">[09:15:10]</span> tokens: <span class="num">842</span>  plausible english words: <span class="num">611</span></div>
<div class="line"><span class="ts">[09:15:10]</span> new words: <span class="num">47</span> (unique <span class="num">31</span>)</div>
<div class="line"><span class="ts">[09:15:10]</span>   * boundary</div>
<div class="line"><span class="ts">[09:15:10]</span>   * conformal</div>
<div class="line"><span class="ts">[09:15:10]</span>   * decimator</div>
<div class="line"><span class="ts">[09:15:10]</span>   * waveguide</div>
<div class="line"></div>
<div class="line"><span class="ts">[09:16:30]</span> <span class="err">error:</span> failed to open <span class="path">/home/wenjiazhi/src/raft_log.cc</span></div>
<div class="line"><span class="ts">[09:16:30]</span> <span class="warn">warning:</span> retry budget exhausted after <span class="num">3</span> attempts</div>
<div class="line"><span class="ts">[09:16:31]</span> connecting to <span class="path">ssh://build@10.0.4.18:22</span></div>
<div class="line"><span class="ts">[09:16:31]</span> host key fingerprint <span class="num">SHA256:q3Vt9Lx2pR7w</span></div>
<div class="line"><span class="ts">[09:16:32]</span> <span class="ok">connected</span> in <span class="num">412</span> ms</div>
<div class="line"><span class="ts">[09:16:32]</span> rsync transfer <span class="num">18.4</span> MB at <span class="num">6.3</span> MB/s</div>
<div class="line"><span class="ts">[09:16:40]</span> remote command exited with status <span class="num">0</span></div>
<div class="line"></div>
<div class="line"><span class="ts">[09:17:05]</span> <span class="cmd">$ git status --short</span></div>
<div class="line"><span class="ts">[09:17:05]</span>  M src/OverlayBar.cpp</div>
<div class="line"><span class="ts">[09:17:05]</span>  M src/TesseractOcr.cpp</div>
<div class="line"><span class="ts">[09:17:05]</span>  ?? tools/bench/</div>
<div class="line"><span class="ts">[09:17:06]</span> <span class="dim">3 files changed, 148 insertions, 27 deletions</span></div>
<div class="line"></div>
<div class="line"><span class="ts">[09:18:22]</span> <span class="cmd">$ ./WordLens.exe --capture-mode window</span></div>
<div class="line"><span class="ts">[09:18:23]</span> <span class="ok">ready</span>  dictionary <span class="num">193242</span> entries  engine tesseract <span class="num">5.5.3</span></div>
<div class="line"><span class="ts">[09:18:25]</span> scan <span class="num">1</span>  capture <span class="num">38</span> ms  ocr <span class="num">2140</span> ms  words <span class="num">12</span></div>
<div class="line"><span class="ts">[09:18:28]</span> scan <span class="num">2</span>  capture <span class="num">36</span> ms  ocr <span class="num">2088</span> ms  words <span class="num">14</span></div>
<div class="line"><span class="ts">[09:18:31]</span> scan <span class="num">3</span>  capture <span class="num">41</span> ms  ocr <span class="num">2305</span> ms  words <span class="num">15</span></div>
<div class="line"><span class="ts">[09:18:34]</span> <span class="warn">slow</span>    frame skipped  motion <span class="num">0.4%%</span>  static <span class="num">18</span> s</div>
</body></html>
"""

# --------------------------------------------------------------------------
# 6. Dense small UI text: 12px labels and code, light theme (hard case).
# --------------------------------------------------------------------------
PAGES["dense"] = """
<!doctype html><html><head><meta charset="utf-8"><title>dense</title><style>
%(base)s
body { background:#f3f4f6; color:#111827; font:12px/1.5 'Segoe UI',system-ui,sans-serif; }
.bar { background:#e5e7eb; padding:7px 16px; font-size:12px; color:#374151; display:flex; gap:24px; }
.main { display:flex; height:calc(100vh - 30px); }
.side { width:340px; background:#fafafa; border-right:1px solid #d1d5db; padding:8px 0; }
.side .item { padding:4px 18px; white-space:nowrap; }
.side .sel { background:#dbeafe; }
.side .head { color:#6b7280; font-size:11px; letter-spacing:.12em; padding:8px 18px 4px; }
.editor { flex:1; padding:14px 26px; background:#ffffff; font:12px/1.65 'Cascadia Mono',Consolas,monospace; }
.editor .ln { color:#9ca3af; display:inline-block; width:34px; text-align:right; margin-right:14px; }
.kw { color:#7c3aed; } .st { color:#047857; } .cm { color:#6b7280; font-style:italic; }
.ty { color:#b45309; } .nm { color:#1d4ed8; }
.status { position:absolute; bottom:0; left:0; right:0; background:#e5e7eb; font-size:12px; padding:6px 18px; color:#374151; border-top:1px solid #d1d5db; display:flex; gap:26px; }
</style></head><body>
<div class="bar"><span>File</span><span>Edit</span><span>View</span><span>Go</span><span>Run</span><span>Terminal</span><span>Help</span></div>
<div class="main">
<div class="side">
  <div class="head">EXPLORER</div>
  <div class="item">THOR_CORE [main]</div>
  <div class="item">&#9662; src</div>
  <div class="item sel">&nbsp;&nbsp;AttributeMgmt.cc</div>
  <div class="item">&nbsp;&nbsp;CellVar.cc</div>
  <div class="item">&nbsp;&nbsp;ComplexConstSeqOpt.cc</div>
  <div class="item">&nbsp;&nbsp;ComputeDFGopt.h</div>
  <div class="item">&nbsp;&nbsp;DFGResourceShare.cc</div>
  <div class="item">&nbsp;&nbsp;GroupDFG.h</div>
  <div class="item">&nbsp;&nbsp;MuxAndMerge.h</div>
  <div class="item">&nbsp;&nbsp;Operators.cpp</div>
  <div class="item">&nbsp;&nbsp;RainaSynth.cc</div>
  <div class="item">&nbsp;&nbsp;SequentialOpt.cc</div>
  <div class="item">&nbsp;&nbsp;SuperCell.cc</div>
  <div class="item">&nbsp;&nbsp;Tcl.cc</div>
  <div class="item">&nbsp;&nbsp;clang-format</div>
  <div class="item">&nbsp;&nbsp;AnalyzeDataPath.cc</div>
  <div class="item">&#9662; include</div>
  <div class="item">&nbsp;&nbsp;backpressure.h</div>
  <div class="item">&nbsp;&nbsp;graph_traits.h</div>
  <div class="item">&nbsp;&nbsp;instrumentation.h</div>
  <div class="item">&nbsp;&nbsp;waveform_view.h</div>
</div>
<div class="editor">
<div><span class="ln">101</span><span class="cm">// Compute the strongly connected components in one pass.</span></div>
<div><span class="ln">102</span><span class="kw">void</span> <span class="ty">DfgBuilder</span>::<span class="nm">FindCycles</span>(<span class="kw">const</span> <span class="ty">Graph</span>&amp; graph) {</div>
<div><span class="ln">103</span>    <span class="kw">for</span> (<span class="kw">const</span> <span class="ty">Node</span>&amp; node : graph.<span class="nm">Nodes</span>()) {</div>
<div><span class="ln">104</span>        <span class="kw">if</span> (node.<span class="nm">visited</span>) <span class="kw">continue</span>;</div>
<div><span class="ln">105</span>        <span class="nm">VisitDepthFirst</span>(node, graph);</div>
<div><span class="ln">106</span>    }</div>
<div><span class="ln">107</span>}</div>
<div><span class="ln">108</span></div>
<div><span class="ln">109</span><span class="cm">// Incremental update keeps the previous ordering stable.</span></div>
<div><span class="ln">110</span><span class="kw">void</span> <span class="ty">DfgBuilder</span>::<span class="nm">Refresh</span>() {</div>
<div><span class="ln">111</span>    <span class="ty">Graph</span> next = m_graph;</div>
<div><span class="ln">112</span>    <span class="nm">Canonicalize</span>(next);</div>
<div><span class="ln">113</span>    <span class="kw">if</span> (next == m_graph) <span class="kw">return</span>;</div>
<div><span class="ln">114</span>    m_graph = std::move(next);</div>
<div><span class="ln">115</span>    <span class="nm">EmitSignal</span>(<span class="st">"graph_changed"</span>);</div>
<div><span class="ln">116</span>}</div>
<div><span class="ln">117</span></div>
<div><span class="ln">118</span><span class="kw">const</span> <span class="ty">Node</span>* <span class="ty">Graph</span>::<span class="nm">Find</span>(<span class="kw">const</span> std::<span class="ty">string</span>&amp; name) <span class="kw">const</span> {</div>
<div><span class="ln">119</span>    <span class="kw">auto</span> it = m_index.<span class="nm">find</span>(name);</div>
<div><span class="ln">120</span>    <span class="kw">return</span> it == m_index.<span class="nm">end</span>() ? <span class="kw">nullptr</span> : &amp;m_nodes[it-&gt;second];</div>
<div><span class="ln">121</span>}</div>
<div><span class="ln">122</span></div>
<div><span class="ln">123</span><span class="cm">// Preconditions: the graph must be acyclic and every cell named.</span></div>
<div><span class="ln">124</span><span class="kw">bool</span> <span class="ty">DfgBuilder</span>::<span class="nm">Validate</span>() <span class="kw">const</span> {</div>
<div><span class="ln">125</span>    <span class="kw">if</span> (m_graph.<span class="nm">Empty</span>()) <span class="kw">return</span> <span class="kw">false</span>;</div>
<div><span class="ln">126</span>    <span class="kw">if</span> (!m_graph.<span class="nm">IsAcyclic</span>()) <span class="kw">return</span> <span class="kw">false</span>;</div>
<div><span class="ln">127</span>    <span class="kw">for</span> (<span class="kw">const</span> <span class="ty">Node</span>&amp; node : m_graph.<span class="nm">Nodes</span>())</div>
<div><span class="ln">128</span>        <span class="kw">if</span> (node.<span class="nm">label</span>.<span class="nm">empty</span>()) <span class="kw">return</span> <span class="kw">false</span>;</div>
<div><span class="ln">129</span>    <span class="kw">return</span> <span class="kw">true</span>;</div>
<div><span class="ln">130</span>}</div>
<div><span class="ln">131</span></div>
<div><span class="ln">132</span><span class="cm">// Timing summary for the last synthesis run.</span></div>
<div><span class="ln">133</span><span class="cm">// elapsed 8.42 s, memory 444.9 MB, iterations 6</span></div>
<div><span class="ln">134</span><span class="kw">void</span> <span class="ty">RainaSynth</span>::<span class="nm">Report</span>(std::ostream&amp; out) <span class="kw">const</span> {</div>
<div><span class="ln">135</span>    out &lt;&lt; <span class="st">"report area "</span> &lt;&lt; m_area &lt;&lt; <span class="st">" design "</span> &lt;&lt; m_design;</div>
<div><span class="ln">136</span>    out &lt;&lt; <span class="st">" version "</span> &lt;&lt; m_version &lt;&lt; std::endl;</div>
<div><span class="ln">137</span>    out &lt;&lt; <span class="st">" library "</span> &lt;&lt; m_library &lt;&lt; std::endl;</div>
<div><span class="ln">138</span>    out &lt;&lt; <span class="st">" command "</span> &lt;&lt; m_command &lt;&lt; std::endl;</div>
<div><span class="ln">139</span>}</div>
</div></div>
<div class="status"><span>main*</span><span>Ln 134, Col 27</span><span>Spaces: 4</span><span>UTF-8</span><span>C++</span><span>RainaSynth</span></div>
</body></html>
"""


def page_html(name: str) -> str:
    return PAGES[name] % {"base": BASE_CSS}


def find_edge() -> Path:
    for candidate in EDGE_CANDIDATES:
        if candidate.is_file():
            return candidate
    found = shutil.which("msedge")
    if found:
        return Path(found)
    raise SystemExit("Microsoft Edge was not found; it is needed to render the corpus")


def render(edge: Path, html_path: Path, png_path: Path) -> None:
    png_path.unlink(missing_ok=True)
    cmd = [
        str(edge),
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        "--no-first-run",
        "--disable-extensions",
        "--force-device-scale-factor=1.5",
        f"--window-size={VIEWPORT[0]},{VIEWPORT[1]}",
        f"--screenshot={png_path}",
        html_path.as_uri(),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=120)
    if not png_path.is_file():
        raise SystemExit(f"render failed for {html_path.name}:\n{result.stderr[-2000:]}")


def load_common(limit: int = 3000) -> set[str]:
    path = ROOT / "data" / "common-10000.txt"
    words = [line.strip().lower() for line in path.read_text(encoding="utf-8").splitlines()]
    return set(words[:limit])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-render", action="store_true", help="only write HTML and truth files")
    args = parser.parse_args()

    HTML_DIR.mkdir(parents=True, exist_ok=True)
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    TRUTH_DIR.mkdir(parents=True, exist_ok=True)

    edge = None if args.no_render else find_edge()
    common = load_common()
    manifest = []

    for name in PAGES:
        page = page_html(name)
        html_path = HTML_DIR / f"{name}.html"
        html_path.write_text(page, encoding="utf-8")

        tokens = truth_tokens(page)
        counts: dict[str, int] = {}
        for token in tokens:
            counts[token] = counts.get(token, 0) + 1

        truth = {
            "page": name,
            "tokens": tokens,
            "counts": counts,
            "unique": len(counts),
            "total": len(tokens),
            "rare": sorted({t for t in counts if t not in common}),
        }
        (TRUTH_DIR / f"{name}.json").write_text(
            json.dumps(truth, ensure_ascii=False, indent=1), encoding="utf-8")

        png_path = PNG_DIR / f"{name}.png"
        if not args.no_render:
            render(edge, html_path, png_path)
            if not png_path.is_file():
                return 1

        manifest.append({
            "page": name,
            "html": str(html_path.relative_to(ROOT)).replace("\\", "/"),
            "png": str(png_path.relative_to(ROOT)).replace("\\", "/"),
            "truth": str((TRUTH_DIR / f"{name}.json").relative_to(ROOT)).replace("\\", "/"),
            "unique_tokens": len(counts),
            "total_tokens": len(tokens),
            "rare_tokens": len(truth["rare"]),
        })
        print(f"{name:<10} tokens={len(tokens):>4}  unique={len(counts):>3}  rare={len(truth['rare']):>3}")

    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\nmanifest: {MANIFEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
