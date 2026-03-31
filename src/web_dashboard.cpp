/**
 * @file web_dashboard.cpp
 * @brief Web-based metrics dashboard (v7.4.0)
 *
 * LAYER 7: User Interface - Web Dashboard
 * Serves a single-page dashboard at "/" showing live Prometheus metrics.
 * Fetches /api/metrics every 3 seconds and parses Prometheus text format.
 *
 * Features: Live metrics, history graphs, alarms, register viewer, register map
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_dashboard.h"
#include "debug.h"

static const char PROGMEM dashboard_html[] = R"rawhtml(<!DOCTYPE html>
<html lang="da">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Monitor Dashboard</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,sans-serif;background:#1e1e2e;color:#cdd6f4;height:100vh;display:flex;flex-direction:column;overflow:hidden}
.topnav{background:#11111b;padding:6px 16px;display:flex;gap:8px;border-bottom:2px solid #89b4fa;flex-shrink:0;align-items:center}
.topnav .brand{font-size:14px;font-weight:700;color:#89b4fa;margin-right:12px}
.topnav a{padding:5px 14px;font-size:12px;font-weight:600;background:#313244;color:#a6adc8;border-radius:4px;text-decoration:none;transition:all .15s}
.topnav a:hover{background:#45475a;color:#cdd6f4}
.topnav a.active{background:#89b4fa;color:#1e1e2e}
.user-badge{margin-left:auto;position:relative;display:flex;align-items:center;gap:6px}
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
.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.7);z-index:1000;align-items:center;justify-content:center}
.modal-bg.show{display:flex}
.modal{background:#1e1e2e;border:1px solid #45475a;border-radius:8px;padding:24px;width:320px;max-width:90vw}
.modal h2{color:#89b4fa;font-size:16px;margin:0 0 8px}
.modal label{display:block;color:#a6adc8;font-size:11px;margin:8px 0 3px}
.modal input{width:100%;padding:6px 8px;background:#181825;border:1px solid #45475a;border-radius:4px;color:#cdd6f4;font-size:13px;box-sizing:border-box}
.modal .actions{margin-top:14px;text-align:right}
.btn-primary{padding:6px 16px;background:#89b4fa;color:#1e1e2e;border:none;border-radius:4px;font-size:12px;cursor:pointer;font-weight:600}
.btn-primary:hover{background:#74c7ec}
.hdr{background:#181825;padding:10px 16px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #313244;flex-shrink:0}
.hdr h1{font-size:16px;color:#89b4fa;flex-shrink:0}
.hdr .nav{margin-left:16px;display:flex;gap:6px}
.hdr .nav a,.hdr .nav button{padding:4px 12px;font-size:12px;background:#313244;color:#a6adc8;border-radius:4px;text-decoration:none;transition:all .15s;border:none;cursor:pointer}
.hdr .nav a:hover,.hdr .nav button:hover{background:#45475a;color:#cdd6f4}
.hdr .nav button.active{background:#89b4fa;color:#1e1e2e}
.hdr .info{font-size:11px;color:#6c7086;margin-left:auto}
.alarm-bar{background:#f38ba8;color:#1e1e2e;padding:4px 16px;font-size:12px;font-weight:600;display:none;flex-shrink:0;animation:pulse 1.5s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.7}}
.grid{flex:1;display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:12px;padding:16px;overflow-y:auto;align-content:start}
.card{background:#181825;border:1px solid #313244;border-radius:8px;padding:14px 16px}
.card h2{font-size:13px;color:#89b4fa;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid #313244;display:flex;align-items:center;gap:6px}
.card h2 .badge{font-size:10px;padding:1px 6px;border-radius:8px;font-weight:400}
.badge-ok{background:#a6e3a1;color:#1e1e2e}
.badge-err{background:#f38ba8;color:#1e1e2e}
.badge-off{background:#45475a;color:#a6adc8}
.row{display:flex;justify-content:space-between;padding:3px 0;font-size:12px}
.row .lbl{color:#6c7086}
.row .val{font-weight:600;font-variant-numeric:tabular-nums}
.val-ok{color:#a6e3a1}.val-err{color:#f38ba8}.val-warn{color:#fab387}.val-n{color:#cdd6f4}
.bar-wrap{height:6px;background:#313244;border-radius:3px;margin-top:4px;overflow:hidden}
.bar-fill{height:100%;border-radius:3px;transition:width .5s}
.bar-ok{background:#a6e3a1}.bar-warn{background:#fab387}.bar-err{background:#f38ba8}
.tbl{width:100%;border-collapse:collapse;font-size:11px;margin-top:6px}
.tbl th{text-align:left;padding:4px 6px;color:#89b4fa;border-bottom:1px solid #313244;font-weight:600}
.tbl td{padding:3px 6px;border-bottom:1px solid rgba(49,50,68,.5)}
.tbl tr:hover{background:rgba(49,50,68,.3)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;vertical-align:middle}
.dot-g{background:#a6e3a1}.dot-r{background:#f38ba8}.dot-y{background:#fab387}.dot-off{background:#45475a}
.empty-msg{color:#6c7086;font-size:11px;padding:8px 0}
.spark-row{display:flex;align-items:center;gap:8px;margin-top:4px}
.spark-row .sl{font-size:10px;color:#6c7086;min-width:70px}
.regview{display:grid;grid-template-columns:repeat(16,1fr);gap:1px;font-size:9px;font-family:monospace}
.regview .rc{padding:2px;text-align:center;background:#313244;border-radius:2px;min-height:20px;display:flex;align-items:center;justify-content:center;cursor:default}
.regview .rc:hover{background:#45475a}
.regview .rc.used{background:#1e3a5f;color:#89b4fa}
.regview .rc.coil-on{background:#2a4a2a;color:#a6e3a1}
.regview .rc.coil-off{background:#313244;color:#585b70}
.regmap{display:flex;flex-wrap:wrap;gap:4px;margin-top:6px}
.regmap .rm{padding:3px 8px;border-radius:3px;font-size:10px;white-space:nowrap}
.rm-counter{background:#1e3a5f;color:#89b4fa}
.rm-timer{background:#3a1e5f;color:#cba6f7}
.rm-st{background:#1e5f3a;color:#a6e3a1}
.rm-manual{background:#5f3a1e;color:#fab387}
.rm-free{background:#313244;color:#585b70}
.page-view{display:none;flex:1;overflow-y:auto;padding:16px}
.page-view.active{display:block}
.foot{background:#181825;border-top:1px solid #313244;padding:4px 16px;font-size:10px;color:#45475a;flex-shrink:0;display:flex;justify-content:space-between}
@media(max-width:768px){.grid{grid-template-columns:1fr;gap:8px;padding:8px}}
</style>
</head>
<body>

<!-- Login Modal (hidden by default on dashboard) -->
<div class="modal-bg" id="loginModal">
<div class="modal">
<h2>Login</h2>
<p style="font-size:12px;color:#6c7086;margin-bottom:12px">Log ind for at se brugerinfo</p>
<div id="loginErr" style="display:none;color:#f38ba8;font-size:12px;padding:6px 0"></div>
<label>Brugernavn</label>
<input type="text" id="authUser" placeholder="Brugernavn" autocomplete="username">
<label>Adgangskode</label>
<input type="password" id="authPass" placeholder="Adgangskode" autocomplete="current-password">
<div class="actions">
<button class="btn-primary" onclick="doLogin()">Log ind</button>
</div>
</div>
</div>

<!-- Top Navigation -->
<div class="topnav">
<span class="brand">HyberFusion PLC</span>
<a href="/" class="active">Monitor</a>
<a href="/editor">ST Editor</a>
<a href="/cli">CLI</a>
<a href="/system">System</a>
<div class="user-badge"><div class="user-btn" id="userBtn" onclick="toggleUserMenu()"><span class="dot dot-off" id="userDot"></span><span id="userName">Ikke logget ind</span></div><div class="user-menu" id="userMenu"><div class="um-row"><span>Bruger:</span><span class="um-val" id="umUser">-</span></div><div class="um-row"><span>Roller:</span><span class="um-val" id="umRoles">-</span></div><div class="um-row"><span>Privilegier:</span><span class="um-val" id="umPriv">-</span></div><div class="um-row"><span>Auth mode:</span><span class="um-val" id="umMode">-</span></div><div class="um-sep"></div><div class="um-btn" id="umLogin" onclick="showLogin()">Log ind</div><div class="um-btn" id="umLogout" onclick="doLogout()" style="display:none">Log ud</div></div></div>
</div>

<!-- Header -->
<div class="hdr">
<h1>Monitor Dashboard</h1>
<div class="nav">
<button class="active" onclick="showPage('metrics',this)">Metrics</button>
<button onclick="showPage('registers',this)">Registre</button>
<button onclick="showPage('regmap',this)">Register Map</button>
</div>
<span class="info" id="hdrInfo"></span>
</div>

<!-- Alarm Bar -->
<div class="alarm-bar" id="alarmBar"></div>

<!-- Metrics Page -->
<div class="page-view active" id="pageMetrics">
<div class="grid" id="grid">

<!-- System Overview -->
<div class="card">
<h2>System</h2>
<div class="row"><span class="lbl">Uptime</span><span class="val val-n" id="mUptime">-</span></div>
<div class="row"><span class="lbl">Heap fri</span><span class="val val-n" id="mHeapFree">-</span></div>
<div class="row"><span class="lbl">Heap min</span><span class="val val-n" id="mHeapMin">-</span></div>
<div class="bar-wrap"><div class="bar-fill bar-ok" id="heapBar" style="width:0%"></div></div>
<div class="row" style="margin-top:2px"><span class="lbl">PSRAM</span><span class="val val-n" id="mPsram">-</span></div>
<div class="spark-row"><span class="sl">Heap trend</span><svg id="sparkHeap" width="200" height="24"></svg></div>
</div>

<!-- Network Status -->
<div class="card">
<h2>Netværk</h2>
<div class="row"><span class="lbl"><span class="dot dot-off" id="dotWifi"></span> WiFi</span><span class="val val-n" id="mWifi">-</span></div>
<div class="row"><span class="lbl"><span class="dot dot-off" id="dotEth"></span> Ethernet</span><span class="val val-n" id="mEth">-</span></div>
<div class="row"><span class="lbl"><span class="dot dot-off" id="dotTelnet"></span> Telnet</span><span class="val val-n" id="mTelnet">-</span></div>
<div class="row"><span class="lbl">SSE klienter</span><span class="val val-n" id="mSse">-</span></div>
<div class="row"><span class="lbl">WiFi reconnects</span><span class="val val-n" id="mWifiRetries">-</span></div>
</div>

<!-- Modbus Slave -->
<div class="card">
<h2>Modbus Slave <span class="badge badge-off" id="badgeSlave">-</span></h2>
<div class="row"><span class="lbl">Requests total</span><span class="val val-n" id="msTotal">-</span></div>
<div class="row"><span class="lbl">Success</span><span class="val val-ok" id="msOk">-</span></div>
<div class="row"><span class="lbl">CRC fejl</span><span class="val val-err" id="msCrc">-</span></div>
<div class="row"><span class="lbl">Exceptions</span><span class="val val-err" id="msExc">-</span></div>
<div class="row"><span class="lbl">Success rate</span><span class="val val-n" id="msRate">-</span></div>
<div class="spark-row"><span class="sl">Requests</span><svg id="sparkMbSlave" width="200" height="24"></svg></div>
</div>

<!-- Modbus Master -->
<div class="card">
<h2>Modbus Master <span class="badge badge-off" id="badgeMaster">-</span></h2>
<div class="row"><span class="lbl">Requests total</span><span class="val val-n" id="mmTotal">-</span></div>
<div class="row"><span class="lbl">Success</span><span class="val val-ok" id="mmOk">-</span></div>
<div class="row"><span class="lbl">Timeout fejl</span><span class="val val-err" id="mmTimeout">-</span></div>
<div class="row"><span class="lbl">CRC fejl</span><span class="val val-err" id="mmCrc">-</span></div>
<div class="row"><span class="lbl">Success rate</span><span class="val val-n" id="mmRate">-</span></div>
<div class="spark-row"><span class="sl">Requests</span><svg id="sparkMbMaster" width="200" height="24"></svg></div>
</div>

<!-- HTTP API -->
<div class="card">
<h2>HTTP API</h2>
<div class="row"><span class="lbl">Requests total</span><span class="val val-n" id="htTotal">-</span></div>
<div class="row"><span class="lbl">Success (2xx)</span><span class="val val-ok" id="htOk">-</span></div>
<div class="row"><span class="lbl">Client fejl (4xx)</span><span class="val val-err" id="ht4xx">-</span></div>
<div class="row"><span class="lbl">Server fejl (5xx)</span><span class="val val-err" id="ht5xx">-</span></div>
<div class="row"><span class="lbl">Auth failures</span><span class="val val-warn" id="htAuth">-</span></div>
</div>

<!-- Counters -->
<div class="card">
<h2>Tællere</h2>
<div id="cntBody"><span class="empty-msg">Ingen aktive tællere</span></div>
</div>

<!-- Timers -->
<div class="card">
<h2>Timere</h2>
<div id="tmrBody"><span class="empty-msg">Ingen aktive timere</span></div>
</div>

<!-- ST Logic -->
<div class="card">
<h2>ST Logic <span class="badge badge-off" id="badgeLogic">-</span></h2>
<div class="row"><span class="lbl">Total cycles</span><span class="val val-n" id="stCycles">-</span></div>
<div class="row"><span class="lbl">Overruns</span><span class="val val-n" id="stOverruns">-</span></div>
<div id="stBody"><span class="empty-msg">Ingen aktive programmer</span></div>
</div>

</div>
</div>

<!-- Register Viewer Page -->
<div class="page-view" id="pageRegisters">
<div class="card" style="margin-bottom:12px">
<h2>Holding Registers (HR 0-255)</h2>
<div style="font-size:10px;color:#6c7086;margin-bottom:6px">16 kolonner × 16 rækker — hover for detaljer</div>
<div class="regview" id="regHR"></div>
</div>
<div class="card">
<h2>Coils (0-255)</h2>
<div style="font-size:10px;color:#6c7086;margin-bottom:6px">Grøn = ON, grå = OFF</div>
<div class="regview" id="regCoils"></div>
</div>
</div>

<!-- Register Map Page -->
<div class="page-view" id="pageRegmap">
<div class="card" style="margin-bottom:12px">
<h2>Register Allokering</h2>
<p style="font-size:11px;color:#6c7086;margin-bottom:8px">Oversigt over Modbus register-brug — hvem ejer hvad</p>
<div class="regmap" id="regmapLegend">
<span class="rm rm-counter">Counter</span>
<span class="rm rm-timer">Timer</span>
<span class="rm rm-st">ST Logic</span>
<span class="rm rm-manual">Manual/System</span>
<span class="rm rm-free">Ledig</span>
</div>
</div>
<div class="card">
<h2>Holding Register Map (HR 0-255)</h2>
<div class="regview" id="regmapHR"></div>
</div>
<div class="card">
<h2>Allokerings-tabel</h2>
<table class="tbl" id="allocTable">
<thead><tr><th>Ejer</th><th>Type</th><th>Start</th><th>Slut</th><th>Antal</th></tr></thead>
<tbody id="allocBody"></tbody>
</table>
</div>
</div>

<!-- Footer -->
<div class="foot">
<span id="footRefresh">Opdaterer...</span>
<span id="footVer"></span>
</div>

<script>
let refreshTimer=null;
const HIST_MAX=120; // 120 samples × 3s = 6 min
let history={heap:[],mbSlave:[],mbMaster:[]};
let prevMbSlave=null,prevMbMaster=null;
let alarms=[];

function showPage(page,btn){
  document.querySelectorAll('.page-view').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.hdr .nav button').forEach(b=>b.classList.remove('active'));
  const el=document.getElementById('page'+page.charAt(0).toUpperCase()+page.slice(1));
  if(el)el.classList.add('active');
  if(btn)btn.classList.add('active');
}

function parsePrometheus(text){
  const m={};
  for(const line of text.split('\n')){
    if(!line||line.startsWith('#'))continue;
    const match=line.match(/^([a-zA-Z_:][a-zA-Z0-9_:]*)\{?([^}]*)\}?\s+(.+)$/);
    if(match){
      const name=match[1],lblStr=match[2],value=parseFloat(match[3]);
      const labels={};
      if(lblStr){const re=/(\w+)="([^"]*)"/g;let lm;while((lm=re.exec(lblStr))!==null)labels[lm[1]]=lm[2]}
      if(!m[name])m[name]=[];
      m[name].push({labels,value});
    }
  }
  return m;
}

function g(m,name,labels){
  const entries=m[name];
  if(!entries)return null;
  if(!labels)return entries[0]?.value??null;
  const e=entries.find(e=>Object.entries(labels).every(([k,v])=>e.labels[k]===v));
  return e?.value??null;
}
function gAll(m,name){return m[name]||[]}

function fmtUp(s){
  if(s==null)return'-';
  const d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),mn=Math.floor((s%3600)/60),sc=Math.floor(s%60);
  let r='';if(d>0)r+=d+'d ';
  r+=String(h).padStart(2,'0')+':'+String(mn).padStart(2,'0')+':'+String(sc).padStart(2,'0');
  return r;
}
function fmtKB(b){return b!=null?(b/1024).toFixed(1)+' KB':'-'}
function fmtN(v){return v!=null?v.toLocaleString('da-DK'):'-'}
function rate(ok,total){return total>0?(ok/total*100).toFixed(1)+'%':'N/A'}
function dot(el,on){el.className='dot '+(on?'dot-g':'dot-r')}
function badge(el,on,lbl){el.textContent=lbl||'';el.className='badge '+(on?'badge-ok':'badge-off')}
function $(id){return document.getElementById(id)}

// === Sparkline SVG ===
function drawSparkline(svgId,data,color){
  const svg=$(svgId);
  if(!svg||!data.length)return;
  const w=200,h=24;
  const mn=Math.min(...data),mx=Math.max(...data);
  const range=mx-mn||1;
  const pts=data.map((v,i)=>{
    const x=(i/(Math.max(data.length-1,1)))*w;
    const y=h-((v-mn)/range)*(h-2)-1;
    return x.toFixed(1)+','+y.toFixed(1);
  }).join(' ');
  svg.innerHTML='<polyline fill="none" stroke="'+(color||'#89b4fa')+'" stroke-width="1.5" points="'+pts+'"/>';
}

// === Alarm System ===
function checkAlarms(m){
  alarms=[];
  const heap=g(m,'esp32_heap_free_bytes');
  if(heap!=null&&heap<30000)alarms.push('Heap kritisk lav: '+fmtKB(heap));
  const heapMin=g(m,'esp32_heap_min_free_bytes');
  if(heapMin!=null&&heapMin<20000)alarms.push('Heap minimum nået: '+fmtKB(heapMin));
  const msCrc=g(m,'modbus_slave_crc_errors_total');
  if(msCrc!=null&&msCrc>100)alarms.push('Modbus Slave CRC fejl: '+fmtN(msCrc));
  const mmTo=g(m,'modbus_master_timeout_errors_total');
  if(mmTo!=null&&mmTo>50)alarms.push('Modbus Master timeouts: '+fmtN(mmTo));
  const stOvr=g(m,'st_logic_cycle_overruns');
  const stCyc=g(m,'st_logic_total_cycles');
  const stOvrPct=(stCyc!=null&&stCyc>100)?(stOvr/stCyc*100):0;
  if(stOvr!=null&&stOvrPct>5)alarms.push('ST Logic overruns: '+fmtN(stOvr)+' ('+stOvrPct.toFixed(1)+'%)');
  const authFail=g(m,'http_auth_failures_total');
  if(authFail!=null&&authFail>20)alarms.push('HTTP auth failures: '+fmtN(authFail));

  const bar=$('alarmBar');
  if(alarms.length){
    bar.style.display='block';
    bar.textContent='ALARM: '+alarms.join(' | ');
  }else{
    bar.style.display='none';
  }
}

function updateDashboard(m){
  // System
  $('mUptime').textContent=fmtUp(g(m,'esp32_uptime_seconds'));
  const heapFree=g(m,'esp32_heap_free_bytes');
  const heapMin=g(m,'esp32_heap_min_free_bytes');
  $('mHeapFree').textContent=fmtKB(heapFree);
  $('mHeapMin').textContent=fmtKB(heapMin);
  if(heapFree!=null){
    const pct=Math.min(100,heapFree/200000*100);
    const bar=$('heapBar');
    bar.style.width=pct.toFixed(0)+'%';
    bar.className='bar-fill '+(pct>40?'bar-ok':pct>20?'bar-warn':'bar-err');
    history.heap.push(heapFree/1024);
    if(history.heap.length>HIST_MAX)history.heap.shift();
    drawSparkline('sparkHeap',history.heap,'#a6e3a1');
  }
  const psram=g(m,'esp32_psram_total_bytes');
  const psramFree=g(m,'esp32_psram_free_bytes');
  if(psram!=null&&psram>0){
    $('mPsram').textContent=fmtKB(psramFree)+' / '+fmtKB(psram);
  }else{
    $('mPsram').textContent='Ikke tilgængelig';
  }

  // Network
  const wifiOn=g(m,'wifi_connected')===1;
  dot($('dotWifi'),wifiOn);
  const rssi=g(m,'wifi_rssi_dbm');
  $('mWifi').textContent=wifiOn?(rssi!=null?'RSSI '+rssi+' dBm':'Forbundet'):'Ikke forbundet';
  const ethOn=g(m,'ethernet_connected')===1;
  dot($('dotEth'),ethOn);
  $('mEth').textContent=ethOn?'Forbundet':'Ikke forbundet';
  const telOn=g(m,'telnet_connected')===1;
  dot($('dotTelnet'),telOn);
  $('mTelnet').textContent=telOn?'Forbundet':'Ikke forbundet';
  $('mSse').textContent=fmtN(g(m,'sse_clients_active'));
  $('mWifiRetries').textContent=fmtN(g(m,'wifi_reconnect_retries'));

  // Modbus Slave
  const msT=g(m,'modbus_slave_requests_total');
  const msO=g(m,'modbus_slave_success_total');
  $('msTotal').textContent=fmtN(msT);
  $('msOk').textContent=fmtN(msO);
  $('msCrc').textContent=fmtN(g(m,'modbus_slave_crc_errors_total'));
  $('msExc').textContent=fmtN(g(m,'modbus_slave_exceptions_total'));
  $('msRate').textContent=rate(msO,msT);
  badge($('badgeSlave'),msT>0,msT>0?'Aktiv':'Idle');
  if(msT!=null){
    const delta=prevMbSlave!=null?msT-prevMbSlave:0;
    prevMbSlave=msT;
    history.mbSlave.push(delta);
    if(history.mbSlave.length>HIST_MAX)history.mbSlave.shift();
    drawSparkline('sparkMbSlave',history.mbSlave,'#89b4fa');
  }

  // Modbus Master
  const mmT=g(m,'modbus_master_requests_total');
  const mmO=g(m,'modbus_master_success_total');
  $('mmTotal').textContent=fmtN(mmT);
  $('mmOk').textContent=fmtN(mmO);
  $('mmTimeout').textContent=fmtN(g(m,'modbus_master_timeout_errors_total'));
  $('mmCrc').textContent=fmtN(g(m,'modbus_master_crc_errors_total'));
  $('mmRate').textContent=rate(mmO,mmT);
  badge($('badgeMaster'),mmT>0,mmT>0?'Aktiv':'Idle');
  if(mmT!=null){
    const delta=prevMbMaster!=null?mmT-prevMbMaster:0;
    prevMbMaster=mmT;
    history.mbMaster.push(delta);
    if(history.mbMaster.length>HIST_MAX)history.mbMaster.shift();
    drawSparkline('sparkMbMaster',history.mbMaster,'#89b4fa');
  }

  // HTTP API
  $('htTotal').textContent=fmtN(g(m,'http_requests_total'));
  $('htOk').textContent=fmtN(g(m,'http_requests_success_total'));
  $('ht4xx').textContent=fmtN(g(m,'http_requests_client_errors_total'));
  $('ht5xx').textContent=fmtN(g(m,'http_requests_server_errors_total'));
  $('htAuth').textContent=fmtN(g(m,'http_auth_failures_total'));

  // Counters
  const cVals=gAll(m,'counter_value');
  const cFreqs=gAll(m,'counter_frequency_hz');
  if(cVals.length>0){
    let h='<table class="tbl"><tr><th>ID</th><th>Værdi</th><th>Frekvens</th></tr>';
    for(const c of cVals){
      const id=c.labels.id||'?';
      const freq=cFreqs.find(f=>f.labels.id===id);
      h+='<tr><td>#'+id+'</td><td>'+fmtN(c.value)+'</td><td>'+(freq?freq.value.toFixed(1)+' Hz':'-')+'</td></tr>';
    }
    h+='</table>';
    $('cntBody').innerHTML=h;
  }else{
    $('cntBody').innerHTML='<span class="empty-msg">Ingen aktive tællere</span>';
  }

  // Timers
  const tOut=gAll(m,'timer_output');
  const tRun=gAll(m,'timer_is_running');
  const tPhase=gAll(m,'timer_current_phase');
  const phases=['Idle','ON','OFF','Pause'];
  if(tOut.length>0){
    let h='<table class="tbl"><tr><th>ID</th><th>Output</th><th>Status</th><th>Fase</th></tr>';
    for(const t of tOut){
      const id=t.labels.id||'?';
      const run=tRun.find(r=>r.labels.id===id);
      const ph=tPhase.find(p=>p.labels.id===id);
      const isOn=t.value===1;
      const isRun=run?.value===1;
      const phName=ph?phases[ph.value]||'?':'-';
      h+='<tr><td>#'+id+'</td>';
      h+='<td><span class="dot '+(isOn?'dot-g':'dot-off')+'"></span> '+(isOn?'ON':'OFF')+'</td>';
      h+='<td>'+(isRun?'Kører':'Stop')+'</td>';
      h+='<td>'+phName+'</td></tr>';
    }
    h+='</table>';
    $('tmrBody').innerHTML=h;
  }else{
    $('tmrBody').innerHTML='<span class="empty-msg">Ingen aktive timere</span>';
  }

  // ST Logic
  const stEnabled=g(m,'st_logic_enabled')===1;
  badge($('badgeLogic'),stEnabled,stEnabled?'Aktiv':'Deaktiveret');
  const stCyc2=g(m,'st_logic_total_cycles');
  $('stCycles').textContent=fmtN(stCyc2);
  const stOvr2=g(m,'st_logic_cycle_overruns');
  const stOvrP2=(stCyc2!=null&&stCyc2>100)?(stOvr2/stCyc2*100):0;
  $('stOverruns').textContent=fmtN(stOvr2)+(stCyc2>100?' ('+stOvrP2.toFixed(1)+'%)':'');
  $('stOverruns').className='val '+(stOvrP2>5?'val-err':stOvrP2>1?'val-warn':'val-n');
  const stExec=gAll(m,'st_logic_execution_count');
  const stErr=gAll(m,'st_logic_error_count');
  const stTime=gAll(m,'st_logic_exec_time_us');
  const stMin=gAll(m,'st_logic_min_exec_us');
  const stMax=gAll(m,'st_logic_max_exec_us');
  if(stExec.length>0){
    let h='<table class="tbl"><tr><th>Slot</th><th>Navn</th><th>Exec</th><th>Fejl</th><th>Tid (us)</th></tr>';
    for(const s of stExec){
      const slot=s.labels.slot||'?';
      const name=s.labels.name||'-';
      const err=stErr.find(e=>e.labels.slot===slot);
      const time=stTime.find(t=>t.labels.slot===slot);
      const mn=stMin.find(t=>t.labels.slot===slot);
      const mx=stMax.find(t=>t.labels.slot===slot);
      const errN=err?.value||0;
      const timeStr=time?time.value.toLocaleString('da-DK'):'-';
      const rangeStr=(mn&&mx)?mn.value+'\u2013'+mx.value:'';
      h+='<tr><td>#'+slot+'</td><td>'+name+'</td><td>'+fmtN(s.value)+'</td>';
      h+='<td class="'+(errN>0?'val-err':'')+'">'+fmtN(errN)+'</td>';
      h+='<td>'+timeStr+(rangeStr?' ('+rangeStr+')':'')+'</td></tr>';
    }
    h+='</table>';
    $('stBody').innerHTML=h;
  }else{
    $('stBody').innerHTML='<span class="empty-msg">Ingen aktive programmer</span>';
  }

  // Update register views if visible
  updateRegisterViewer(m);
  updateRegisterMap(m);

  // Alarms
  checkAlarms(m);

  // Footer
  $('footRefresh').textContent='Opdateret: '+new Date().toLocaleTimeString('da-DK');
  const up=g(m,'esp32_uptime_seconds');
  if(up!=null)$('hdrInfo').textContent='Uptime: '+fmtUp(up);
}

// === Register Viewer ===
function updateRegisterViewer(m){
  if(!$('pageRegisters').classList.contains('active'))return;
  // HR grid
  const hrGrid=$('regHR');
  let hrHtml='';
  for(let i=0;i<256;i++){
    const val=g(m,'holding_register_value',{addr:String(i)});
    const v=val!=null?Math.round(val):0;
    const cls=v!==0?'rc used':'rc';
    hrHtml+='<div class="'+cls+'" title="HR['+i+'] = '+v+'">'+v+'</div>';
  }
  hrGrid.innerHTML=hrHtml;

  // Coil grid
  const coilGrid=$('regCoils');
  let cHtml='';
  for(let i=0;i<256;i++){
    const val=g(m,'coil_value',{addr:String(i)});
    const v=val!=null?val:0;
    cHtml+='<div class="rc '+(v?'coil-on':'coil-off')+'" title="Coil['+i+'] = '+(v?'ON':'OFF')+'">'+(v?'1':'0')+'</div>';
  }
  coilGrid.innerHTML=cHtml;
}

// === Register Map ===
function updateRegisterMap(m){
  if(!$('pageRegmap').classList.contains('active'))return;
  // Build allocation map from known metrics
  const alloc=new Array(256).fill(null);

  // Counters use known register ranges
  const cVals=gAll(m,'counter_value');
  cVals.forEach(c=>{
    const id=parseInt(c.labels.id)||1;
    const base=(id-1)*4;// Counters typically start at HR base
    for(let j=0;j<4;j++)if(base+j<256)alloc[base+j]={type:'counter',label:'Counter #'+id};
  });

  // ST Logic bindings (estimated from metrics)
  const stExec=gAll(m,'st_logic_execution_count');
  stExec.forEach(s=>{
    const slot=parseInt(s.labels.slot)||1;
    // IR pool starts at 220 for exports
    const base=220+(slot-1)*8;
    for(let j=0;j<8;j++)if(base+j<256)alloc[base+j]={type:'st',label:'ST #'+slot+' '+s.labels.name};
  });

  // System registers (0-9 typically)
  for(let i=0;i<10;i++)if(!alloc[i])alloc[i]={type:'manual',label:'System'};

  // Render map grid
  const mapGrid=$('regmapHR');
  let html='';
  for(let i=0;i<256;i++){
    const a=alloc[i];
    let cls='rc rm-free';
    let title='HR['+i+'] Ledig';
    if(a){
      cls='rc rm-'+a.type;
      title='HR['+i+'] '+a.label;
    }
    html+='<div class="'+cls+'" title="'+title+'">'+i+'</div>';
  }
  mapGrid.innerHTML=html;

  // Allocation table
  const tbody=$('allocBody');
  let thtml='';
  const ranges=[];
  let cur=null;
  for(let i=0;i<256;i++){
    const a=alloc[i];
    const key=a?a.type+':'+a.label:'free';
    if(!cur||cur.key!==key){
      if(cur)ranges.push(cur);
      cur={key,type:a?a.type:'free',label:a?a.label:'Ledig',start:i,end:i};
    }else{cur.end=i;}
  }
  if(cur)ranges.push(cur);
  ranges.filter(r=>r.type!=='free').forEach(r=>{
    thtml+='<tr><td>'+r.label+'</td><td><span class="rm rm-'+r.type+'">'+r.type+'</span></td><td>'+r.start+'</td><td>'+r.end+'</td><td>'+(r.end-r.start+1)+'</td></tr>';
  });
  tbody.innerHTML=thtml||'<tr><td colspan="5" class="empty-msg">Ingen allokeringer fundet</td></tr>';
}

async function fetchMetrics(){
  try{
    const r=await fetch('/api/metrics',{});
    if(!r.ok)return;
    const text=await r.text();
    const m=parsePrometheus(text);
    updateDashboard(m);
  }catch(e){
    $('footRefresh').textContent='Forbindelse tabt...';
  }
}

function init(){
  // Initialize register grids
  let hrInit='',coilInit='';
  for(let i=0;i<256;i++){
    hrInit+='<div class="rc" title="HR['+i+']">0</div>';
    coilInit+='<div class="rc coil-off" title="Coil['+i+']">0</div>';
  }
  $('regHR').innerHTML=hrInit;
  $('regCoils').innerHTML=coilInit;

  fetchMetrics();
  refreshTimer=setInterval(fetchMetrics,3000);
}
init();

// === User Badge ===
function toggleUserMenu(){var m=document.getElementById('userMenu');m.classList.toggle('show')}
document.addEventListener('click',function(e){var b=document.getElementById('userBtn');var m=document.getElementById('userMenu');if(b&&m&&!b.contains(e.target)&&!m.contains(e.target))m.classList.remove('show')});
function updateUserBadge(){
var auth=sessionStorage.getItem('hfplc_auth');
var opts=auth?{headers:{'Authorization':auth}}:{};
fetch('/api/user/me',opts).then(function(r){return r.json()}).then(function(d){
if(d.authenticated){document.getElementById('userName').textContent=d.username;document.getElementById('userDot').className='dot dot-on';document.getElementById('umUser').textContent=d.username;document.getElementById('umRoles').textContent=d.roles||'all';document.getElementById('umPriv').textContent=d.privilege||'rw';document.getElementById('umMode').textContent=d.mode||'legacy';document.getElementById('umLogout').style.display='block';document.getElementById('umLogin').style.display='none'}
else{document.getElementById('userName').textContent='Ikke logget ind';document.getElementById('userDot').className='dot dot-off';document.getElementById('umLogout').style.display='none';document.getElementById('umLogin').style.display='block'}
}).catch(function(){})
}
function showLogin(){document.getElementById('userMenu').classList.remove('show');document.getElementById('loginModal').classList.add('show')}
function doLogin(){
var u=document.getElementById('authUser').value;
var p=document.getElementById('authPass').value;
var auth='Basic '+btoa(u+':'+p);
fetch('/api/user/me',{headers:{'Authorization':auth}}).then(function(r){return r.json()}).then(function(d){
if(d.authenticated){sessionStorage.setItem('hfplc_auth',auth);document.getElementById('loginModal').classList.remove('show');updateUserBadge()}
else{document.getElementById('loginErr').style.display='block';document.getElementById('loginErr').textContent='Forkert brugernavn eller adgangskode'}
}).catch(function(){document.getElementById('loginErr').style.display='block';document.getElementById('loginErr').textContent='Forbindelsesfejl'})
}
function doLogout(){sessionStorage.removeItem('hfplc_auth');document.getElementById('userName').textContent='Ikke logget ind';document.getElementById('userDot').className='dot dot-off';document.getElementById('umLogout').style.display='none';document.getElementById('umLogin').style.display='block';document.getElementById('userMenu').classList.remove('show')}
document.getElementById('authPass').addEventListener('keydown',function(e){if(e.key==='Enter')doLogin()});
document.getElementById('authUser').addEventListener('keydown',function(e){if(e.key==='Enter')document.getElementById('authPass').focus()});
updateUserBadge();
</script>
</body>
</html>)rawhtml";

/* ============================================================================
 * HTTP HANDLER
 * ============================================================================ */

esp_err_t web_dashboard_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, dashboard_html, strlen(dashboard_html));
}
