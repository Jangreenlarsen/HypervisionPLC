
// BUG-393: login now uses a server-set, HttpOnly session cookie
// (Set-Cookie from POST /api/login) instead of a Bearer token this page
// has to remember itself — the browser sends it automatically on every
// same-origin request. No client-side token state left to lose, which is
// what made BUG-392/389/389b (all iOS-Safari-specific breakage) possible
// in the first place.
let pendingConfirmFn=null;
function $(id){return document.getElementById(id)}

// --- Tabs ---
function switchTab(name){
  const isAudit=name==='audit';
  $('panelAudit').hidden=!isAudit;
  $('panelSys').hidden=isAudit;
  $('tabBtnAudit').classList.toggle('active',isAudit);
  $('tabBtnSys').classList.toggle('active',!isAudit);
  if(isAudit)fetchLogs(); else fetchSyslog();
}

// --- Auth ---
function doLogin(){
  const u=$('authUser').value, p=$('authPass').value;
  if(!u||!p){$('loginErr').style.display='block';$('loginErr').textContent='Udfyld begge felter';return}
  const basicAuth='Basic '+btoa(u+':'+p);
  // FEAT-399-følgefejl: skeln 403 ("Blocked by IP ACL", se §10.7.1) fra 401
  // (reelt forkerte credentials) — ellers viser UI'et "forkert kodeord" for
  // en helt anden, forbigående årsag som auto-ruller tilbage af sig selv.
  fetch('/api/login',{method:'POST',headers:{'Authorization':basicAuth}}).then(r=>{
    if(r.status===403)throw new Error('acl');
    if(!r.ok)throw new Error('auth');
    return r.json();
  }).then(d=>{
    if(!d.authenticated)throw new Error('auth');
    $('loginModal').classList.remove('show');
    // BUG-393b: updateUserBadge() first — see the same reasoning at this
    // page's page-load bootstrap call further down.
    updateUserBadge();
    fetchLogs();
  }).catch(e=>{
    $('loginErr').style.display='block';
    $('loginErr').textContent=(e.message==='acl')
      ? 'Denne IP-adresse er blokeret af IP ACL\'en. Enheden retter en ny, ubekræftet regel automatisk inden for 5 minutter — prøv igen om lidt.'
      : 'Forkert brugernavn eller adgangskode';
  });
}
$('authPass').addEventListener('keydown',e=>{if(e.key==='Enter')doLogin()});

// BUG-369c-mønster: hårdt loft på samtidige forbindelser.
// BUG-394b: denne blok SKAL staa foer det foerste updateUserBadge()/fetchLogs()
// bootstrap-kald nedenfor — de bruger queuedFetch(), som laeser
// _apiFetchQueue/_apiFetchActive. Disse er `let`/`const` (ikke hoisted som
// funktionserklaeringer), saa et kald FOeR denne blok udfoeres rammer
// midlertidig-dod-zone (TDZ): "Cannot access '_apiFetchQueue' before
// initialization" — kastet inde i queuedFetch()'s Promise-executor, hvilket
// goer det til et rejected promise (ikke en synkron exception), som
// updateUserBadge()s stille .catch(()=>{}) sluger usynligt. Ramte /logs (og
// /io, /system — samme fejl der) fordi BUG-393b flyttede updateUserBadge()
// til FOeRST i bootstrap-raekkefoelgen uden at tjekke at koeens deklarationer
// stod foer det nye kaldested.
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
function queuedFetch(url,opts){return new Promise((resolve,reject)=>{_apiFetchQueue.push({url,opts,resolve,reject});_apiFetchPump();});}
function api(url,opts={}){
  return queuedFetch(url,opts).then(r=>{
    if(r.status===401){$('loginModal').classList.add('show');throw new Error('Login');}
    return r;
  });
}

// BUG-393: used to proactively probe /api/status first (with its own
// network-vs-401 distinction to get wrong, see BUG-389/389b). Simpler and
// structurally immune to that bug class: just start loading — if not
// logged in (or the cookie's session expired), the first real request
// 401s and api()'s own handling below shows the modal.
// BUG-393b: updateUserBadge() called FIRST, not after fetchLogs() — if
// fetchLogs() ever throws synchronously, the next statement would never
// run, silently leaving the badge on its default "not logged in" text.
updateUserBadge();
fetchLogs();

// --- Confirm Dialog ---
function askConfirm(title,msg,fn){$('confirmTitle').textContent=title;$('confirmMsg').textContent=msg;pendingConfirmFn=fn;$('confirmDlg').classList.add('show');}
function closeConfirm(){$('confirmDlg').classList.remove('show');pendingConfirmFn=null}
function confirmAction(){var fn=pendingConfirmFn;closeConfirm();if(fn)fn()}

// === User Badge ===
function toggleUserMenu(){$('userMenu').classList.toggle('show')}
document.addEventListener('click',function(e){const b=$('userBtn'),m=$('userMenu');if(b&&m&&!b.contains(e.target)&&!m.contains(e.target))m.classList.remove('show')});
function updateUserBadge(){
  // BUG-393b: route through the same queuedFetch() throttle every other
  // request on this page uses — a plain unqueued fetch() here could exceed
  // the device's max_open_sockets alongside the page's own burst of queued
  // calls and lose the race (BUG-369's original heap-crash class of bug, on
  // this page it just silently left the badge unset instead).
  queuedFetch('/api/user/me',{}).then(r=>r.json()).then(d=>{
    if(d.authenticated){$('userName').textContent=d.username;$('userDot').className='dot dot-on';$('umUser').textContent=d.username;$('umRoles').textContent=d.roles||'all';$('umPriv').textContent=d.privilege||'rw';$('umMode').textContent=d.mode||'legacy';$('umLogout').style.display='block'}
    else{$('userName').textContent='Ikke logget ind';$('userDot').className='dot dot-off';$('umLogout').style.display='none'}
  }).catch(()=>{});
}
function doLogout(){
// BUG-393: cookie is HttpOnly, always POST unconditionally — server reads
// it itself and clears it in the response either way. Reload afterward to
// reliably stop this page's polling and blank any already-rendered data.
fetch('/api/logout',{method:'POST'}).catch(()=>{}).then(()=>{location.reload();});}

// ============================================================
// AUDIT LOG (FEAT-033)
// ============================================================
function fmtTime(e){
  if(e.epoch_s&&e.epoch_s>0)return new Date(e.epoch_s*1000).toLocaleTimeString('da-DK');
  return 'oppetid '+Math.round(e.timestamp_ms/1000)+'s';
}
function methodClass(m){if(m==='GET')return 'method-get';if(m==='DELETE')return 'method-delete';return 'method-post';}
function fetchLogs(){
  const lim=$('limitSel').value;
  api('/api/system/logs?limit='+lim).then(r=>r.json()).then(d=>{
    $('logBadge').textContent=(d.logging?'Aktiv':'Pause')+' — '+d.total+' / '+d.capacity+' poster';
    const tbody=$('logBody');
    if(!d.entries||d.entries.length===0){
      tbody.innerHTML='<tr><td colspan="6" class="empty-msg">Ingen requests logget endnu</td></tr>';
      return;
    }
    const rows=d.entries.slice().reverse();
    tbody.innerHTML=rows.map(e=>{
      const statusCls=(e.status>=200&&e.status<400)?'status-ok':'status-err';
      return '<tr><td>'+fmtTime(e)+'</td>'+
        '<td><span class="method '+methodClass(e.method)+'">'+e.method+'</span></td>'+
        '<td>'+e.path+'</td>'+
        '<td class="'+statusCls+'">'+e.status+'</td>'+
        '<td>'+e.ip+'</td>'+
        '<td>'+e.username+'</td></tr>';
    }).join('');
  }).catch(()=>{$('logBody').innerHTML='<tr><td colspan="6" class="empty-msg">Kunne ikke hente log</td></tr>';});
}
function clearLogs(){
  askConfirm('Ryd Audit Log','Ryd hele API audit-loggen?',()=>{
    api('/api/system/logs/clear',{method:'POST'}).then(r=>{if(r.ok)fetchLogs();}).catch(()=>{});
  });
}

// ============================================================
// SYSTEM HÆNDELSESLOG (FEAT-086/089) — portet fra dashboard.html's
// tidligere lille indlejrede "syslog"-kort til denne fulde side.
// ============================================================
let _syslogEntries=[];
let _syslogLogging=true;
let _syslogTotal=0;
const _syslogSrcNames={rest:'REST API',modbus_slave:'Modbus (ekstern)',system:'System'};

function fetchSyslog(){
  const lim=Math.max(5,parseInt($('syslogLimit').value)||100);
  api('/api/syslog?limit='+lim).then(r=>r.json()).then(d=>{
    _syslogEntries=d.entries||[];
    _syslogLogging=(d.logging!==false);
    _syslogTotal=d.total||_syslogEntries.length;
    updateSyslogToggleBtn();
    renderSyslog();
  }).catch(()=>{$('syslogBody').innerHTML='<span class="empty-msg">Kunne ikke hente hændelseslog</span>';});
}
function updateSyslogToggleBtn(){
  const b=$('syslogToggleBtn');
  b.textContent=_syslogLogging?'Stop logning':'Start logning';
  b.style.color=_syslogLogging?'#a6adc8':'#f9e2af';
  b.style.borderColor=_syslogLogging?'#45475a':'#f9e2af';
}
function toggleSyslog(){
  const ep=_syslogLogging?'/api/syslog/stop':'/api/syslog/start';
  api(ep,{method:'POST'}).then(r=>r.json()).then(d=>{
    _syslogLogging=(d.logging!==false);
    updateSyslogToggleBtn();
    renderSyslog();
  }).catch(()=>{});
}
function syslogTimeString(e){
  if(e.epoch_s&&e.epoch_s>0){
    const d=new Date(e.epoch_s*1000);
    const pad=n=>String(n).padStart(2,'0');
    return pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());
  }
  const s=Math.floor(e.timestamp_ms/1000);
  return String(Math.floor(s/3600)).padStart(2,'0')+':'+String(Math.floor((s%3600)/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0');
}
function syslogDetail(e){
  if(e.category==='regchange'){
    const kind=e.is_coil?'Coil':'HR';
    return kind+e.reg_addr+': '+e.old_value+' → '+e.new_value;
  }
  return e.message||'';
}
function exportSyslog(){
  api('/api/syslog').then(r=>r.json()).then(d=>{
    const rows=d.entries||[];
    if(!rows.length){alert('Loggen er tom');return;}
    let csv='tid;uptime_ms;epoch_s;kategori;kilde;bruger;ip;register;coil;gammel_vaerdi;ny_vaerdi;besked\n';
    for(const e of rows){
      csv+=syslogTimeString(e)+';'+e.timestamp_ms+';'+(e.epoch_s||0)+';'+e.category+';'+e.source+';'
        +e.username+';'+e.ip+';'+(e.reg_addr===65535?'':e.reg_addr)+';'+e.is_coil+';'
        +e.old_value+';'+e.new_value+';'+e.message+'\n';
    }
    const blob=new Blob([csv],{type:'text/csv;charset=utf-8'});
    const url=URL.createObjectURL(blob);
    const link=document.createElement('a');
    link.href=url;
    const now=new Date();
    const pad=n=>String(n).padStart(2,'0');
    link.download='haendelseslog_'+now.getFullYear()+pad(now.getMonth()+1)+pad(now.getDate())+'_'+pad(now.getHours())+pad(now.getMinutes())+pad(now.getSeconds())+'.csv';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }).catch(e=>alert('Eksport fejlede: '+e.message));
}
function renderSyslog(){
  $('syslogBadge').textContent=String(_syslogEntries.length);
  const el=$('syslogBody');
  if(!_syslogEntries||_syslogEntries.length===0){
    el.innerHTML='<span class="empty-msg">Ingen hændelser endnu</span>';
    $('syslogInfo').textContent='0 hændelser';
    return;
  }
  const catFilter=$('syslogCat').value;
  const srcFilter=$('syslogSrc').value;
  const limit=Math.max(5,parseInt($('syslogLimit').value)||100);
  const filtered=_syslogEntries.filter(e=>{
    if(catFilter!=='any'&&e.category!==catFilter)return false;
    if(srcFilter!=='any'&&e.source!==srcFilter)return false;
    return true;
  });
  const tot=_syslogTotal||_syslogEntries.length;
  $('syslogInfo').textContent=tot+' på enheden, '+filtered.length+' vist'+(_syslogLogging?'':' — LOGNING STOPPET');
  if(filtered.length===0){
    el.innerHTML='<span class="empty-msg">Ingen hændelser matcher filter</span>';
    return;
  }
  const recent=filtered.slice(-limit).reverse();
  let h='<table class="tbl"><thead><tr><th>Tid</th><th>Kategori</th><th>Kilde</th><th>Bruger</th><th>IP</th><th>Detaljer</th></tr></thead><tbody>';
  for(const e of recent){
    const catLbl=e.category==='regchange'?'<span style="color:#f9e2af">Reg.ændring</span>':'<span style="color:#89b4fa">Hændelse</span>';
    const srcLbl=_syslogSrcNames[e.source]||e.source;
    h+='<tr><td>'+syslogTimeString(e)+'</td><td>'+catLbl+'</td><td>'+srcLbl+'</td><td>'+e.username+'</td><td>'+e.ip+'</td><td>'+syslogDetail(e)+'</td></tr>';
  }
  h+='</tbody></table>';
  el.innerHTML=h;
}
function clearSyslog(){
  askConfirm('Ryd Hændelseslog','Ryd hele system-hændelsesloggen (events + registerændringer)?',()=>{
    api('/api/syslog/clear',{method:'POST'}).then(()=>fetchSyslog()).catch(()=>{});
  });
}

// ============================================================
// BOOTSTRAP
// ============================================================
setInterval(()=>{if(!$('panelAudit').hidden)fetchLogs(); else fetchSyslog();},5000);
updateUserBadge();
api('/api/version').then(r=>r.json()).then(d=>{$('footVer').textContent='v'+(d.firmware_version||'-');}).catch(()=>{});
