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
.btbl th{text-align:left;padding:6px 8px;background:#181825;color:#89b4fa;border-bottom:1px solid #313244;font-weight:600}
.btbl td{padding:5px 8px;border-bottom:1px solid #313244}
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
.dbg-bar .badge{padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600}
.dbg-run{background:#a6e3a1;color:#1e1e2e}.dbg-pause{background:#f9e2af;color:#1e1e2e}.dbg-off{background:#45475a;color:#a6adc8}
.spark{display:inline-block;vertical-align:middle;margin-left:6px}
@media(max-width:768px){.sidebar{display:none}.pool-bar{max-width:120px}.mstats{gap:6px}.mstat{min-width:80px;padding:6px 8px}}
</style>
</head>
<body>

<!-- Login Modal -->
<div class="modal-bg show" id="loginModal">
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
<div class="view-btns">
<button class="vbtn act" onclick="setView('editor',this)">Editor</button>
<button class="vbtn" onclick="setView('bindings',this)">Bindings</button>
<button class="vbtn" onclick="setView('monitor',this)">Monitor</button>
</div>
<div style="flex:1"></div>
<span style="font-size:11px;color:#6c7086">Pool:</span>
<div class="pool-bar">
<div class="pool-fill" id="poolFill"></div>
<div class="pool-text" id="poolText">-</div>
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
<button class="btn btn-sm btn-warn" onclick="dbgAction('pause')">Pause</button>
<button class="btn btn-sm btn-primary" onclick="dbgAction('step')">Step</button>
<button class="btn btn-sm btn-success" onclick="dbgAction('continue')">Continue</button>
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="dbgAction('stop')">Stop Debug</button>
<span class="badge dbg-off" id="dbgBadge">Normal</span>
<span style="font-size:11px;color:#6c7086" id="dbgPC"></span>
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
let varHistory={};  // sparkline data: {varName: [values]}
const HIST_LEN=60;

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

async function init(){
  try{
    const st=await api('GET','status');
    document.getElementById('deviceInfo').textContent=
      'v'+(st.version||'?')+' Build #'+(st.build||'?')+' — Heap: '+((st.heap_free/1024)|0)+'KB fri';
  }catch(e){}
  await loadAll();
}

async function loadAll(){
  try{
    const d=await api('GET','logic');
    if(d.programs){
      d.programs.forEach((p,i)=>{programs[i]=p;});
    }
    updateTabs();
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
    }else{
      log('error','Kompileringsfejl: '+(d.compile_error||d.error||'ukendt fejl'));
      updateStatus('err','Fejl');
    }
    dirty=false;
    await loadAll();
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

function updatePool(){
  let used=0,total=8000;
  programs.forEach(p=>{if(p)used+=(p.source_size||0);});
  const pct=Math.min(100,(used/total*100)|0);
  document.getElementById('poolFill').style.width=pct+'%';
  document.getElementById('poolFill').style.background=pct>90?'#f38ba8':pct>70?'#fab387':'#89b4fa';
  document.getElementById('poolText').textContent=used+'/'+total+' ('+pct+'%)';
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
  const nums=[];for(let i=1;i<=n;i++)nums.push(i);
  document.getElementById('lineNums').textContent=nums.join('\n');
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
// Auto-login if session auth exists
if(AUTH){fetch('/api/status',{headers:{'Authorization':AUTH}}).then(r=>{if(r.ok){document.getElementById('loginModal').classList.remove('show');init();updateUserBadge();}else{AUTH='';sessionStorage.removeItem('hfplc_auth');}}).catch(()=>{});}
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
  document.getElementById('monSlot').textContent=SLOT;
  refreshMonitor();
  monTimer=setInterval(refreshMonitor,1500);
}
function stopMonitor(){if(monTimer){clearInterval(monTimer);monTimer=null;}}

function sparkSVG(vals,w,h){
  if(!vals||vals.length<2)return'';
  const mn=Math.min(...vals),mx=Math.max(...vals);
  const range=mx-mn||1;
  const pts=vals.map((v,i)=>{
    const x=(i/(vals.length-1))*w;
    const y=h-((v-mn)/range)*h;
    return x.toFixed(1)+','+y.toFixed(1);
  }).join(' ');
  return'<svg class="spark" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'"><polyline fill="none" stroke="#89b4fa" stroke-width="1.5" points="'+pts+'"/></svg>';
}

async function refreshMonitor(){
  document.getElementById('monSlot').textContent=SLOT;
  const p=programs[SLOT-1];
  if(!p||!p.compiled){
    document.getElementById('monEmpty').style.display='block';
    document.getElementById('monBody').innerHTML='';
    ['monExec','monTime','monMinMax','monErrors','monOverruns'].forEach(id=>document.getElementById(id).textContent='-');
    return;
  }
  try{
    const d=await api('GET','logic/'+SLOT);
    document.getElementById('monExec').textContent=(d.execution_count||0).toLocaleString();
    document.getElementById('monTime').textContent=d.last_execution_us||0;
    document.getElementById('monMinMax').textContent=(d.min_execution_us||0)+' / '+(d.max_execution_us||0);
    const errEl=document.getElementById('monErrors');
    errEl.textContent=d.error_count||0;
    errEl.className='mv'+((d.error_count>0)?' merr':'');
    document.getElementById('monOverruns').textContent=d.overrun_count||0;

    // Debug state - fetch from debug/state endpoint
    const dbgBadge=document.getElementById('dbgBadge');
    const dbgPC=document.getElementById('dbgPC');
    try{
      const ds=await api('GET','logic/'+SLOT+'/debug/state');
      const modeMap={off:'dbg-off',paused:'dbg-pause',step:'dbg-pause',run:'dbg-run'};
      dbgBadge.textContent=(ds.mode||'off').charAt(0).toUpperCase()+(ds.mode||'off').slice(1);
      dbgBadge.className='badge '+(modeMap[ds.mode]||'dbg-off');
      dbgPC.textContent=ds.snapshot?'PC='+ds.snapshot.pc:'';
    }catch(e){
      dbgBadge.textContent='Normal';
      dbgBadge.className='badge dbg-off';
      dbgPC.textContent='';
    }

    const body=document.getElementById('monBody');
    body.innerHTML='';
    const vars=d.variables||[];
    document.getElementById('monEmpty').style.display=vars.length?'none':'block';
    vars.forEach(v=>{
      // Track history
      if(!varHistory[v.name])varHistory[v.name]=[];
      const numVal=v.type==='BOOL'?(v.value?1:0):parseFloat(v.value)||0;
      varHistory[v.name].push(numVal);
      if(varHistory[v.name].length>HIST_LEN)varHistory[v.name].shift();

      const tr=document.createElement('tr');
      const val=v.type==='BOOL'?(v.value?'TRUE':'FALSE'):v.value;
      const spark=sparkSVG(varHistory[v.name],80,20);
      tr.innerHTML='<td>'+v.name+'</td><td>'+v.type+'</td><td style="font-family:monospace;color:'+(v.type==='BOOL'?(v.value?'#a6e3a1':'#6c7086'):'#f9e2af')+'">'+val+'</td><td>'+spark+'</td>';
      body.appendChild(tr);
    });
  }catch(e){}
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
