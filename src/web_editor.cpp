/**
 * @file web_editor.cpp
 * @brief Web-based ST Logic program editor (v7.4.0)
 *
 * LAYER 7: User Interface - Web Editor
 * Serves a single-page HTML/CSS/JS editor for ST Logic programs.
 * All program operations use existing /api/logic/* REST endpoints.
 *
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 *
 * Panels: Editor, Bindings, Monitor, CLI Console
 * Features: Syntax highlighting, Auto-complete, Sparklines, Step debug, Backup
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_editor.h"
#include "debug.h"

static const char PROGMEM editor_html[] = R"rawhtml(<!DOCTYPE html>
<html lang="da">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ST Logic Editor</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,sans-serif;background:#1e1e2e;color:#cdd6f4;height:100vh;display:flex;flex-direction:column;overflow:hidden}
.topnav{background:#11111b;padding:6px 16px;display:flex;gap:8px;border-bottom:2px solid #89b4fa;flex-shrink:0;align-items:center}
.topnav .brand{font-size:14px;font-weight:700;color:#89b4fa;margin-right:12px}
.topnav a{padding:5px 14px;font-size:12px;font-weight:600;background:#313244;color:#a6adc8;border-radius:4px;text-decoration:none;transition:all .15s}
.topnav a:hover{background:#45475a;color:#cdd6f4}
.topnav a.active{background:#89b4fa;color:#1e1e2e}
.save-btn{margin-left:auto;padding:4px 12px;font-size:11px;font-weight:600;background:#a6e3a1;color:#1e1e2e;border:none;border-radius:4px;cursor:pointer;transition:all .2s}
.save-btn:hover{background:#94e2d5}.save-btn.saving{background:#fab387;cursor:wait}.save-btn.saved{background:#a6e3a1}.save-btn.save-err{background:#f38ba8}
.user-badge{position:relative;display:flex;align-items:center;gap:6px}
.user-btn{padding:4px 10px;font-size:11px;background:#313244;color:#a6adc8;border-radius:4px;cursor:pointer;border:1px solid #45475a;display:flex;align-items:center;gap:5px}
.user-btn:hover{background:#45475a;color:#cdd6f4}
.user-btn .dot{width:6px;height:6px;border-radius:50%;display:inline-block}
.user-btn .dot-on{background:#a6e3a1}
.user-btn .dot-off{background:#f38ba8}
.user-menu{display:none;position:absolute;top:100%;right:0;margin-top:4px;background:#181825;border:1px solid #45475a;border-radius:6px;padding:8px 0;min-width:200px;z-index:999;box-shadow:0 4px 12px rgba(0,0,0,.4)}
.user-menu.show{display:block}
.user-menu .um-row{padding:4px 14px;font-size:11px;color:#a6adc8;display:flex;justify-content:space-between}
.user-menu .um-row .um-val{color:#cdd6f4;font-weight:600}
.user-menu .um-sep{border-top:1px solid #313244;margin:4px 0}
.user-menu .um-btn{display:block;width:calc(100% - 16px);margin:4px 8px;padding:5px 10px;font-size:11px;background:#313244;color:#a6adc8;border:none;border-radius:4px;cursor:pointer;text-align:center}
.user-menu .um-btn:hover{background:#f38ba8;color:#1e1e2e}
.hdr{background:#181825;padding:8px 16px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #313244;flex-shrink:0}
.hdr h1{font-size:16px;color:#89b4fa;flex-shrink:0}
.hdr .info{font-size:12px;color:#6c7086;margin-left:auto}
.tabs{display:flex;gap:2px;background:#181825;padding:4px 16px;border-bottom:1px solid #313244;flex-shrink:0}
.tab{padding:6px 16px;border:none;border-radius:6px 6px 0 0;cursor:pointer;font-size:13px;background:#313244;color:#a6adc8;transition:all .15s}
.tab:hover{background:#45475a;color:#cdd6f4}
.tab.active{background:#1e1e2e;color:#89b4fa;font-weight:600}
.tab.compiled{border-top:2px solid #a6e3a1}
.tab.error{border-top:2px solid #f38ba8}
.tab.empty{opacity:.5}
.toolbar{display:flex;gap:8px;padding:8px 16px;background:#181825;border-bottom:1px solid #313244;flex-wrap:wrap;align-items:center;flex-shrink:0}
.btn{padding:5px 14px;border:none;border-radius:4px;cursor:pointer;font-size:12px;font-weight:600;transition:all .15s}
.btn-primary{background:#89b4fa;color:#1e1e2e}.btn-primary:hover{background:#74c7ec}
.btn-success{background:#a6e3a1;color:#1e1e2e}.btn-success:hover{background:#94e2d5}
.btn-danger{background:#f38ba8;color:#1e1e2e}.btn-danger:hover{background:#eba0ac}
.btn-warn{background:#fab387;color:#1e1e2e}.btn-warn:hover{background:#f9e2af}
.btn-sm{padding:3px 10px;font-size:11px}
.btn:disabled{opacity:.4;cursor:not-allowed}
.toggle{display:flex;align-items:center;gap:6px;font-size:12px}
.toggle input[type=checkbox]{width:16px;height:16px;accent-color:#a6e3a1}
.pool-bar{flex:1;max-width:200px;height:16px;background:#313244;border-radius:8px;overflow:hidden;position:relative}
.pool-fill{height:100%;background:#89b4fa;transition:width .3s;border-radius:8px}
.pool-text{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;font-size:10px;color:#cdd6f4;font-weight:600}
.main{display:flex;flex:1;overflow:hidden;min-height:0}
.editor-wrap{flex:1;display:flex;flex-direction:column;overflow:hidden}
.editor-container{flex:1;display:flex;overflow:hidden;position:relative}
.line-nums{min-width:44px;background:#181825;color:#585b70;font:12px/18px 'Cascadia Code','Fira Code','Courier New',monospace;padding:8px 6px 8px 4px;text-align:right;overflow:hidden;user-select:none;border-right:1px solid #313244;white-space:pre}
.line-nums .errln{background:#f38ba8;color:#1e1e2e;font-weight:700;display:inline-block;width:100%;padding-right:6px;margin-right:-6px}
.find-bar{display:none;position:absolute;top:8px;right:24px;z-index:60;background:#181825;border:1px solid #45475a;border-radius:6px;padding:8px;box-shadow:0 4px 16px rgba(0,0,0,.5);min-width:280px}
.find-bar.show{display:block}
.find-bar input{width:140px;padding:4px 8px;background:#313244;border:1px solid #45475a;border-radius:3px;color:#cdd6f4;font:12px 'Cascadia Code',monospace}
.find-bar button{padding:3px 10px;font-size:11px;background:#313244;color:#a6adc8;border:1px solid #45475a;border-radius:3px;cursor:pointer;margin-left:2px}
.find-bar button:hover{background:#45475a;color:#cdd6f4}
.find-bar .fb-row{display:flex;gap:4px;align-items:center;margin-bottom:4px}
.find-bar .fb-info{font-size:10px;color:#6c7086}
#editor{position:absolute;top:0;left:0;width:100%;height:100%;background:transparent;color:transparent;caret-color:#cdd6f4;font:12px/18px 'Cascadia Code','Fira Code','Courier New',monospace;padding:8px;border:none;outline:none;resize:none;tab-size:2;white-space:pre;overflow:auto;z-index:2}
#editor::placeholder{color:#585b70}
#highlight{position:absolute;top:0;left:0;width:100%;min-height:100%;font:12px/18px 'Cascadia Code','Fira Code','Courier New',monospace;padding:8px;white-space:pre;overflow:visible;z-index:1;pointer-events:none;tab-size:2;color:#cdd6f4}
.hl-kw{color:#cba6f7;font-weight:600}.hl-fn{color:#f9e2af}.hl-ty{color:#89b4fa}.hl-num{color:#fab387}.hl-str{color:#a6e3a1}.hl-cmt{color:#585b70;font-style:italic}.hl-op{color:#89dceb}.hl-io{color:#f5c2e7}
.status-bar{display:flex;gap:12px;padding:4px 16px;background:#181825;border-top:1px solid #313244;font-size:11px;color:#6c7086;flex-wrap:wrap;flex-shrink:0}
.status-bar .ok{color:#a6e3a1}.status-bar .err{color:#f38ba8}.status-bar .warn{color:#fab387}
.output{max-height:160px;overflow-y:auto;padding:8px 16px;background:#11111b;font:12px/1.6 'Cascadia Code',monospace;border-top:1px solid #313244;white-space:pre-wrap;flex-shrink:0}
.output.hidden{display:none}
.output .error{color:#f38ba8}.output .success{color:#a6e3a1}.output .info{color:#89b4fa}
.sidebar{width:220px;background:#181825;border-left:1px solid #313244;padding:8px;overflow-y:auto;font-size:11px}
.sidebar h3{font-size:12px;color:#89b4fa;margin:8px 0 4px;padding-bottom:2px;border-bottom:1px solid #313244}
.sidebar .kw{color:#cba6f7}.sidebar .fn{color:#f9e2af}.sidebar .ty{color:#89b4fa}.sidebar .op{color:#fab387}
.sidebar code{font-size:10px;line-height:1.4;display:block;padding:1px 0;color:#a6adc8}
.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:100;align-items:center;justify-content:center}
.modal-bg.show{display:flex}
.modal{background:#1e1e2e;border:1px solid #313244;border-radius:8px;padding:24px;width:320px}
.modal h2{font-size:16px;color:#89b4fa;margin-bottom:16px}
.modal label{display:block;font-size:12px;color:#a6adc8;margin:8px 0 4px}
.modal input{width:100%;padding:6px 10px;background:#313244;border:1px solid #45475a;border-radius:4px;color:#cdd6f4;font-size:13px}
.modal .actions{display:flex;gap:8px;margin-top:16px;justify-content:flex-end}
.view-btns{display:flex;gap:2px;margin-right:8px}
.vbtn{padding:3px 10px;font-size:11px;border:1px solid #45475a;background:#313244;color:#a6adc8;border-radius:4px;cursor:pointer;transition:all .15s}
.vbtn:hover{background:#45475a;color:#cdd6f4}
.vbtn.act{background:#89b4fa;color:#1e1e2e;border-color:#89b4fa}
.panel{flex:1;display:flex;flex-direction:column;overflow:hidden;min-height:0}
.panel.hid{display:none}
.ppad{padding:12px 16px;flex:1;overflow-y:auto}
.ppad h3{color:#89b4fa;font-size:14px;margin-bottom:10px}
.btbl{width:100%;border-collapse:collapse;font-size:12px;margin-top:8px}
.btbl th{text-align:left;padding:3px 6px;background:#181825;color:#89b4fa;border-bottom:1px solid #313244;font-weight:600}
.btbl td{padding:2px 6px;border-bottom:1px solid #313244}
.btbl tr:hover{background:#313244}
.bform{display:flex;gap:6px;flex-wrap:wrap;align-items:center;margin-bottom:8px}
.bform select,.bform input[type=number]{padding:4px 8px;background:#313244;border:1px solid #45475a;border-radius:4px;color:#cdd6f4;font-size:12px}
.bform select{min-width:120px}
.mstats{display:flex;gap:12px;margin-bottom:12px;flex-wrap:wrap}
.mstat{background:#181825;border:1px solid #313244;border-radius:6px;padding:8px 14px;min-width:110px}
.mstat .ml{font-size:10px;color:#6c7086;text-transform:uppercase;letter-spacing:.5px}
.mstat .mv{font-size:18px;font-weight:700;color:#cdd6f4;margin-top:2px}
.mstat .mv.merr{color:#f38ba8}
.empty-msg{color:#6c7086;padding:16px;text-align:center;font-size:12px}
.ac-popup{position:fixed;background:#181825;border:1px solid #45475a;border-radius:4px;max-height:160px;overflow-y:auto;z-index:50;font:12px/1.6 'Cascadia Code',monospace;min-width:180px;display:none}
.ac-popup div{padding:3px 10px;cursor:pointer}
.ac-popup div:hover,.ac-popup div.sel{background:#313244;color:#89b4fa}
.ac-popup .ac-kw{color:#cba6f7}.ac-popup .ac-fn{color:#f9e2af}.ac-popup .ac-var{color:#a6e3a1}
.dbg-bar{display:flex;gap:6px;align-items:center;margin-bottom:10px;flex-wrap:wrap}
.dbg-bar .dbg-sep{width:1px;height:24px;background:#45475a;margin:0 4px}
.dbg-bar .badge{padding:3px 10px;border-radius:4px;font-size:11px;font-weight:600;white-space:nowrap}
.dbg-run{background:#a6e3a1;color:#1e1e2e}.dbg-pause{background:#f9e2af;color:#1e1e2e}.dbg-off{background:#45475a;color:#a6adc8}
.spark{display:inline-block;vertical-align:middle}
.trend-cell{display:flex;align-items:center;gap:4px;min-width:380px}
.trend-labels{display:flex;flex-direction:column;justify-content:space-between;font-size:10px;font-family:monospace;min-width:70px;text-align:right;height:40px}
.trend-labels .tmax{color:#a6e3a1}.trend-labels .tmin{color:#f38ba8}
.trend-time{font-size:10px;color:#89b4fa;font-family:monospace;min-width:46px;text-align:center}
.trend-paused{color:#f9e2af;font-weight:600}
.speed-ctrl{display:flex;align-items:center;gap:8px;margin-bottom:10px}
.speed-ctrl label{font-size:12px;color:#a6adc8}
.speed-ctrl select{background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:4px;padding:2px 6px;font-size:12px}
@media(max-width:768px){.sidebar{display:none}.pool-bar{max-width:120px}.mstats{gap:6px}.mstat{min-width:80px;padding:6px 8px}}
</style>
</head>
<body>

<!-- Login Modal -->
<div class="modal-bg" id="loginModal">
<div class="modal">
<h2>ST Logic Editor</h2>
<p style="font-size:12px;color:#6c7086;margin-bottom:12px">Brug credentials fra CLI: <code>show config http</code></p>
<div id="loginErr" style="display:none;color:#f38ba8;font-size:12px;padding:6px 0"></div>
<label>Brugernavn</label>
<input type="text" id="authUser" placeholder="Brugernavn" autocomplete="username">
<label>Adgangskode</label>
<input type="password" id="authPass" placeholder="Adgangskode" autocomplete="current-password">
<div class="actions">
<button class="btn btn-primary" id="btnLogin" onclick="doLogin()">Forbind</button>
</div>
</div>
</div>

<!-- Top Navigation -->
<div class="topnav">
<span class="brand">HyberFusion PLC</span>
<a href="/">Monitor</a>
<a href="/editor" class="active">ST Editor</a>
<a href="/cli">CLI</a>
<a href="/system">System</a>
<button class="save-btn" id="saveBtn" onclick="doGlobalSave()" title="Gem konfiguration til NVS">&#128190; Save</button>
<div class="user-badge"><div class="user-btn" id="userBtn" onclick="toggleUserMenu()"><span class="dot dot-off" id="userDot"></span><span id="userName">Ikke logget ind</span></div><div class="user-menu" id="userMenu"><div class="um-row"><span>Bruger:</span><span class="um-val" id="umUser">-</span></div><div class="um-row"><span>Roller:</span><span class="um-val" id="umRoles">-</span></div><div class="um-row"><span>Privilegier:</span><span class="um-val" id="umPriv">-</span></div><div class="um-row"><span>Auth mode:</span><span class="um-val" id="umMode">-</span></div><div class="um-sep"></div><div class="um-btn" id="umLogout" onclick="doLogout()">Log ud</div></div></div>
</div>

<!-- Header -->
<div class="hdr">
<h1>ST Logic Editor</h1>
<span id="deviceInfo" class="info"></span>
</div>

<!-- Program Tabs -->
<div class="tabs" id="tabs"></div>

<!-- Toolbar -->
<div class="toolbar">
<button class="btn btn-primary" onclick="uploadCode()" id="btnUpload" title="Upload & kompilér (Ctrl+S)">Kompilér</button>
<button class="btn btn-success btn-sm" onclick="saveConfig()" title="Gem til NVS flash">Gem Config</button>
<button class="btn btn-sm" id="btnStartStop" style="background:#a6e3a1;color:#1e1e2e" onclick="toggleStartStop()" title="Start/stop program">Start</button>
<button class="btn btn-sm" style="background:#fab387;color:#1e1e2e" onclick="reinitProgram()" title="Cold restart: nulstil variabler til startværdier">Reinit</button>
<button class="btn btn-danger btn-sm" onclick="deleteProgram()">Slet</button>
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="downloadBackup()" title="Download .st fil">Download</button>
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="document.getElementById('uploadFile').click()" title="Upload .st fil">Upload</button>
<input type="file" id="uploadFile" accept=".st,.txt" style="display:none" onchange="handleFileUpload(event)">
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="toggleFind()" title="Find/Replace (Ctrl+F)">Find</button>
<div class="view-btns">
<button class="vbtn act" onclick="setView('editor',this)">Editor</button>
<button class="vbtn" onclick="setView('bindings',this)">Bindings</button>
<button class="vbtn" onclick="setView('monitor',this)">Monitor</button>
</div>
<div style="flex:1"></div>
<span style="font-size:11px;color:#6c7086" title="Kildekode pool">Pool:</span>
<div class="pool-bar">
<div class="pool-fill" id="poolFill"></div>
<div class="pool-text" id="poolText">-</div>
</div>
<span style="font-size:11px;color:#6c7086;margin-left:6px" title="Heap til compiler">Heap:</span>
<div class="pool-bar" style="min-width:110px">
<div class="pool-fill" id="heapFill"></div>
<div class="pool-text" id="heapText">-</div>
</div>
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="toggleSidebar()">Ref</button>
</div>

<!-- Editor View -->
<div class="panel" id="viewEditor">
<div class="main">
<div class="editor-wrap">
<div class="editor-container">
<div class="line-nums" id="lineNums">1</div>
<div style="position:relative;flex:1;overflow:hidden">
<pre id="highlight" aria-hidden="true"></pre>
<textarea id="editor" placeholder="(* Skriv dit ST program her *)&#10;&#10;PROGRAM Blink&#10;  VAR&#10;    toggle : BOOL := FALSE;&#10;  END_VAR&#10;&#10;  toggle := NOT toggle;&#10;  coil[1] := toggle;&#10;END_PROGRAM" spellcheck="false"></textarea>
<div class="find-bar" id="findBar">
<div class="fb-row"><input type="text" id="findInput" placeholder="Find..." onkeydown="findKey(event)"><button onclick="findNext()">&#9661;</button><button onclick="findPrev()">&#9651;</button><button onclick="toggleFind()">&#10005;</button></div>
<div class="fb-row"><input type="text" id="replaceInput" placeholder="Erstat..."><button onclick="replaceOne()">Erstat</button><button onclick="replaceAll()">Alle</button></div>
<div class="fb-info" id="findInfo">0 resultater</div>
</div>
</div>
</div>
</div>
<div class="sidebar" id="sidebar">
<h3>Nøgleord</h3>
<code class="kw">PROGRAM END_PROGRAM
FUNCTION FUNCTION_BLOCK
END_FUNCTION END_FUNCTION_BLOCK
VAR VAR_INPUT VAR_OUTPUT
END_VAR VAR_GLOBAL EXPORT
BEGIN END
IF THEN ELSIF ELSE END_IF
CASE OF END_CASE
FOR TO BY DO END_FOR
WHILE END_WHILE
REPEAT UNTIL END_REPEAT
RETURN EXIT</code>
<h3>Typer</h3>
<code class="ty">BOOL INT DINT UINT
REAL BYTE WORD DWORD
STRING TIME</code>
<h3>Operatorer</h3>
<code class="op">AND OR XOR NOT
MOD SHL SHR ROL ROR
:= = &lt;&gt; &lt; &gt; &lt;= &gt;=</code>
<h3>Timer/Tæller</h3>
<code class="fn">TON TOF TP
CTU CTD CTUD
SR RS R_TRIG F_TRIG</code>
<h3>Indbyggede</h3>
<code class="fn">ABS MIN MAX LIMIT
SCALE SQRT EXPT LN LOG
SEL MUX MOVE
HYSTERESIS CLAMP
BIT_SET BIT_CLR BIT_TST</code>
<h3>Modbus Master</h3>
<code class="fn">MB_READ_HOLDING(id,addr)
MB_READ_INPUT_REG(id,addr)
MB_READ_COIL(id,addr)
MB_READ_INPUT(id,addr)
MB_WRITE_HOLDING(id,addr,val)
MB_WRITE_COIL(id,addr,val)
MB_SUCCESS() MB_BUSY()
MB_ERROR()</code>
<h3>HW Counter</h3>
<code class="fn">CNT_SETUP(id,mode,edge,dir,pre,gpio)
CNT_SETUP_ADV(id,scale,bw,db,start)
CNT_SETUP_CMP(id,mode,val,src,ror)
CNT_ENABLE(id,on_off)
CNT_CTRL(id,cmd) 0=rst 1=start 2=stop
CNT_VALUE(id) CNT_RAW(id)
CNT_FREQ(id) CNT_STATUS(id)</code>
<h3>Modbus I/O</h3>
<code class="fn">hr[addr] — Holding Register
ir[addr] — Input Register
coil[addr] — Coil Output
di[addr] — Discrete Input</code>
</div>
</div>
</div>

<!-- Bindings View -->
<div class="panel hid" id="viewBindings">
<div class="ppad">
<h3>Variabel-bindinger — Program <span id="bindSlot">1</span></h3>
<div class="bform" id="bindForm">
<select id="bindVar"><option>Kompilér først</option></select>
<select id="bindDir">
<option value="output">Output →</option>
<option value="input">← Input</option>
</select>
<select id="bindType">
<option value="reg">HR (Holding Reg)</option>
<option value="coil">Coil</option>
<option value="input">DI (Discrete In)</option>
</select>
<input type="number" id="bindAddr" placeholder="Addr" min="0" max="255" style="width:75px">
<input type="number" id="bindGpio" placeholder="GPIO pin" min="0" max="255" style="width:85px">
<button class="btn btn-primary btn-sm" id="btnBind" onclick="addBinding()">Tilføj</button>
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4;display:none" id="btnBindCancel" onclick="cancelEdit()">Annullér</button>
</div>
<table class="btbl">
<thead><tr><th>ST Variabel</th><th>ST variabel Type</th><th>Retning</th><th>Modbus Register</th><th>Modbus Adresse</th><th>Local GPIO</th><th></th></tr></thead>
<tbody id="bindBody"></tbody>
</table>
<div class="empty-msg" id="bindEmpty">Ingen bindinger konfigureret</div>
</div>
</div>

<!-- Monitor View -->
<div class="panel hid" id="viewMonitor">
<div class="ppad">
<h3>Runtime Monitor — Program <span id="monSlot">1</span></h3>
<div class="dbg-bar">
<button class="btn btn-sm btn-warn" onclick="dbgAction('pause')" title="Pause programudførelse">Pause Execute</button>
<button class="btn btn-sm btn-primary" onclick="dbgAction('step')" title="Udfør én enkelt instruktion">Single Step</button>
<button class="btn btn-sm btn-success" onclick="dbgAction('continue')" title="Udfør én hel programcyklus">Single Cycle</button>
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="dbgAction('stop')" title="Genoptag normal kontinuerlig udførelse">Normal Execute</button>
<div class="dbg-sep"></div>
<span class="badge dbg-off" id="dbgBadge">Normal</span>
</div>
<div class="speed-ctrl">
<label>Hastighed:</label>
<select id="monSpeed" onchange="changeMonSpeed()">
<option value="500">500ms</option>
<option value="1000">1s</option>
<option value="2500" selected>2.5s</option>
<option value="5000">5s</option>
<option value="10000">10s</option>
</select>
<label>Historik:</label>
<select id="monHistLen" onchange="changeHistLen()">
<option value="30">30</option>
<option value="60" selected>60</option>
<option value="120">120</option>
<option value="240">240</option>
</select>
<label>Kurvefarve:</label>
<select id="monColor" onchange="changeTrendColor()">
<option value="#89b4fa" selected>Blå</option>
<option value="#a6e3a1">Grøn</option>
<option value="#f9e2af">Gul</option>
<option value="#f38ba8">Rød</option>
<option value="#cba6f7">Lilla</option>
<option value="#fab387">Orange</option>
<option value="#94e2d5">Cyan</option>
<option value="#cdd6f4">Hvid</option>
</select>
</div>
<div class="mstats" id="monStats">
<div class="mstat"><div class="ml">Udførelser</div><div class="mv" id="monExec">-</div></div>
<div class="mstat"><div class="ml">Tid (µs)</div><div class="mv" id="monTime">-</div></div>
<div class="mstat"><div class="ml">Min/Max µs</div><div class="mv" id="monMinMax">-</div></div>
<div class="mstat"><div class="ml">Fejl</div><div class="mv" id="monErrors">-</div></div>
<div class="mstat"><div class="ml">Overruns</div><div class="mv" id="monOverruns">-</div></div>
</div>
<table class="btbl">
<thead><tr><th>Variabel</th><th>Type</th><th>Værdi</th><th>Trend</th></tr></thead>
<tbody id="monBody"></tbody>
</table>
<div class="empty-msg" id="monEmpty">Intet kompileret program i dette slot</div>
</div>
</div>

<!-- Global Output Log -->
<div class="output hidden" id="output"></div>

<!-- Autocomplete Popup -->
<div class="ac-popup" id="acPopup"></div>

<!-- Status Bar -->
<div class="status-bar">
<span id="stSlot">Slot 1</span>
<span id="stStatus">-</span>
<span id="stSize">-</span>
<span id="stLine">Linje 1, Kol 1</span>
</div>

<script>
let AUTH=sessionStorage.getItem('hfplc_auth')||'';
let SLOT=1;
let programs=[null,null,null,null];
let dirty=false;
let VIEW='editor';
let monTimer=null;
let varHistory={};  // sparkline data: {varName: [{v:number, t:number, exec:number}]}
let HIST_LEN=60;
let monInterval=2500;  // monitor refresh interval ms
let trendColor='#89b4fa';  // trend line color
let monBusy=false;  // guard against overlapping refreshes
let dbgMode='off';  // current debug mode: off, paused, step, run
let lastExecCount=0;  // track execution count for trend gating
let monStartTime=0;  // when monitor started (Date.now)
let errorLine=0;  // FEAT-131: inline compile error line (0 = none)
let sseConn=null; // FEAT-130: SSE connection for editor monitor

// === ST Syntax Keywords ===
const ST_KW=['PROGRAM','END_PROGRAM','FUNCTION','FUNCTION_BLOCK','END_FUNCTION','END_FUNCTION_BLOCK','VAR','VAR_INPUT','VAR_OUTPUT','END_VAR','VAR_GLOBAL','BEGIN','END','IF','THEN','ELSIF','ELSE','END_IF','CASE','OF','END_CASE','FOR','TO','BY','DO','END_FOR','WHILE','END_WHILE','REPEAT','UNTIL','END_REPEAT','RETURN','EXIT','TRUE','FALSE','NOT','AND','OR','XOR','MOD','EXPORT'];
const ST_FN=['ABS','MIN','MAX','LIMIT','SCALE','SQRT','EXPT','LN','LOG','SEL','MUX','MOVE','HYSTERESIS','CLAMP','BIT_SET','BIT_CLR','BIT_TST','TON','TOF','TP','CTU','CTD','CTUD','SR','RS','R_TRIG','F_TRIG','SHL','SHR','ROL','ROR','MB_READ_HOLDING','MB_READ_INPUT','MB_READ_COIL','MB_READ_INPUT_REG','MB_WRITE_HOLDING','MB_WRITE_COIL','MB_SUCCESS','MB_BUSY','MB_ERROR','CNT_SETUP','CNT_SETUP_ADV','CNT_SETUP_CMP','CNT_ENABLE','CNT_CTRL','CNT_VALUE','CNT_RAW','CNT_FREQ','CNT_STATUS'];
const ST_TY=['BOOL','INT','DINT','UINT','REAL','BYTE','WORD','DWORD','STRING','TIME'];

// === Syntax Highlighting ===
function highlightST(code){
  let esc=code.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  // Comments (* ... *)
  esc=esc.replace(/\(\*[\s\S]*?\*\)/g,s=>'<span class="hl-cmt">'+s+'</span>');
  // Strings
  esc=esc.replace(/'[^']*'/g,s=>'<span class="hl-str">'+s+'</span>');
  // I/O access: hr[N], ir[N], coil[N], di[N]
  esc=esc.replace(/\b(hr|ir|coil|di)\[(\d+)\]/gi,(m,io,n)=>'<span class="hl-io">'+m+'</span>');
  // Numbers
  esc=esc.replace(/\b(\d+\.?\d*)\b/g,'<span class="hl-num">$1</span>');
  // Types
  const tyRe=new RegExp('\\b('+ST_TY.join('|')+')\\b','gi');
  esc=esc.replace(tyRe,s=>'<span class="hl-ty">'+s+'</span>');
  // Functions
  const fnRe=new RegExp('\\b('+ST_FN.join('|')+')\\b','gi');
  esc=esc.replace(fnRe,s=>'<span class="hl-fn">'+s+'</span>');
  // Keywords
  const kwRe=new RegExp('\\b('+ST_KW.join('|')+')\\b','gi');
  esc=esc.replace(kwRe,s=>'<span class="hl-kw">'+s+'</span>');
  // Operators
  esc=esc.replace(/:=/g,'<span class="hl-op">:=</span>');
  return esc+'\n';
}

function syncHighlight(){
  const ed=document.getElementById('editor');
  const hl=document.getElementById('highlight');
  hl.innerHTML=highlightST(ed.value);
  hl.style.transform='translate('+(-ed.scrollLeft)+'px,'+(-ed.scrollTop)+'px)';
}

// === Autocomplete ===
let acVisible=false;
let acItems=[];
let acIdx=0;

function getWordAt(text,pos){
  let start=pos;
  while(start>0&&/[a-zA-Z_0-9]/.test(text[start-1]))start--;
  return{word:text.substring(start,pos),start};
}

function showAutocomplete(){
  const ed=document.getElementById('editor');
  const pos=ed.selectionStart;
  const {word,start}=getWordAt(ed.value,pos);
  if(word.length<2){hideAC();return;}
  const wUp=word.toUpperCase();
  // Build suggestion list
  const suggestions=[];
  ST_KW.forEach(k=>{if(k.startsWith(wUp))suggestions.push({text:k,cls:'ac-kw'});});
  ST_FN.forEach(f=>{if(f.startsWith(wUp))suggestions.push({text:f,cls:'ac-fn'});});
  ST_TY.forEach(t=>{if(t.startsWith(wUp))suggestions.push({text:t,cls:'ac-kw'});});
  // Add current program variables
  const p=programs[SLOT-1];
  if(p&&p.variables){
    p.variables.forEach(v=>{if(v.name.toUpperCase().startsWith(wUp))suggestions.push({text:v.name,cls:'ac-var'});});
  }
  if(!suggestions.length){hideAC();return;}
  acItems=suggestions;
  acIdx=0;
  const popup=document.getElementById('acPopup');
  popup.innerHTML=suggestions.map((s,i)=>
    '<div class="'+(i===0?'sel ':'')+s.cls+'" data-i="'+i+'">'+s.text+'</div>'
  ).join('');
  // Position popup near cursor
  const rect=ed.getBoundingClientRect();
  // Approximate cursor position
  const lines=ed.value.substring(0,pos).split('\n');
  const row=lines.length-1;
  const col=lines[row].length;
  popup.style.left=Math.min(rect.left+52+col*7.2,window.innerWidth-200)+'px';
  popup.style.top=Math.min(rect.top+8+row*18-ed.scrollTop+18,window.innerHeight-180)+'px';
  popup.style.display='block';
  acVisible=true;
}

function hideAC(){
  document.getElementById('acPopup').style.display='none';
  acVisible=false;
}

function insertAC(){
  if(!acVisible||!acItems.length)return;
  const ed=document.getElementById('editor');
  const pos=ed.selectionStart;
  const {word,start}=getWordAt(ed.value,pos);
  const chosen=acItems[acIdx].text;
  ed.value=ed.value.substring(0,start)+chosen+ed.value.substring(pos);
  ed.selectionStart=ed.selectionEnd=start+chosen.length;
  hideAC();
  dirty=true;
  syncHighlight();
  updateLines();
}

// === Auth & API ===
async function doLogin(){
  const u=document.getElementById('authUser').value.trim();
  const p=document.getElementById('authPass').value;
  const errEl=document.getElementById('loginErr');
  const btn=document.getElementById('btnLogin');
  if(!u||!p){errEl.textContent='Udfyld brugernavn og adgangskode';errEl.style.display='block';return;}
  errEl.style.display='none';
  btn.disabled=true;btn.textContent='Forbinder...';
  AUTH='Basic '+btoa(u+':'+p);
  try{
    const r=await fetch('/api/status',{method:'GET',headers:{'Authorization':AUTH}});
    if(r.status===401){
      AUTH='';errEl.textContent='Forkert brugernavn eller adgangskode';errEl.style.display='block';
      btn.disabled=false;btn.textContent='Forbind';return;
    }
    if(r.status===429){
      AUTH='';errEl.textContent='For mange forsøg — vent lidt';errEl.style.display='block';
      btn.disabled=false;btn.textContent='Forbind';return;
    }
    if(!r.ok){
      AUTH='';errEl.textContent='Serverfejl: HTTP '+r.status;errEl.style.display='block';
      btn.disabled=false;btn.textContent='Forbind';return;
    }
    sessionStorage.setItem('hfplc_auth',AUTH);
    document.getElementById('loginModal').classList.remove('show');
    init();
    updateUserBadge();
  }catch(e){
    AUTH='';errEl.textContent='Kan ikke forbinde til ESP32';errEl.style.display='block';
  }
  btn.disabled=false;btn.textContent='Forbind';
}

async function api(method,path,body){
  const opts={method,headers:{'Authorization':AUTH}};
  if(body){opts.headers['Content-Type']='application/json';opts.body=JSON.stringify(body);}
  const r=await fetch('/api/'+path,opts);
  if(r.status===401){AUTH='';sessionStorage.removeItem('hfplc_auth');document.getElementById('loginModal').classList.add('show');throw new Error('Login kræves');}
  if(r.status===429){throw new Error('Rate limit — vent lidt');}
  if(!r.ok){const t=await r.text();throw new Error('HTTP '+r.status+': '+(t||'ukendt fejl'));}
  return r.json();
}

let resourceTimer=null;
async function init(){
  try{
    const st=await api('GET','status');
    document.getElementById('deviceInfo').textContent=
      'v'+(st.version||'?')+' Build #'+(st.build||'?');
  }catch(e){}
  await loadAll();
  // Poll compiler resources every 5 seconds
  if(resourceTimer)clearInterval(resourceTimer);
  resourceTimer=setInterval(async()=>{
    try{const d=await api('GET','logic');if(d.resources)updatePool(d.resources);}catch(e){}
  },5000);
}

async function loadAll(){
  try{
    const d=await api('GET','logic');
    if(d.programs){
      d.programs.forEach((p,i)=>{programs[i]=p;});
    }
    updateTabs();
    updatePool(d.resources||null);
    await loadSource(SLOT);
  }catch(e){log('error','Fejl: '+e.message);}
}

function updateTabs(){
  const t=document.getElementById('tabs');
  t.innerHTML='';
  for(let i=0;i<4;i++){
    const p=programs[i];
    const b=document.createElement('button');
    b.className='tab'+(i+1===SLOT?' active':'');
    if(p){
      if(p.compiled)b.classList.add('compiled');
      else if(p.source_size>0)b.classList.add('error');
      b.textContent=(i+1)+': '+(p.name||'Program'+(i+1));
    }else{
      b.classList.add('empty');
      b.textContent=(i+1)+': (tom)';
    }
    b.onclick=()=>selectSlot(i+1);
    t.appendChild(b);
  }
}

async function selectSlot(s){
  if(dirty&&!confirm('Ugemte ændringer. Skift alligevel?'))return;
  SLOT=s;
  varHistory={};
  lastExecCount=0;
  errorLine=0;  // FEAT-131: clear error marker on slot switch
  updateTabs();
  await loadSource(s);
  if(VIEW==='bindings') loadBindings();
  if(VIEW==='monitor') refreshMonitor();
}

async function loadSource(s){
  const ed=document.getElementById('editor');
  const p=programs[s-1];
  updateStartStopBtn(p&&p.enabled);
  document.getElementById('stSlot').textContent='Slot '+s;
  updatePool();
  if(!p||!p.source_size){
    ed.value='';
    updateStatus('-','');
    updateLines();syncHighlight();
    dirty=false;
    return;
  }
  try{
    const d=await api('GET','logic/'+s+'/source');
    ed.value=d.source||'';
    updateStatus(p.compiled?'ok':'err',p.compiled?'Kompileret':'Kompileringsfejl');
    if(p.compiled){
      document.getElementById('stSize').textContent=
        (p.source_size||0)+' bytes / '+(p.instr_count||'?')+' instr';
    }
  }catch(e){ed.value='';log('error','Kunne ikke hente kildekode: '+e.message);}
  updateLines();syncHighlight();
  dirty=false;
}

async function uploadCode(){
  const src=document.getElementById('editor').value;
  if(!src.trim()){log('error','Tom kildekode');return;}
  const p=programs[SLOT-1];
  if(p&&p.compiled&&!dirty){log('info','Ingen ændringer — allerede kompileret');return;}
  document.getElementById('btnUpload').disabled=true;
  try{
    const d=await api('POST','logic/'+SLOT+'/source',{source:src});
    if(d.compiled){
      log('success','Kompileret OK — '+(d.instr_count||0)+' instruktioner, '+(d.source_size||0)+' bytes');
      updateStatus('ok','Kompileret');
      errorLine=0;  // FEAT-131: clear error marker on success
    }else{
      const errMsg=d.compile_error||d.error||'ukendt fejl';
      log('error','Kompileringsfejl: '+errMsg);
      updateStatus('err','Fejl');
      // FEAT-131: highlight error line
      errorLine=parseErrorLine(errMsg);
      if(errorLine>0){
        log('info','Springer til linje '+errorLine);
        setTimeout(()=>scrollToLine(errorLine),100);
      }
    }
    dirty=false;
    await loadAll();
    updateLines();  // refresh line numbers (with error marker)
  }catch(e){log('error','Upload fejlede: '+e.message);}
  document.getElementById('btnUpload').disabled=false;
}

function updateStartStopBtn(running){
  const b=document.getElementById('btnStartStop');
  if(running){b.textContent='Stop';b.style.background='#f38ba8';}
  else{b.textContent='Start';b.style.background='#a6e3a1';}
  b.dataset.running=running?'1':'0';
}
async function toggleStartStop(){
  const b=document.getElementById('btnStartStop');
  const running=b.dataset.running==='1';
  try{
    await api('POST','logic/'+SLOT+'/'+(running?'disable':'enable'));
    log('info',(running?'Stoppet':'Startet')+' program '+SLOT);
    await loadAll();
  }catch(e){log('error','Fejl: '+e.message);}
}

async function reinitProgram(){
  try{
    await api('POST','logic/'+SLOT+'/reinit');
    log('info','Program '+SLOT+' cold restart (variabler nulstillet)');
    await loadAll();
  }catch(e){log('error','Reinit fejl: '+e.message);}
}

async function deleteProgram(){
  if(!confirm('Slet program '+SLOT+'? Dette kan ikke fortrydes.'))return;
  try{
    await api('DELETE','logic/'+SLOT);
    document.getElementById('editor').value='';
    log('info','Program '+SLOT+' slettet');
    dirty=false;
    await loadAll();
  }catch(e){log('error','Fejl: '+e.message);}
}

async function saveConfig(){
  try{
    await api('POST','system/save');
    log('success','Konfiguration gemt til NVS flash');
  }catch(e){log('error','Gem fejlede: '+e.message);}
}

// === FEAT-057: Backup Download ===
function downloadBackup(){
  const src=document.getElementById('editor').value;
  if(!src.trim()){log('error','Intet program at downloade');return;}
  const p=programs[SLOT-1];
  const name=(p&&p.name?p.name:'program'+SLOT)+'.st';
  const blob=new Blob([src],{type:'text/plain'});
  const a=document.createElement('a');
  a.href=URL.createObjectURL(blob);
  a.download=name;
  a.click();
  URL.revokeObjectURL(a.href);
  log('info','Downloaded: '+name);
}

let lastResources=null;
function updatePool(res){
  // Pool bar (kildekode)
  const total=res?res.pool_total:8000;
  const used=res?res.pool_used:0;
  const pct=Math.min(100,(used/total*100)|0);
  document.getElementById('poolFill').style.width=pct+'%';
  document.getElementById('poolFill').style.background=pct>90?'#f38ba8':pct>70?'#fab387':'#89b4fa';
  document.getElementById('poolText').textContent=used+'/'+total+' ('+pct+'%)';
  // Heap bar (compiler ressourcer)
  if(res){
    lastResources=res;
    const hf=res.heap_free;const lb=res.largest_block;const nodes=res.max_ast_nodes;
    const hpct=Math.min(100,Math.max(0,100-((lb/65536*100)|0)));
    document.getElementById('heapFill').style.width=hpct+'%';
    document.getElementById('heapFill').style.background=lb<20000?'#f38ba8':lb<35000?'#fab387':'#a6e3a1';
    document.getElementById('heapText').textContent=((lb/1024)|0)+'KB blok / ~'+nodes+' nodes';
    document.getElementById('heapFill').parentElement.title=
      'Heap fri: '+((hf/1024)|0)+'KB | Storste blok: '+((lb/1024)|0)+'KB | Max AST nodes: '+nodes+' | Node: '+res.ast_node_size+' bytes';
  }
}

function updateStatus(cls,text){
  const el=document.getElementById('stStatus');
  el.className=cls;
  el.textContent=text;
}

function log(cls,msg){
  const out=document.getElementById('output');
  out.classList.remove('hidden');
  const ts=new Date().toLocaleTimeString();
  out.innerHTML+='<span class="'+cls+'">['+ts+'] '+msg+'</span>\n';
  out.scrollTop=out.scrollHeight;
}

function updateLines(){
  const ed=document.getElementById('editor');
  const n=ed.value.split('\n').length;
  const ln=document.getElementById('lineNums');
  if(errorLine>0&&errorLine<=n){
    // FEAT-131: highlight error line via HTML
    let h='';
    for(let i=1;i<=n;i++){
      if(i===errorLine)h+='<span class="errln">'+i+'</span>\n';
      else h+=i+'\n';
    }
    ln.innerHTML=h;
  }else{
    const nums=[];for(let i=1;i<=n;i++)nums.push(i);
    ln.textContent=nums.join('\n');
  }
}

// FEAT-131: scroll editor to line
function scrollToLine(lineNum){
  const ed=document.getElementById('editor');
  const lines=ed.value.split('\n');
  if(lineNum<1||lineNum>lines.length)return;
  // Calculate character position
  let pos=0;
  for(let i=0;i<lineNum-1;i++)pos+=lines[i].length+1;
  ed.focus();
  ed.selectionStart=pos;
  ed.selectionEnd=pos+(lines[lineNum-1]||'').length;
  // Scroll line into view (line-height 18px)
  ed.scrollTop=Math.max(0,(lineNum-4)*18);
}

// FEAT-131: Parse "at line N" from error message
function parseErrorLine(msg){
  if(!msg)return 0;
  const m=msg.match(/(?:at\s+line|line)\s+(\d+)/i);
  return m?parseInt(m[1]):0;
}

function toggleSidebar(){
  const s=document.getElementById('sidebar');
  s.style.display=s.style.display==='none'?'':'none';
}

// === Editor Events ===
const ed=document.getElementById('editor');
ed.addEventListener('input',()=>{dirty=true;updateLines();syncHighlight();showAutocomplete();});
ed.addEventListener('scroll',()=>{
  document.getElementById('lineNums').scrollTop=ed.scrollTop;
  syncHighlight();
  hideAC();
});
ed.addEventListener('keyup',(e)=>{
  if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key)){
    const v=ed.value.substring(0,ed.selectionStart);
    const lines=v.split('\n');
    document.getElementById('stLine').textContent='Linje '+lines.length+', Kol '+(lines[lines.length-1].length+1);
  }
});
ed.addEventListener('click',()=>{
  const v=ed.value.substring(0,ed.selectionStart);
  const lines=v.split('\n');
  document.getElementById('stLine').textContent='Linje '+lines.length+', Kol '+(lines[lines.length-1].length+1);
  hideAC();
});
ed.addEventListener('keydown',(e)=>{
  if(acVisible){
    if(e.key==='ArrowDown'){e.preventDefault();acIdx=Math.min(acIdx+1,acItems.length-1);updateACsel();return;}
    if(e.key==='ArrowUp'){e.preventDefault();acIdx=Math.max(acIdx-1,0);updateACsel();return;}
    if(e.key==='Enter'||e.key==='Tab'){e.preventDefault();insertAC();return;}
    if(e.key==='Escape'){e.preventDefault();hideAC();return;}
  }
  if(e.key==='Tab'&&!acVisible){
    e.preventDefault();
    const s=ed.selectionStart,end=ed.selectionEnd;
    ed.value=ed.value.substring(0,s)+'  '+ed.value.substring(end);
    ed.selectionStart=ed.selectionEnd=s+2;
    dirty=true;updateLines();syncHighlight();
  }
});
function updateACsel(){
  const items=document.getElementById('acPopup').children;
  for(let i=0;i<items.length;i++)items[i].classList.toggle('sel',i===acIdx);
  if(items[acIdx])items[acIdx].scrollIntoView({block:'nearest'});
}
document.getElementById('acPopup').addEventListener('click',(e)=>{
  const d=e.target.closest('[data-i]');
  if(d){acIdx=parseInt(d.dataset.i);insertAC();}
});
document.getElementById('authUser').addEventListener('keydown',(e)=>{if(e.key==='Enter')doLogin();});
document.getElementById('authPass').addEventListener('keydown',(e)=>{if(e.key==='Enter')doLogin();});
// Auto-login if session auth exists, otherwise show login modal
if(AUTH){fetch('/api/status',{headers:{'Authorization':AUTH}}).then(r=>{if(r.ok){init();updateUserBadge();}else{AUTH='';sessionStorage.removeItem('hfplc_auth');document.getElementById('loginModal').classList.add('show');}}).catch(()=>{document.getElementById('loginModal').classList.add('show');});}
else{document.getElementById('loginModal').classList.add('show');}
document.addEventListener('keydown',(e)=>{
  if((e.ctrlKey||e.metaKey)&&e.key==='s'){e.preventDefault();uploadCode();}
});

// === View Switching ===
function setView(v,btn){
  VIEW=v;
  document.querySelectorAll('.vbtn').forEach(b=>b.classList.remove('act'));
  if(btn)btn.classList.add('act');
  ['viewEditor','viewBindings','viewMonitor'].forEach(id=>{
    const el=document.getElementById(id);
    const match=id==='view'+v.charAt(0).toUpperCase()+v.slice(1);
    el.classList.toggle('hid',!match);
  });
  stopMonitor();
  if(v==='bindings') loadBindings();
  if(v==='monitor') startMonitor();
}

// === Bindings Panel ===
let editIdx=-1;
let editGpioPin=-1;

function editBinding(b){
  editIdx=b.index;
  editGpioPin=b.gpio_pin>=0?b.gpio_pin:-1;
  const sel=document.getElementById('bindVar');
  for(let i=0;i<sel.options.length;i++){
    if(sel.options[i].value===b.name){sel.selectedIndex=i;break;}
  }
  document.getElementById('bindDir').value=b.direction||'output';
  const rtMap={'HR':'reg','Coil':'coil','DI':'input'};
  document.getElementById('bindType').value=rtMap[b.register_type]||'reg';
  document.getElementById('bindAddr').value=b.register_addr||0;
  document.getElementById('bindGpio').value=b.gpio_pin>=0?b.gpio_pin:'';
  const btn=document.getElementById('btnBind');
  btn.textContent='Opdatér';
  btn.className='btn btn-warn btn-sm';
  document.getElementById('btnBindCancel').style.display='';
}

function cancelEdit(){
  editIdx=-1;
  editGpioPin=-1;
  const btn=document.getElementById('btnBind');
  btn.textContent='Tilføj';
  btn.className='btn btn-primary btn-sm';
  document.getElementById('btnBindCancel').style.display='none';
  document.getElementById('bindAddr').value='';
  document.getElementById('bindGpio').value='';
}

async function loadBindings(){
  document.getElementById('bindSlot').textContent=SLOT;
  const p=programs[SLOT-1];
  const sel=document.getElementById('bindVar');
  sel.innerHTML='';
  if(p&&p.compiled){
    try{
      const d=await api('GET','logic/'+SLOT);
      if(d.variables){
        d.variables.forEach(v=>{
          const o=document.createElement('option');
          o.value=v.name;o.textContent=v.name+' ('+v.type+')';
          sel.appendChild(o);
        });
      }
    }catch(e){}
  }
  if(!sel.options.length){
    const o=document.createElement('option');o.textContent='Kompilér først';o.disabled=true;sel.appendChild(o);
  }
  try{
    const [d,gd]=await Promise.all([api('GET','bindings'),api('GET','gpio')]);
    const gpios=(gd.gpios||[]);
    const body=document.getElementById('bindBody');
    body.innerHTML='';
    const all=d.bindings||[];
    const mine=all.filter(b=>b.program===SLOT);
    document.getElementById('bindEmpty').style.display=mine.length?'none':'block';
    all.forEach(b=>{
      if(b.program!==SLOT)return;
      // Find matching GPIO: same register addr + compatible direction
      let gpioPin=-1;
      const regAddr=b.register_addr;
      gpios.forEach(g=>{
        if(b.direction==='input'&&g.direction==='input'&&g.register===regAddr) gpioPin=g.pin;
        if(b.direction==='output'&&g.direction==='output'&&g.coil===regAddr) gpioPin=g.pin;
      });
      const tr=document.createElement('tr');
      const bj=JSON.stringify({index:b.index,name:b.name||'',type:b.type||'',direction:b.direction||'output',register_type:b.register_type||'HR',register_addr:b.register_addr||0,gpio_pin:gpioPin}).replace(/'/g,'&#39;');
      const gpioTd=gpioPin>=0?'<span style="color:#a6e3a1">GPIO '+gpioPin+'</span>':'<span style="color:#585b70">—</span>';
      tr.innerHTML='<td>'+(b.name||'?')+'</td><td>'+(b.type||'?')+'</td><td>'+
        (b.direction==='input'?'← Input':'Output →')+'</td><td>'+b.register_type+
        '</td><td>'+b.register_addr+'</td><td>'+gpioTd+'</td><td><button class="btn btn-sm" style="background:#89b4fa;color:#1e1e2e;margin-right:4px" onclick=\'editBinding('+bj+')\'>Redigér</button><button class="btn btn-danger btn-sm" onclick="delBinding('+b.index+')">Slet</button></td>';
      body.appendChild(tr);
    });
  }catch(e){log('error','Kunne ikke hente bindinger: '+e.message);}
}

async function addBinding(){
  const vn=document.getElementById('bindVar').value;
  const dir=document.getElementById('bindDir').value;
  const tp=document.getElementById('bindType').value;
  const addr=document.getElementById('bindAddr').value;
  const gpioVal=document.getElementById('bindGpio').value;
  if(!vn||!addr){log('error','Udfyld alle felter');return;}
  try{
    if(editIdx>=0){await api('DELETE','bindings/'+editIdx);}
    const d=await api('POST','logic/'+SLOT+'/bind',{variable:vn,binding:tp+':'+addr,direction:dir});
    if(d.error){log('error','Binding fejl: '+d.error);}
    else{log('success',editIdx>=0?'Binding opdateret: '+vn+' → '+tp+':'+addr:'Binding oprettet: '+vn+' → '+tp+':'+addr);}
    // GPIO: create, update, or delete mapping
    const newPin=gpioVal!==''?parseInt(gpioVal):-1;
    if(newPin>=0&&newPin<=255){
      // Delete old GPIO if pin changed
      if(editGpioPin>=0&&editGpioPin!==newPin){
        try{await api('DELETE','gpio/'+editGpioPin);}catch(e){}
      }
      try{
        const gpioDir=dir==='input'?'input':'output';
        const body=gpioDir==='input'?{direction:'input',register:parseInt(addr)}:{direction:'output',coil:parseInt(addr)};
        await api('POST','gpio/'+newPin+'/config',body);
        log('success','GPIO '+newPin+' konfigureret som '+gpioDir+' → '+(gpioDir==='input'?'reg':'coil')+':'+addr);
      }catch(ge){log('error','GPIO config fejl: '+ge.message);}
    }else if(editGpioPin>=0){
      // GPIO field cleared — delete existing mapping
      try{
        await api('DELETE','gpio/'+editGpioPin);
        log('info','GPIO '+editGpioPin+' mapping slettet');
      }catch(ge){log('error','GPIO slet fejl: '+ge.message);}
    }
    cancelEdit();
    loadBindings();
  }catch(e){log('error','Binding fejlede: '+e.message);}
}

async function delBinding(idx){
  if(!confirm('Slet denne binding?'))return;
  try{
    await api('DELETE','bindings/'+idx);
    log('info','Binding slettet');
    loadBindings();
  }catch(e){log('error','Slet fejlede: '+e.message);}
}

// === Monitor Panel with Sparklines ===
function startMonitor(){
  stopMonitor();
  monStartTime=Date.now();
  lastExecCount=0;
  document.getElementById('monSlot').textContent=SLOT;
  refreshMonitor();
  monTimer=setInterval(refreshMonitor,monInterval);
}
function changeMonSpeed(){
  monInterval=parseInt(document.getElementById('monSpeed').value);
  if(monTimer){stopMonitor();startMonitor();}
}
function changeHistLen(){
  const newLen=parseInt(document.getElementById('monHistLen').value);
  HIST_LEN=newLen;
  Object.keys(varHistory).forEach(k=>{
    if(varHistory[k].length>HIST_LEN)varHistory[k]=varHistory[k].slice(-HIST_LEN);
  });
}
function changeTrendColor(){
  trendColor=document.getElementById('monColor').value;
}
function stopMonitor(){if(monTimer){clearInterval(monTimer);monTimer=null;}}

function sparkSVG(entries,w,h){
  if(!entries||entries.length<2)return{svg:'',min:0,max:0};
  const vals=entries.map(e=>e.v);
  const mn=Math.min(...vals),mx=Math.max(...vals);
  const range=mx-mn||1;
  // X-axis: proportional to real timestamps
  const t0=entries[0].t,tEnd=entries[entries.length-1].t;
  const tRange=tEnd-t0||1;
  const pts=entries.map((e,i)=>{
    const x=((e.t-t0)/tRange)*w;
    const y=h-((e.v-mn)/range)*h;
    return x.toFixed(1)+','+y.toFixed(1);
  }).join(' ');
  const cur=vals[vals.length-1];
  const curY=h-((cur-mn)/range)*h;
  // Grid lines (oscilloscope style)
  let grid='';
  const gridColor='#313244';
  for(let i=0;i<=4;i++){
    const gy=(h*i/4).toFixed(1);
    grid+='<line x1="0" y1="'+gy+'" x2="'+w+'" y2="'+gy+'" stroke="'+gridColor+'" stroke-width="0.5"/>';
  }
  for(let i=0;i<=8;i++){
    const gx=(w*i/8).toFixed(1);
    grid+='<line x1="'+gx+'" y1="0" x2="'+gx+'" y2="'+h+'" stroke="'+gridColor+'" stroke-width="0.5"/>';
  }
  const clr=trendColor;
  const svg='<svg class="spark" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" style="background:#11111b;border-radius:3px">'
    +grid
    +'<polyline fill="none" stroke="'+clr+'" stroke-width="1.5" points="'+pts+'"/>'
    +'<circle cx="'+w+'" cy="'+curY.toFixed(1)+'" r="2.5" fill="'+clr+'"/>'
    +'</svg>';
  return{svg:svg,min:mn,max:mx};
}

async function refreshMonitor(){
  if(monBusy)return;
  monBusy=true;
  document.getElementById('monSlot').textContent=SLOT;
  const p=programs[SLOT-1];
  if(!p||!p.compiled){
    document.getElementById('monEmpty').style.display='block';
    document.getElementById('monBody').innerHTML='';
    ['monExec','monTime','monMinMax','monErrors','monOverruns'].forEach(id=>document.getElementById(id).textContent='-');
    monBusy=false;
    return;
  }
  try{
    const d=await api('GET','logic/'+SLOT);
    const curExecCount=d.execution_count||0;
    const execDelta=curExecCount-lastExecCount;
    const firstPoll=(lastExecCount===0);
    const progressed=firstPoll||(execDelta>0);
    lastExecCount=curExecCount;

    const execLabel=firstPoll?curExecCount.toLocaleString()
      :curExecCount.toLocaleString()+(execDelta>0?' (+'+execDelta+')':' (stopped)');
    document.getElementById('monExec').textContent=execLabel;
    document.getElementById('monTime').textContent=d.last_execution_us||0;
    document.getElementById('monMinMax').textContent=(d.min_execution_us||0)+' / '+(d.max_execution_us||0);
    const errEl=document.getElementById('monErrors');
    errEl.textContent=d.error_count||0;
    errEl.className='mv'+((d.error_count>0)?' merr':'');
    document.getElementById('monOverruns').textContent=d.overrun_count||0;

    // Debug state - fetch from debug/state endpoint
    const dbgBadge=document.getElementById('dbgBadge');
    try{
      const ds=await api('GET','logic/'+SLOT+'/debug/state');
      dbgMode=ds.mode||'off';
      const modeMap={off:'dbg-off',paused:'dbg-pause',step:'dbg-pause',run:'dbg-run'};
      const modeLabel=dbgMode.charAt(0).toUpperCase()+dbgMode.slice(1);
      const pcInfo=ds.snapshot?' | PC='+ds.snapshot.pc:'';
      dbgBadge.textContent=modeLabel+pcInfo;
      dbgBadge.className='badge '+(modeMap[dbgMode]||'dbg-off');
    }catch(e){
      dbgMode='off';
      dbgBadge.textContent='Normal';
      dbgBadge.className='badge dbg-off';
    }

    const trendPaused=!progressed;
    const nowMs=Date.now();

    const body=document.getElementById('monBody');
    body.innerHTML='';
    const vars=d.variables||[];
    document.getElementById('monEmpty').style.display=vars.length?'none':'block';
    vars.forEach(v=>{
      // Track history with real timestamps and execution count
      if(!varHistory[v.name])varHistory[v.name]=[];
      const numVal=v.type==='BOOL'?(v.value?1:0):parseFloat(v.value)||0;
      if(!trendPaused){
        varHistory[v.name].push({v:numVal,t:nowMs,exec:curExecCount});
        if(varHistory[v.name].length>HIST_LEN)varHistory[v.name].shift();
      }

      const tr=document.createElement('tr');
      const val=v.type==='BOOL'?(v.value?'TRUE':'FALSE'):v.value;
      const hist=varHistory[v.name];
      const trend=sparkSVG(hist,320,40);
      // Calculate real elapsed time from timestamps
      let timeStr;
      if(hist.length>=2){
        const spanMs=hist[hist.length-1].t-hist[0].t;
        const spanSec=spanMs/1000;
        const execSpan=hist[hist.length-1].exec-hist[0].exec;
        const timeP=spanSec>=60?(spanSec/60).toFixed(1)+'m':spanSec.toFixed(0)+'s';
        timeStr=timeP+' #'+execSpan;
      }else{
        timeStr='-';
      }
      if(trendPaused&&hist.length>=2) timeStr+=' \u23F8';
      const labelsHtml=trend.svg?'<div class="trend-labels"><span class="tmax">'+trend.max.toFixed(2)+'</span><span class="tmin">'+trend.min.toFixed(2)+'</span></div>':'';
      const timeClass=trendPaused?'trend-time trend-paused':'trend-time';
      const trendHtml=trend.svg?'<div class="trend-cell">'+labelsHtml+trend.svg+'<div class="'+timeClass+'">'+timeStr+'</div></div>':'';
      tr.innerHTML='<td>'+v.name+'</td><td>'+v.type+'</td><td style="font-family:monospace;color:'+(v.type==='BOOL'?(v.value?'#a6e3a1':'#6c7086'):'#f9e2af')+'">'+val+'</td><td>'+trendHtml+'</td>';
      body.appendChild(tr);
    });
  }catch(e){}
  monBusy=false;
}

// === Debug Control (uses existing FEAT-008 API: /api/logic/{id}/debug/{action}) ===
async function dbgAction(action){
  try{
    await api('POST','logic/'+SLOT+'/debug/'+action);
    // Fetch updated state
    const d=await api('GET','logic/'+SLOT+'/debug/state');
    log('info','Debug: '+action+' → '+(d.mode||'?'));
    refreshMonitor();
  }catch(e){log('error','Debug fejl: '+e.message);}
}

// === User Badge ===
function toggleUserMenu(){var m=document.getElementById('userMenu');m.classList.toggle('show')}
document.addEventListener('click',function(e){var b=document.getElementById('userBtn');var m=document.getElementById('userMenu');if(b&&m&&!b.contains(e.target)&&!m.contains(e.target))m.classList.remove('show')});
function updateUserBadge(){
var auth=sessionStorage.getItem('hfplc_auth');
var opts=auth?{headers:{'Authorization':auth}}:{};
fetch('/api/user/me',opts).then(function(r){return r.json()}).then(function(d){
if(d.authenticated){document.getElementById('userName').textContent=d.username;document.getElementById('userDot').className='dot dot-on';document.getElementById('umUser').textContent=d.username;document.getElementById('umRoles').textContent=d.roles||'all';document.getElementById('umPriv').textContent=d.privilege||'rw';document.getElementById('umMode').textContent=d.mode||'legacy';document.getElementById('umLogout').style.display='block'}
else{document.getElementById('userName').textContent='Ikke logget ind';document.getElementById('userDot').className='dot dot-off';document.getElementById('umLogout').style.display='none'}
}).catch(function(){})
}
function doLogout(){sessionStorage.removeItem('hfplc_auth');AUTH='';document.getElementById('userName').textContent='Ikke logget ind';document.getElementById('userDot').className='dot dot-off';document.getElementById('userMenu').classList.remove('show');if(document.getElementById('loginModal'))document.getElementById('loginModal').classList.add('show')}
async function doGlobalSave(){var btn=document.getElementById('saveBtn');btn.classList.add('saving');btn.textContent='\u23F3 Gemmer...';try{var h={method:'POST'};if(AUTH)h.headers={'Authorization':AUTH};var r=await fetch('/api/system/save',h);if(r.ok){btn.classList.remove('saving');btn.classList.add('saved');btn.textContent='\u2705 Gemt!';}else{btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}}catch(e){btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}setTimeout(()=>{btn.className='save-btn';btn.innerHTML='&#128190; Save';},2000);}

// === FEAT-132: File Upload ===
function handleFileUpload(e){
  const f=e.target.files[0];
  if(!f)return;
  if(f.size>16384){log('error','Fil for stor ('+f.size+' bytes, max 16KB)');return;}
  const reader=new FileReader();
  reader.onload=function(ev){
    const ed=document.getElementById('editor');
    if(dirty&&!confirm('Overskriv nuværende kode med fil-indhold?')){e.target.value='';return;}
    ed.value=ev.target.result;
    dirty=true;
    errorLine=0;
    updateLines();syncHighlight();
    log('success','Indlæst: '+f.name+' ('+f.size+' bytes)');
    e.target.value='';  // reset file input
  };
  reader.onerror=function(){log('error','Kunne ikke læse fil');};
  reader.readAsText(f);
}

// === FEAT-133: Find/Replace ===
function toggleFind(){
  const bar=document.getElementById('findBar');
  const show=!bar.classList.contains('show');
  bar.classList.toggle('show',show);
  if(show){
    const ed=document.getElementById('editor');
    const sel=ed.value.substring(ed.selectionStart,ed.selectionEnd);
    const fi=document.getElementById('findInput');
    if(sel&&sel.length<64)fi.value=sel;
    fi.focus();fi.select();
    updateFindCount();
  }
}
function updateFindCount(){
  const needle=document.getElementById('findInput').value;
  const info=document.getElementById('findInfo');
  if(!needle){info.textContent='0 resultater';return;}
  const ed=document.getElementById('editor');
  let count=0,idx=0;
  const hay=ed.value;
  while((idx=hay.indexOf(needle,idx))>=0){count++;idx+=needle.length;}
  info.textContent=count+' resultater';
}
function findNext(){
  const needle=document.getElementById('findInput').value;
  if(!needle)return;
  const ed=document.getElementById('editor');
  const start=ed.selectionEnd;
  let idx=ed.value.indexOf(needle,start);
  if(idx<0)idx=ed.value.indexOf(needle);  // wrap
  if(idx<0){log('info','Ikke fundet: '+needle);return;}
  ed.focus();
  ed.selectionStart=idx;
  ed.selectionEnd=idx+needle.length;
  // Scroll into view
  const before=ed.value.substring(0,idx);
  const line=before.split('\n').length;
  ed.scrollTop=Math.max(0,(line-4)*18);
}
function findPrev(){
  const needle=document.getElementById('findInput').value;
  if(!needle)return;
  const ed=document.getElementById('editor');
  const before=ed.value.substring(0,Math.max(0,ed.selectionStart-1));
  let idx=before.lastIndexOf(needle);
  if(idx<0)idx=ed.value.lastIndexOf(needle);
  if(idx<0){log('info','Ikke fundet: '+needle);return;}
  ed.focus();
  ed.selectionStart=idx;
  ed.selectionEnd=idx+needle.length;
  const beforeSel=ed.value.substring(0,idx);
  const line=beforeSel.split('\n').length;
  ed.scrollTop=Math.max(0,(line-4)*18);
}
function replaceOne(){
  const needle=document.getElementById('findInput').value;
  const repl=document.getElementById('replaceInput').value;
  if(!needle)return;
  const ed=document.getElementById('editor');
  const sel=ed.value.substring(ed.selectionStart,ed.selectionEnd);
  if(sel===needle){
    ed.value=ed.value.substring(0,ed.selectionStart)+repl+ed.value.substring(ed.selectionEnd);
    ed.selectionStart=ed.selectionEnd=ed.selectionStart+repl.length;
    dirty=true;updateLines();syncHighlight();
  }
  findNext();
  updateFindCount();
}
function replaceAll(){
  const needle=document.getElementById('findInput').value;
  const repl=document.getElementById('replaceInput').value;
  if(!needle)return;
  const ed=document.getElementById('editor');
  const parts=ed.value.split(needle);
  const count=parts.length-1;
  if(count===0){log('info','Ikke fundet: '+needle);return;}
  ed.value=parts.join(repl);
  dirty=true;updateLines();syncHighlight();
  log('success','Erstattede '+count+' forekomster');
  updateFindCount();
}
function findKey(e){
  if(e.key==='Enter'){e.preventDefault();if(e.shiftKey)findPrev();else findNext();}
  else if(e.key==='Escape'){e.preventDefault();toggleFind();}
  else setTimeout(updateFindCount,0);
}
document.addEventListener('keydown',function(e){
  if((e.ctrlKey||e.metaKey)&&(e.key==='f'||e.key==='h')){
    e.preventDefault();
    if(!document.getElementById('findBar').classList.contains('show'))toggleFind();
    if(e.key==='h')document.getElementById('replaceInput').focus();
  }
});

// === FEAT-130: SSE-driven editor monitor refresh ===
function editorSseConnect(){
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts=auth?{headers:{'Authorization':auth}}:{};
    fetch('/api/events/status',opts).then(r=>r.json()).then(s=>{
      if(!s||!s.sse_enabled)return;
      let port=s.sse_port;
      if(!port||port===0){
        const mainPort=parseInt(location.port||'80')||80;
        port=mainPort+1;
      }
      const tok=s.sse_token?('&token='+encodeURIComponent(s.sse_token)):'';
      const url=location.protocol+'//'+location.hostname+':'+port+'/api/events?subscribe=all'+tok;
      try{sseConn=new EventSource(url);}catch(e){return;}
      // SSE events only trigger a flag; polling timer handles actual refresh
      function kickMonitor(){
        // SSE events are informational — polling timer controls rate
        // No direct refreshMonitor() call to avoid bypassing interval
      }
      sseConn.addEventListener('register',kickMonitor);
      sseConn.addEventListener('counter',kickMonitor);
      sseConn.addEventListener('timer',kickMonitor);
      sseConn.onerror=()=>{if(sseConn){sseConn.close();sseConn=null;}setTimeout(editorSseConnect,10000);};
    }).catch(()=>{});
  }catch(e){}
}
editorSseConnect();

// Pause monitor polling when tab is hidden
document.addEventListener('visibilitychange',function(){
  if(document.hidden&&monTimer){clearInterval(monTimer);monTimer=null;}
  else if(!document.hidden&&VIEW==='monitor'&&!monTimer){startMonitor();}
});
updateUserBadge();

</script>
</body>
</html>)rawhtml";

/* ============================================================================
 * HTTP HANDLER
 * ============================================================================ */

esp_err_t web_editor_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, editor_html, strlen(editor_html));
}
