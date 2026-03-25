/**
 * @file web_editor.cpp
 * @brief Web-based ST Logic program editor (v7.2.3+)
 *
 * LAYER 7: User Interface - Web Editor
 * Serves a single-page HTML/CSS/JS editor for ST Logic programs.
 * All program operations use existing /api/logic/* REST endpoints.
 *
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 * Flash impact: ~12KB for embedded HTML/CSS/JS
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_editor.h"
#include "debug.h"

/* ============================================================================
 * EMBEDDED HTML - ST Logic Web Editor
 *
 * Single-page app with:
 * - Program slot selector (1-4)
 * - Code editor with line numbers
 * - Compile/upload with error feedback
 * - Enable/disable toggle
 * - Pool usage meter
 * - ST language keyword reference
 * ============================================================================ */

static const char PROGMEM editor_html[] = R"rawhtml(<!DOCTYPE html>
<html lang="da">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ST Logic Editor</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,sans-serif;background:#1e1e2e;color:#cdd6f4;min-height:100vh;display:flex;flex-direction:column}
.hdr{background:#181825;padding:8px 16px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #313244}
.hdr h1{font-size:16px;color:#89b4fa;flex-shrink:0}
.hdr .info{font-size:12px;color:#6c7086;margin-left:auto}
.tabs{display:flex;gap:2px;background:#181825;padding:4px 16px;border-bottom:1px solid #313244}
.tab{padding:6px 16px;border:none;border-radius:6px 6px 0 0;cursor:pointer;font-size:13px;background:#313244;color:#a6adc8;transition:all .15s}
.tab:hover{background:#45475a;color:#cdd6f4}
.tab.active{background:#1e1e2e;color:#89b4fa;font-weight:600}
.tab.compiled{border-top:2px solid #a6e3a1}
.tab.error{border-top:2px solid #f38ba8}
.tab.empty{opacity:.5}
.toolbar{display:flex;gap:8px;padding:8px 16px;background:#181825;border-bottom:1px solid #313244;flex-wrap:wrap;align-items:center}
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
.main{display:flex;flex:1;overflow:hidden}
.editor-wrap{flex:1;display:flex;flex-direction:column;overflow:hidden}
.editor-container{flex:1;display:flex;overflow:hidden;position:relative}
.line-nums{min-width:44px;background:#181825;color:#585b70;font:12px/18px 'Cascadia Code','Fira Code','Courier New',monospace;padding:8px 6px 8px 4px;text-align:right;overflow:hidden;user-select:none;border-right:1px solid #313244;white-space:pre}
#editor{flex:1;background:#1e1e2e;color:#cdd6f4;font:12px/18px 'Cascadia Code','Fira Code','Courier New',monospace;padding:8px;border:none;outline:none;resize:none;tab-size:2;white-space:pre;overflow:auto}
#editor::placeholder{color:#585b70}
.status-bar{display:flex;gap:12px;padding:4px 16px;background:#181825;border-top:1px solid #313244;font-size:11px;color:#6c7086;flex-wrap:wrap}
.status-bar .ok{color:#a6e3a1}.status-bar .err{color:#f38ba8}.status-bar .warn{color:#fab387}
.output{max-height:160px;overflow-y:auto;padding:8px 16px;background:#11111b;font:12px/1.6 'Cascadia Code',monospace;border-top:1px solid #313244;white-space:pre-wrap}
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
@media(max-width:768px){.sidebar{display:none}.pool-bar{max-width:120px}}
</style>
</head>
<body>

<!-- Login Modal -->
<div class="modal-bg show" id="loginModal">
<div class="modal">
<h2>ST Logic Editor</h2>
<p style="font-size:12px;color:#6c7086;margin-bottom:12px">Log ind for at forbinde til ESP32 API</p>
<label>Brugernavn</label>
<input type="text" id="authUser" value="api_user" autocomplete="username">
<label>Adgangskode</label>
<input type="password" id="authPass" autocomplete="current-password">
<div class="actions">
<button class="btn btn-primary" onclick="doLogin()">Forbind</button>
</div>
</div>
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
<div class="toggle">
<input type="checkbox" id="chkEnabled" onchange="toggleEnabled()">
<label for="chkEnabled">Aktiveret</label>
</div>
<button class="btn btn-danger btn-sm" onclick="deleteProgram()">Slet</button>
<div style="flex:1"></div>
<span style="font-size:11px;color:#6c7086">Pool:</span>
<div class="pool-bar">
<div class="pool-fill" id="poolFill"></div>
<div class="pool-text" id="poolText">-</div>
</div>
<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="toggleSidebar()">Ref</button>
</div>

<!-- Main Area -->
<div class="main">
<div class="editor-wrap">
<div class="editor-container">
<div class="line-nums" id="lineNums">1</div>
<textarea id="editor" placeholder="(* Skriv dit ST program her *)&#10;&#10;PROGRAM Blink&#10;  VAR&#10;    toggle : BOOL := FALSE;&#10;  END_VAR&#10;&#10;  toggle := NOT toggle;&#10;  coil[1] := toggle;&#10;END_PROGRAM" spellcheck="false"></textarea>
</div>
<div class="output hidden" id="output"></div>
</div>
<div class="sidebar" id="sidebar">
<h3>Nøgleord</h3>
<code class="kw">PROGRAM END_PROGRAM
FUNCTION FUNCTION_BLOCK
END_FUNCTION END_FUNCTION_BLOCK
VAR VAR_INPUT VAR_OUTPUT
END_VAR VAR_GLOBAL
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
:= = <> < > <= >=</code>
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
<h3>Modbus I/O</h3>
<code class="fn">hr[addr] — Holding Register
ir[addr] — Input Register
coil[addr] — Coil Output
di[addr] — Discrete Input</code>
</div>
</div>

<!-- Status Bar -->
<div class="status-bar">
<span id="stSlot">Slot 1</span>
<span id="stStatus">-</span>
<span id="stSize">-</span>
<span id="stLine">Linje 1, Kol 1</span>
</div>

<script>
let AUTH='';
let SLOT=1;
let programs=[null,null,null,null];
let dirty=false;

function doLogin(){
  const u=document.getElementById('authUser').value;
  const p=document.getElementById('authPass').value;
  AUTH='Basic '+btoa(u+':'+p);
  document.getElementById('loginModal').classList.remove('show');
  init();
}

async function api(method,path,body){
  const opts={method,headers:{'Authorization':AUTH}};
  if(body){opts.headers['Content-Type']='application/json';opts.body=JSON.stringify(body);}
  const r=await fetch('/api/'+path,opts);
  if(r.status===401){document.getElementById('loginModal').classList.add('show');throw new Error('Auth');}
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
  updateTabs();
  await loadSource(s);
}

async function loadSource(s){
  const ed=document.getElementById('editor');
  const p=programs[s-1];
  document.getElementById('chkEnabled').checked=p&&p.enabled;
  document.getElementById('stSlot').textContent='Slot '+s;
  updatePool();
  if(!p||!p.source_size){
    ed.value='';
    updateStatus('-','');
    updateLines();
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
  updateLines();
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

async function toggleEnabled(){
  const en=document.getElementById('chkEnabled').checked;
  try{
    await api('POST','logic/'+SLOT+'/'+(en?'enable':'disable'));
    log('info',(en?'Aktiveret':'Deaktiveret')+' program '+SLOT);
    await loadAll();
  }catch(e){log('error','Fejl: '+e.message);}
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

// Editor events
const ed=document.getElementById('editor');
ed.addEventListener('input',()=>{dirty=true;updateLines();});
ed.addEventListener('scroll',()=>{
  document.getElementById('lineNums').scrollTop=ed.scrollTop;
});
ed.addEventListener('keyup',()=>{
  const v=ed.value.substring(0,ed.selectionStart);
  const lines=v.split('\n');
  document.getElementById('stLine').textContent='Linje '+lines.length+', Kol '+(lines[lines.length-1].length+1);
});
// Tab key inserts 2 spaces
ed.addEventListener('keydown',(e)=>{
  if(e.key==='Tab'){
    e.preventDefault();
    const s=ed.selectionStart,end=ed.selectionEnd;
    ed.value=ed.value.substring(0,s)+'  '+ed.value.substring(end);
    ed.selectionStart=ed.selectionEnd=s+2;
    dirty=true;updateLines();
  }
});
// Ctrl+S to compile
document.addEventListener('keydown',(e)=>{
  if((e.ctrlKey||e.metaKey)&&e.key==='s'){e.preventDefault();uploadCode();}
});
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
