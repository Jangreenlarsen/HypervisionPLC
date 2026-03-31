/**
 * @file web_cli.cpp
 * @brief Web-based CLI console (v7.6.1)
 *
 * LAYER 7: User Interface - Web CLI
 * Serves a standalone CLI console page at /cli
 * Uses /api/cli endpoint for command execution.
 *
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_cli.h"
#include "debug.h"

static const char PROGMEM cli_html[] = R"rawhtml(<!DOCTYPE html>
<html lang="da">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Web CLI</title>
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
.hdr{background:#181825;padding:8px 16px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #313244;flex-shrink:0}
.hdr h1{font-size:16px;color:#89b4fa;flex-shrink:0}
.hdr .info{font-size:12px;color:#6c7086;margin-left:auto}
.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:100;align-items:center;justify-content:center}
.modal-bg.show{display:flex}
.modal{background:#1e1e2e;border:1px solid #313244;border-radius:8px;padding:24px;width:320px}
.modal h2{font-size:16px;color:#89b4fa;margin-bottom:16px}
.modal label{display:block;font-size:12px;color:#a6adc8;margin:8px 0 4px}
.modal input{width:100%;padding:6px 10px;background:#313244;border:1px solid #45475a;border-radius:4px;color:#cdd6f4;font-size:13px}
.modal .actions{display:flex;gap:8px;margin-top:16px;justify-content:flex-end}
.btn{padding:5px 14px;border:none;border-radius:4px;cursor:pointer;font-size:12px;font-weight:600;transition:all .15s}
.btn-primary{background:#89b4fa;color:#1e1e2e}.btn-primary:hover{background:#74c7ec}
.btn-sm{padding:3px 10px;font-size:11px}
.cli-wrap{flex:1;display:flex;flex-direction:column;overflow:hidden}
.cli-out{flex:1;overflow-y:auto;padding:12px 16px;background:#11111b;font:13px/1.6 'Cascadia Code','Fira Code','Courier New',monospace;white-space:pre-wrap}
.cli-out .ci{color:#89b4fa}
.cli-out .ce{color:#f38ba8}
.cli-row{display:flex;align-items:center;background:#181825;border-top:1px solid #313244;padding:6px 12px;gap:8px;flex-shrink:0}
.cli-row span{color:#a6e3a1;font:bold 14px monospace}
.cli-row input{flex:1;background:transparent;border:none;outline:none;color:#cdd6f4;font:14px/1.4 'Cascadia Code','Fira Code','Courier New',monospace;padding:4px}
.status-bar{display:flex;gap:12px;padding:4px 16px;background:#181825;border-top:1px solid #313244;font-size:11px;color:#6c7086;flex-shrink:0}
.status-bar .ok{color:#a6e3a1}
</style>
</head>
<body>

<!-- Login Modal -->
<div class="modal-bg show" id="loginModal">
<div class="modal">
<h2>Web CLI</h2>
<p style="font-size:12px;color:#6c7086;margin-bottom:12px">Brug credentials fra CLI: <code>show config http</code></p>
<div id="loginErr" style="display:none;color:#f38ba8;font-size:12px;padding:6px 0"></div>
<label>Brugernavn</label>
<input type="text" id="authUser" placeholder="Brugernavn" autocomplete="username">
<label>Adgangskode</label>
<input type="password" id="authPass" placeholder="Adgangskode" autocomplete="current-password">
<div class="actions">
<button class="btn btn-primary" onclick="doLogin()">Forbind</button>
</div>
</div>
</div>

<!-- Top Navigation -->
<div class="topnav">
<span class="brand">HyberFusion PLC</span>
<a href="/">Monitor</a>
<a href="/editor">ST Editor</a>
<a href="/cli" class="active">CLI</a>
<a href="/system">System</a>
<div class="user-badge"><div class="user-btn" id="userBtn" onclick="toggleUserMenu()"><span class="dot dot-off" id="userDot"></span><span id="userName">Ikke logget ind</span></div><div class="user-menu" id="userMenu"><div class="um-row"><span>Bruger:</span><span class="um-val" id="umUser">-</span></div><div class="um-row"><span>Roller:</span><span class="um-val" id="umRoles">-</span></div><div class="um-row"><span>Privilegier:</span><span class="um-val" id="umPriv">-</span></div><div class="um-row"><span>Auth mode:</span><span class="um-val" id="umMode">-</span></div><div class="um-sep"></div><div class="um-btn" id="umLogout" onclick="doLogout()">Log ud</div></div></div>
</div>

<!-- Header -->
<div class="hdr">
<h1>Web CLI Console</h1>
<span id="deviceInfo" class="info"></span>
</div>

<!-- CLI Console -->
<div class="cli-wrap">
<div class="cli-out" id="cliOut">Velkommen til Web CLI. Skriv 'help' for kommandoliste.
Tip: 'show config http' viser konfigureret brugernavn/adgangskode.
Tip: Brug piletasterne op/ned for kommandohistorik.
</div>
<div class="cli-row">
<span>$</span>
<input type="text" id="cliIn" placeholder="Skriv kommando..." autocomplete="off">
</div>
</div>

<!-- Status Bar -->
<div class="status-bar">
<span id="stStatus" class="ok">Forbundet</span>
<span id="stCmds">0 kommandoer</span>
</div>

<script>
let AUTH=sessionStorage.getItem('hfplc_auth')||'';
let cliHist=[];
let cliIdx=-1;
let cmdCount=0;

async function api(method,path,body){
  const opts={method,headers:{'Authorization':AUTH}};
  if(body){opts.headers['Content-Type']='application/json';opts.body=JSON.stringify(body);}
  const r=await fetch('/api/'+path,opts);
  if(r.status===401){throw new Error('Login required');}
  if(!r.ok){const t=await r.text();throw new Error(t||r.statusText);}
  return r.json();
}

async function doLogin(){
  const u=document.getElementById('authUser').value;
  const p=document.getElementById('authPass').value;
  AUTH='Basic '+btoa(u+':'+p);
  try{
    const r=await fetch('/api/status',{headers:{'Authorization':AUTH}});
    if(r.ok){
      sessionStorage.setItem('hfplc_auth',AUTH);
      document.getElementById('loginModal').classList.remove('show');
      init();
      updateUserBadge();
    }else{
      document.getElementById('loginErr').style.display='block';
      document.getElementById('loginErr').textContent='Forkert brugernavn eller adgangskode';
    }
  }catch(e){
    document.getElementById('loginErr').style.display='block';
    document.getElementById('loginErr').textContent='Forbindelsesfejl: '+e.message;
  }
}

document.getElementById('authPass').addEventListener('keydown',e=>{if(e.key==='Enter')doLogin();});
document.getElementById('authUser').addEventListener('keydown',e=>{if(e.key==='Enter')document.getElementById('authPass').focus();});

async function init(){
  try{
    const d=await api('GET','status');
    document.getElementById('deviceInfo').textContent=
      (d.hostname||'ESP32')+' | v'+(d.version||'?')+' | Build #'+(d.build||'?')+' | Heap: '+((d.heap_free||0)/1024).toFixed(0)+'KB';
  }catch(e){}
  document.getElementById('cliIn').focus();
}

// CLI Console
const cliIn=document.getElementById('cliIn');
cliIn.addEventListener('keydown',async(e)=>{
  if(e.key==='Enter'){
    const cmd=cliIn.value.trim();
    if(!cmd)return;
    cliHist.push(cmd);cliIdx=cliHist.length;
    cliAppend('$ '+cmd,'ci');
    cliIn.value='';
    try{
      const d=await api('POST','cli',{command:cmd});
      cliAppend(d.output||'(ingen output)','');
    }catch(err){cliAppend('Fejl: '+err.message,'ce');}
    cmdCount++;
    document.getElementById('stCmds').textContent=cmdCount+' kommandoer';
  }else if(e.key==='ArrowUp'){
    e.preventDefault();
    if(cliIdx>0){cliIdx--;cliIn.value=cliHist[cliIdx];}
  }else if(e.key==='ArrowDown'){
    e.preventDefault();
    if(cliIdx<cliHist.length-1){cliIdx++;cliIn.value=cliHist[cliIdx];}
    else{cliIdx=cliHist.length;cliIn.value='';}
  }
});

function cliAppend(text,cls){
  const out=document.getElementById('cliOut');
  const d=document.createElement('div');
  if(cls)d.className=cls;
  d.textContent=text;
  out.appendChild(d);
  out.scrollTop=out.scrollHeight;
}

// Auto-login if session exists
if(AUTH){
  document.getElementById('loginModal').classList.remove('show');
  init();
  updateUserBadge();
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

esp_err_t web_cli_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, cli_html, strlen(cli_html));
}
