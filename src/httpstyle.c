/*
 * TASK R7 (D61) -- the new skin; Band 90 (2026-11-17) -- design-system
 * upgrade. One internal stylesheet, served INLINE in every page head
 * (no second request, no CDN, no external font or script: the
 * sandboxed preview and an air-gapped LAN render identically). Every
 * legacy class hook the page generators emit is kept: .menu(+selected
 * +disabled), .maintable, .infotable, .option, .infomenu,
 * .alt1/.alt2/.alt3, .success/.failed/.busy/.online/.offline,
 * .left/.center/.right, .connect_data, .sbutton, div.outer/right/
 * top/bottom (the editor's corner layout). Band 90 deepens the
 * system without touching any hook: richer palette with a dark
 * variant (theme toggle, persisted; OS preference honoured when the
 * operator never toggled), a gradient sticky menu, sticky table
 * headers, pill status chips, modern form controls and cards,
 * focus-visible rings, custom scrollbars, reduced-motion support,
 * and the phone swipe block. The operator's STYLESHEET FILE: config
 * still overrides the whole skin via the /style.css link branch.
 */
char style_css[] =
/* ---- palette + base ---- */
":root{--bg:#e9eef3;--bg-grad:linear-gradient(180deg,#f2f6fa,#e9eef3);--panel:#ffffff;--panel-2:#f6f8fa;--ink:#182635;--muted:#5b6b7b;"
"--line:#d3dce4;--line-strong:#b9c7d4;--accent:#0f5f8a;--accent-2:#12838f;--accent-grad:linear-gradient(135deg,#0f5f8a,#12838f);--accent-soft:#e2eef5;--good:#177245;"
"--bad:#c0392b;--warn:#96690d;--good-bg:#e2f2e7;--bad-bg:#fbe9e7;--warn-bg:#fbf0d9;--card-shadow:0 2px 10px rgba(16,35,55,.09),0 1px 3px rgba(16,35,55,.06);--radius:10px;}\n"
"[data-theme=dark]{--bg:#0d1319;--bg-grad:linear-gradient(180deg,#111a23,#0d1319);--panel:#18222d;--panel-2:#141d28;--ink:#e6edf3;--muted:#8b9bb4;"
"--line:#2b3949;--line-strong:#3a4d63;--accent:#3b93c9;--accent-2:#2dd4bf;--accent-grad:linear-gradient(135deg,#1d6fa5,#0f766e);--accent-soft:#1b2c40;--good:#3fb950;"
"--bad:#f85149;--warn:#d29922;--good-bg:#12351f;--bad-bg:#3d1a17;--warn-bg:#3a2d0c;--card-shadow:0 2px 10px rgba(0,0,0,.45);}\n"
"@media (prefers-color-scheme:dark){:root:not([data-theme]){--bg:#0d1319;--bg-grad:linear-gradient(180deg,#111a23,#0d1319);--panel:#18222d;--panel-2:#141d28;--ink:#e6edf3;--muted:#8b9bb4;--line:#2b3949;--line-strong:#3a4d63;--accent:#3b93c9;--accent-2:#2dd4bf;--accent-grad:linear-gradient(135deg,#1d6fa5,#0f766e);--accent-soft:#1b2c40;--good:#3fb950;--bad:#f85149;--warn:#d29922;--good-bg:#12351f;--bad-bg:#3d1a17;--warn-bg:#3a2d0c;--card-shadow:0 2px 10px rgba(0,0,0,.45);}}\n"
"*{box-sizing:border-box;}\n"
"::selection{background:var(--accent);color:#fff;}\n"
"body{margin:0;background:var(--bg-grad);background-color:var(--bg);color:var(--ink);"
"font:14px/1.5 system-ui,'Segoe UI',Tahoma,Arial,sans-serif;-webkit-font-smoothing:antialiased;}\n"
"a{color:var(--accent);text-decoration:none;font-weight:600;}\n"
"a:hover{text-decoration:underline;}\n"
"hr{border:0;border-top:1px solid var(--line);margin:14px 0;}\n"
"h1{font-size:21px;margin:14px 0 8px;color:var(--ink);letter-spacing:.2px;}\n"
"h2{font-size:16px;margin:16px 0 6px;color:var(--ink);}\n"
"h3{font-size:13.5px;margin:13px 0 5px;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;}\n"
"code,kbd{font-family:ui-monospace,'Cascadia Mono',Consolas,monospace;font-size:.92em;background:var(--accent-soft);padding:1px 5px;border-radius:5px;}\n"
/* ---- the menu bar ---- */
".menu{position:sticky;top:0;z-index:50;background:var(--panel);"
"border-bottom:2px solid var(--accent);box-shadow:0 2px 10px rgba(16,35,55,.13);"
"padding:5px 10px;overflow:hidden;}\n"
".menu ul{margin:0;padding:0;display:flex;flex-wrap:wrap;"
"align-items:center;list-style:none;gap:2px;}\n"
".menu li{margin:2px 1px;}\n"
".menu li a{display:block;padding:6px 12px;border-radius:8px;"
"font-weight:600;font-size:13px;color:var(--ink);transition:background .15s,color .15s,box-shadow .15s;}\n"
".menu li a:hover{background:var(--accent-soft);text-decoration:none;box-shadow:inset 0 0 0 1px var(--line);}\n"
".menu a.selected{background:var(--accent-grad);color:#fff;box-shadow:0 2px 8px rgba(15,95,138,.35);}\n"
".menu a.selected:hover{background:var(--accent-grad);color:#fff;}\n"
".menu a.disabled{color:#a7b5c1;text-decoration:line-through;pointer-events:none;}\n"
".menu span{float:right;color:var(--muted);font-size:11px;padding:8px 2px;}\n"
"html[dir=rtl] .menu span{float:left !important;}\n"
/* ---- tables ---- */
"table{border-collapse:separate;border-spacing:0;}\n"
"td,th{font-size:12.5px;padding:5px 8px;}\n"
".maintable{background:var(--panel);font-size:12.5px;width:auto;border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--card-shadow);overflow:hidden;}\n"
".maintable th{padding:8px 9px;text-align:left;background:var(--accent-grad);color:#fff;"
"font-size:11px;text-transform:uppercase;letter-spacing:.5px;border-bottom:2px solid var(--accent);position:sticky;top:0;}\n"
".maintable tr{transition:background .12s;}\n"
".maintable tr:hover{background-color:var(--accent-soft);}\n"
".maintable td{border-bottom:1px solid var(--line);padding:5px 9px;}\n"
".maintable tr:last-child td{border-bottom:0;}\n"
".maintable a{font-weight:700;}\n"
".infotable{width:100%;background:var(--panel);border:1px solid var(--line);"
"border-radius:var(--radius);margin:8px 0;box-shadow:var(--card-shadow);overflow:hidden;}\n"
".infotable th{padding:7px;font-size:11px;text-align:center;background:var(--accent-grad);"
"color:#fff;border-bottom:2px solid var(--accent);text-transform:uppercase;letter-spacing:.4px;}\n"
".infotable tr:hover{background-color:var(--accent-soft);}\n"
".infotable td{border-bottom:1px solid var(--line);}\n"
".infotable .left{text-transform:uppercase;font-weight:700;color:var(--muted);font-size:11px;letter-spacing:.4px;}\n"
".option{background:var(--panel-2);border-radius:var(--radius);}\n"
".option th,.option td{font-size:11.5px;background:transparent;color:var(--warn);}\n"
".option th{border-bottom:1px solid var(--line);text-align:left;}\n"
".infomenu{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--card-shadow);}\n"
".infomenu td{padding:3px 5px;}\n"
/* ---- row stripes + status words ---- */
".alt1{background-color:var(--panel);}\n"
".alt2{background-color:var(--panel-2);}\n"
".alt3{background-color:var(--good-bg);}\n"
".success{color:var(--good);font-weight:700;}\n"
".failed{color:var(--bad);font-weight:700;}\n"
".busy,.online,.offline{display:inline-block;min-width:58px;padding:2px 9px;border-radius:999px;font-size:11px;font-weight:700;text-align:center;color:#fff;}\n"
".busy{background-color:#8a9a12;}\n"
".online{background-color:var(--good);}\n"
".offline{background-color:var(--bad);}\n"
".left{text-align:left;}.center{text-align:center;}.right{text-align:right;}\n"
/* ---- hover card on connection icons (legacy geometry kept) ---- */
".online:hover .connect_data,.offline:hover .connect_data,"
".busy:hover .connect_data{visibility:visible;}\n"
".connect_data{border-collapse:collapse;padding:0;margin:-1px 0 0 55px;"
"position:absolute;visibility:hidden;border:1px solid var(--line);border-radius:8px;overflow:hidden;"
"font-size:11px;background:var(--panel);color:var(--ink);"
"box-shadow:0 8px 22px rgba(16,35,55,.22);z-index:60;}\n"
".connect_data td{background-color:var(--accent-soft);"
"border-bottom:1px solid var(--line);padding:3px 10px;}\n"
/* ---- forms ---- */
"input,textarea,select{font:inherit;font-size:13px;padding:6px 9px;border:1px solid var(--line-strong);"
"border-radius:8px;background:var(--panel);color:var(--ink);transition:border-color .15s,box-shadow .15s;}\n"
"input:focus,textarea:focus,select:focus{outline:none;"
"border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft);}\n"
"input:focus-visible,textarea:focus-visible,select:focus-visible,button:focus-visible,a:focus-visible{outline:2px solid var(--accent);outline-offset:1px;}\n"
"input,select{margin:4px 2px;}\n"
"textarea{width:100%;height:81%;margin:5px 0;padding:9px;font-family:ui-monospace,Consolas,monospace;font-size:12.5px;line-height:1.5;}\n"
"button,.sbutton{font-weight:700;background:var(--accent-grad);color:#fff;"
"border:1px solid transparent;border-radius:8px;cursor:pointer;padding:6px 14px;box-shadow:0 2px 6px rgba(15,95,138,.25);transition:filter .15s,transform .1s;}\n"
"button:hover,.sbutton:hover{filter:brightness(1.12);}\n"
"button:active,.sbutton:active{transform:translateY(1px);}\n"
"button:disabled,.sbutton:disabled{opacity:.55;cursor:not-allowed;box-shadow:none;}\n"
"fieldset{border:1px solid var(--line);border-radius:var(--radius);background:var(--panel);padding:10px 14px;margin:8px 0;}\n"
"legend{font-weight:700;color:var(--accent);padding:0 6px;}\n"
"label{font-weight:600;color:var(--muted);font-size:12.5px;}\n"
/* ---- editor corner layout (legacy geometry, do not touch) ---- */
"div.outer{position:relative;width:100%;}\n"
"div.right{width:100%;margin-left:50%;}\n"
"div.top,div.bottom{position:absolute;width:50%;}\n"
"div.top{top:0;}\n"
"div.bottom{bottom:0;}\n"
/* ---- ECM performance cards + theme toggle ---- */
".perf-cards{display:flex;flex-wrap:wrap;gap:12px;margin:12px 0;}\n"
".perf-card{background:var(--panel);border:1px solid var(--line);border-top:3px solid var(--accent);border-radius:12px;padding:14px 18px;min-width:160px;flex:1;box-shadow:var(--card-shadow);transition:transform .18s,box-shadow .18s;}\n"
".perf-card:hover{transform:translateY(-3px);box-shadow:0 8px 20px rgba(16,35,55,.14);}\n"
".perf-card .label{font-size:11px;text-transform:uppercase;letter-spacing:.6px;color:var(--muted);margin-bottom:4px;font-weight:700;}\n"
".perf-card .value{font-size:25px;font-weight:800;color:var(--ink);font-variant-numeric:tabular-nums;}\n"
".perf-card .sub{font-size:11.5px;color:var(--muted);margin-top:2px;}\n"
".perf-card.good{border-top-color:var(--good);}\n"
".perf-card.good .value{color:var(--good);}\n"
".perf-card.bad{border-top-color:var(--bad);}\n"
".perf-card.bad .value{color:var(--bad);}\n"
".perf-card.warn{border-top-color:var(--warn);}\n"
".perf-card.warn .value{color:var(--warn);}\n"
".theme-toggle{cursor:pointer;padding:6px 10px !important;}\n"
/* ---- scrollbars ---- */
"::-webkit-scrollbar{width:10px;height:10px;}\n"
"::-webkit-scrollbar-thumb{background:var(--line-strong);border-radius:8px;}\n"
"::-webkit-scrollbar-track{background:transparent;}\n"
"@media (prefers-reduced-motion:reduce){*{transition:none !important;animation:none !important;}}\n"
/* ---- phone: the tables swipe, the brand caption hides ---- */
"@media (max-width:760px){\n"
" body{font-size:13px;}\n"
" td,th{font-size:11px;padding:3px 5px;}\n"
" .menu li a{padding:5px 9px;font-size:12px;}\n"
" .menu span{display:none;}\n"
" .maintable,.infotable,.option{display:block;overflow-x:auto;"
" -webkit-overflow-scrolling:touch;white-space:nowrap;}\n"
" div.right{margin-left:0;}\n"
" div.top,div.bottom{position:static;width:100%;}\n"
" .perf-cards{gap:8px;}\n"
" .perf-card{min-width:130px;padding:10px 12px;}\n"
" .perf-card .value{font-size:20px;}\n"
"}\n"
;
