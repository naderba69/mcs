/*
 * TASK R7 (D61) -- the new skin. One internal stylesheet, served INLINE in
 * every page head (no second request, no CDN, no external font or script:
 * the sandboxed preview and an air-gapped LAN render identically). The
 * legacy class hooks every page generator already emits are ALL kept:
 * .menu(+selected+disabled), .maintable, .infotable, .option, .infomenu,
 * .alt1/.alt2/.alt3, .success/.failed/.busy/.online/.offline,
 * .left/.center/.right, .connect_data, .sbutton, div.outer/right/top/bottom
 * (the editor's corner layout). New in this skin: viewport responsiveness
 * (tables become swipeable blocks on phones), an RTL block driven by
 * <html dir=rtl lang=ar> (the Arabic toggle writes that), and CSS
 * variables for the palette. The operator's STYLESHEET FILE: config still
 * overrides the whole skin via the /style.css link branch.
 */
char style_css[] =
/* ---- palette + base ---- */
":root{--bg:#eef2f6;--panel:#ffffff;--ink:#1c2733;--muted:#5b6b7b;"
"--line:#d5dde5;--accent:#0f5f8a;--accent-soft:#e3eef5;--good:#1e7a46;"
"--bad:#c0392b;--warn:#9a6b0f;}\n"
"*{box-sizing:border-box;}\n"
"body{margin:0;background:var(--bg);color:var(--ink);"
"font:14px/1.45 system-ui,'Segoe UI',Tahoma,Arial,sans-serif;}\n"
"a{color:var(--accent);text-decoration:none;}\n"
"a:hover{text-decoration:underline;}\n"
"hr{border:0;border-top:1px solid var(--line);}\n"
/* ---- the menu bar ---- */
".menu{position:sticky;top:0;z-index:5;background:var(--panel);"
"border-bottom:1px solid var(--line);box-shadow:0 1px 4px rgba(16,35,55,.08);"
"padding:4px 10px;overflow:hidden;}\n"
".menu ul{margin:0;padding:0;display:flex;flex-wrap:wrap;"
"align-items:center;list-style:none;}\n"
".menu li{margin:2px 2px;}\n"
".menu li a{display:block;padding:6px 13px;border-radius:6px;"
"font-weight:600;font-size:13px;color:var(--ink);}\n"
".menu li a:hover{background:var(--accent-soft);text-decoration:none;}\n"
".menu a.selected{background:var(--accent);color:#fff;}\n"
".menu a.selected:hover{background:var(--accent);}\n"
".menu a.disabled{color:#b3bfc9;text-decoration:line-through;}\n"
".menu span{float:right;color:var(--muted);font-size:11px;padding:8px 2px;}\n"
"html[dir=rtl] .menu span{float:left !important;}\n"
/* ---- tables ---- */
"table{border-collapse:collapse;}\n"
"td,th{font-size:12px;padding:4px 7px;}\n"
".maintable{background:var(--panel);font-size:12px;width:auto;}\n"
".maintable th{padding:6px 8px;text-align:left;border-top:2px solid var(--accent);"
"border-bottom:1px solid var(--line);background:var(--accent-soft);color:var(--ink);"
"font-size:11px;text-transform:uppercase;letter-spacing:.4px;}\n"
".maintable tr:hover{background-color:#f0f6fa;}\n"
".maintable td{border-bottom:1px solid var(--line);padding:4px 8px;}\n"
".maintable a{font-weight:700;}\n"
".infotable{width:100%;background:var(--panel);border:1px solid var(--line);"
"border-radius:6px;margin:6px 0;}\n"
".infotable th{padding:6px;font-size:11px;text-align:center;background:#3d4d5c;"
"color:#fff;border-bottom:1px solid var(--line);}\n"
".infotable tr:hover{background-color:#f0f6fa;}\n"
".infotable td{border-bottom:1px solid var(--line);}\n"
".infotable .left{text-transform:uppercase;font-weight:700;}\n"
".option th,.option td{font-size:11px;background:var(--panel);color:var(--warn);}\n"
".option th{border-bottom:1px solid var(--line);text-align:left;}\n"
".infomenu{background:var(--panel);border:1px solid var(--line);border-radius:6px;}\n"
".infomenu td{padding:2px 4px;}\n"
/* ---- row stripes + status words ---- */
".alt1{background-color:#ffffff;}\n"
".alt2{background-color:#f6f8fa;}\n"
".alt3{background-color:#eaf1ec;}\n"
".success{color:var(--good);font-weight:700;}\n"
".failed{color:var(--bad);font-weight:700;}\n"
".busy{background-color:#8a9a12;color:#fff;text-align:center;}\n"
".online{background-color:var(--good);color:#fff;text-align:center;}\n"
".offline{background-color:var(--bad);color:#fff;text-align:center;}\n"
".left{text-align:left;}.center{text-align:center;}.right{text-align:right;}\n"
/* ---- hover card on connection icons (legacy geometry kept) ---- */
".online:hover .connect_data,.offline:hover .connect_data,"
".busy:hover .connect_data{visibility:visible;}\n"
".connect_data{border-collapse:collapse;padding:0;margin:-1px 0 0 55px;"
"position:absolute;visibility:hidden;border:1px solid var(--line);"
"font-size:11px;background:var(--panel);color:var(--ink);"
"box-shadow:0 3px 10px rgba(16,35,55,.18);}\n"
".connect_data td{background-color:var(--accent-soft);"
"border-bottom:1px solid var(--line);padding:2px 10px;}\n"
/* ---- forms ---- */
"input,textarea,select{font:inherit;padding:4px 7px;border:1px solid var(--line);"
"border-radius:6px;background:#fff;color:var(--ink);}\n"
"input:focus,textarea:focus,select:focus{outline:2px solid var(--accent);"
"outline-offset:0;border-color:var(--accent);}\n"
"input,select{margin:4px 2px;background:#fff;}\n"
"textarea{width:100%;height:81%;margin:5px 0;padding:6px;}\n"
".sbutton{font-weight:700;background:var(--accent);color:#fff;"
"border:1px solid var(--accent);border-radius:6px;cursor:pointer;}\n"
".sbutton:hover{filter:brightness(1.1);}\n"
"fieldset{border:1px solid var(--line);border-radius:8px;}\n"
/* ---- editor corner layout (legacy geometry, do not touch) ---- */
"div.outer{position:relative;width:100%;}\n"
"div.right{width:100%;margin-left:50%;}\n"
"div.top,div.bottom{position:absolute;width:50%;}\n"
"div.top{top:0;}\n"
"div.bottom{bottom:0;}\n"
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
"}\n"
;
