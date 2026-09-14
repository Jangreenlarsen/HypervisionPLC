
// BUG-393: login now uses a server-set, HttpOnly session cookie
// (Set-Cookie from POST /api/login) instead of a Bearer token this page
// has to remember itself — the browser sends it automatically on every
// same-origin request. No client-side token state left to lose, which is
// what made BUG-392/389/389b (all iOS-Safari-specific breakage) possible
// in the first place.
let cliHist=[];
let cliIdx=-1;
let cmdCount=0;

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

async function api(method,path,body){
  const opts={method,headers:{}};
  if(body){opts.headers['Content-Type']='application/json';opts.body=JSON.stringify(body);}
  const r=await queuedFetch('/api/'+path,opts);
  if(r.status===401){document.getElementById('loginModal').classList.add('show');throw new Error('Login required');}
  if(!r.ok){const t=await r.text();throw new Error(t||r.statusText);}
  return r.json();
}

async function doLogin(){
  const u=document.getElementById('authUser').value;
  const p=document.getElementById('authPass').value;
  const basicAuth='Basic '+btoa(u+':'+p);
  try{
    // BUG-353: POST /api/login udsteder et session-token vi bruger fremover
    const r=await fetch('/api/login',{method:'POST',headers:{'Authorization':basicAuth}});
    if(r.ok){
      const d=await r.json();
      if(!d.token){
        document.getElementById('loginErr').style.display='block';
        document.getElementById('loginErr').textContent='Login fejlede (intet token)';
        return;
      }
      document.getElementById('loginModal').classList.remove('show');
      // BUG-393b: updateUserBadge() first — see the same reasoning at this
      // page's page-load bootstrap call further down.
      updateUserBadge();
      init();
    }else if(r.status===403){
      // FEAT-399-følgefejl: "Blocked by IP ACL" (se §10.7.1), ikke forkerte credentials.
      document.getElementById('loginErr').style.display='block';
      document.getElementById('loginErr').textContent='Denne IP-adresse er blokeret af IP ACL\'en. Enheden retter en ny, ubekræftet regel automatisk inden for 5 minutter — prøv igen om lidt.';
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

// BUG-393: used to proactively probe /api/status first (with its own
// network-vs-401 distinction to get wrong, see BUG-389/389b). Simpler and
// structurally immune to that bug class: just start loading — if not
// logged in (or the cookie's session expired), the first real request
// 401s and api()'s own handling above shows the modal.
init();

// === User Badge ===
function toggleUserMenu(){var m=document.getElementById('userMenu');m.classList.toggle('show')}
document.addEventListener('click',function(e){var b=document.getElementById('userBtn');var m=document.getElementById('userMenu');if(b&&m&&!b.contains(e.target)&&!m.contains(e.target))m.classList.remove('show')});
function updateUserBadge(){
// BUG-393b: route through the same queuedFetch() throttle every other
// request on this page uses — a plain unqueued fetch() here could exceed
// the device's max_open_sockets alongside init()'s own burst of queued
// calls and lose the race (BUG-369's original heap-crash class of bug, on
// this page it just silently left the badge unset instead).
queuedFetch('/api/user/me',{}).then(function(r){return r.json()}).then(function(d){
if(d.authenticated){document.getElementById('userName').textContent=d.username;document.getElementById('userDot').className='dot dot-on';document.getElementById('umUser').textContent=d.username;document.getElementById('umRoles').textContent=d.roles||'all';document.getElementById('umPriv').textContent=d.privilege||'rw';document.getElementById('umMode').textContent=d.mode||'legacy';document.getElementById('umLogout').style.display='block'}
else{document.getElementById('userName').textContent='Ikke logget ind';document.getElementById('userDot').className='dot dot-off';document.getElementById('umLogout').style.display='none'}
}).catch(function(){})
}
function doLogout(){
// BUG-393: cookie is HttpOnly, always POST unconditionally \u2014 server reads
// it itself and clears it in the response either way. Reload afterward to
// reliably stop this page's polling and blank any already-rendered data.
fetch('/api/logout',{method:'POST'}).catch(()=>{}).then(()=>{location.reload();});}
async function doGlobalSave(){var btn=document.getElementById('saveBtn');btn.classList.add('saving');btn.textContent='\u23F3 Gemmer...';try{var r=await fetch('/api/system/save',{method:'POST'});if(r.ok){btn.classList.remove('saving');btn.classList.add('saved');btn.textContent='\u2705 Gemt!';}else{btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}}catch(e){btn.classList.remove('saving');btn.classList.add('save-err');btn.textContent='\u274C Fejl';}setTimeout(()=>{btn.className='save-btn';btn.innerHTML='&#128190; Save';},2000);}
updateUserBadge();
