/*
 * TASK R7 (D61) -- the new skin; Band 90 -- design-system upgrade;
 * Band 91 (2026-11-17) -- professional per-page/component design.
 * One internal stylesheet, served INLINE in every page head (no
 * second request, no CDN, no external font or script: the sandboxed
 * preview and an air-gapped LAN render identically). Every legacy
 * class hook the page generators emit is kept and restyled:
 * .menu(+selected+disabled+.brand), .maintable, .infotable, .module,
 * .option, .infomenu, .alt1/.alt2/.alt3, .success/.failed/.busy/
 * .online/.offline, .left/.center/.right, .connect_data, .sbutton,
 * .button, div.outer/right/top/bottom (the editor's corner layout),
 * .perf-cards/.perf-card(+good/bad/warn/.sys). Band 91 turns the
 * skin into an app shell: a dark gradient navbar with per-section
 * icons and a brand badge, card tables with zebra rows and sticky
 * headers under the navbar, status chips with live dots, a toolbar
 * card for the autorefresh bar, a dark code-editor theme for the
 * config editor, KPI cards with gradient accents, secondary
 * buttons, styled legacy border tables and <pre> blocks, print
 * rules, and a horizontally scrolling nav on phones. The operator's
 * STYLESHEET FILE: config still overrides the whole skin via the
 * /style.css link branch.
 */
char style_css[] =
/* ---- palette + base ---- */
":root{--bg:#e8edf2;--bg-grad:linear-gradient(180deg,#f4f7fa,#e8edf2 55%,#e2e9f0);--panel:#ffffff;--panel-2:#f4f7fa;--ink:#16222f;--muted:#5b6b7b;"
"--line:#d3dce4;--line-strong:#b9c7d4;--accent:#0f5f8a;--accent-2:#12838f;--accent-grad:linear-gradient(135deg,#0f5f8a,#12838f);--accent-soft:#e2eef5;--good:#177245;"
"--bad:#c0392b;--warn:#96690d;--good-bg:#e2f2e7;--bad-bg:#fbe9e7;--warn-bg:#fbf0d9;--nav-grad:linear-gradient(135deg,#0b2f47,#0f5f8a 60%,#12838f);"
"--card-shadow:0 2px 10px rgba(16,35,55,.09),0 1px 3px rgba(16,35,55,.06);--lift-shadow:0 10px 24px rgba(16,35,55,.16);--radius:10px;}\n"
"[data-theme=dark]{--bg:#0d1319;--bg-grad:linear-gradient(180deg,#111a23,#0d1319);--panel:#18222d;--panel-2:#141d28;--ink:#e6edf3;--muted:#8b9bb4;"
"--line:#2b3949;--line-strong:#3a4d63;--accent:#3b93c9;--accent-2:#2dd4bf;--accent-grad:linear-gradient(135deg,#1d6fa5,#0f766e);--accent-soft:#1b2c40;--good:#3fb950;"
"--bad:#f85149;--warn:#d29922;--good-bg:#12351f;--bad-bg:#3d1a17;--warn-bg:#3a2d0c;--nav-grad:linear-gradient(135deg,#0a1826,#123c5c 60%,#0d4f4a);--card-shadow:0 2px 10px rgba(0,0,0,.45);--lift-shadow:0 10px 24px rgba(0,0,0,.55);}\n"
"@media (prefers-color-scheme:dark){:root:not([data-theme]){--bg:#0d1319;--bg-grad:linear-gradient(180deg,#111a23,#0d1319);--panel:#18222d;--panel-2:#141d28;--ink:#e6edf3;--muted:#8b9bb4;--line:#2b3949;--line-strong:#3a4d63;--accent:#3b93c9;--accent-2:#2dd4bf;--accent-grad:linear-gradient(135deg,#1d6fa5,#0f766e);--accent-soft:#1b2c40;--good:#3fb950;--bad:#f85149;--warn:#d29922;--good-bg:#12351f;--bad-bg:#3d1a17;--warn-bg:#3a2d0c;--nav-grad:linear-gradient(135deg,#0a1826,#123c5c 60%,#0d4f4a);--card-shadow:0 2px 10px rgba(0,0,0,.45);--lift-shadow:0 10px 24px rgba(0,0,0,.55);}}\n"
"*{box-sizing:border-box;}\n"
"::selection{background:var(--accent);color:#fff;}\n"
"body{margin:0;padding:0 16px 22px;background:var(--bg-grad);background-color:var(--bg);color:var(--ink);"
"font:14px/1.5 system-ui,'Segoe UI',Tahoma,Arial,sans-serif;-webkit-font-smoothing:antialiased;}\n"
"a{color:var(--accent);text-decoration:none;font-weight:600;}\n"
"a:hover{text-decoration:underline;}\n"
"a img{vertical-align:middle;}\n"
"hr{border:0;border-top:1px solid var(--line);margin:14px 0;}\n"
"h1{font-size:22px;margin:16px 0 8px;color:var(--ink);letter-spacing:.2px;}\n"
"h2{font-size:16.5px;margin:20px 0 8px;color:var(--ink);padding:0 0 6px 10px;border-bottom:2px solid var(--line);position:relative;}\n"
"h2::before{content:'';position:absolute;left:0;top:2px;bottom:8px;width:4px;border-radius:3px;background:var(--accent-grad);}\n"
"html[dir=rtl] h2{padding:0 10px 6px 0;}\n"
"html[dir=rtl] h2::before{left:auto;right:0;}\n"
"h3{font-size:13px;margin:14px 0 5px;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;}\n"
"code,kbd{font-family:ui-monospace,'Cascadia Mono',Consolas,monospace;font-size:.92em;background:var(--accent-soft);padding:1px 5px;border-radius:5px;}\n"
"pre{background:#0d1520;color:#d7e6f5;border:1px solid #22344c;border-radius:10px;padding:12px 14px;overflow:auto;font:12.5px/1.55 ui-monospace,Consolas,monospace;box-shadow:var(--card-shadow);}\n"
/* ---- the navbar: dark gradient app bar with brand + icons ---- */
".menu{position:sticky;top:0;z-index:50;margin:0 -16px 14px;background:var(--nav-grad);"
"box-shadow:0 3px 14px rgba(8,30,48,.35);padding:6px 14px;}\n"
/* Band 98: one-row nav that scrolls sideways at EVERY width. The old
 * wrapping nav grew to 2-3 rows on medium screens, so sticky table
 * headers (fixed top:52px) slid underneath it and overlapped. */
".menu ul{margin:0;padding:0;display:flex;flex-wrap:nowrap;overflow-x:auto;scrollbar-width:thin;"
"align-items:center;list-style:none;gap:2px;}\n"
".menu li{margin:2px 1px;}\n"
".menu li a{display:block;padding:6px 11px;border-radius:8px;"
"font-weight:600;font-size:12.5px;color:#dcebf5;transition:background .15s,color .15s,box-shadow .15s;white-space:nowrap;}\n"
".menu li a:hover{background:rgba(255,255,255,.14);color:#fff;text-decoration:none;}\n"
".menu a.selected{background:rgba(255,255,255,.92);color:#0f5f8a;box-shadow:0 2px 8px rgba(0,0,0,.25);}\n"
".menu a.selected:hover{background:#fff;color:#0f5f8a;}\n"
".menu a.disabled{color:rgba(220,235,245,.45);text-decoration:line-through;pointer-events:none;}\n"
".menu span,.menu .brand{float:right;color:#fff;font-size:12px;font-weight:800;letter-spacing:.4px;"
"padding:7px 12px;margin:2px 0 2px 8px;background:rgba(255,255,255,.13);border:1px solid rgba(255,255,255,.25);border-radius:999px;}\n"
"html[dir=rtl] .menu span,html[dir=rtl] .menu .brand{float:left !important;}\n"
".menu a[href='/']::before{content:'\\1F3E0 ';}"
".menu a[href='/debug']::before{content:'\\1F41B ';}"
".menu a[href='/threads']::before{content:'\\1F9F5 ';}"
".menu a[href='/servers']::before{content:'\\1F5C4\\FE0F ';}"
".menu a[href='/cache']::before{content:'\\1F4BE ';}"
".menu a[href='/biss']::before{content:'\\1F511 ';}"
".menu a[href='/profiles']::before{content:'\\1F4C2 ';}"
".menu a[href='/newcamd']::before{content:'\\1F4E1 ';}"
".menu a[href='/cacheex']::before{content:'\\1F504 ';}"
".menu a[href='/cwlog']::before{content:'\\1F4CB ';}"
".menu a[href='/smartfilter']::before{content:'\\1F6E1\\FE0F ';}"
".menu a[href='/editor']::before{content:'\\270F\\FE0F ';}"
".menu a[href='/restart']::before{content:'\\1F501 ';}"
".menu a[href='/diagnostics']::before{content:'\\1FA7A ';}"
".menu a[href='/debug-ecm']::before{content:'\\1F50D ';}"
".menu a[href='/server-stats']::before{content:'\\1F4CA ';}"
".menu a[href='/log-viewer']::before{content:'\\1F4DC ';}"
".menu a[href='/user-trace']::before{content:'\\1F4E1 ';}"
".menu a[href='/cache-analytics']::before{content:'\\1F4C8 ';}"
".menu a[href='/auto-tuning']::before{content:'\\1F39A\\FE0F ';}"
".menu a[href='/logout']::before{content:'\\1F6AA ';}"
".theme-toggle{cursor:pointer;}\n"
/* ---- the autorefresh toolbar ---- */
"div[align=right]{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);"
"box-shadow:var(--card-shadow);padding:6px 12px;margin:0 0 12px;color:var(--muted);font-size:12.5px;font-weight:600;}\n"
"div[align=right] select{margin:0 0 0 6px;}\n"
/* ---- tables: cards with zebra rows + sticky headers ---- */
"table{border-collapse:separate;border-spacing:0;}\n"
"td,th{font-size:12.5px;padding:5px 8px;}\n"
"table[border]{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--card-shadow);overflow:hidden;}\n"
"table[border] td{border-bottom:1px solid var(--line);padding:7px 10px;}\n"
".maintable,.module{background:var(--panel);font-size:12.5px;width:auto;border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--card-shadow);overflow:hidden;}\n"
".maintable th,.module th{padding:8px 9px;text-align:left;background:var(--accent-grad);color:#fff;"
"font-size:11px;text-transform:uppercase;letter-spacing:.5px;border-bottom:2px solid var(--accent);position:sticky;top:46px;z-index:5;}\n"
".maintable tr,.module tr{transition:background .12s;}\n"
".maintable tr:nth-child(even),.module tr:nth-child(even){background-color:var(--panel-2);}\n"
".maintable tr:hover,.module tr:hover{background-color:var(--accent-soft);}\n"
".maintable td,.module td{border-bottom:1px solid var(--line);padding:5px 9px;font-variant-numeric:tabular-nums;}\n"
".maintable tr:last-child td,.module tr:last-child td{border-bottom:0;}\n"
".maintable a{font-weight:700;}\n"
".infotable{width:100%;background:var(--panel);border:1px solid var(--line);"
"border-radius:var(--radius);margin:8px 0;box-shadow:var(--card-shadow);overflow:hidden;}\n"
".infotable th{padding:7px;font-size:11px;text-align:center;background:var(--accent-grad);"
"color:#fff;border-bottom:2px solid var(--accent);text-transform:uppercase;letter-spacing:.4px;}\n"
".infotable tr:nth-child(even){background-color:var(--panel-2);}\n"
".infotable tr:hover{background-color:var(--accent-soft);}\n"
".infotable td{border-bottom:1px solid var(--line);font-variant-numeric:tabular-nums;}\n"
".infotable .left{text-transform:uppercase;font-weight:700;color:var(--muted);font-size:11px;letter-spacing:.4px;}\n"
".option{background:var(--panel-2);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--card-shadow);overflow:hidden;}\n"
".option th,.option td{font-size:11.5px;background:transparent;color:var(--warn);}\n"
".option th{border-bottom:1px solid var(--line);text-align:left;background:var(--warn-bg);}\n"
".option tr:nth-child(even){background:rgba(127,127,127,.05);}\n"
".infomenu{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--card-shadow);}\n"
".infomenu td{padding:3px 5px;}\n"
/* ---- row stripes + status chips with live dots ---- */
".alt1{background-color:var(--panel);}\n"
".alt2{background-color:var(--panel-2);}\n"
".alt3{background-color:var(--good-bg);}\n"
".success{color:var(--good);font-weight:700;}\n"
".failed{color:var(--bad);font-weight:700;}\n"
".busy,.online,.offline{display:inline-flex;align-items:center;gap:6px;min-width:58px;padding:2px 10px;border-radius:999px;font-size:11px;font-weight:700;text-align:center;color:#fff;box-shadow:inset 0 -1px 0 rgba(0,0,0,.18);}\n"
".busy::before,.online::before,.offline::before{content:'';width:7px;height:7px;border-radius:50%;background:#fff;flex:none;}\n"
".busy{background-color:#8a9a12;}\n"
".online{background-color:var(--good);}\n"
".online::before{animation:mcsPulse 1.8s ease-in-out infinite;}\n"
".offline{background-color:var(--bad);}\n"
/* Band 99: status classes on a table CELL (the profiles port cell,
 * server/client status cells) must not become pills: the old
 * inline-flex pill stretched across the cell, and a link inside it
 * (the port number) rendered accent-blue on the green/red pill --
 * the clash the operator reported. A status cell is now a softly
 * tinted table cell whose text AND links take the status color on
 * a matching pale background (contrast-checked in both themes). */
"td.online,td.offline,td.busy{display:table-cell;min-width:0;border-radius:0;box-shadow:none;text-align:center;font-weight:800;}\n"
"td.online{background:var(--good-bg);color:var(--good);}\n"
"td.offline{background:var(--bad-bg);color:var(--bad);}\n"
"td.busy{background:var(--warn-bg);color:var(--warn);}\n"
"td.online a{color:var(--good);}\n"
"td.offline a{color:var(--bad);}\n"
"td.busy a{color:var(--warn);}\n"
"td.online::before,td.offline::before,td.busy::before{background:currentColor;}\n"
"@keyframes mcsPulse{0%,100%{opacity:1;}50%{opacity:.35;}}\n"
".left{text-align:left;}.center{text-align:center;}.right{text-align:right;}\n"
/* Band 100: was an inline style= on hundreds of generated spans. */
".pct{float:right;font-variant-numeric:tabular-nums;}\n"
/* Band 93: legacy generators hardcode dark-red provider names
 * (<font color=#CC3300>) that sink into dark panels; lift them in
 * the dark theme (explicit and OS-driven) so text never vanishes. */
"[data-theme=dark] font[color='#CC3300'],[data-theme=dark] font[color='#cc3300']{color:#ff8a75;}\n"
"@media (prefers-color-scheme:dark){:root:not([data-theme=light]) font[color='#CC3300'],:root:not([data-theme=light]) font[color='#cc3300']{color:#ff8a75;}}\n"
/* Band 98: same lift for legacy pure-red <font> text (sinks on dark). */
"[data-theme=dark] font[color='#ff0000'],[data-theme=dark] font[color='red']{color:#ff7b72;}\n"
"@media (prefers-color-scheme:dark){:root:not([data-theme=light]) font[color='#ff0000'],:root:not([data-theme=light]) font[color='red']{color:#ff7b72;}}\n"
"img{max-width:100%;height:auto;}\n"
/* ---- hover card on connection icons (legacy geometry kept) ---- */
".online:hover .connect_data,.offline:hover .connect_data,"
".busy:hover .connect_data{visibility:visible;opacity:1;transform:translateY(0);}\n"
".connect_data{border-collapse:collapse;padding:0;margin:-1px 0 0 55px;"
"position:absolute;visibility:hidden;opacity:0;transform:translateY(3px);transition:opacity .15s,transform .15s;border:1px solid var(--line);border-radius:8px;overflow:hidden;"
"font-size:11px;background:var(--panel);color:var(--ink);"
"box-shadow:var(--lift-shadow);z-index:60;}\n"
".connect_data td{background-color:var(--accent-soft);"
"border-bottom:1px solid var(--line);padding:3px 10px;}\n"
/* ---- forms + buttons ---- */
"input,textarea,select{font:inherit;font-size:13px;padding:6px 9px;border:1px solid var(--line-strong);"
"border-radius:8px;background:var(--panel);color:var(--ink);transition:border-color .15s,box-shadow .15s;}\n"
"input:hover,textarea:hover,select:hover{border-color:var(--accent);}\n"
"input:focus,textarea:focus,select:focus{outline:none;"
"border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft);}\n"
"input:focus-visible,textarea:focus-visible,select:focus-visible,button:focus-visible,a:focus-visible{outline:2px solid var(--accent);outline-offset:1px;}\n"
"input,select{margin:4px 2px;}\n"
"input[type=checkbox],input[type=radio]{accent-color:var(--accent);width:15px;height:15px;vertical-align:-2px;}\n"
"textarea{width:100%;height:81%;margin:5px 0;padding:12px;background:#0d1520;color:#d7e6f5;caret-color:#7fd4ff;border:1px solid #22344c;"
"font-family:ui-monospace,'Cascadia Mono',Consolas,monospace;font-size:12.5px;line-height:1.55;border-radius:10px;box-shadow:var(--card-shadow);tab-size:4;}\n"
"textarea:focus{border-color:var(--accent-2);box-shadow:0 0 0 3px rgba(18,131,143,.35);}\n"
"button,.sbutton,input[type=submit],input[type=button].sbutton{font-weight:700;background:var(--accent-grad);color:#fff;"
"border:1px solid transparent;border-radius:8px;cursor:pointer;padding:6px 14px;box-shadow:0 2px 6px rgba(15,95,138,.25);transition:filter .15s,transform .1s,box-shadow .15s;}\n"
"button:hover,.sbutton:hover,input[type=submit]:hover{filter:brightness(1.12);box-shadow:0 4px 12px rgba(15,95,138,.3);}\n"
"button:active,.sbutton:active,input[type=submit]:active{transform:translateY(1px);}\n"
"button:disabled,.sbutton:disabled,input[type=submit]:disabled{opacity:.55;cursor:not-allowed;box-shadow:none;}\n"
".button,input[type=button].button{font-weight:600;background:var(--panel);color:var(--accent);border:1px solid var(--line-strong);"
"border-radius:8px;cursor:pointer;padding:6px 14px;transition:background .15s,border-color .15s;box-shadow:none;}\n"
".button:hover,input[type=button].button:hover{background:var(--accent-soft);border-color:var(--accent);filter:none;}\n"
"input[type=submit]{padding:8px 20px;font-size:13.5px;}\n"
"fieldset{border:1px solid var(--line);border-radius:var(--radius);background:var(--panel);padding:12px 16px;margin:10px 0;box-shadow:var(--card-shadow);}\n"
"legend{font-weight:800;color:var(--accent);padding:0 8px;font-size:13px;}\n"
"label{font-weight:600;color:var(--muted);font-size:12.5px;}\n"
/* ---- editor corner layout (legacy geometry, do not touch) ---- */
"div.outer{position:relative;width:100%;}\n"
"div.right{width:100%;margin-left:50%;}\n"
"div.top,div.bottom{position:absolute;width:50%;}\n"
"div.top{top:0;}\n"
"div.bottom{bottom:0;}\n"
/* ---- KPI cards (home + SmartFilter + ECM performance) ---- */
".perf-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;margin:14px 0;}\n"
".perf-card{background:var(--panel);border:1px solid var(--line);border-top:3px solid var(--accent);border-radius:12px;padding:15px 18px;box-shadow:var(--card-shadow);transition:transform .18s,box-shadow .18s;}\n"
".perf-card:hover{transform:translateY(-3px);box-shadow:var(--lift-shadow);}\n"
".perf-card .label{font-size:11px;text-transform:uppercase;letter-spacing:.6px;color:var(--muted);margin-bottom:4px;font-weight:700;}\n"
".perf-card .value{font-size:25px;font-weight:800;color:var(--ink);font-variant-numeric:tabular-nums;line-height:1.15;}\n"
".perf-card .sub{font-size:11.5px;color:var(--muted);margin-top:2px;}\n"
".perf-card.good{border-top-color:var(--good);}\n"
".perf-card.good .value{color:var(--good);}\n"
".perf-card.bad{border-top-color:var(--bad);}\n"
".perf-card.bad .value{color:var(--bad);}\n"
".perf-card.warn{border-top-color:var(--warn);}\n"
".perf-card.warn .value{color:var(--warn);}\n"
".perf-cards.sys{grid-template-columns:repeat(auto-fit,minmax(128px,1fr));gap:9px;margin:10px 0 4px;}\n"
".perf-cards.sys .perf-card{padding:10px 13px;border-top-width:2px;}\n"
".perf-cards.sys .value{font-size:16.5px;}\n"
".perf-cards.sys .label{margin-bottom:2px;}\n"
/* ---- Band 99: home control-center hero + live badge ---- */
".home-hero{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px 18px;"
"background:var(--nav-grad);border-radius:14px;padding:16px 20px;margin:2px 0 14px;color:#eaf4fb;"
"box-shadow:0 8px 22px rgba(8,30,48,.28);border:1px solid rgba(255,255,255,.14);}\\n"
".hero-title{font-size:21px;font-weight:800;letter-spacing:.3px;color:#fff;line-height:1.2;}\\n"
".hero-sub{font-size:12.5px;color:#c4dcee;margin-top:3px;font-variant-numeric:tabular-nums;}\\n"
".hero-chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:9px;}\\n"
".hero-chip{background:rgba(255,255,255,.13);border:1px solid rgba(255,255,255,.22);color:#eaf4fb;"
"border-radius:999px;padding:2px 11px;font-size:11.5px;font-weight:700;}\\n"
".live-badge{display:inline-flex;align-items:center;gap:8px;background:rgba(0,0,0,.28);"
"border:1px solid rgba(255,255,255,.25);border-radius:999px;padding:6px 14px;font-weight:800;"
"font-size:12px;letter-spacing:1.2px;color:#fff;font-variant-numeric:tabular-nums;}\\n"
".live-dot{width:9px;height:9px;border-radius:50%;background:#3fb950;box-shadow:0 0 0 3px rgba(63,185,80,.25);animation:mcsPulse 1.6s ease-in-out infinite;}\\n"
".home-sec{font-size:12px;font-weight:800;text-transform:uppercase;letter-spacing:.8px;color:var(--muted);margin:16px 0 6px;}\\n"
"@media (max-width:760px){.home-hero{padding:13px 14px;}.hero-title{font-size:18px;}}\\n"
/* ---- scrollbars ---- */
"::-webkit-scrollbar{width:10px;height:10px;}\n"
"::-webkit-scrollbar-thumb{background:var(--line-strong);border-radius:8px;}\n"
"::-webkit-scrollbar-thumb:hover{background:var(--accent);}\n"
"::-webkit-scrollbar-track{background:transparent;}\n"
"@media (prefers-reduced-motion:reduce){*{transition:none !important;animation:none !important;}}\n"
"@media print{.menu,div[align=right]{display:none !important;}body{padding:0;}}\n"
/* ---- Band 98: medium screens -- data tables swipe instead of
 * overflowing the page (this used to exist only under 760px, so
 * 761-1100px tablets/laptops pushed wide tables off-screen). ---- */
"@media (max-width:1100px){\\n"
" .maintable,.infotable,.option,.module{display:block;overflow-x:auto;-webkit-overflow-scrolling:touch;max-width:100%;}\\n"
" .connect_data{max-width:82vw;margin-left:8px;}\\n"
"}\\n"
/* ---- phone: the nav scrolls sideways, the tables swipe ---- */
"@media (max-width:760px){\n"
" body{font-size:13px;padding:0 8px 16px;}\n"
" td,th{font-size:11px;padding:3px 5px;}\n"
" .menu{margin:0 -8px 10px;padding:5px 8px;}\n"
" .menu ul{flex-wrap:nowrap;overflow-x:auto;-webkit-overflow-scrolling:touch;}\n"
" .menu li a{padding:5px 9px;font-size:12px;}\n"
" .menu span,.menu .brand{display:none;}\n"
" .maintable,.infotable,.option,.module{display:block;overflow-x:auto;"
" -webkit-overflow-scrolling:touch;white-space:nowrap;}\n"
" div.right{margin-left:0;}\n"
" div.top,div.bottom{position:static;width:100%;}\n"
" .perf-cards{grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;}\n"
" .perf-card{padding:10px 12px;}\n"
" .perf-card .value{font-size:20px;}\n"
"}\n"
;
