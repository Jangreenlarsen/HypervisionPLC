/**
 * @file web_system.cpp
 * @brief Web-based system administration page (v7.4.0)
 *
 * LAYER 7: User Interface - System Administration
 * Serves a system management page at "/system" with:
 * - Full config backup/restore (JSON export/import)
 * - Config save/load to NVS
 * - Factory defaults
 * - Reboot
 * - Persist group management
 * - System information
 *
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_system.h"
#include "debug.h"

static const char PROGMEM system_html[] = R"rawhtml(<!DOCTYPE html>
<html lang="da">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HyberFusion PLC Dashboard : System</title>
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
.hdr{background:#181825;padding:10px 16px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #313244;flex-shrink:0}
.hdr h1{font-size:16px;color:#89b4fa;flex-shrink:0}
.hdr .info{font-size:11px;color:#6c7086;margin-left:auto}
.content{flex:1;overflow-y:auto;padding:16px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(380px,1fr));gap:16px;align-content:start}
.card{background:#181825;border:1px solid #313244;border-radius:8px;padding:16px 20px}
.card h2{font-size:14px;color:#89b4fa;margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid #313244;display:flex;align-items:center;gap:8px}
.card h2 .icon{font-size:16px}
.card p{font-size:12px;color:#6c7086;margin-bottom:12px;line-height:1.5}
.btn{padding:6px 16px;border:none;border-radius:4px;cursor:pointer;font-size:12px;font-weight:600;transition:all .15s;display:inline-flex;align-items:center;gap:6px}
.btn-primary{background:#89b4fa;color:#1e1e2e}.btn-primary:hover{background:#74c7ec}
.btn-success{background:#a6e3a1;color:#1e1e2e}.btn-success:hover{background:#94e2d5}
.btn-danger{background:#f38ba8;color:#1e1e2e}.btn-danger:hover{background:#eba0ac}
.btn-warn{background:#fab387;color:#1e1e2e}.btn-warn:hover{background:#f9e2af}
.btn-sm{padding:4px 12px;font-size:11px}
.btn:disabled{opacity:.4;cursor:not-allowed}
.btn-row{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}
.alert{padding:8px 12px;border-radius:4px;font-size:12px;margin-top:10px;display:none}
.alert-ok{background:rgba(166,227,161,.15);color:#a6e3a1;border:1px solid rgba(166,227,161,.3)}
.alert-err{background:rgba(243,139,168,.15);color:#f38ba8;border:1px solid rgba(243,139,168,.3)}
.alert-warn{background:rgba(250,179,135,.15);color:#fab387;border:1px solid rgba(250,179,135,.3)}
.alert-info{background:rgba(137,180,250,.15);color:#89b4fa;border:1px solid rgba(137,180,250,.3)}
.file-drop{border:2px dashed #45475a;border-radius:6px;padding:20px;text-align:center;color:#6c7086;font-size:12px;cursor:pointer;transition:all .2s;margin-top:8px}
.file-drop:hover,.file-drop.drag{border-color:#89b4fa;color:#89b4fa;background:rgba(137,180,250,.05)}
.file-drop input{display:none}
.info-grid{display:grid;grid-template-columns:140px 1fr;gap:4px 12px;font-size:12px;margin-top:8px}
.info-grid .lbl{color:#6c7086}.info-grid .val{color:#cdd6f4;font-weight:600;font-variant-numeric:tabular-nums}
.tbl{width:100%;border-collapse:collapse;font-size:12px;margin-top:8px}
.tbl th{text-align:left;padding:6px 8px;color:#89b4fa;border-bottom:1px solid #313244;font-weight:600;background:#181825}
.tbl td{padding:5px 8px;border-bottom:1px solid rgba(49,50,68,.5)}
.tbl tr:hover{background:rgba(49,50,68,.3)}
.confirm-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:100;align-items:center;justify-content:center}
.confirm-overlay.show{display:flex}
.confirm-box{background:#1e1e2e;border:1px solid #313244;border-radius:8px;padding:24px;width:360px;text-align:center}
.confirm-box h3{color:#f38ba8;font-size:16px;margin-bottom:12px}
.confirm-box p{font-size:12px;color:#a6adc8;margin-bottom:16px;line-height:1.5}
.confirm-box .actions{display:flex;gap:8px;justify-content:center}
.spinner{display:inline-block;width:14px;height:14px;border:2px solid rgba(30,30,46,.3);border-top-color:#1e1e2e;border-radius:50%;animation:spin .6s linear infinite;vertical-align:middle}
@keyframes spin{to{transform:rotate(360deg)}}
.foot{background:#181825;border-top:1px solid #313244;padding:4px 16px;font-size:10px;color:#45475a;flex-shrink:0;display:flex;justify-content:space-between}
@media(max-width:768px){.grid{grid-template-columns:1fr}.topnav{flex-wrap:wrap}}
</style>
</head>
<body>

<!-- Top Navigation -->
<div class="topnav">
<span class="brand">HyberFusion PLC</span>
<a href="/">Monitor</a>
<a href="/editor">ST Editor</a>
<a href="/cli">CLI</a>
<a href="/system" class="active">System</a>
<button class="save-btn" id="saveBtn" onclick="doGlobalSave()" title="Gem konfiguration til NVS">&#128190; Save</button>
<div class="user-badge"><div class="user-btn" id="userBtn" onclick="toggleUserMenu()"><span class="dot dot-off" id="userDot"></span><span id="userName">Ikke logget ind</span></div><div class="user-menu" id="userMenu"><div class="um-row"><span>Bruger:</span><span class="um-val" id="umUser">-</span></div><div class="um-row"><span>Roller:</span><span class="um-val" id="umRoles">-</span></div><div class="um-row"><span>Privilegier:</span><span class="um-val" id="umPriv">-</span></div><div class="um-row"><span>Auth mode:</span><span class="um-val" id="umMode">-</span></div><div class="um-sep"></div><div class="um-btn" id="umLogout" onclick="doLogout()">Log ud</div></div></div>
</div>

<!-- Header -->
<div class="hdr">
<h1>System Administration</h1>
<span class="info" id="hdrInfo"></span>
</div>

<!-- Login Modal -->
<div class="confirm-overlay" id="loginModal">
<div class="confirm-box" style="text-align:left">
<h3 style="color:#89b4fa">Login</h3>
<p>System-siden kræver autentificering.</p>
<div style="margin-bottom:8px"><label style="font-size:12px;color:#a6adc8">Brugernavn</label><br>
<input type="text" id="authUser" style="width:100%;padding:6px 10px;background:#313244;border:1px solid #45475a;border-radius:4px;color:#cdd6f4;font-size:13px;margin-top:4px" autocomplete="username"></div>
<div style="margin-bottom:8px"><label style="font-size:12px;color:#a6adc8">Adgangskode</label><br>
<input type="password" id="authPass" style="width:100%;padding:6px 10px;background:#313244;border:1px solid #45475a;border-radius:4px;color:#cdd6f4;font-size:13px;margin-top:4px" autocomplete="current-password"></div>
<div id="loginErr" style="display:none;color:#f38ba8;font-size:12px;padding:4px 0"></div>
<div class="actions" style="justify-content:flex-end;margin-top:12px">
<button class="btn btn-primary" onclick="doLogin()">Forbind</button>
</div>
</div>
</div>

<!-- Confirm Dialog -->
<div class="confirm-overlay" id="confirmDlg">
<div class="confirm-box">
<h3 id="confirmTitle">Bekræft</h3>
<p id="confirmMsg">Er du sikker?</p>
<div class="actions">
<button class="btn btn-primary" onclick="closeConfirm()">Annuller</button>
<button class="btn btn-danger" id="confirmBtn" onclick="confirmAction()">Bekræft</button>
</div>
</div>
</div>

<!-- Content -->
<div class="content">
<div class="grid">

<!-- System Info -->
<div class="card">
<h2>System Information</h2>
<div class="info-grid" id="sysInfo">
<span class="lbl">Version</span><span class="val" id="siVersion">-</span>
<span class="lbl">Uptime</span><span class="val" id="siUptime">-</span>
<span class="lbl">Heap fri</span><span class="val" id="siHeap">-</span>
<span class="lbl">Heap minimum</span><span class="val" id="siHeapMin">-</span>
<span class="lbl">PSRAM</span><span class="val" id="siPsram">-</span>
<span class="lbl">WiFi RSSI</span><span class="val" id="siRssi">-</span>
<span class="lbl">Modbus slave ID</span><span class="val" id="siSlaveId">-</span>
<span class="lbl">Modbus baud</span><span class="val" id="siBaud">-</span>
<span class="lbl">ST Logic slots</span><span class="val" id="siSlots">-</span>
<span class="lbl">Counters aktive</span><span class="val" id="siCounters">-</span>
<span class="lbl">Timers aktive</span><span class="val" id="siTimers">-</span>
</div>
<div class="btn-row" style="margin-top:12px">
<button class="btn btn-sm btn-primary" onclick="refreshInfo()">Opdater</button>
</div>
</div>

<!-- Config Backup -->
<div class="card">
<h2>Konfiguration Backup</h2>
<p>Download komplet system-konfiguration som JSON fil. Inkluderer alle counters, timers, ST programs, bindings, Modbus settings, GPIO m.m.</p>
<div class="btn-row">
<button class="btn btn-primary" onclick="doBackup()">Download Backup</button>
</div>
<div class="alert" id="backupAlert"></div>
</div>

<!-- Config Restore -->
<div class="card">
<h2>Konfiguration Restore</h2>
<p>Gendan komplet system-konfiguration fra en backup JSON fil. OBS: Dette overskriver alle nuværende indstillinger!</p>
<div class="file-drop" id="restoreDrop" onclick="document.getElementById('restoreFile').click()">
Klik her eller træk en backup-fil hertil
<input type="file" id="restoreFile" accept=".json" onchange="handleRestoreFile(event)">
</div>
<div id="restorePreview" style="display:none;margin-top:8px">
<div class="info-grid" id="restoreInfo"></div>
<div class="btn-row">
<button class="btn btn-warn" onclick="doRestore()">Gendan Konfiguration</button>
<button class="btn btn-sm btn-primary" onclick="cancelRestore()">Annuller</button>
</div>
</div>
<div class="alert" id="restoreAlert"></div>
</div>

<!-- NVS Save/Load -->
<div class="card">
<h2>NVS Persistent Storage</h2>
<p>Gem aktuel konfiguration til NVS flash, eller genindlæs fra NVS. Ændringer der ikke er gemt går tabt ved reboot.</p>
<div class="btn-row">
<button class="btn btn-success" onclick="doNvsSave()">Gem til NVS</button>
<button class="btn btn-primary" onclick="doNvsLoad()">Indlæs fra NVS</button>
</div>
<div class="alert" id="nvsAlert"></div>
</div>

<!-- Persist Groups -->
<div class="card" style="grid-column:1/-1">
<h2>Persist Grupper</h2>
<p>Administrer persistente register-grupper der automatisk gemmes/gendannes.</p>
<div class="btn-row" style="margin-bottom:8px">
<button class="btn btn-sm btn-primary" onclick="loadPersistGroups()">Opdater</button>
<button class="btn btn-sm btn-success" onclick="doPersistSave()">Gem alle grupper</button>
<button class="btn btn-sm btn-warn" onclick="doPersistRestore()">Gendan alle grupper</button>
</div>
<table class="tbl" id="persistTable">
<thead><tr><th>Gruppe</th><th>Start Register</th><th>Antal</th><th>Type</th><th>Auto-save</th></tr></thead>
<tbody id="persistBody"><tr><td colspan="5" style="color:#6c7086;text-align:center">Indlæser...</td></tr></tbody>
</table>
<div class="alert" id="persistAlert"></div>
</div>

<!-- Factory Defaults -->
<div class="card">
<h2>Fabriksindstillinger</h2>
<p>Nulstil alle indstillinger til fabriksværdier. OBS: Alle konfigurationer, ST-programmer, counters, timers og bindings slettes!</p>
<div class="btn-row">
<button class="btn btn-danger" onclick="askConfirm('Fabriksindstillinger','Alle indstillinger nulstilles til fabriksværdier. Denne handling kan IKKE fortrydes! Tag backup først.',doDefaults)">Nulstil til Fabriksindstillinger</button>
</div>
<div class="alert" id="defaultsAlert"></div>
</div>

<!-- Reboot -->
<div class="card">
<h2>Genstart System</h2>
<p>Genstart ESP32 controlleren. Gem konfiguration til NVS først hvis der er ugemte ændringer.</p>
<div class="btn-row">
<button class="btn btn-success" onclick="doSaveAndReboot()">Gem &amp; Genstart</button>
<button class="btn btn-warn" onclick="askConfirm('Genstart','Genstart ESP32 uden at gemme? Ugemte ændringer går tabt.',doReboot)">Genstart uden gem</button>
</div>
<div class="alert" id="rebootAlert"></div>
</div>

<!-- SSE Client Management -->
<div class="card" style="grid-column:1/-1">
<h2>SSE Klienter</h2>
<p>Server-Sent Events forbindelser. Overvåg og afbryd klienter.</p>
<div class="btn-row" style="margin-bottom:8px">
<button class="btn btn-primary btn-sm" onclick="refreshSseClients()">Opdater</button>
<button class="btn btn-danger btn-sm" onclick="askConfirm('Afbryd alle','Afbryd alle SSE-klienter?',disconnectAllSse)">Afbryd alle</button>
</div>
<div id="sseInfo" style="font-size:12px;color:#6c7086;margin-bottom:8px">Indlæser...</div>
<table class="tbl" id="sseTbl">
<thead><tr><th>Slot</th><th>IP-adresse</th><th>Bruger</th><th>Topics</th><th>Uptime</th><th></th></tr></thead>
<tbody id="sseTbody"><tr><td colspan="6" style="color:#6c7086;text-align:center">Indlæser...</td></tr></tbody>
</table>
<div class="alert" id="sseAlert"></div>
</div>

<!-- OTA Firmware Update (FEAT-031) -->
<div class="card" style="grid-column:1/-1">
<h2><span class="icon">&#9889;</span> OTA Firmware Update</h2>
<div class="info-grid" style="margin-bottom:12px">
<span class="lbl">Firmware version</span><span class="val" id="otaCurVer">-</span>
<span class="lbl">Aktiv partition</span><span class="val" id="otaRunPart">-</span>
<span class="lbl">Boot partition</span><span class="val" id="otaBootPart">-</span>
<span class="lbl">Rollback mulig</span><span class="val" id="otaRollback">-</span>
</div>
<div class="file-drop" id="otaDrop" onclick="document.getElementById('otaFile').click()">
Klik her eller traek en firmware .bin fil hertil
<input type="file" id="otaFile" accept=".bin" onchange="handleOtaFile(event)">
</div>
<div id="otaFileName" style="font-size:12px;color:#a6e3a1;margin-top:4px;display:none"></div>
<div id="otaProgress" style="display:none;margin-top:10px">
<div style="height:20px;background:#313244;border-radius:10px;overflow:hidden;position:relative">
<div id="otaFill" style="height:100%;background:linear-gradient(90deg,#89b4fa,#74c7ec);border-radius:10px;transition:width .3s;width:0%"></div>
<div id="otaPct" style="position:absolute;top:0;left:0;right:0;height:20px;line-height:20px;text-align:center;font-size:11px;font-weight:600;color:#1e1e2e">0%</div>
</div>
</div>
<div class="btn-row">
<button class="btn btn-primary" id="otaUploadBtn" disabled onclick="startOta()">Upload &amp; Flash</button>
<button class="btn btn-danger btn-sm" id="otaRollbackBtn" disabled onclick="doOtaRollback()">Rollback</button>
</div>
<div class="alert" id="otaAlert"></div>
</div>

</div><!-- grid -->
</div><!-- content -->

<!-- Footer -->
<div class="foot">
<span>HyberFusion PLC System Administration</span>
<span id="footVer"></span>
</div>

<script>
let AUTH=sessionStorage.getItem('hfplc_auth')||'';
let pendingConfirmFn=null;
let restoreData=null;

function $(id){return document.getElementById(id)}

// --- Auth ---
function doLogin(){
  const u=$('authUser').value, p=$('authPass').value;
  if(!u||!p){$('loginErr').style.display='block';$('loginErr').textContent='Udfyld begge felter';return}
  AUTH='Basic '+btoa(u+':'+p);
  fetch('/api/status',{headers:{'Authorization':AUTH}}).then(r=>{
    if(!r.ok)throw new Error('Auth failed');
    return r.json();
  }).then(()=>{
    sessionStorage.setItem('hfplc_auth',AUTH);
    $('loginModal').classList.remove('show');
    refreshInfo();
    loadPersistGroups();
    fetchOtaStatus();
    refreshSseClients();
    updateUserBadge();
  }).catch(()=>{
    $('loginErr').style.display='block';
    $('loginErr').textContent='Forkert brugernavn eller adgangskode';
  });
}
$('authPass').addEventListener('keydown',e=>{if(e.key==='Enter')doLogin()});
// Auto-login if session auth exists, otherwise show login modal
if(AUTH){fetch('/api/status',{headers:{'Authorization':AUTH}}).then(r=>{if(r.ok){refreshInfo();loadPersistGroups();fetchOtaStatus();refreshSseClients();updateUserBadge();}else{AUTH='';sessionStorage.removeItem('hfplc_auth');$('loginModal').classList.add('show');}}).catch(()=>{$('loginModal').classList.add('show');});}
else{$('loginModal').classList.add('show');}

// --- Confirm Dialog ---
function askConfirm(title,msg,fn){
  $('confirmTitle').textContent=title;
  $('confirmMsg').textContent=msg;
  pendingConfirmFn=fn;
  $('confirmDlg').classList.add('show');
}
function closeConfirm(){$('confirmDlg').classList.remove('show');pendingConfirmFn=null}
function confirmAction(){var fn=pendingConfirmFn;closeConfirm();if(fn)fn()}

// --- Alert helper ---
function showAlert(id,type,msg){
  const el=$(id);
  el.className='alert alert-'+type;
  el.textContent=msg;
  el.style.display='block';
  setTimeout(()=>{el.style.display='none'},8000);
}

// --- API helper ---
function api(url,opts={}){
  opts.headers=opts.headers||{};
  opts.headers['Authorization']=AUTH;
  return fetch(url,opts).then(r=>{if(r.status===401){AUTH='';sessionStorage.removeItem('hfplc_auth');$('loginModal').classList.add('show');}return r;});
}

// --- System Info ---
function refreshInfo(){
  api('/api/metrics').then(r=>r.text()).then(txt=>{
    const m={};
    txt.split('\n').forEach(l=>{
      if(l.startsWith('#')||!l.trim())return;
      const parts=l.split(' ');
      if(parts.length>=2)m[parts[0]]=parseFloat(parts[1]);
    });
    const fmt=v=>v!=null?v.toLocaleString('da-DK'):'-';
    const fmtB=v=>v!=null?(v>1048576?(v/1048576).toFixed(1)+' MB':(v/1024).toFixed(0)+' KB'):'-';
    const fmtUp=s=>{if(s==null)return'-';const d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),mi=Math.floor((s%3600)/60);return(d?d+'d ':'')+(h?h+'t ':'')+mi+'m'};
    $('siVersion').textContent=m['esp32_firmware_info']!=null?'-':'se /api/status';
    $('siUptime').textContent=fmtUp(m['esp32_uptime_seconds']);
    $('siHeap').textContent=fmtB(m['esp32_heap_free_bytes']);
    $('siHeapMin').textContent=fmtB(m['esp32_heap_minimum_free_bytes']);
    $('siPsram').textContent=m['esp32_psram_total_bytes']?fmtB(m['esp32_psram_free_bytes'])+' / '+fmtB(m['esp32_psram_total_bytes']):'Ikke tilgængelig';
    $('siRssi').textContent=m['esp32_wifi_rssi_dbm']!=null?m['esp32_wifi_rssi_dbm']+' dBm':'-';
    $('siSlaveId').textContent=fmt(m['modbus_slave_id']);
    $('siBaud').textContent=fmt(m['modbus_baudrate']);
    $('siSlots').textContent=fmt(m['st_logic_active_programs']);
    $('siCounters').textContent=fmt(m['counter_active_count']);
    $('siTimers').textContent=fmt(m['timer_active_count']);
  }).catch(()=>{});

  api('/api/status').then(r=>r.json()).then(d=>{
    var ver=d.firmware||('v'+d.version+'.'+d.build)||d.version||'';
    $('siVersion').textContent=ver;
    $('footVer').textContent=ver;
  }).catch(()=>{});
}

// --- Backup ---
function doBackup(){
  api('/api/system/backup').then(r=>{
    if(!r.ok)throw new Error('Backup failed');
    return r.json();
  }).then(data=>{
    const blob=new Blob([JSON.stringify(data,null,2)],{type:'application/json'});
    const url=URL.createObjectURL(blob);
    const a=document.createElement('a');
    const ts=new Date().toISOString().replace(/[:.]/g,'-').slice(0,19);
    a.href=url;a.download='hyberfusion_backup_'+ts+'.json';
    a.click();URL.revokeObjectURL(url);
    showAlert('backupAlert','ok','Backup downloadet!');
  }).catch(e=>showAlert('backupAlert','err','Backup fejlede: '+e.message));
}

// --- Restore ---
const drop=$('restoreDrop');
drop.addEventListener('dragover',e=>{e.preventDefault();drop.classList.add('drag')});
drop.addEventListener('dragleave',()=>drop.classList.remove('drag'));
drop.addEventListener('drop',e=>{
  e.preventDefault();drop.classList.remove('drag');
  if(e.dataTransfer.files.length)readRestoreFile(e.dataTransfer.files[0]);
});
function handleRestoreFile(e){if(e.target.files.length)readRestoreFile(e.target.files[0])}
function readRestoreFile(file){
  const reader=new FileReader();
  reader.onload=e=>{
    try{
      restoreData=JSON.parse(e.target.result);
      const info=$('restoreInfo');
      info.innerHTML='';
      const add=(l,v)=>{info.innerHTML+='<span class="lbl">'+l+'</span><span class="val">'+v+'</span>'};
      add('Filnavn',file.name);
      add('Størrelse',(file.size/1024).toFixed(1)+' KB');
      if(restoreData.version)add('Backup version',restoreData.version);
      if(restoreData.counters)add('Counters',Array.isArray(restoreData.counters)?restoreData.counters.length:'ja');
      if(restoreData.timers)add('Timers',Array.isArray(restoreData.timers)?restoreData.timers.length:'ja');
      if(restoreData.programs||restoreData.logic)add('ST Programs','ja');
      if(restoreData.modbus)add('Modbus config','ja');
      if(restoreData.bindings)add('Bindings',Array.isArray(restoreData.bindings)?restoreData.bindings.length:'ja');
      $('restorePreview').style.display='block';
      drop.style.display='none';
    }catch(err){
      showAlert('restoreAlert','err','Ugyldig JSON fil: '+err.message);
    }
  };
  reader.readAsText(file);
}
function cancelRestore(){
  restoreData=null;
  $('restorePreview').style.display='none';
  drop.style.display='block';
  $('restoreFile').value='';
}
function doRestore(){
  if(!restoreData){showAlert('restoreAlert','err','Ingen fil valgt');return}
  askConfirm('Gendan Konfiguration','Alle nuværende indstillinger overskrives med backup-filen. Fortsæt?',()=>{
    api('/api/system/restore',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify(restoreData)
    }).then(r=>{
      if(!r.ok)throw new Error('HTTP '+r.status);
      return r.json();
    }).then(d=>{
      showAlert('restoreAlert','ok','Konfiguration gendannet! '+(d.message||''));
      cancelRestore();
      refreshInfo();
    }).catch(e=>showAlert('restoreAlert','err','Restore fejlede: '+e.message));
  });
}

// --- Global Save (topnav) ---
async function doGlobalSave(){
  var btn=document.getElementById('saveBtn');
  btn.classList.add('saving');btn.textContent='\u23F3 Gemmer...';
  try{
    var r=await api('/api/system/save',{method:'POST'});
    if(r.ok){btn.classList.remove('saving');btn.classList.add('saved');btn.textContent='\u2705 Gemt!';}
    else{btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}
  }catch(e){btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}
  setTimeout(()=>{btn.className='save-btn';btn.innerHTML='&#128190; Save';},2000);
}

// --- NVS Save/Load ---
function doNvsSave(){
  api('/api/system/save',{method:'POST'}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>showAlert('nvsAlert','ok','Konfiguration gemt til NVS! '+(d.message||'')))
  .catch(e=>showAlert('nvsAlert','err','Gem fejlede: '+e.message));
}
function doNvsLoad(){
  api('/api/system/load',{method:'POST'}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>{
    showAlert('nvsAlert','ok','Konfiguration indlæst fra NVS! '+(d.message||''));
    refreshInfo();
  }).catch(e=>showAlert('nvsAlert','err','Indlæsning fejlede: '+e.message));
}

// --- Persist Groups ---
function loadPersistGroups(){
  api('/api/persist/groups').then(r=>r.json()).then(data=>{
    const tbody=$('persistBody');
    const groups=data.groups||data||[];
    if(!Array.isArray(groups)||groups.length===0){
      tbody.innerHTML='<tr><td colspan="5" style="color:#6c7086;text-align:center">Ingen persist grupper konfigureret</td></tr>';
      return;
    }
    tbody.innerHTML='';
    groups.forEach(g=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td>'+(g.name||g.id||'-')+'</td><td>'+(g.start!=null?g.start:'-')+'</td><td>'+(g.count||g.length||'-')+'</td><td>'+(g.type||'HR')+'</td><td>'+(g.auto_save?'Ja':'Nej')+'</td>';
      tbody.appendChild(tr);
    });
  }).catch(()=>{
    $('persistBody').innerHTML='<tr><td colspan="5" style="color:#6c7086;text-align:center">Kunne ikke indlæse persist grupper</td></tr>';
  });
}
function doPersistSave(){
  api('/api/persist/save',{method:'POST'}).then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>showAlert('persistAlert','ok','Persist grupper gemt! '+(d.message||'')))
  .catch(e=>showAlert('persistAlert','err','Gem fejlede: '+e.message));
}
function doPersistRestore(){
  api('/api/persist/restore',{method:'POST'}).then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>showAlert('persistAlert','ok','Persist grupper gendannet! '+(d.message||'')))
  .catch(e=>showAlert('persistAlert','err','Restore fejlede: '+e.message));
}

// --- Factory Defaults ---
function doDefaults(){
  api('/api/system/defaults',{method:'POST'}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>{
    showAlert('defaultsAlert','ok','Fabriksindstillinger anvendt! '+(d.message||''));
    refreshInfo();
  }).catch(e=>showAlert('defaultsAlert','err','Nulstilling fejlede: '+e.message));
}

// --- Reboot ---
function doReboot(){
  showAlert('rebootAlert','warn','Genstarter...');
  api('/api/system/reboot',{method:'POST'}).then(r=>{
    if(r.status===403){showAlert('rebootAlert','err','Ingen skriveadgang (privilege read)');return}
    if(!r.ok)throw new Error('HTTP '+r.status);
    setTimeout(()=>{showAlert('rebootAlert','info','System genstarter. Siden genindlæses om 10 sekunder...')},1000);
    setTimeout(()=>{window.location.reload()},12000);
  }).catch(e=>{showAlert('rebootAlert','err','Genstart fejlede: '+e.message)});
}
function doSaveAndReboot(){
  showAlert('rebootAlert','info','Gemmer konfiguration...');
  api('/api/system/save',{method:'POST'}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('Save failed: HTTP '+r.status);
    showAlert('rebootAlert','warn','Gemt! Genstarter...');
    return api('/api/system/reboot',{method:'POST'});
  }).then(r=>{
    setTimeout(()=>{showAlert('rebootAlert','info','System genstarter. Siden genindlæses om 10 sekunder...')},1000);
    setTimeout(()=>{window.location.reload()},12000);
  }).catch(e=>{showAlert('rebootAlert','err','Fejl: '+e.message)});
}

// --- OTA Firmware Update (FEAT-031) ---
let otaSelectedFile=null;
const otaDrop=$('otaDrop');
otaDrop.addEventListener('dragover',e=>{e.preventDefault();otaDrop.classList.add('drag')});
otaDrop.addEventListener('dragleave',()=>otaDrop.classList.remove('drag'));
otaDrop.addEventListener('drop',e=>{e.preventDefault();otaDrop.classList.remove('drag');if(e.dataTransfer.files.length)pickOtaFile(e.dataTransfer.files[0])});
function handleOtaFile(e){if(e.target.files.length)pickOtaFile(e.target.files[0])}
function pickOtaFile(f){
  if(!f.name.endsWith('.bin')){showAlert('otaAlert','err','Kun .bin filer er tilladt');return}
  if(f.size>0x1A0000){showAlert('otaAlert','err','Fil for stor (max 1.625MB)');return}
  if(f.size<256){showAlert('otaAlert','err','Fil for lille');return}
  otaSelectedFile=f;
  $('otaFileName').textContent=f.name+' ('+Math.round(f.size/1024)+' KB)';
  $('otaFileName').style.display='block';
  $('otaUploadBtn').disabled=false;
}
function startOta(){
  if(!otaSelectedFile)return;
  $('otaUploadBtn').disabled=true;
  $('otaRollbackBtn').disabled=true;
  $('otaProgress').style.display='block';
  $('otaFill').style.width='0%';$('otaPct').textContent='0%';
  showAlert('otaAlert','info','Uploader firmware...');
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/api/system/ota',true);
  xhr.setRequestHeader('Authorization',AUTH);
  xhr.setRequestHeader('Content-Type','application/octet-stream');
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){const p=Math.round(e.loaded*100/e.total);$('otaFill').style.width=p+'%';$('otaPct').textContent=p+'%';}
  };
  xhr.onload=function(){
    if(xhr.status===200){
      try{const j=JSON.parse(xhr.responseText);
        showAlert('otaAlert','ok','OTA faerdig! Version: '+(j.new_version||'?')+'. Genstarter...');
        $('otaFill').style.width='100%';$('otaPct').textContent='100%';
        setTimeout(()=>window.location.reload(),(j.reboot_in_ms||2000)+8000);
      }catch(e){showAlert('otaAlert','ok','OTA faerdig! Genstarter...');setTimeout(()=>window.location.reload(),10000);}
    }else{
      try{const j=JSON.parse(xhr.responseText);showAlert('otaAlert','err','Fejl: '+(j.error||xhr.statusText));}
      catch(e){showAlert('otaAlert','err','Fejl: HTTP '+xhr.status);}
      $('otaUploadBtn').disabled=false;
    }
  };
  xhr.onerror=function(){showAlert('otaAlert','err','Netvaerksfejl');$('otaUploadBtn').disabled=false};
  xhr.send(otaSelectedFile);
}
function doOtaRollback(){
  askConfirm('Rollback Firmware','Rul firmware tilbage til forrige version? Enheden genstarter.',()=>{
    $('otaRollbackBtn').disabled=true;
    api('/api/system/ota/rollback',{method:'POST'}).then(r=>{
      if(r.ok){showAlert('otaAlert','ok','Rollback OK — genstarter...');setTimeout(()=>window.location.reload(),10000);}
      else r.json().then(j=>showAlert('otaAlert','err','Rollback fejl: '+(j.error||'ukendt')));
    }).catch(e=>showAlert('otaAlert','err','Fejl: '+e.message));
  });
}
// --- SSE Client Management ---
function fmtUptime(s){if(s<60)return s+'s';if(s<3600)return Math.floor(s/60)+'m '+s%60+'s';return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m'}
function refreshSseClients(){
  api('/api/events/clients').then(r=>r.json()).then(d=>{
    $('sseInfo').textContent='Aktive klienter: '+d.active_clients;
    const tbody=$('sseTbody');
    if(!d.clients||d.clients.length===0){tbody.innerHTML='<tr><td colspan="6" style="color:#6c7086;text-align:center">Ingen forbundne klienter</td></tr>';return}
    tbody.innerHTML=d.clients.map(c=>'<tr><td>'+c.slot+'</td><td>'+c.ip+'</td><td>'+c.username+'</td><td>'+c.topics+'</td><td>'+fmtUptime(c.uptime_s)+'</td><td><button class="btn btn-danger btn-sm" onclick="disconnectSse('+c.slot+')">Afbryd</button></td></tr>').join('');
  }).catch(()=>{$('sseInfo').textContent='Kunne ikke hente SSE status';$('sseTbody').innerHTML='<tr><td colspan="6" style="color:#f38ba8;text-align:center">Fejl</td></tr>'});
}
function disconnectSse(slot){
  api('/api/events/disconnect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:slot})}).then(r=>r.json()).then(d=>{
    if(d.ok)showAlert('sseAlert','ok','Klient slot '+slot+' afbrudt');
    else showAlert('sseAlert','err','Kunne ikke afbryde slot '+slot);
    setTimeout(refreshSseClients,500);
  }).catch(e=>showAlert('sseAlert','err','Fejl: '+e.message));
}
function disconnectAllSse(){
  api('/api/events/disconnect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:-1})}).then(r=>r.json()).then(d=>{
    showAlert('sseAlert','ok',d.disconnected+' klienter afbrudt');
    setTimeout(refreshSseClients,1000);
  }).catch(e=>showAlert('sseAlert','err','Fejl: '+e.message));
}

function fetchOtaStatus(){
  api('/api/system/ota/status').then(r=>{if(!r.ok)throw new Error();return r.json()}).then(d=>{
    $('otaCurVer').textContent=d.current_version||'-';
    $('otaRunPart').textContent=d.running_partition||'-';
    $('otaBootPart').textContent=d.boot_partition||'-';
    $('otaRollback').textContent=d.rollback_possible?'Ja':'Nej';
    $('otaRollbackBtn').disabled=!d.rollback_possible;
  }).catch(()=>{});
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
updateUserBadge();
</script>
</body>
</html>)rawhtml";

/* ============================================================================
 * HTTP HANDLER
 * ============================================================================ */

esp_err_t web_system_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, system_html, strlen(system_html));
}
