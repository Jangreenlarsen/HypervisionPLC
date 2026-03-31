/**
 * @file web_ota.cpp
 * @brief Web-based OTA firmware update page (FEAT-031)
 *
 * LAYER 7: User Interface - OTA Update
 * Serves an OTA management page at "/ota" with:
 * - Firmware .bin file upload with progress bar
 * - Real-time status polling during upload
 * - Rollback to previous firmware
 * - Current firmware version display
 *
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_ota.h"
#include "debug.h"

static const char PROGMEM ota_html[] = R"rawhtml(<!DOCTYPE html>
<html lang="da">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HyperFusion PLC : OTA Firmware Update</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,sans-serif;background:#1e1e2e;color:#cdd6f4;height:100vh;display:flex;flex-direction:column;overflow:hidden}
nav{background:#181825;padding:8px 16px;display:flex;gap:12px;align-items:center;border-bottom:1px solid #313244}
nav a{color:#89b4fa;text-decoration:none;padding:6px 12px;border-radius:6px;font-size:13px;transition:background .2s}
nav a:hover{background:#313244}
nav a.active{background:#89b4fa;color:#1e1e2e;font-weight:600}
nav .brand{font-weight:700;color:#cba6f7;margin-right:auto;font-size:15px}
.container{flex:1;padding:24px;overflow-y:auto;max-width:700px;margin:0 auto;width:100%}
.card{background:#181825;border:1px solid #313244;border-radius:12px;padding:20px;margin-bottom:16px}
.card h2{font-size:16px;color:#cba6f7;margin-bottom:12px}
.card h3{font-size:14px;color:#a6adc8;margin-bottom:8px}
.info-grid{display:grid;grid-template-columns:140px 1fr;gap:6px 12px;font-size:13px}
.info-grid .label{color:#a6adc8}
.info-grid .value{color:#cdd6f4;font-family:monospace}
.upload-zone{border:2px dashed #45475a;border-radius:8px;padding:32px;text-align:center;margin:12px 0;transition:border-color .3s,background .3s;cursor:pointer}
.upload-zone:hover,.upload-zone.dragover{border-color:#89b4fa;background:#1e1e3e}
.upload-zone input[type=file]{display:none}
.upload-zone .icon{font-size:32px;margin-bottom:8px}
.upload-zone p{font-size:13px;color:#a6adc8}
.upload-zone .filename{color:#a6e3a1;font-family:monospace;margin-top:8px;font-size:14px}
.progress-wrap{margin:16px 0;display:none}
.progress-bar{height:24px;background:#313244;border-radius:12px;overflow:hidden;position:relative}
.progress-fill{height:100%;background:linear-gradient(90deg,#89b4fa,#74c7ec);border-radius:12px;transition:width .3s;width:0%}
.progress-text{position:absolute;top:0;left:0;right:0;height:24px;line-height:24px;text-align:center;font-size:12px;font-weight:600;color:#1e1e2e}
.status-msg{margin-top:8px;font-size:13px;padding:8px 12px;border-radius:6px;display:none}
.status-msg.info{display:block;background:#1e1e3e;border:1px solid #89b4fa;color:#89b4fa}
.status-msg.ok{display:block;background:#1e2e1e;border:1px solid #a6e3a1;color:#a6e3a1}
.status-msg.err{display:block;background:#2e1e1e;border:1px solid #f38ba8;color:#f38ba8}
.btn{padding:10px 20px;border:none;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer;transition:opacity .2s}
.btn:disabled{opacity:.4;cursor:not-allowed}
.btn-primary{background:#89b4fa;color:#1e1e2e}
.btn-danger{background:#f38ba8;color:#1e1e2e}
.btn-row{display:flex;gap:12px;margin-top:16px}
.modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:100;align-items:center;justify-content:center}
.modal-overlay.show{display:flex}
.modal{background:#181825;border:1px solid #313244;border-radius:12px;padding:24px;width:340px}
.modal h3{color:#cba6f7;margin-bottom:12px}
.modal input{width:100%;padding:8px;margin:4px 0 8px;background:#1e1e2e;border:1px solid #45475a;border-radius:6px;color:#cdd6f4;font-size:14px}
.modal .btn{width:100%;margin-top:8px}
footer{background:#181825;border-top:1px solid #313244;padding:6px 16px;font-size:11px;color:#585b70;text-align:center}
</style>
</head>
<body>
<nav>
<span class="brand">HyperFusion PLC</span>
<a href="/">Monitor</a>
<a href="/editor">ST Editor</a>
<a href="/cli">CLI</a>
<a href="/system" class="active">System</a>
</nav>

<div class="container">
<!-- Firmware Info -->
<div class="card" id="infoCard">
<h2>Firmware Information</h2>
<div class="info-grid">
<span class="label">Nuvaerende version:</span><span class="value" id="curVer">-</span>
<span class="label">Aktiv partition:</span><span class="value" id="runPart">-</span>
<span class="label">Boot partition:</span><span class="value" id="bootPart">-</span>
<span class="label">Rollback mulig:</span><span class="value" id="rollbackOk">-</span>
</div>
</div>

<!-- Upload -->
<div class="card">
<h2>Upload Firmware</h2>
<div class="upload-zone" id="dropZone" onclick="document.getElementById('fileInput').click()">
<div class="icon">&#128229;</div>
<p>Klik eller traek en .bin fil hertil</p>
<input type="file" id="fileInput" accept=".bin">
<div class="filename" id="fileName"></div>
</div>
<div class="progress-wrap" id="progressWrap">
<div class="progress-bar"><div class="progress-fill" id="progressFill"></div><div class="progress-text" id="progressText">0%</div></div>
</div>
<div class="status-msg" id="statusMsg"></div>
<div class="btn-row">
<button class="btn btn-primary" id="uploadBtn" disabled onclick="startUpload()">Upload & Flash</button>
<button class="btn btn-danger" id="rollbackBtn" disabled onclick="doRollback()">Rollback</button>
</div>
</div>
</div>

<!-- Login Modal -->
<div class="modal-overlay" id="loginModal">
<div class="modal">
<h3>Login</h3>
<input id="authUser" placeholder="Brugernavn" autocomplete="username">
<input id="authPass" type="password" placeholder="Adgangskode" autocomplete="current-password">
<button class="btn btn-primary" onclick="doLogin()">Log ind</button>
</div>
</div>

<footer id="footStatus">OTA Firmware Update - FEAT-031</footer>

<script>
let AUTH=sessionStorage.getItem('hfplc_auth')||'';
let selectedFile=null;
let pollTimer=null;

function api(url,opts={}){
  if(AUTH){if(!opts.headers)opts.headers={};opts.headers['Authorization']='Basic '+AUTH;}
  return fetch(url,opts).then(r=>{
    if(r.status===401){AUTH='';sessionStorage.removeItem('hfplc_auth');document.getElementById('loginModal').classList.add('show');throw new Error('Login');}
    return r;
  });
}
function doLogin(){
  const u=document.getElementById('authUser').value;
  const p=document.getElementById('authPass').value;
  AUTH=btoa(u+':'+p);
  sessionStorage.setItem('hfplc_auth',AUTH);
  document.getElementById('loginModal').classList.remove('show');
  fetchStatus();
}
function setStatus(cls,msg){
  const el=document.getElementById('statusMsg');
  el.className='status-msg '+cls;
  el.textContent=msg;
}
function setProgress(pct){
  document.getElementById('progressWrap').style.display='block';
  document.getElementById('progressFill').style.width=pct+'%';
  document.getElementById('progressText').textContent=pct+'%';
}

// Drag & drop
const dz=document.getElementById('dropZone');
dz.addEventListener('dragover',e=>{e.preventDefault();dz.classList.add('dragover')});
dz.addEventListener('dragleave',()=>dz.classList.remove('dragover'));
dz.addEventListener('drop',e=>{e.preventDefault();dz.classList.remove('dragover');handleFile(e.dataTransfer.files[0])});
document.getElementById('fileInput').addEventListener('change',e=>handleFile(e.target.files[0]));

function handleFile(f){
  if(!f)return;
  if(!f.name.endsWith('.bin')){setStatus('err','Kun .bin filer er tilladt');return;}
  if(f.size>0x1A0000){setStatus('err','Fil for stor (max 1.625MB)');return;}
  if(f.size<256){setStatus('err','Fil for lille — ugyldig firmware');return;}
  selectedFile=f;
  document.getElementById('fileName').textContent=f.name+' ('+Math.round(f.size/1024)+'KB)';
  document.getElementById('uploadBtn').disabled=false;
  setStatus('info','Klar til upload: '+f.name);
}

function startUpload(){
  if(!selectedFile)return;
  document.getElementById('uploadBtn').disabled=true;
  document.getElementById('rollbackBtn').disabled=true;
  setProgress(0);
  setStatus('info','Uploader firmware...');

  const xhr=new XMLHttpRequest();
  xhr.open('POST','/api/system/ota',true);
  if(AUTH)xhr.setRequestHeader('Authorization','Basic '+AUTH);
  xhr.setRequestHeader('Content-Type','application/octet-stream');

  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      const pct=Math.round(e.loaded*100/e.total);
      setProgress(pct);
      setStatus('info','Sender: '+Math.round(e.loaded/1024)+'KB / '+Math.round(e.total/1024)+'KB');
    }
  };

  xhr.onload=function(){
    if(xhr.status===200){
      try{const j=JSON.parse(xhr.responseText);
        setStatus('ok','OTA faerdig! Version: '+(j.new_version||'?')+'. Genstarter om '+(j.reboot_in_ms/1000)+'s...');
        setProgress(100);
        setTimeout(()=>{setStatus('info','Genindlaeser siden...');},j.reboot_in_ms||2000);
        setTimeout(()=>{window.location.reload();},((j.reboot_in_ms||2000)+8000));
      }catch(e){setStatus('ok','OTA faerdig! Genstarter...');setTimeout(()=>window.location.reload(),10000);}
    }else if(xhr.status===401){
      AUTH='';sessionStorage.removeItem('hfplc_auth');
      document.getElementById('loginModal').classList.add('show');
      setStatus('err','Login kraeves');
      document.getElementById('uploadBtn').disabled=false;
    }else{
      try{const j=JSON.parse(xhr.responseText);setStatus('err','Fejl: '+(j.error||xhr.statusText));}
      catch(e){setStatus('err','Fejl: HTTP '+xhr.status);}
      document.getElementById('uploadBtn').disabled=false;
    }
  };

  xhr.onerror=function(){
    setStatus('err','Netvaerksfejl under upload');
    document.getElementById('uploadBtn').disabled=false;
  };

  xhr.send(selectedFile);

  // Start polling server-side progress
  if(pollTimer)clearInterval(pollTimer);
  pollTimer=setInterval(()=>{
    api('/api/system/ota/status').then(r=>r.json()).then(d=>{
      if(d.state==='done'||d.state==='error'||d.state==='idle'){
        clearInterval(pollTimer);pollTimer=null;
      }
    }).catch(()=>{});
  },1000);
}

function doRollback(){
  if(!confirm('Rollback til forrige firmware? Enheden genstarter.'))return;
  document.getElementById('rollbackBtn').disabled=true;
  setStatus('info','Ruller tilbage...');
  api('/api/system/ota/rollback',{method:'POST'}).then(r=>{
    if(r.ok){
      setStatus('ok','Rollback OK — genstarter...');
      setTimeout(()=>window.location.reload(),10000);
    }else{
      return r.json().then(j=>{setStatus('err','Rollback fejl: '+(j.error||'ukendt'));});
    }
  }).catch(e=>setStatus('err','Fejl: '+e.message));
}

function fetchStatus(){
  api('/api/system/ota/status').then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>{
    document.getElementById('curVer').textContent=d.current_version||'-';
    document.getElementById('runPart').textContent=d.running_partition||'-';
    document.getElementById('bootPart').textContent=d.boot_partition||'-';
    document.getElementById('rollbackOk').textContent=d.rollback_possible?'Ja':'Nej';
    document.getElementById('rollbackBtn').disabled=!d.rollback_possible;
    if(d.state==='receiving'){
      setProgress(d.percent);
      setStatus('info','Upload i gang: '+d.percent+'%');
    }else if(d.state==='error'){
      setStatus('err','Sidste fejl: '+d.error);
    }
  }).catch(()=>{});
}

// Initial load
fetchStatus();
</script>
</body>
</html>)rawhtml";

/* ============================================================================
 * HTTP HANDLER
 * ============================================================================ */

esp_err_t web_ota_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, ota_html, strlen(ota_html));
}
