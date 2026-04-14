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
.save-btn{margin-left:auto;padding:4px 12px;font-size:11px;font-weight:600;background:#a6e3a1;color:#1e1e2e;border:none;border-radius:4px;cursor:pointer;transition:all .2s}
.save-btn:hover{background:#94e2d5}
.save-btn.saving{background:#fab387;cursor:wait}
.save-btn.saved{background:#a6e3a1}
.save-btn.save-err{background:#f38ba8}
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
.grid{flex:1;display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));grid-auto-rows:8px;gap:12px;padding:16px;overflow-y:auto;align-content:start}
.card-wide{grid-column:span 2}
.card h2 .sz-btn{cursor:pointer;color:#45475a;margin-left:auto;font-size:11px;user-select:none;padding:0 4px;border:none;background:none}
.card h2 .sz-btn:hover{color:#89b4fa}
.card h2 .sz-btn.is-wide{color:#89b4fa}
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
.dio-led{display:flex;flex-direction:column;align-items:center;gap:2px;min-width:36px}
.dio-led .led{width:18px;height:18px;border-radius:50%;border:2px solid #45475a;transition:all .3s}
.dio-led .led-on{background:#a6e3a1;border-color:#a6e3a1;box-shadow:0 0 6px #a6e3a1}
.dio-led .led-off{background:#313244;border-color:#45475a}
.dio-led .led-lbl{font-size:9px;color:#6c7086}
.dio-led .led-out{cursor:pointer}
.dio-led .led-out:hover{border-color:#89b4fa}
.sev-crit{color:#f38ba8;font-weight:600}
.sev-warn{color:#fab387}
.sev-info{color:#a6adc8}
.page-view{display:none;flex:1;overflow-y:auto;padding:16px}
.page-view.active{display:block}
.foot{background:#181825;border-top:1px solid #313244;padding:4px 16px;font-size:10px;color:#45475a;flex-shrink:0;display:flex;justify-content:space-between}
.regmap-sticky{position:sticky;top:0;z-index:10;background:#1e1e2e;padding:12px 16px;border-bottom:2px solid #313244;box-shadow:0 2px 8px rgba(0,0,0,.4)}
.regmap-sticky .tbl{max-height:168px;overflow-y:auto;display:block}
.regmap-sticky .tbl thead,.regmap-sticky .tbl tbody{display:table;width:100%;table-layout:fixed}
.card[draggable="true"]{cursor:grab}.card[draggable="true"]:active{cursor:grabbing}
.card.drag-over{border:2px dashed #89b4fa;opacity:.7}
.card.dragging{opacity:.4}
.drag-handle{cursor:grab;color:#45475a;margin-right:4px;font-size:14px;user-select:none}
.drag-handle:hover{color:#89b4fa}
@media(max-width:768px){.grid{grid-template-columns:1fr;gap:8px;padding:8px;grid-auto-rows:auto}.card-wide{grid-column:span 1}.grid .card{grid-row-end:auto!important}}
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
<button class="save-btn" id="saveBtn" onclick="doSystemSave()" title="Gem konfiguration til NVS">&#128190; Save</button>
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

<!-- System Overview (merged with FreeRTOS Tasks) -->
<div class="card" data-card-id="system">
<h2>System</h2>
<div class="row"><span class="lbl">Firmware</span><span class="val val-n" id="mFirmware">-</span></div>
<div class="row"><span class="lbl">Uptime</span><span class="val val-n" id="mUptime">-</span></div>
<div class="row"><span class="lbl">Heap fri</span><span class="val val-n" id="mHeapFree">-</span></div>
<div class="row"><span class="lbl">Heap min</span><span class="val val-n" id="mHeapMin">-</span></div>
<div class="bar-wrap"><div class="bar-fill bar-ok" id="heapBar" style="width:0%"></div></div>
<div class="row" style="margin-top:2px"><span class="lbl">PSRAM</span><span class="val val-n" id="mPsram">-</span></div>
<div class="row"><span class="lbl">Largest block</span><span class="val val-n" id="cpuLargestBlock">-</span></div>
<div class="row"><span class="lbl">Fragmentering</span><span class="val val-n" id="cpuFragPct">-</span></div>
<div class="bar-wrap"><div class="bar-fill bar-ok" id="fragBar" style="width:0%"></div></div>
<div class="row"><span class="lbl">Aktive tasks</span><span class="val val-n" id="cpuTaskCount">-</span></div>
<div id="cpuTaskBody"></div>
<div class="spark-row"><span class="sl">Heap trend</span><svg id="sparkHeap" width="200" height="24"></svg></div>
</div>

<!-- Network Status -->
<div class="card" data-card-id="network">
<h2>Netværk</h2>
<div class="row"><span class="lbl">Hostname</span><span class="val val-n" id="netHostname">-</span></div>
<div style="border-top:1px solid #313244;margin-top:4px;padding-top:4px">
<div class="row"><span class="lbl"><span class="dot dot-off" id="dotWifi"></span> WiFi</span><span class="val val-n" id="mWifi">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">IP / Mask</span><span class="val val-n" id="netWifiIp" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">Gateway</span><span class="val val-n" id="netWifiGw" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">DNS</span><span class="val val-n" id="netWifiDns" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">SSID</span><span class="val val-n" id="netWifiSsid" style="font-size:11px">-</span></div>
</div>
<div style="border-top:1px solid #313244;margin-top:4px;padding-top:4px">
<div class="row"><span class="lbl"><span class="dot dot-off" id="dotEth"></span> Ethernet</span><span class="val val-n" id="mEth">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">IP / Mask</span><span class="val val-n" id="netEthIp" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">Gateway</span><span class="val val-n" id="netEthGw" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">DNS</span><span class="val val-n" id="netEthDns" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">MAC</span><span class="val val-n" id="netEthMac" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl" style="padding-left:16px">Speed</span><span class="val val-n" id="netEthSpeed" style="font-size:11px">-</span></div>
</div>
<div style="border-top:1px solid #313244;margin-top:4px;padding-top:4px">
<div class="row"><span class="lbl"><span class="dot dot-off" id="dotTelnet"></span> Telnet</span><span class="val val-n" id="mTelnet">-</span></div>
<div class="row"><span class="lbl">WiFi reconnects</span><span class="val val-n" id="mWifiRetries">-</span></div>
</div>
</div>

<!-- Modbus Slave -->
<div class="card" data-card-id="modbusslave">
<h2>Modbus Slave <span class="badge badge-off" id="badgeSlave">-</span></h2>
<div class="row"><span class="lbl">Interface</span><span class="val val-n" id="msConfigInfo" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl">Requests total</span><span class="val val-n" id="msTotal">-</span></div>
<div class="row"><span class="lbl">Success</span><span class="val val-ok" id="msOk">-</span></div>
<div class="row"><span class="lbl">CRC fejl</span><span class="val val-err" id="msCrc">-</span></div>
<div class="row"><span class="lbl">Exceptions</span><span class="val val-err" id="msExc">-</span></div>
<div class="row"><span class="lbl">Success rate</span><span class="val val-n" id="msRate">-</span></div>
<div class="spark-row"><span class="sl">Requests</span><svg id="sparkMbSlave" width="200" height="24"></svg></div>
</div>

<!-- Modbus Master (merged with Cache) -->
<div class="card" data-card-id="modbusmaster">
<h2>Modbus Master <span class="badge badge-off" id="badgeMaster">-</span></h2>
<div class="row"><span class="lbl">Interface</span><span class="val val-n" id="mmConfigInfo" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl">Statistik siden</span><span class="val val-n" id="mmStatsSince" style="font-size:11px">-</span></div>
<div class="row"><span class="lbl">Requests total</span><span class="val val-n" id="mmTotal">-</span></div>
<div class="row"><span class="lbl">Success</span><span class="val val-ok" id="mmOk">-</span></div>
<div class="row"><span class="lbl">Timeout fejl</span><span class="val val-err" id="mmTimeout">-</span></div>
<div class="row"><span class="lbl">CRC fejl</span><span class="val val-err" id="mmCrc">-</span></div>
<div class="row"><span class="lbl">Success rate</span><span class="val val-n" id="mmRate">-</span></div>
<div class="spark-row"><span class="sl">Requests</span><svg id="sparkMbMaster" width="200" height="24"></svg></div>
<div style="border-top:1px solid #313244;margin-top:6px;padding-top:6px">
<div class="row"><span class="lbl">Cache entries</span><span class="val val-n" id="mcEntries">-</span></div>
<div class="row"><span class="lbl">Cache hits/misses</span><span class="val val-n" id="mcHitMiss">-</span></div>
<div class="row"><span class="lbl">Hit rate</span><span class="val val-n" id="mcHitRate">-</span></div>
<div class="row"><span class="lbl">Queue full</span><span class="val val-err" id="mcQueueFull">-</span></div>
<div class="row"><span class="lbl">Cache TTL</span><span class="val val-n" id="mcTtl">-</span></div>
<div id="mcSlaves"><span class="empty-msg">Ingen aktive slaves</span></div>
<div id="mcBackoff" style="margin-top:6px"></div>
<div style="border-top:1px solid #313244;margin-top:8px;padding-top:6px;text-align:center"><button id="btnResetMasterStats" onclick="resetMasterStats()" style="background:#45475a;color:#cdd6f4;border:1px solid #585b70;border-radius:4px;padding:4px 14px;cursor:pointer;font-size:12px">Nulstil statistik</button></div>
<!-- FEAT-135: mb read/write mini-form -->
<div style="border-top:1px solid #313244;margin-top:8px;padding-top:6px">
<div style="font-size:11px;color:#89b4fa;margin-bottom:4px;font-weight:600">Read/Write</div>
<div style="display:flex;gap:6px;flex-wrap:wrap;align-items:flex-end;margin-bottom:4px">
<div><div style="font-size:10px;color:#a6adc8;margin-bottom:2px">FC Type</div>
<select id="mbOp" onchange="updateMbForm()" style="padding:2px 4px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px">
<option value="read holding">Read Holding (FC03)</option>
<option value="read input-reg">Read Input Reg (FC04)</option>
<option value="read coil">Read Coil (FC01)</option>
<option value="read input">Read Discrete In (FC02)</option>
<option value="write holding">Write Holding (FC06)</option>
<option value="write coil">Write Coil (FC05)</option>
</select></div>
<div><div style="font-size:10px;color:#a6adc8;margin-bottom:2px">Slave ID</div>
<input type="number" id="mbSlave" value="1" min="1" max="247" title="Slave ID" style="width:55px;padding:2px 4px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px"></div>
<div><div style="font-size:10px;color:#a6adc8;margin-bottom:2px">Adresse</div>
<input type="number" id="mbAddr" value="0" min="0" max="65535" title="Addr" style="width:60px;padding:2px 4px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px"></div>
<div><div style="font-size:10px;color:#a6adc8;margin-bottom:2px" id="mbValLabel">Antal</div>
<input type="number" id="mbVal" value="1" title="Count (read) / Value (write)" style="width:55px;padding:2px 4px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px"></div>
<button onclick="doMbCmd()" style="padding:2px 10px;background:#89b4fa;color:#1e1e2e;border:none;border-radius:3px;cursor:pointer;font-size:10px;font-weight:600;align-self:flex-end">Udfør</button>
</div>
<pre id="mbResult" style="font-size:10px;background:#11111b;color:#a6adc8;padding:4px 6px;border-radius:3px;max-height:100px;overflow-y:auto;margin:0;white-space:pre-wrap;font-family:'Cascadia Code',monospace;user-select:text;cursor:text">(klar)</pre>
</div>
</div>
</div>

<!-- HTTP API + SSE -->
<div class="card" data-card-id="httpapi">
<h2>HTTP API</h2>
<div class="row"><span class="lbl">Requests total</span><span class="val val-n" id="htTotal">-</span></div>
<div class="row"><span class="lbl">Success (2xx)</span><span class="val val-ok" id="htOk">-</span></div>
<div class="row"><span class="lbl">Client fejl (4xx)</span><span class="val val-err" id="ht4xx">-</span></div>
<div class="row"><span class="lbl">Server fejl (5xx)</span><span class="val val-err" id="ht5xx">-</span></div>
<div class="row"><span class="lbl">Auth failures</span><span class="val val-warn" id="htAuth">-</span></div>
<div style="border-top:1px solid #313244;margin-top:6px;padding-top:6px">
<div class="row"><span class="lbl"><span class="dot dot-off" id="dotSse"></span> SSE Server</span><span class="val val-n" id="sseStatus">-</span></div>
<div class="row"><span class="lbl">SSE port</span><span class="val val-n" id="ssePort">-</span></div>
<div class="row"><span class="lbl">SSE klienter</span><span class="val val-n" id="mSse">-</span></div>
<div class="row"><span class="lbl">SSE max klienter</span><span class="val val-n" id="sseMax">-</span></div>
<div class="row"><span class="lbl">Heartbeat</span><span class="val val-n" id="sseHeartbeat">-</span></div>
</div>
</div>

<!-- Counters -->
<div class="card" data-card-id="counters">
<h2>Tællere</h2>
<div id="cntBody"><span class="empty-msg">Ingen aktive tællere</span></div>
</div>

<!-- Timers -->
<div class="card" data-card-id="timers">
<h2>Timere</h2>
<div id="tmrBody"><span class="empty-msg">Ingen aktive timere</span></div>
</div>

<!-- ST Logic -->
<div class="card" data-card-id="stlogic">
<h2>ST Logic <span class="badge badge-off" id="badgeLogic">-</span></h2>
<div class="row"><span class="lbl">Interval</span><span class="val val-n" id="stInterval">-</span></div>
<div class="row"><span class="lbl">Total cycles</span><span class="val val-n" id="stCycles">-</span></div>
<div class="row"><span class="lbl">Overruns</span><span class="val val-n" id="stOverruns">-</span></div>
<div id="stBody"><span class="empty-msg">Ingen aktive programmer</span></div>
</div>

<!-- NTP Tidssynkronisering -->
<div class="card" data-card-id="ntp">
<h2>NTP Tid <span class="badge badge-off" id="badgeNtp">-</span></h2>
<div class="row"><span class="lbl">Lokal tid</span><span class="val val-n" id="ntpTime" style="font-size:14px;color:#89b4fa">-</span></div>
<div class="row"><span class="lbl">Server</span><span class="val val-n" id="ntpServer">-</span></div>
<div class="row"><span class="lbl">Tidszone</span><span class="val val-n" id="ntpTz">-</span></div>
<div class="row"><span class="lbl">Antal syncs</span><span class="val val-n" id="ntpSyncs">-</span></div>
<div class="row"><span class="lbl">Sidste sync</span><span class="val val-n" id="ntpAge">-</span></div>
</div>

<!-- FEAT-072: Modbus RTU Trafik -->
<div class="card" data-card-id="rtutrafik">
<h2>Modbus RTU Trafik</h2>
<div class="row"><span class="lbl">Slave req/3s</span><span class="val val-n" id="rtSlaveRate">-</span></div>
<div class="row"><span class="lbl">Master req/3s</span><span class="val val-n" id="rtMasterRate">-</span></div>
<div class="row"><span class="lbl">Slave success rate</span><span class="val val-n" id="rtSlaveSucc">-</span></div>
<div class="row"><span class="lbl">Master success rate</span><span class="val val-n" id="rtMasterSucc">-</span></div>
<div class="row"><span class="lbl">CRC fejl (slave/master)</span><span class="val val-err" id="rtCrcBoth">-</span></div>
<div class="row"><span class="lbl">Timeouts (master)</span><span class="val val-err" id="rtTimeout">-</span></div>
<div class="row"><span class="lbl">Exceptions (slave/master)</span><span class="val val-warn" id="rtExcBoth">-</span></div>
<div class="spark-row"><span class="sl">Slave req/3s</span><svg id="sparkRtSlave" width="200" height="24"></svg></div>
<div class="spark-row"><span class="sl">Master req/3s</span><svg id="sparkRtMaster" width="200" height="24"></svg></div>
</div>

<!-- FEAT-075: TCP Forbindelsesmonitor -->
<div class="card" data-card-id="tcpmonitor">
<h2>TCP Forbindelser <span class="badge badge-off" id="badgeTcp">0</span></h2>
<div class="row"><span class="lbl">SSE klienter</span><span class="val val-n" id="tcpSseCount">0</span></div>
<div class="row"><span class="lbl">Telnet</span><span class="val val-n" id="tcpTelnet">Ingen</span></div>
<div class="row"><span class="lbl">HTTP req total</span><span class="val val-n" id="tcpHttpTotal">-</span></div>
<div id="tcpConnBody"><span class="empty-msg">Ingen aktive forbindelser</span></div>
</div>

<!-- FEAT-085: Alarm Historik (expanded v7.8.3.1) -->
<div class="card card-wide" data-card-id="alarms">
<h2>Alarm Historik <span class="badge badge-off" id="badgeAlarm">0</span></h2>
<div style="display:flex;justify-content:space-between;margin-bottom:6px;gap:8px;flex-wrap:wrap">
<span style="font-size:10px;color:#6c7086" id="alarmInfo">-</span>
<button onclick="ackAlarms()" style="font-size:10px;padding:2px 8px;background:#313244;color:#a6adc8;border:1px solid #45475a;border-radius:3px;cursor:pointer">Kvittér alle</button>
</div>
<!-- FEAT-134: Alarm filter bar -->
<div style="display:flex;gap:6px;margin-bottom:6px;flex-wrap:wrap;align-items:center">
<select id="alarmSev" onchange="renderAlarms()" style="padding:3px 6px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px">
<option value="-1">Alle severity</option>
<option value="2">Kritiske</option>
<option value="1">Advarsler</option>
<option value="0">Info</option>
</select>
<select id="alarmAck" onchange="renderAlarms()" style="padding:3px 6px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px">
<option value="any">Alle</option>
<option value="unack">Kun ukvitterede</option>
<option value="ack">Kun kvitterede</option>
</select>
<input type="text" id="alarmSearch" placeholder="Søg i tekst..." oninput="renderAlarms()" style="padding:3px 6px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px;flex:1;min-width:120px">
<input type="number" id="alarmLimit" value="25" min="5" max="500" onchange="renderAlarms()" title="Max antal" style="padding:3px 6px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:3px;font-size:10px;width:60px">
<button onclick="clearAlarmFilter()" style="font-size:10px;padding:3px 8px;background:#45475a;color:#cdd6f4;border:none;border-radius:3px;cursor:pointer">Nulstil</button>
</div>
<div id="alarmBody" style="max-height:320px;overflow-y:auto"><span class="empty-msg">Ingen alarmer</span></div>
</div>

<!-- FEAT-095: Digital I/O Dashboard -->
<div class="card" data-card-id="dio" id="cardDio"
<h2>Digital I/O</h2>
<div style="margin-bottom:8px">
<div style="font-size:10px;color:#89b4fa;margin-bottom:4px;font-weight:600">Inputs (IN1-IN8)</div>
<div style="display:flex;gap:6px;flex-wrap:wrap" id="dioInputs">
<span class="empty-msg">Ikke tilgængelig</span>
</div>
</div>
<div>
<div style="font-size:10px;color:#cba6f7;margin-bottom:4px;font-weight:600">Outputs (CH1-CH8)</div>
<div style="display:flex;gap:6px;flex-wrap:wrap" id="dioOutputs">
<span class="empty-msg">Ikke tilgængelig</span>
</div>
</div>
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
<div class="page-view" id="pageRegmap" style="padding:0">
<div class="regmap-sticky">
<div style="display:flex;align-items:center;gap:12px;margin-bottom:6px;flex-wrap:wrap">
<h2 style="font-size:13px;color:#89b4fa;margin:0">Register Allokering</h2>
<div class="regmap" id="regmapLegend" style="margin:0">
<span class="rm rm-counter">Counter</span>
<span class="rm rm-timer">Timer</span>
<span class="rm rm-st">ST Logic</span>
<span class="rm rm-manual">Manual/System</span>
<span class="rm rm-free">Ledig</span>
</div>
</div>
<table class="tbl" id="allocTable" style="margin:0">
<thead><tr><th>Ejer</th><th>Type</th><th>Register</th><th>Start</th><th>Slut</th><th>Antal</th></tr></thead>
<tbody id="allocBody"></tbody>
</table>
</div>
<div style="padding:16px;padding-top:8px">
<div class="card" style="margin-bottom:12px">
<h2>Holding Registers (HR 0-255)</h2>
<div class="regview" id="regmapHR"></div>
</div>
<div class="card" style="margin-bottom:12px">
<h2>Input Registers (IR 0-255)</h2>
<div class="regview" id="regmapIR"></div>
</div>
<div class="card" style="margin-bottom:12px">
<h2>Coils (0-255)</h2>
<div class="regview" id="regmapCoils"></div>
</div>
<div class="card">
<h2>Discrete Inputs (DI 0-255)</h2>
<div class="regview" id="regmapDI"></div>
</div>
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
let history={heap:[],mbSlave:[],mbMaster:[],rtSlave:[],rtMaster:[]};
let prevMbSlave=null,prevMbMaster=null;
let prevSlaveTotal=null,prevMasterTotal=null;
let alarms=[];
let _stBindings=null;
let _alarmLog=[];

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
  {const fi=gAll(m,'firmware_info');if(fi.length>0){const v=fi[0].labels.version||'?';const b=fi[0].labels.build||'?';$('mFirmware').textContent='v'+v+'.'+b;}}
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
  const sseAct=g(m,'sse_clients_active');
  dot($('dotSse'),sseAct!=null&&sseAct>0);
  $('mWifiRetries').textContent=fmtN(g(m,'wifi_reconnect_retries'));

  // Modbus Slave
  const msT=g(m,'modbus_slave_requests_total');
  const msO=g(m,'modbus_slave_success_total');
  $('msTotal').textContent=fmtN(msT);
  $('msOk').textContent=fmtN(msO);
  $('msCrc').textContent=fmtN(g(m,'modbus_slave_crc_errors_total'));
  $('msExc').textContent=fmtN(g(m,'modbus_slave_exceptions_total'));
  $('msRate').textContent=rate(msO,msT);
  const msEn=g(m,'modbus_slave_config_enabled');
  badge($('badgeSlave'),msEn===1&&msT>0,msEn===1?(msT>0?'Aktiv':'Idle'):'Deaktiveret');
  // Config info row
  {const baud=g(m,'modbus_slave_config_baudrate');const par=g(m,'modbus_slave_config_parity');const sb=g(m,'modbus_slave_config_stopbits');const sid=g(m,'modbus_slave_config_id');
  if(baud!=null){const pStr=par===1?'Even':par===2?'Odd':'None';const bK=baud>=1000?(baud/1000)+'K':baud;$('msConfigInfo').textContent=msEn===1?'ID:'+sid+' Baud:'+bK+' Data:8 Parity:'+pStr+' Stop:'+sb:'deaktiveret';}}
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
  {const ageMs=g(m,'modbus_master_stats_age_ms');
  $('mmStatsSince').textContent=ageMs!=null?fmtUptime(ageMs)+' siden':'-';}
  $('mmTotal').textContent=fmtN(mmT);
  $('mmOk').textContent=fmtN(mmO);
  $('mmTimeout').textContent=fmtN(g(m,'modbus_master_timeout_errors_total'));
  $('mmCrc').textContent=fmtN(g(m,'modbus_master_crc_errors_total'));
  $('mmRate').textContent=rate(mmO,mmT);
  const mmEn=g(m,'modbus_master_config_enabled');
  badge($('badgeMaster'),mmEn===1&&mmT>0,mmEn===1?(mmT>0?'Aktiv':'Idle'):'Deaktiveret');
  // Config info row
  {const baud=g(m,'modbus_master_config_baudrate');const par=g(m,'modbus_master_config_parity');const sb=g(m,'modbus_master_config_stopbits');
  if(baud!=null){const pStr=par===1?'Even':par===2?'Odd':'None';const bK=baud>=1000?(baud/1000)+'K':baud;$('mmConfigInfo').textContent=mmEn===1?'Baud:'+bK+' Data:8 Parity:'+pStr+' Stop:'+sb:'deaktiveret';}}
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
  // Fetch interval from /api/logic (less frequent)
  if(!window._stIntervalFetched||(Date.now()-window._stIntervalFetched>15000)){
    window._stIntervalFetched=Date.now();
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts=auth?{headers:{'Authorization':auth}}:{};
    fetch('/api/logic',opts).then(r=>r.json()).then(d=>{
      if(d.execution_interval_ms!=null)$('stInterval').textContent=d.execution_interval_ms+' ms';
    }).catch(()=>{});
  }
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
  const stOvrP=gAll(m,'st_logic_overrun_count');
  if(stExec.length>0){
    let h='<table class="tbl"><tr><th>Slot</th><th>Navn</th><th>Exec</th><th>Fejl</th><th>Ovr</th><th>Tid (min/last/max us)</th></tr>';
    for(const s of stExec){
      const slot=s.labels.slot||'?';
      const name=s.labels.name||'-';
      const err=stErr.find(e=>e.labels.slot===slot);
      const time=stTime.find(t=>t.labels.slot===slot);
      const mn=stMin.find(t=>t.labels.slot===slot);
      const mx=stMax.find(t=>t.labels.slot===slot);
      const ovr=stOvrP.find(t=>t.labels.slot===slot);
      const errN=err?.value||0;
      const ovrN=ovr?.value||0;
      const timeStr=(mn&&time&&mx)?mn.value+' / '+time.value+' / '+mx.value:time?String(time.value):'-';
      h+='<tr><td>#'+slot+'</td><td>'+name+'</td><td>'+fmtN(s.value)+'</td>';
      h+='<td class="'+(errN>0?'val-err':'')+'">'+fmtN(errN)+'</td>';
      h+='<td class="'+(ovrN>0?'val-warn':'')+'">'+fmtN(ovrN)+'</td>';
      h+='<td>'+timeStr+'</td></tr>';
    }
    h+='</table>';
    $('stBody').innerHTML=h;
  }else{
    $('stBody').innerHTML='<span class="empty-msg">Ingen aktive programmer</span>';
  }

  // === NTP Time ===
  {
    const ntpOn=g(m,'ntp_enabled')===1;
    const ntpSync=g(m,'ntp_synced')===1;
    badge($('badgeNtp'),ntpSync,ntpSync?'Synkroniseret':ntpOn?'Venter...':'Deaktiveret');
    $('ntpSyncs').textContent=fmtN(g(m,'ntp_sync_count'));
    if(ntpSync){
      // Use ESP32 local_time (follows NTP timezone setting) instead of browser timezone
      if(window._ntpLocalTime)$('ntpTime').textContent=window._ntpLocalTime;
      const age=g(m,'ntp_last_sync_age_ms');
      if(age!=null)$('ntpAge').textContent=(age/1000).toFixed(0)+' sek. siden';
    }else{
      $('ntpTime').textContent=ntpOn?'Venter på sync...':'Deaktiveret';
      $('ntpAge').textContent='-';
    }
  }
  // Fetch NTP config + local time from ESP32 (every 3s via /api/ntp)
  {
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts=auth?{headers:{'Authorization':auth}}:{};
    fetch('/api/ntp',opts).then(r=>r.json()).then(d=>{
      $('ntpServer').textContent=d.server||'-';
      $('ntpTz').textContent=d.timezone||'UTC';
      if(d.local_time)window._ntpLocalTime=d.local_time;
    }).catch(()=>{});
  }

  // === FEAT-072: Modbus RTU Trafik ===
  {
    const sT=g(m,'modbus_slave_requests_total')||0;
    const mT=g(m,'modbus_master_requests_total')||0;
    const sDelta=prevSlaveTotal!=null?sT-prevSlaveTotal:0;
    const mDelta=prevMasterTotal!=null?mT-prevMasterTotal:0;
    prevSlaveTotal=sT;prevMasterTotal=mT;
    history.rtSlave.push(sDelta);if(history.rtSlave.length>HIST_MAX)history.rtSlave.shift();
    history.rtMaster.push(mDelta);if(history.rtMaster.length>HIST_MAX)history.rtMaster.shift();
    $('rtSlaveRate').textContent=fmtN(sDelta);
    $('rtMasterRate').textContent=fmtN(mDelta);
    const sO=g(m,'modbus_slave_success_total')||0;
    const mO=g(m,'modbus_master_success_total')||0;
    $('rtSlaveSucc').textContent=rate(sO,sT);
    $('rtMasterSucc').textContent=rate(mO,mT);
    const sCrc=g(m,'modbus_slave_crc_errors_total')||0;
    const mCrc=g(m,'modbus_master_crc_errors_total')||0;
    $('rtCrcBoth').textContent=fmtN(sCrc)+' / '+fmtN(mCrc);
    $('rtTimeout').textContent=fmtN(g(m,'modbus_master_timeout_errors_total'));
    const sExc=g(m,'modbus_slave_exceptions_total')||0;
    const mExc=g(m,'modbus_master_exception_errors_total')||0;
    $('rtExcBoth').textContent=fmtN(sExc)+' / '+fmtN(mExc);
    drawSparkline('sparkRtSlave',history.rtSlave,'#89b4fa');
    drawSparkline('sparkRtMaster',history.rtMaster,'#cba6f7');
  }

  // === FEAT-073: Modbus Master Cache ===
  {
    const hits=g(m,'modbus_master_cache_hits');
    const misses=g(m,'modbus_master_cache_misses');
    const entries=g(m,'modbus_master_cache_entries');
    const qfull=g(m,'modbus_master_queue_full_count');
    if(hits!=null){
      $('mcEntries').textContent=fmtN(entries)+' / '+MB_CACHE_MAX;
      $('mcHitMiss').textContent=fmtN(hits)+' / '+fmtN(misses);
      const total=hits+misses;
      $('mcHitRate').textContent=total>0?(hits/total*100).toFixed(1)+'%':'N/A';
      $('mcQueueFull').textContent=fmtN(qfull);
      const ttl=g(m,'modbus_master_cache_ttl_ms');
      $('mcTtl').textContent=(ttl==null)?'-':(ttl==0?'0 (aldrig expire)':ttl+' ms');
      // Per-slave table
      const slaves=gAll(m,'modbus_master_slave_status');
      if(slaves.length>0){
        let h='<table class="tbl"><tr><th>Slave</th><th>Addr</th><th>FC</th><th>Status</th><th>Age</th></tr>';
        for(const s of slaves){
          const st=s.labels.status||'?';
          const ams=parseInt(s.labels.age_ms||'0');
          const exp=(ttl>0 && ams>=ttl);
          const cls=exp?'val-warn':(st==='valid'?'val-ok':st==='error'?'val-err':'val-warn');
          const age=ams<1000?ams+'ms':ams<60000?(ams/1000).toFixed(1)+'s':(ams/60000).toFixed(1)+'m';
          h+='<tr><td>#'+s.labels.slave+'</td><td>'+s.labels.addr+'</td><td>FC'+s.labels.fc+'</td>';
          h+='<td class="'+cls+'">'+st+(exp?' [EXP]':'')+'</td><td>'+age+'</td></tr>';
        }
        h+='</table>';
        $('mcSlaves').innerHTML=h;
      }else{$('mcSlaves').innerHTML='<span class="empty-msg">Ingen aktive slaves</span>';}
      // Adaptive backoff table
      const boffs=gAll(m,'modbus_master_slave_backoff');
      if(boffs.length>0){
        let bh='<div style="border-top:1px solid #313244;margin-top:6px;padding-top:4px"><span class="lbl" style="font-size:11px;color:#6c7086">Adaptive Backoff</span></div>';
        bh+='<table class="tbl"><tr><th>Slave</th><th>Backoff</th><th>Timeouts</th><th>OK</th></tr>';
        for(const b of boffs){
          const ms=parseInt(b.value);
          const to=b.labels.timeouts||'0';
          const ok=b.labels.successes||'0';
          const cls=ms>=1000?'val-err':ms>0?'val-warn':'val-ok';
          bh+='<tr><td>#'+b.labels.slave+'</td><td class="'+cls+'">'+(ms>0?ms+'ms':'0')+'</td>';
          bh+='<td>'+to+'</td><td>'+ok+'</td></tr>';
        }
        bh+='</table>';
        $('mcBackoff').innerHTML=bh;
      }else{$('mcBackoff').innerHTML='';}
    }else{
      $('mcEntries').textContent='Inaktiv';
      $('mcSlaves').innerHTML='<span class="empty-msg">Master ikke startet</span>';
      $('mcBackoff').innerHTML='';
    }
  }

  // === FEAT-078: FreeRTOS Tasks ===
  {
    $('cpuTaskCount').textContent=fmtN(g(m,'freertos_task_count'));
    const largest=g(m,'esp32_heap_largest_free_block');
    const heapTotal=g(m,'esp32_heap_free_bytes');
    $('cpuLargestBlock').textContent=fmtKB(largest);
    if(largest!=null&&heapTotal!=null&&heapTotal>0){
      const fragPct=((1-largest/heapTotal)*100);
      $('cpuFragPct').textContent=fragPct.toFixed(1)+'%';
      const bar=$('fragBar');
      bar.style.width=Math.min(100,fragPct).toFixed(0)+'%';
      bar.className='bar-fill '+(fragPct<30?'bar-ok':fragPct<60?'bar-warn':'bar-err');
    }
    const tasks=gAll(m,'freertos_task_stack_hwm');
    if(tasks.length>0){
      let h='<table class="tbl"><tr><th>Task</th><th>Stack HWM</th></tr>';
      for(const t of tasks){
        const name=t.labels.task||'?';
        const hwm=t.value;
        const hwmCls=hwm<200?'val-err':hwm<500?'val-warn':'val-n';
        h+='<tr><td>'+name+'</td><td class="'+hwmCls+'">'+hwm+' B</td></tr>';
      }
      h+='</table>';
      $('cpuTaskBody').innerHTML=h;
    }
  }

  // === FEAT-075: TCP Forbindelsesmonitor ===
  {
    const sseN=g(m,'sse_clients_active')||0;
    const telOn=g(m,'telnet_connected')===1;
    const httpT=g(m,'http_requests_total');
    $('tcpSseCount').textContent=sseN;
    $('tcpHttpTotal').textContent=fmtN(httpT);

    // Telnet info from extended metrics
    const telIp=gAll(m,'telnet_client_ip');
    const telUp=g(m,'telnet_client_uptime_seconds');
    if(telOn&&telIp.length>0){
      const ip=telIp[0].labels.ip||'?';
      const usr=telIp[0].labels.user||'';
      $('tcpTelnet').textContent=ip+(usr?' ('+usr+')':'')+' — '+fmtUp(telUp);
    }else{
      $('tcpTelnet').textContent=telOn?'Forbundet':'Ingen';
    }

    const total=sseN+(telOn?1:0);
    badge($('badgeTcp'),total>0,String(total));
  }

  // === FEAT-085: Alarm Historik ===
  {
    const cnt=g(m,'alarm_log_count')||0;
    const unack=g(m,'alarm_unacknowledged_count')||0;
    badge($('badgeAlarm'),unack>0,String(unack));
    $('alarmInfo').textContent=cnt+' alarmer totalt, '+unack+' ukvitterede';
  }

  // === FEAT-095: Digital I/O ===
  {
    const dins=gAll(m,'gpio_digital_input');
    const douts=gAll(m,'gpio_digital_output');
    if(dins.length>0){
      let h='';
      for(const d of dins){
        const pin=d.labels.pin;
        const on=d.value===1;
        const num=parseInt(pin)-100;
        h+='<div class="dio-led"><div class="led '+(on?'led-on':'led-off')+'"></div><span class="led-lbl">IN'+num+'</span></div>';
      }
      $('dioInputs').innerHTML=h;
    }
    if(douts.length>0){
      let h='';
      for(const d of douts){
        const pin=d.labels.pin;
        const on=d.value===1;
        const num=parseInt(pin)-200;
        h+='<div class="dio-led"><div class="led led-out '+(on?'led-on':'led-off')+'" onclick="toggleDO('+pin+')" title="Klik for at skifte"></div><span class="led-lbl">CH'+num+'</span></div>';
      }
      $('dioOutputs').innerHTML=h;
    }
    if(dins.length===0&&douts.length===0){
      $('cardDio').style.display='none';
    }
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
// Helper: render a 256-cell grid from an alloc array
function renderMapGrid(el,alloc,prefix){
  let html='';
  for(let i=0;i<256;i++){
    const a=alloc[i];
    let cls='rc rm-free';
    let title=prefix+'['+i+'] Ledig';
    if(a){cls='rc rm-'+a.type;title=prefix+'['+i+'] '+a.label;}
    html+='<div class="'+cls+'" title="'+title+'">'+i+'</div>';
  }
  el.innerHTML=html;
}

// Helper: collect ranges from alloc array for table
function collectRanges(alloc,prefix){
  const ranges=[];
  let cur=null;
  for(let i=0;i<256;i++){
    const a=alloc[i];
    const key=a?a.type+':'+a.label:'free';
    if(!cur||cur.key!==key){
      if(cur)ranges.push(cur);
      cur={key,type:a?a.type:'free',label:a?a.label:'Ledig',start:i,end:i,prefix};
    }else{cur.end=i;}
  }
  if(cur)ranges.push(cur);
  return ranges.filter(r=>r.type!=='free');
}

function updateRegisterMap(m){
  if(!$('pageRegmap').classList.contains('active'))return;

  // === HR Allocation ===
  const hrAlloc=new Array(256).fill(null);

  // Counters: base=100+(id-1)*20, stride 20 per counter
  const cVals=gAll(m,'counter_value');
  cVals.forEach(c=>{
    const id=parseInt(c.labels.id)||1;
    const base=100+(id-1)*20;
    const lbl='Counter #'+id;
    for(let j=0;j<4;j++){if(base+j<256)hrAlloc[base+j]={type:'counter',label:lbl+' Value'};}
    for(let j=4;j<8;j++){if(base+j<256)hrAlloc[base+j]={type:'counter',label:lbl+' Raw'};}
    if(base+8<256)hrAlloc[base+8]={type:'counter',label:lbl+' Freq'};
    if(base+10<256)hrAlloc[base+10]={type:'counter',label:lbl+' Ctrl'};
    for(let j=11;j<15;j++){if(base+j<256)hrAlloc[base+j]={type:'counter',label:lbl+' Compare'};}
  });

  // Timers: ctrl_reg = 180+(id-1)*5
  for(let id=1;id<=4;id++){
    const base=180+(id-1)*5;
    for(let j=0;j<5;j++){if(base+j<256)hrAlloc[base+j]={type:'timer',label:'Timer #'+id};}
  }

  // System registers (0-9)
  for(let i=0;i<10;i++)if(!hrAlloc[i])hrAlloc[i]={type:'manual',label:'System'};

  // ST Logic bindings → HR (input bindings with input_type=HR)
  if(_stBindings&&_stBindings.bindings){
    _stBindings.bindings.forEach(b=>{
      if(b.register_type==='HR'){
        const addr=b.register_addr;
        const lbl='ST P'+b.program+' '+(b.name||('var'+b.var_index))+' ('+b.direction+')';
        for(let j=0;j<(b.word_count||1);j++){
          if(addr+j<256)hrAlloc[addr+j]={type:'st',label:lbl};
        }
      }
    });
  }

  renderMapGrid($('regmapHR'),hrAlloc,'HR');

  // === IR Allocation ===
  const irAlloc=new Array(256).fill(null);

  // ST Logic EXPORT pool: IR 220-251
  const stExec=gAll(m,'st_logic_execution_count');
  stExec.forEach(s=>{
    const slot=parseInt(s.labels.slot)||1;
    const base=220+(slot-1)*8;
    const name=s.labels.name||('Program '+slot);
    for(let j=0;j<8;j++)if(base+j<256)irAlloc[base+j]={type:'st',label:'ST #'+slot+' '+name};
  });
  // Mark IR 220-251 as ST pool even without active programs
  for(let i=220;i<=251;i++)if(!irAlloc[i])irAlloc[i]={type:'st',label:'ST EXPORT Pool'};

  renderMapGrid($('regmapIR'),irAlloc,'IR');

  // === Coil Allocation ===
  const coilAlloc=new Array(256).fill(null);

  // Timer output coils (from metrics if available)
  for(let id=1;id<=4;id++){
    const val=g(m,'timer_value',{id:String(id)});
    if(val!=null){
      const coilBase=(id-1)*2;
      coilAlloc[coilBase]={type:'timer',label:'Timer #'+id+' Output'};
    }
  }

  // ST Logic bindings → Coil (output bindings with output_type=Coil)
  if(_stBindings&&_stBindings.bindings){
    _stBindings.bindings.forEach(b=>{
      if(b.register_type==='Coil'){
        const addr=b.register_addr;
        const lbl='ST P'+b.program+' '+(b.name||('var'+b.var_index))+' (output)';
        for(let j=0;j<(b.word_count||1);j++){
          if(addr+j<256)coilAlloc[addr+j]={type:'st',label:lbl};
        }
      }
    });
  }

  renderMapGrid($('regmapCoils'),coilAlloc,'Coil');

  // === DI Allocation ===
  const diAlloc=new Array(256).fill(null);

  // Counter SW mode uses input_dis (DI index)
  cVals.forEach(c=>{
    const id=parseInt(c.labels.id)||1;
    const diIdx=(id-1);
    if(diIdx<256)diAlloc[diIdx]={type:'counter',label:'Counter #'+id+' Input'};
  });

  // ST Logic bindings → DI (input bindings with input_type=DI)
  if(_stBindings&&_stBindings.bindings){
    _stBindings.bindings.forEach(b=>{
      if(b.register_type==='DI'){
        const addr=b.register_addr;
        const lbl='ST P'+b.program+' '+(b.name||('var'+b.var_index))+' (input)';
        for(let j=0;j<(b.word_count||1);j++){
          if(addr+j<256)diAlloc[addr+j]={type:'st',label:lbl};
        }
      }
    });
  }

  renderMapGrid($('regmapDI'),diAlloc,'DI');

  // === Combined Allocation Table ===
  const allRanges=[
    ...collectRanges(hrAlloc,'HR'),
    ...collectRanges(irAlloc,'IR'),
    ...collectRanges(coilAlloc,'Coil'),
    ...collectRanges(diAlloc,'DI')
  ];
  const tbody=$('allocBody');
  let thtml='';
  allRanges.forEach(r=>{
    thtml+='<tr><td>'+r.label+'</td><td><span class="rm rm-'+r.type+'">'+r.type+'</span></td><td>'+r.prefix+'</td><td>'+r.start+'</td><td>'+r.end+'</td><td>'+(r.end-r.start+1)+'</td></tr>';
  });
  tbody.innerHTML=thtml||'<tr><td colspan="6" class="empty-msg">Ingen allokeringer fundet</td></tr>';
}

async function fetchNetworkInfo(){
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts=auth?{headers:{'Authorization':auth}}:{};
    // Hostname
    fetch('/api/hostname',opts).then(r=>r.json()).then(d=>{
      $('netHostname').textContent=d.hostname||'-';
    }).catch(()=>{});
    // WiFi details
    fetch('/api/wifi',opts).then(r=>r.json()).then(d=>{
      const rt=d.runtime||{};
      if(rt.connected){
        $('netWifiIp').textContent=(rt.ip||'-')+(rt.netmask?' / '+rt.netmask:'');
        $('netWifiGw').textContent=rt.gateway||'-';
        $('netWifiDns').textContent=rt.dns||'-';
        $('netWifiSsid').textContent=rt.ssid||d.config?.ssid||'-';
      }else{
        $('netWifiIp').textContent='-';$('netWifiGw').textContent='-';
        $('netWifiDns').textContent='-';$('netWifiSsid').textContent=d.config?.ssid||'-';
      }
    }).catch(()=>{});
    // Ethernet details
    fetch('/api/ethernet',opts).then(r=>r.json()).then(d=>{
      const rt=d.runtime||{};
      if(rt.connected){
        $('netEthIp').textContent=(rt.ip||'-')+(rt.netmask?' / '+rt.netmask:'');
        $('netEthGw').textContent=rt.gateway||'-';
        $('netEthDns').textContent=rt.dns||'-';
        $('netEthMac').textContent=rt.mac||'-';
        const spd=rt.speed_mbps;
        $('netEthSpeed').textContent=spd?(spd+'Mbps '+(rt.full_duplex?'Full-Duplex':'Half-Duplex')):'-';
      }else{
        $('netEthIp').textContent='-';$('netEthGw').textContent='-';
        $('netEthDns').textContent='-';$('netEthMac').textContent='-';$('netEthSpeed').textContent='-';
      }
    }).catch(()=>{});
    // SSE status
    fetch('/api/events/status',opts).then(r=>r.json()).then(d=>{
      $('sseStatus').textContent=d.sse_enabled?'Aktiv':'Deaktiveret';
      $('ssePort').textContent=d.sse_port||'-';
      $('sseMax').textContent=d.max_clients+' max';
      $('sseHeartbeat').textContent=(d.heartbeat_ms/1000)+'s';
    }).catch(()=>{});
  }catch(e){}
}
// FEAT-075: Hent SSE klient-detaljer til TCP forbindelsesmonitor
async function fetchConnections(){
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts=auth?{headers:{'Authorization':auth}}:{};
    const r=await fetch('/api/events/clients',opts);
    if(!r.ok)return;
    const d=await r.json();
    const el=$('tcpConnBody');
    if(!d.clients||d.clients.length===0){
      el.innerHTML='<span class="empty-msg">Ingen aktive forbindelser</span>';
      return;
    }
    let h='<table class="tbl"><tr><th>Proto</th><th>IP</th><th>Bruger</th><th>Uptime</th><th>Info</th></tr>';
    for(const c of d.clients){
      h+='<tr><td><span class="dot dot-g"></span> SSE</td>';
      h+='<td>'+c.ip+'</td>';
      h+='<td>'+(c.username||'-')+'</td>';
      h+='<td>'+fmtUp(c.uptime_s)+'</td>';
      h+='<td style="font-size:10px;color:#6c7086">slot '+c.slot+', '+c.topics+'</td></tr>';
    }
    h+='</table>';
    el.innerHTML=h;
  }catch(e){}
}
async function fetchBindings(){
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts=auth?{headers:{'Authorization':auth}}:{};
    const r=await fetch('/api/bindings',opts);
    if(r.ok){_stBindings=await r.json();}
  }catch(e){}
}
async function fetchMetrics(){
  try{
    const r=await fetch('/api/metrics',{});
    if(!r.ok)return;
    const text=await r.text();
    const m=parsePrometheus(text);
    updateDashboard(m);
    autoSizeCards();
  }catch(e){
    $('footRefresh').textContent='Forbindelse tabt...';
  }
}

const MB_CACHE_MAX=32;

async function fetchAlarms(){
  try{
    const r=await fetch('/api/alarms',{});
    if(!r.ok)return;
    _alarmLog=await r.json();
    renderAlarms();
  }catch(e){}
}
// FEAT-134: Client-side alarm filtering
function clearAlarmFilter(){
  $('alarmSev').value='-1';
  $('alarmAck').value='any';
  $('alarmSearch').value='';
  $('alarmLimit').value='25';
  renderAlarms();
}
function renderAlarms(){
  const el=$('alarmBody');
  if(!_alarmLog||_alarmLog.length===0){
    el.innerHTML='<span class="empty-msg">Ingen alarmer</span>';
    return;
  }
  const sevFilter=parseInt($('alarmSev').value);
  const ackFilter=$('alarmAck').value;
  const search=($('alarmSearch').value||'').toLowerCase();
  const limit=Math.max(5,parseInt($('alarmLimit').value)||25);
  // Filter
  const filtered=_alarmLog.filter(a=>{
    if(sevFilter>=0&&a.severity!==sevFilter)return false;
    if(ackFilter==='unack'&&a.acknowledged)return false;
    if(ackFilter==='ack'&&!a.acknowledged)return false;
    if(search){
      const hay=(a.message+' '+(a.source_ip||'')+' '+(a.username||'')+' '+(a.time||'')).toLowerCase();
      if(hay.indexOf(search)<0)return false;
    }
    return true;
  });
  $('alarmInfo').textContent=_alarmLog.length+' total, '+filtered.length+' matcher filter';
  if(filtered.length===0){
    el.innerHTML='<span class="empty-msg">Ingen alarmer matcher filter</span>';
    return;
  }
  // Show newest first
  const recent=filtered.slice(-limit).reverse();
  let h='<table class="tbl"><tr><th>Tid</th><th>Sev.</th><th>Alarm</th><th>Kilde / Detaljer</th><th>Kvit</th></tr>';
  for(const a of recent){
    const sevCls=a.severity===2?'val-err':a.severity===1?'val-warn':'val-ok';
    const sevLbl=a.severity===2?'KRIT':a.severity===1?'ADV':'INFO';
    const ack=a.acknowledged?'<span class="dot dot-off" title="Kvitteret"></span>':'<span class="dot dot-r" title="Aktiv"></span>';
    const timeStr=a.time||a.uptime;
    let src='';
    if(a.source_ip)src+='<span title="Klient IP" style="color:#89b4fa">'+a.source_ip+'</span>';
    if(a.username)src+=(src?' / ':'')+'<span title="Bruger" style="color:#cba6f7">'+a.username+'</span>';
    const txMatch=a.message.match(/TX\s*->\s*ID:(\d+)\s*@(\d+)/);
    const rxMatch=a.message.match(/RX\s*<-\s*ID:(\d+)/);
    if(txMatch)src+=(src?'<br>':'')+'<span title="Master TX til slave" style="color:#fab387">TX &rarr; Slave ID:'+txMatch[1]+' Reg:'+txMatch[2]+'</span>';
    else if(rxMatch)src+=(src?'<br>':'')+'<span title="Slave RX" style="color:#fab387">RX &larr; Slave ID:'+rxMatch[1]+'</span>';
    if(!src)src='<span style="color:#585b70">-</span>';
    h+='<tr><td style="white-space:nowrap;font-size:10px">'+timeStr+'</td>';
    h+='<td class="'+sevCls+'" style="font-weight:700;font-size:10px">'+sevLbl+'</td>';
    h+='<td>'+a.message+'</td>';
    h+='<td style="font-size:10px">'+src+'</td>';
    h+='<td>'+ack+'</td></tr>';
  }
  h+='</table>';
  el.innerHTML=h;
  autoSizeCards();
}
async function ackAlarms(){
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts={method:'POST',headers:{}};
    if(auth)opts.headers['Authorization']=auth;
    await fetch('/api/alarms/ack',opts);
    fetchAlarms();
  }catch(e){}
}
async function toggleDO(pin){
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    // Read current state from last metrics, then toggle
    var opts1=auth?{headers:{'Authorization':auth}}:{};
    const r=await fetch('/api/gpio/'+pin,opts1);
    if(!r.ok){alert('GPIO læs fejl: HTTP '+r.status);return;}
    const d=await r.json();
    const cur=(d.value===1||d.value===true)?1:0;
    const newVal=cur?0:1;
    var opts2={method:'POST',headers:{'Content-Type':'application/json'}};
    if(auth)opts2.headers['Authorization']=auth;
    opts2.body=JSON.stringify({value:newVal});
    const w=await fetch('/api/gpio/'+pin,opts2);
    if(!w.ok){const t=await w.text();alert('GPIO skriv fejl: '+(t||w.status));return;}
    // Instant visual feedback — SSE register event will confirm
    fetchMetrics();
  }catch(e){alert('GPIO toggle: '+e.message);}
}

// === Masonry Auto-Size & Card Width Toggle ===
const _RH=8,_RG=12;
function autoSizeCards(){
  const g=$('grid');
  if(!g||!g.offsetParent)return;
  if(window.innerWidth<=768)return;
  const scrollY=g.scrollTop;
  const frozenH=g.scrollHeight;
  g.style.minHeight=frozenH+'px';
  g.querySelectorAll('.card').forEach(c=>{c.style.gridRowEnd='';});
  void g.offsetHeight;
  g.querySelectorAll('.card').forEach(c=>{
    const span=Math.ceil((c.scrollHeight+_RG)/(_RH+_RG));
    c.style.gridRowEnd='span '+span;
  });
  g.style.minHeight='';
  g.scrollTop=scrollY;
}
function loadCardSizes(){
  try{
    const s=localStorage.getItem('hfplc_card_sizes');
    if(!s)return;
    const map=JSON.parse(s);
    Object.keys(map).forEach(id=>{
      const c=document.querySelector('.card[data-card-id="'+id+'"]');
      if(c&&map[id]==='wide')c.classList.add('card-wide');
    });
  }catch(e){}
}
function saveCardSizes(){
  const map={};
  document.querySelectorAll('.card[data-card-id]').forEach(c=>{
    if(c.classList.contains('card-wide'))map[c.dataset.cardId]='wide';
  });
  try{localStorage.setItem('hfplc_card_sizes',JSON.stringify(map));}catch(e){}
}
function toggleCardWide(btn){
  const card=btn.closest('.card');
  if(!card)return;
  card.classList.toggle('card-wide');
  btn.classList.toggle('is-wide',card.classList.contains('card-wide'));
  btn.title=card.classList.contains('card-wide')?'Normal bredde':'Bred (2 kolonner)';
  saveCardSizes();
  autoSizeCards();
}
function initSizeButtons(){
  document.querySelectorAll('.card[data-card-id] h2').forEach(h2=>{
    const btn=document.createElement('span');
    btn.className='sz-btn';
    btn.textContent='\u21d4';
    btn.title='Bred (2 kolonner)';
    btn.onclick=function(e){e.stopPropagation();toggleCardWide(this);};
    const card=h2.closest('.card');
    if(card&&card.classList.contains('card-wide')){btn.classList.add('is-wide');btn.title='Normal bredde';}
    h2.appendChild(btn);
  });
}
window.addEventListener('resize',function(){clearTimeout(window._szTmr);window._szTmr=setTimeout(autoSizeCards,150);});

// === Drag & Drop Card Reorder ===
function initDragDrop(){
  const grid=$('grid');
  if(!grid)return;
  const cards=grid.querySelectorAll('.card[data-card-id]');
  let dragCard=null;
  cards.forEach(card=>{
    card.draggable=true;
    const h2=card.querySelector('h2');
    if(h2){const handle=document.createElement('span');handle.className='drag-handle';handle.textContent='\u2261';h2.insertBefore(handle,h2.firstChild);}
    card.addEventListener('dragstart',e=>{dragCard=card;card.classList.add('dragging');e.dataTransfer.effectAllowed='move';});
    card.addEventListener('dragend',()=>{dragCard=null;card.classList.remove('dragging');grid.querySelectorAll('.drag-over').forEach(c=>c.classList.remove('drag-over'));});
    card.addEventListener('dragover',e=>{e.preventDefault();e.dataTransfer.dropEffect='move';if(card!==dragCard)card.classList.add('drag-over');});
    card.addEventListener('dragleave',()=>{card.classList.remove('drag-over');});
    card.addEventListener('drop',e=>{
      e.preventDefault();card.classList.remove('drag-over');
      if(!dragCard||dragCard===card)return;
      const allCards=[...grid.querySelectorAll('.card[data-card-id]')];
      const fromIdx=allCards.indexOf(dragCard);
      const toIdx=allCards.indexOf(card);
      if(fromIdx<toIdx)card.after(dragCard);else card.before(dragCard);
      saveCardOrder();
    });
  });
  restoreCardOrder();
}
function saveCardOrder(){
  const grid=$('grid');
  const ids=[...grid.querySelectorAll('.card[data-card-id]')].map(c=>c.dataset.cardId);
  const order=ids.join(',');
  // Save to ESP32 via API (persisted on next system save)
  var auth=sessionStorage.getItem('hfplc_auth');
  var opts={method:'POST',headers:{'Content-Type':'application/json'}};
  if(auth)opts.headers['Authorization']=auth;
  opts.body=JSON.stringify({card_order:order});
  fetch('/api/dashboard/layout',opts).catch(()=>{});
}
function restoreCardOrder(){
  fetch('/api/dashboard/layout',{}).then(r=>r.json()).then(d=>{
    if(!d.card_order||d.card_order.length<3)return;
    const saved=d.card_order.split(',').filter(s=>s.length>0);
    if(saved.length<2)return;
    const grid=$('grid');
    const map={};
    grid.querySelectorAll('.card[data-card-id]').forEach(c=>{map[c.dataset.cardId]=c;});
    saved.forEach(id=>{if(map[id])grid.appendChild(map[id]);});
  }).catch(()=>{});
}

// FEAT-135: Modbus Master mini-form handler
// Matches CLI syntax in cli_commands_modbus_master.cpp:
//   mb read coil     <slave> <addr>
//   mb read input    <slave> <addr>
//   mb read holding  <slave> <addr> [count=1..16]
//   mb read input-reg <slave> <addr>
//   mb write coil    <slave> <addr> <0|1>
//   mb write holding <slave> <addr> <value 0..65535>
function updateMbForm(){
  const op=$('mbOp').value;
  const valInp=$('mbVal');
  const lbl=$('mbValLabel');
  if(op.startsWith('write')){lbl.textContent='Værdi';}else if(op==='read holding'){lbl.textContent='Antal';}else{lbl.textContent='Antal';}
  if(op==='read holding'){
    valInp.title='Count (1-16)';
    valInp.placeholder='count';
    valInp.disabled=false;
    if(parseInt(valInp.value)>16||parseInt(valInp.value)<1)valInp.value=1;
  }else if(op==='write holding'){
    valInp.title='Value (0-65535)';
    valInp.placeholder='value';
    valInp.disabled=false;
  }else if(op==='write coil'){
    valInp.title='Value (0 eller 1)';
    valInp.placeholder='0/1';
    valInp.disabled=false;
    if(parseInt(valInp.value)!==0&&parseInt(valInp.value)!==1)valInp.value=1;
  }else{
    // read coil / read input / read input-reg — ingen værdi
    valInp.title='(ikke brugt)';
    valInp.placeholder='-';
    valInp.disabled=true;
  }
}
async function doMbCmd(){
  const opFull=$('mbOp').value;
  const slave=parseInt($('mbSlave').value)||1;
  const addr=parseInt($('mbAddr').value)||0;
  const valRaw=$('mbVal').value;
  const result=$('mbResult');
  // Parse "read holding" → op="read", type="holding"
  const parts=opFull.split(' ');
  const op=parts[0];
  const type=parts.slice(1).join(' ')||'holding';

  if(op==='write'){
    const v=parseInt(valRaw);
    if(type==='holding'&&(isNaN(v)||v<0||v>65535)){result.textContent='Fejl: value skal være 0-65535';return;}
    if(type==='coil'&&v!==0&&v!==1){result.textContent='Fejl: coil value skal være 0 eller 1';return;}
  }

  const label=opFull+' slave='+slave+' addr='+addr;
  result.textContent='> '+label+'\nSender via kø...';
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts={method:'POST',headers:{'Content-Type':'application/json'}};
    if(auth)opts.headers['Authorization']=auth;
    var body={op:op,type:type,slave:slave,addr:addr};
    if(op==='write')body.value=parseInt(valRaw)||0;
    opts.body=JSON.stringify(body);
    const r=await fetch('/api/modbus/master/rw',opts);
    if(r.status===401){result.textContent='> '+label+'\nFejl: Login kræves';return;}
    if(r.status===403){result.textContent='> '+label+'\nFejl: Skriv-rettigheder kræves';return;}
    if(!r.ok){result.textContent='> '+label+'\nHTTP '+r.status;return;}
    const d=await r.json();
    if(d.status==='ok'){
      result.textContent='> '+label+'\nVærdi: '+d.value+' ('+d.hex+') signed: '+d.signed+'\nCache alder: '+d.age_ms+'ms';
    }else if(d.status==='pending'||d.status==='queued'){
      result.textContent='> '+label+'\n'+d.message+'\nVenter på svar...';
      // Poll for result after async queue processes
      setTimeout(()=>doMbPoll(op,type,slave,addr,label),500);
    }else{
      result.textContent='> '+label+'\n'+(d.message||JSON.stringify(d));
    }
  }catch(e){
    result.textContent='> '+label+'\nNetværksfejl: '+e.message;
  }
}
async function doMbPoll(op,type,slave,addr,label,attempt){
  attempt=attempt||1;
  if(attempt>6)return; // max 3s
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts={method:'POST',headers:{'Content-Type':'application/json'}};
    if(auth)opts.headers['Authorization']=auth;
    opts.body=JSON.stringify({op:'read',type:type,slave:slave,addr:addr});
    const r=await fetch('/api/modbus/master/rw',opts);
    if(!r.ok)return;
    const d=await r.json();
    const result=$('mbResult');
    if(d.status==='ok'){
      result.textContent='> '+label+'\nVærdi: '+d.value+' ('+d.hex+') signed: '+d.signed+'\nCache alder: '+d.age_ms+'ms';
    }else if(d.status==='pending'){
      setTimeout(()=>doMbPoll(op,type,slave,addr,label,attempt+1),500);
    }else{
      result.textContent='> '+label+'\n'+(d.message||d.status);
    }
  }catch(e){}
}

function resetMasterStats(){
  var auth=sessionStorage.getItem('hfplc_auth');
  var opts={method:'POST'};
  if(auth)opts.headers={'Authorization':auth};
  fetch('/api/modbus/master/reset-stats',opts).then(r=>{
    if(r.ok){fetchMetrics();}else{alert('Fejl ved nulstilling');}
  }).catch(()=>alert('Netværksfejl'));
}
function fmtUptime(ms){
  if(ms==null||ms<=0)return'-';
  var s=Math.floor(ms/1000);
  if(s<60)return s+'s';
  var m=Math.floor(s/60);s%=60;
  if(m<60)return m+'m '+s+'s';
  var h=Math.floor(m/60);m%=60;
  if(h<24)return h+'t '+m+'m';
  var d=Math.floor(h/24);h%=24;
  return d+'d '+h+'t';
}

// === FEAT-130: SSE-driven live updates ===
// Subscribes to ESP32 SSE event stream for instant register/coil/counter/timer updates.
// Falls back to polling if SSE unavailable.
let sseConn=null;
let sseReconnectTimer=null;
let sseLastEventMs=0;
function sseSetIndicator(connected){
  const el=$('footRefresh');
  if(!el)return;
  if(connected){el.style.color='#a6e3a1';}
  else{el.style.color='';}
}
function sseApplyRegisterEvent(d){
  // d = {type:"hr"|"ir"|"coil"|"di", addr:N, value:N}
  if(!d||d.addr==null)return;
  const addr=d.addr|0;
  const val=d.value|0;
  if(d.type==='hr'){
    // Update both regHR grid and regmapHR grid
    ['regHR','regmapHR'].forEach(gid=>{
      const g=$(gid);if(!g||!g.children[addr])return;
      const cell=g.children[addr];
      cell.textContent=val;
      cell.title='HR['+addr+'] = '+val;
      cell.className=val!==0?'rc used':'rc';
    });
  }else if(d.type==='ir'){
    const g=$('regmapIR');if(g&&g.children[addr]){
      const cell=g.children[addr];
      cell.textContent=val;
      cell.title='IR['+addr+'] = '+val;
      cell.className=val!==0?'rc used':'rc';
    }
  }else if(d.type==='coil'){
    ['regCoils','regmapCoils'].forEach(gid=>{
      const g=$(gid);if(!g||!g.children[addr])return;
      const cell=g.children[addr];
      cell.textContent=val;
      cell.title='Coil['+addr+'] = '+(val?'ON':'OFF');
      cell.className='rc '+(val?'coil-on':'coil-off');
    });
    // Also update DIO output LEDs (pins 201-208 map to coils via gpio metric labels)
    const ledParent=$('dioOutputs');
    if(ledParent){
      // Re-fetch metrics soon to pick up gpio_digital_output change
      // (no direct coil->pin mapping on client side)
    }
  }else if(d.type==='di'){
    const g=$('regmapDI');if(g&&g.children[addr]){
      const cell=g.children[addr];
      cell.textContent=val;
      cell.title='DI['+addr+'] = '+val;
      cell.className=val!==0?'rc used':'rc';
    }
  }
  sseLastEventMs=Date.now();
  // Coil/DI changes drive DIO LEDs which come from /api/metrics gpio labels
  // — kick a debounced metrics refresh so LEDs flip instantly too.
  if(d.type==='coil'||d.type==='di'){sseKickMetrics();}
}
// Debounced metrics refresh — max 1 fetch per 150ms regardless of event burst
var sseMetricsPending=false;
function sseKickMetrics(){
  if(sseMetricsPending)return;
  sseMetricsPending=true;
  setTimeout(function(){sseMetricsPending=false;fetchMetrics();},150);
}
function sseApplyCounterEvent(d){
  sseLastEventMs=Date.now();
  // Full metrics refresh picks up counter widget values + master stats
  sseKickMetrics();
}
function sseApplyTimerEvent(d){
  sseLastEventMs=Date.now();
  sseKickMetrics();
}
function sseConnect(){
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts=auth?{headers:{'Authorization':auth}}:{};
    fetch('/api/events/status',opts).then(r=>r.json()).then(s=>{
      if(!s||!s.sse_enabled){
        console.log('[SSE] disabled, using polling only');
        return;
      }
      let port=s.sse_port;
      if(!port||port===0){
        // Auto port = main port + 1
        const mainPort=parseInt(location.port||'80')||80;
        port=mainPort+1;
      }
      // Query-string token workaround: EventSource cannot set Basic Auth
      // header, and browsers do not share credentials across ports. Token is
      // issued by /api/events/status (on main port, authenticated normally).
      const tok=s.sse_token?('&token='+encodeURIComponent(s.sse_token)):'';
      const url=location.protocol+'//'+location.hostname+':'+port+'/api/events?subscribe=all'+tok;
      try{
        sseConn=new EventSource(url);
      }catch(e){
        console.log('[SSE] EventSource construct failed:',e);
        return;
      }
      sseConn.addEventListener('connected',()=>{
        console.log('[SSE] connected');
        sseSetIndicator(true);
        // Slow polling since SSE handles instant updates
        if(refreshTimer){clearInterval(refreshTimer);refreshTimer=setInterval(fetchMetrics,5000);}
      });
      sseConn.addEventListener('register',e=>{
        try{sseApplyRegisterEvent(JSON.parse(e.data));}catch(err){}
      });
      sseConn.addEventListener('counter',e=>{
        try{sseApplyCounterEvent(JSON.parse(e.data));}catch(err){}
      });
      sseConn.addEventListener('timer',e=>{
        try{sseApplyTimerEvent(JSON.parse(e.data));}catch(err){}
      });
      sseConn.addEventListener('heartbeat',()=>{sseLastEventMs=Date.now();});
      sseConn.onerror=()=>{
        console.log('[SSE] connection error, will retry');
        sseSetIndicator(false);
        if(sseConn){sseConn.close();sseConn=null;}
        // Restore fast polling as fallback
        if(refreshTimer){clearInterval(refreshTimer);refreshTimer=setInterval(fetchMetrics,3000);}
        if(sseReconnectTimer)clearTimeout(sseReconnectTimer);
        sseReconnectTimer=setTimeout(sseConnect,5000);
      };
    }).catch(e=>console.log('[SSE] status check failed:',e));
  }catch(e){console.log('[SSE] init failed:',e);}
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

  loadCardSizes();
  initSizeButtons();
  initDragDrop();
  autoSizeCards();
  updateMbForm();
  fetchBindings();
  fetchMetrics();
  fetchAlarms();
  fetchNetworkInfo();
  fetchConnections();
  refreshTimer=setInterval(fetchMetrics,3000);
  setInterval(fetchBindings,15000);
  setInterval(fetchAlarms,5000);
  setInterval(fetchNetworkInfo,10000);
  setInterval(fetchConnections,5000);
  sseConnect();
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
async function doSystemSave(){
  const btn=$('saveBtn');
  btn.classList.add('saving');btn.textContent='\u23F3 Gemmer...';
  try{
    var auth=sessionStorage.getItem('hfplc_auth');
    var opts={method:'POST',headers:{}};
    if(auth)opts.headers['Authorization']=auth;
    const r=await fetch('/api/system/save',opts);
    if(r.ok){btn.classList.remove('saving');btn.classList.add('saved');btn.textContent='\u2705 Gemt!';}
    else{btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}
  }catch(e){btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}
  setTimeout(()=>{btn.className='save-btn';btn.innerHTML='&#128190; Save';},2000);
}
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
