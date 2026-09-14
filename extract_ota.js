
// BUG-393: login now uses a server-set, HttpOnly session cookie
// (Set-Cookie from POST /api/login) instead of a Bearer token this page
// has to remember itself — the browser sends it automatically on every
// same-origin request. No client-side token state left to lose, which is
// what made BUG-392/389/389b (all iOS-Safari-specific breakage) possible
// in the first place.
let selectedFile=null;
let pollTimer=null;

// BUG-369c: hardt loft paa samtidige forbindelser fra denne side — se
// dashboard.html's dashFetch()-kommentar for den fulde forklaring
// (max_open_sockets=3 paa HTTPS-serveren; flere samtidige forsoeg kan
// udloese en ESP-IDF/mbedTLS-fejl der i vaerste fald leder mod
// heap-udtoemning).
let _apiFetchActive=0;
const _apiFetchQueue=[];
const API_FETCH_MAX_CONCURRENT=2;
function _apiFetchPump(){
  while(_apiFetchActive<API_FETCH_MAX_CONCURRENT && _apiFetchQueue.length>0){
    const job=_apiFetchQueue.shift();
    _apiFetchActive++;
    fetch(job.url,job.opts).then(r=>{_apiFetchActive--;_apiFetchPump();job.resolve(r);})
      .catch(e=>{_apiFetchActive--;_apiFetchPump();job.reject(e);});
  }
}
function queuedFetch(url,opts){
  return new Promise((resolve,reject)=>{
    _apiFetchQueue.push({url,opts,resolve,reject});
    _apiFetchPump();
  });
}

function api(url,opts={}){
  return queuedFetch(url,opts).then(r=>{
    if(r.status===401){document.getElementById('loginModal').classList.add('show');throw new Error('Login');}
    return r;
  });
}
async function doLogin(){
  const u=document.getElementById('authUser').value;
  const p=document.getElementById('authPass').value;
  const basicAuth='Basic '+btoa(u+':'+p);
  try{
    // BUG-353: POST /api/login udsteder et session-token vi bruger fremover
    // — foer blev det raa base64(user:pass) bare gemt uden verifikation her.
    const r=await fetch('/api/login',{method:'POST',headers:{'Authorization':basicAuth}});
    // FEAT-399-følgefejl: skeln 403 ("Blocked by IP ACL", se §10.7.1) fra 401
    // (reelt forkerte credentials).
    if(r.status===403){
      document.getElementById('loginErr').style.display='block';
      document.getElementById('loginErr').textContent='Denne IP-adresse er blokeret af IP ACL\'en. Enheden retter en ny, ubekræftet regel automatisk inden for 5 minutter — prøv igen om lidt.';
      return;
    }
    if(!r.ok){
      document.getElementById('loginErr').style.display='block';
      document.getElementById('loginErr').textContent='Forkert brugernavn eller adgangskode';
      return;
    }
    const d=await r.json();
    if(!d.authenticated){
      document.getElementById('loginErr').style.display='block';
      document.getElementById('loginErr').textContent='Login fejlede';
      return;
    }
    document.getElementById('loginModal').classList.remove('show');
    fetchStatus();
  }catch(e){
    document.getElementById('loginErr').style.display='block';
    document.getElementById('loginErr').textContent='Forbindelsesfejl';
  }
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
  if(f.size>0x1D0000){setStatus('err','Fil for stor (max 1.8125MB)');return;}
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
