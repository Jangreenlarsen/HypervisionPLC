
// BUG-393: login now uses a server-set, HttpOnly session cookie
// (Set-Cookie from POST /api/login) instead of a Bearer token this page
// has to remember itself — the browser sends it automatically on every
// same-origin request. No client-side token state left to lose, which is
// what made BUG-392/389/389b (all iOS-Safari-specific breakage) possible
// in the first place.
let pendingConfirmFn=null;
let restoreData=null;

function $(id){return document.getElementById(id)}

// --- Auth ---
function doLogin(){
  const u=$('authUser').value, p=$('authPass').value;
  if(!u||!p){$('loginErr').style.display='block';$('loginErr').textContent='Udfyld begge felter';return}
  const basicAuth='Basic '+btoa(u+':'+p);
  // BUG-353: POST /api/login (samme Basic Auth-header som foer) udsteder nu
  // et session-token vi bruger fremover, saa vi ikke skal gensende
  // brugernavn/kodeord paa hvert request.
  // FEAT-399-følgefejl: skeln 403 ("Blocked by IP ACL" — bevidst IKKE
  // undtaget fra ACL'en, se §10.7.1) fra 401 (reelt forkerte credentials) —
  // uden dette viste UI'et "forkert kodeord" for en helt anden, forbigående
  // årsag, og skjulte at auto-rollback (5 min) selv retter det.
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
    refreshInfo();
    loadPersistGroups();
    fetchOtaStatus();
    refreshSseClients();
    refreshTlsStatus();
    loadAllSettings();
  }).catch(e=>{
    $('loginErr').style.display='block';
    $('loginErr').textContent=(e.message==='acl')
      ? 'Denne IP-adresse er blokeret af IP ACL\'en. Hvis dette skyldes en ny, endnu ubekræftet regel, retter enheden sig selv automatisk inden for 5 minutter (se §10.7.1 i manualen) — prøv igen om lidt, eller log ind fra en anden IP for at bekræfte/fjerne reglen.'
      : 'Forkert brugernavn eller adgangskode';
  });
}
$('authPass').addEventListener('keydown',e=>{if(e.key==='Enter')doLogin()});

// BUG-369c: loadAllSettings() (og andre steder) fyrer op til 10+ parallelle
// api()-kald ved side-indlæsning — se dashboard.html's dashFetch()-kommentar
// for den fulde forklaring (HTTPS-serveren tillader kun max_open_sockets=3
// samtidige TLS-sessioner; flere samtidige forsøg kan udløse en
// ESP-IDF/mbedTLS-fejl der i værste fald leder mod heap-udtømning). Samme
// simple kø-princip her: et hårdt loft på samtidige forbindelser, delt af
// ALLE api()-kald fra denne side.
// BUG-394b: denne blok SKAL staa foer det foerste updateUserBadge()-bootstrap-
// kald nedenfor. _apiFetchQueue/_apiFetchActive er `let`/`const` (ikke
// hoisted som funktionserklaeringer), saa et queuedFetch()-kald FOeR denne
// blok udfoeres rammer midlertidig-dod-zone (TDZ): "Cannot access
// '_apiFetchQueue' before initialization" — kastet inde i Promise-executoren,
// hvilket goer det til et rejected promise (ikke en synkron exception), som
// updateUserBadge()s stille .catch(()=>{}) sluger usynligt. Ramte /system
// (og /io, /logs — samme fejl der) fordi BUG-393b flyttede updateUserBadge()
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
function queuedFetch(url,opts){
  return new Promise((resolve,reject)=>{
    _apiFetchQueue.push({url,opts,resolve,reject});
    _apiFetchPump();
  });
}

// --- API helper ---
function api(url,opts={}){
  return queuedFetch(url,opts).then(r=>{if(r.status===401){$('loginModal').classList.add('show');}return r;});
}

// BUG-393: used to proactively probe /api/status first (with its own
// network-vs-401 distinction to get wrong, see BUG-389/389b). Simpler and
// structurally immune to that bug class: just start loading — if not
// logged in (or the cookie's session expired), the first real request
// 401s and api()'s own handling below shows the modal.
// BUG-393b: updateUserBadge() called FIRST — if any of the calls after it
// ever throws synchronously, the rest of this line would never run,
// silently leaving the badge on its default "not logged in" text.
updateUserBadge();refreshInfo();loadPersistGroups();fetchOtaStatus();refreshSseClients();refreshTlsStatus();loadAllSettings();

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

// --- HTTPS/TLS (samme betingelser som CLI: on/off + dedikeret port, cert er build-time) ---
// BUG-350: HTTPS har sin EGEN port (default 443) — deler ikke port med HTTP
// (default 80) længere. Før delte de samme portnummer, hvilket gjorde at
// aktivering af TLS gjorde HTTP-porten om til en TLS-only-lytter uden varsel.
let _tlsHttpPort=80, _tlsHttpsPort=443;
function updateTlsUrlHint(){
  const host=window.location.hostname;
  $('tlsUrlHint').textContent = $('tlsToggle').checked
    ? 'Tilgå via: https://'+host+':'+_tlsHttpsPort+'/  (browseren vil advare om selvsigneret certifikat — det er forventet)'
    : 'Tilgå via: http://'+host+':'+_tlsHttpPort+'/';
}
function refreshTlsStatus(){
  api('/api/config').then(r=>r.json()).then(d=>{
    const enabled=!!(d.http&&d.http.tls_enabled);
    _tlsHttpPort=(d.http&&d.http.port)||80;
    _tlsHttpsPort=(d.http&&d.http.https_port)||443;
    $('tlsToggle').checked=enabled;
    $('tlsStatus').textContent=enabled?'Aktiveret (HTTPS)':'Deaktiveret (HTTP)';
    $('tlsHttpPort').textContent=_tlsHttpPort;
    $('tlsHttpsPort').textContent=_tlsHttpsPort;
    $('tlsPortInput').value=_tlsHttpsPort;
    updateTlsUrlHint();
  }).catch(()=>{});
}
function doTlsToggle(){
  const enabled=$('tlsToggle').checked;
  api('/api/http',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({tls_enabled:enabled})
  }).then(r=>{
    if(r.status===403){$('tlsToggle').checked=!enabled;throw new Error('Ingen skriveadgang (privilege read)');}
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{
    $('tlsStatus').textContent=(enabled?'Aktiveret (HTTPS)':'Deaktiveret (HTTP)')+' — ikke gemt endnu';
    updateTlsUrlHint();
    showAlert('tlsAlert','warn',(enabled?'HTTPS/TLS aktiveret':'HTTPS/TLS deaktiveret')+' i RAM. Brug "Gem & Genstart" nedenfor for at anvende ændringen, og skift derefter selv adressen i browseren til '+($('tlsUrlHint').textContent||'').replace('Tilgå via: ','')+'.');
  }).catch(e=>showAlert('tlsAlert','err','Fejl: '+e.message));
}
function doTlsPortSave(){
  const p=parseInt($('tlsPortInput').value);
  if(!p||p<1||p>65535){showAlert('tlsAlert','err','Ugyldig port (1-65535)');return}
  api('/api/http',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({https_port:p})
  }).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{
    _tlsHttpsPort=p;
    $('tlsHttpsPort').textContent=p;
    updateTlsUrlHint();
    showAlert('tlsAlert','warn','HTTPS-port sat til '+p+' i RAM. Brug "Gem & Genstart" nedenfor for at anvende ændringen.');
  }).catch(e=>showAlert('tlsAlert','err','Fejl: '+e.message));
}

// --- Persist Groups ---
function loadPersistGroups(){
  api('/api/persist/groups').then(r=>r.json()).then(data=>{
    if($('persistEnabled'))$('persistEnabled').checked=!!data.enabled;
    if($('persistAutoLoad'))$('persistAutoLoad').checked=!!data.auto_load_enabled;
    const tbody=$('persistBody');
    const groups=data.groups||[];
    if(!Array.isArray(groups)||groups.length===0){
      tbody.innerHTML='<tr><td colspan="4" style="color:#6c7086;text-align:center">Ingen persist grupper konfigureret</td></tr>';
      return;
    }
    tbody.innerHTML='';
    groups.forEach(g=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td>'+(g.name||'-')+'</td><td>'+(g.reg_count!=null?g.reg_count:'-')+' / '+(g.max_regs||'-')+'</td><td>'+(g.last_save_ms?g.last_save_ms+' ms uptime':'Aldrig')+'</td>'+
        '<td><button class="btn btn-sm btn-danger" onclick="deletePersistGroup(\''+(g.name||'')+'\')">Slet</button></td>';
      tbody.appendChild(tr);
    });
  }).catch(()=>{
    $('persistBody').innerHTML='<tr><td colspan="4" style="color:#6c7086;text-align:center">Kunne ikke indlæse persist grupper</td></tr>';
  });
}
function savePersistConfig(){
  const body={enabled:$('persistEnabled').checked,auto_load_enabled:$('persistAutoLoad').checked};
  api('/api/persist/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('persistAlert','ok','Persistens-indstillinger opdateret.'))
  .catch(e=>{loadPersistGroups();showAlert('persistAlert','err','Fejl: '+e.message)});
}
function savePersistGroupEdit(){
  const name=$('pgName').value.trim();
  if(!name){showAlert('persistAlert','err','Angiv et gruppenavn');return}
  const body={};
  const addVal=$('pgAddRegs').value.trim();
  if(addVal)body.registers=addVal.split(',').map(s=>parseInt(s.trim())).filter(n=>!isNaN(n));
  const remVal=$('pgRemoveRegs').value.trim();
  if(remVal)body.remove=remVal.split(',').map(s=>parseInt(s.trim())).filter(n=>!isNaN(n));
  api('/api/persist/groups/'+encodeURIComponent(name),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{$('pgName').value='';$('pgAddRegs').value='';$('pgRemoveRegs').value='';showAlert('persistAlert','ok','Gruppe \''+name+'\' oprettet/opdateret.');loadPersistGroups()})
  .catch(e=>showAlert('persistAlert','err','Fejl: '+e.message));
}
function deletePersistGroupPrompt(){
  const name=$('pgName').value.trim();
  if(!name){showAlert('persistAlert','err','Angiv gruppenavnet der skal slettes i feltet ovenfor');return}
  deletePersistGroup(name);
}
function deletePersistGroup(name){
  askConfirm('Slet persist-gruppe','Slet gruppen \''+name+'\'? Registrene i gruppen bevares, men stopper med at blive gemt/gendannet automatisk.',()=>{
    api('/api/persist/groups/'+encodeURIComponent(name),{method:'DELETE'}).then(r=>{
      if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
      if(!r.ok)throw new Error('HTTP '+r.status);
      return r.json();
    }).then(()=>{showAlert('persistAlert','ok','Gruppe \''+name+'\' slettet.');loadPersistGroups()})
    .catch(e=>showAlert('persistAlert','err','Fejl: '+e.message));
  });
}
function doPersistSave(){
  api('/api/persist/save',{method:'POST'}).then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>{showAlert('persistAlert','ok','Persist grupper gemt! '+(d.message||''));loadPersistGroups()})
  .catch(e=>showAlert('persistAlert','err','Gem fejlede: '+e.message));
}
function doPersistRestore(){
  api('/api/persist/restore',{method:'POST'}).then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>{showAlert('persistAlert','ok','Persist grupper gendannet! '+(d.message||''));loadPersistGroups()})
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
  if(f.size>0x1D0000){showAlert('otaAlert','err','Fil for stor (max 1.8125MB)');return}
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

// --- FEAT-169/BUG-377: GitHub Releases OTA (manuelt tjek/installér) ---
// Start+poll-moenster: POST starter, svarer straks; GET poller resultatet.
// Handleren blokerer IKKE laengere forbindelsen imens enheden taler med
// GitHub — det holdt-aabne-forbindelses-moenster var den mistaenkte
// udloeser for et uafklaret device-panic (se BUGS_INDEX.md BUG-377).
function pollUntilDone(url,onTick,isDone,intervalMs,maxAttempts){
  return new Promise((resolve,reject)=>{
    let n=0;
    const tick=()=>{
      n++;
      api(url).then(r=>r.json().then(d=>({ok:r.ok,d}))).then(({ok,d})=>{
        if(!ok){reject(new Error(d.error||'Fejl'));return}
        onTick(d);
        if(isDone(d)){resolve(d);return}
        if(n>=maxAttempts){reject(new Error('Timeout — intet endeligt svar fra enheden'));return}
        setTimeout(tick,intervalMs);
      }).catch(e=>{
        if(n>=maxAttempts){reject(e);return}
        setTimeout(tick,intervalMs);
      });
    };
    tick();
  });
}
function checkGithubOta(){
  // BUG-377: midlertidigt deaktiveret — krascher enheden (under undersøgelse).
  showAlert('otaAlert','err','GitHub-tjek er midlertidigt deaktiveret (BUG-377 — krascher enheden). Brug Manuel upload i stedet.');
  return;
  $('ghOtaCheckBtn').disabled=true;
  $('ghOtaResult').style.display='none';
  $('ghOtaInstallRow').style.display='none';
  showAlert('otaAlert','info','Tjekker GitHub for opdateringer...');
  api('/api/system/ota/github-check',{method:'POST'}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(()=>pollUntilDone(
    '/api/system/ota/github-check',
    ()=>{},
    d=>d.state==='done',
    1000, 30
  )).then(d=>{
    showAlert('otaAlert','ok','GitHub-tjek gennemført.');
    const sizeKb=d.asset_size?Math.round(d.asset_size/1024)+' KB':'-';
    let msg='Nuværende: '+d.current_version+' — Seneste release: '+(d.latest_version||'-');
    if(d.message)msg+=' ('+d.message+')';
    $('ghOtaResult').textContent=msg;
    $('ghOtaResult').style.display='block';
    if(d.available){
      $('ghOtaInstallBtn').textContent='Installér '+d.latest_version+' ('+sizeKb+')';
      $('ghOtaInstallRow').style.display='flex';
    }else{
      showAlert('otaAlert','ok','Ingen nyere version fundet — allerede opdateret.');
    }
  }).catch(e=>showAlert('otaAlert','err','Kunne ikke tjekke GitHub: '+e.message))
  .finally(()=>{$('ghOtaCheckBtn').disabled=false;});
}
function installGithubOta(){
  askConfirm('Installér firmware fra GitHub','Downloader og installerer den nyeste release. Enheden genstarter automatisk bagefter. Fortsæt?',()=>{
    $('ghOtaInstallBtn').disabled=true;
    $('otaProgress').style.display='block';
    $('otaFill').style.width='0%';$('otaPct').textContent='Starter...';
    showAlert('otaAlert','info','Henter og installerer firmware fra GitHub — dette kan tage et minuts tid...');
    api('/api/system/ota/github-install',{method:'POST'}).then(r=>{
      if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
      return r.json();
    }).then(()=>pollUntilDone(
      '/api/system/ota/status',
      d=>{
        if(d.total>0){const p=Math.round((d.received||0)*100/d.total);$('otaFill').style.width=p+'%';$('otaPct').textContent=p+'%'}
        else{$('otaPct').textContent=d.state||'...'}
      },
      d=>d.state==='done'||d.state==='error',
      1000, 100
    )).then(d=>{
      if(d.state==='error')throw new Error(d.error||'Ukendt fejl');
      showAlert('otaAlert','ok','OTA færdig! Version: '+(d.new_version||'?')+'. Genstarter...');
      $('otaFill').style.width='100%';$('otaPct').textContent='100%';
      setTimeout(()=>window.location.reload(),10000);
    }).catch(e=>{
      showAlert('otaAlert','err','GitHub-OTA fejlede: '+e.message);
      $('ghOtaInstallBtn').disabled=false;
      $('otaProgress').style.display='none';
    });
  });
}

// === FEAT-166: samlet system-settings-audit — RBAC brugerstyring + de
// resterende CLI-only config-grupper der havde et fungerende REST-endpoint
// i forvejen, men ingen GUI (WiFi/Ethernet/Hostname, Modbus Slave/Master
// seriel-config, HTTP legacy auth, Telnet, NTP, Analog kalibrering) ===
function loadAllSettings(){
  loadRbac();loadAcl();loadWifi();loadEthernet();loadHostname();
  loadModbusSlave();loadModbusMaster();loadTransceiver();
  loadHttpAuth();loadRateLimit();loadSseSettings();loadTelnet();loadNtp();loadAnalog();
}

// --- RBAC Brugerstyring ---
let rbacEditingUsername=null;
function loadRbac(){
  api('/api/rbac').then(r=>r.json()).then(d=>{
    $('rbacToggle').checked=!!d.enabled;
    $('rbacStatusLine').textContent=(d.enabled?'Aktiveret':'Deaktiveret')+' — '+d.user_count+' / '+d.max_users+' brugere konfigureret';
    const tbody=$('rbacBody');
    if(!d.users||d.users.length===0){tbody.innerHTML='<tr><td colspan="5" style="color:#6c7086;text-align:center">Ingen brugere oprettet endnu</td></tr>';return}
    tbody.innerHTML=d.users.map(u=>{
      const un=u.username.replace(/'/g,"\\'").replace(/"/g,'&quot;');
      return '<tr><td>'+u.index+'</td><td>'+u.username+'</td><td>'+u.roles+'</td><td>'+u.privilege+
        '</td><td><button class="btn btn-sm btn-primary" onclick="editRbacUser(\''+un+'\',\''+u.roles+'\',\''+u.privilege+'\')">Redigér</button> '+
        '<button class="btn btn-sm btn-danger" onclick="deleteRbacUser(\''+un+'\')">Slet</button></td></tr>';
    }).join('');
  }).catch(()=>{$('rbacBody').innerHTML='<tr><td colspan="5" style="color:#f38ba8;text-align:center">Kunne ikke hente brugerliste</td></tr>'});
}
function doRbacToggle(){
  const want=$('rbacToggle').checked;
  api('/api/rbac',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:want})}).then(r=>r.json()).then(d=>{
    if(d.warning)showAlert('rbacAlert','warn',d.warning);
    else showAlert('rbacAlert','ok',d.message||'RBAC-status opdateret');
    loadRbac();
  }).catch(e=>{$('rbacToggle').checked=!want;showAlert('rbacAlert','err','Fejl: '+e.message)});
}
function clearRbacForm(){
  rbacEditingUsername=null;
  $('rbacUFormUser').value='';$('rbacUFormUser').disabled=false;
  $('rbacUFormPass').value='';
  $('rbacUFormPriv').value='read/write';
  $('rbacRoleApi').checked=false;$('rbacRoleCli').checked=false;$('rbacRoleEditor').checked=false;$('rbacRoleMonitor').checked=true;
}
function editRbacUser(username,roles,privilege){
  rbacEditingUsername=username;
  $('rbacUFormUser').value=username;
  $('rbacUFormUser').disabled=true; // username er opslagsnøglen for rbac_set_user()'s opdatér-sti — kan ikke omdøbes her (samme begrænsning som CLI: slet+genopret i stedet)
  $('rbacUFormPass').value='';
  $('rbacUFormPriv').value=privilege;
  $('rbacRoleApi').checked=roles.indexOf('api')>=0;
  $('rbacRoleCli').checked=roles.indexOf('cli')>=0;
  $('rbacRoleEditor').checked=roles.indexOf('editor')>=0;
  $('rbacRoleMonitor').checked=roles.indexOf('monitor')>=0;
}
function saveRbacUser(){
  const username=$('rbacUFormUser').value.trim();
  const password=$('rbacUFormPass').value;
  if(!username){showAlert('rbacAlert','err','Brugernavn er påkrævet');return}
  if(!password){showAlert('rbacAlert','err','Adgangskode er påkrævet (ved opret OG redigering)');return}
  const roles=[];
  if($('rbacRoleApi').checked)roles.push('api');
  if($('rbacRoleCli').checked)roles.push('cli');
  if($('rbacRoleEditor').checked)roles.push('editor');
  if($('rbacRoleMonitor').checked)roles.push('monitor');
  if(roles.length===0){showAlert('rbacAlert','err','Vælg mindst én rolle');return}
  api('/api/rbac/users',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:username,password:password,roles:roles.join(','),privilege:$('rbacUFormPriv').value})}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(d=>{
    showAlert('rbacAlert','ok','Bruger gemt (slot '+d.index+'). Brug "Save" i toppen for at overleve reboot.');
    clearRbacForm();
    loadRbac();
  }).catch(e=>showAlert('rbacAlert','err','Fejl: '+e.message));
}
function deleteRbacUser(username){
  askConfirm('Slet bruger','Slet brugeren "'+username+'"? Kan ikke fortrydes efter "Save".',()=>{
    api('/api/rbac/users/'+encodeURIComponent(username),{method:'DELETE'}).then(r=>{
      if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
      return r.json();
    }).then(d=>{
      showAlert('rbacAlert','ok',d.message||'Bruger slettet');
      if(rbacEditingUsername===username)clearRbacForm();
      loadRbac();
    }).catch(e=>showAlert('rbacAlert','err','Fejl: '+e.message));
  });
}

// --- IP Access Control List (FEAT-399, ordnet permit/deny FEAT-401) ---
let aclCountdownTimer=null;
let aclEditingIndex=null;
let aclRuleCount=0;
function loadAcl(){
  api('/api/acl').then(r=>r.json()).then(d=>{
    $('aclToggle').checked=!!d.enabled;
    $('aclStatusLine').textContent=(d.enabled?'Aktiveret':'Deaktiveret')+' — '+d.rule_count+' regel/regler konfigureret';
    aclRuleCount=d.rule_count||0;

    if(d.pending_confirm){
      $('aclPendingBanner').style.display='block';
      startAclCountdown(d.pending_remaining_ms);
    }else{
      $('aclPendingBanner').style.display='none';
      stopAclCountdown();
    }

    const tbody=$('aclBody');
    if(!d.rules||d.rules.length===0){tbody.innerHTML='<tr><td colspan="6" style="color:#6c7086;text-align:center">Ingen regler konfigureret — alt tilladt</td></tr>';return}
    tbody.innerHTML=d.rules.map(r=>{
      const actionLabel=(r.action==='allow')?'<span style="color:#a6e3a1">Tillad</span>':'<span style="color:#f38ba8">Bloker</span>';
      const cidrEsc=r.cidr.replace(/'/g,"\\'");
      const up=(r.index>0)?'<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4;padding:2px 7px" onclick="moveAclRule('+r.index+','+(r.index-1)+')" title="Flyt op">▲</button>':'';
      const down=(r.index<aclRuleCount-1)?'<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4;padding:2px 7px" onclick="moveAclRule('+r.index+','+(r.index+1)+')" title="Flyt ned">▼</button>':'';
      return '<tr><td>'+r.index+' '+up+down+'</td><td>'+actionLabel+'</td><td>'+r.cidr+'</td><td>'+r.service+'</td><td>'+(r.enabled?'Aktiv':'Deaktiveret')+
        '</td><td><button class="btn btn-sm btn-primary" onclick="editAclRule('+r.index+',\''+r.action+'\',\''+cidrEsc+'\',\''+r.service+'\')">Redigér</button> '+
        '<button class="btn btn-sm btn-primary" onclick="toggleAclRule('+r.index+','+(!r.enabled)+')">'+(r.enabled?'Deaktivér':'Aktivér')+'</button> '+
        '<button class="btn btn-sm btn-danger" onclick="deleteAclRule('+r.index+')">Slet</button></td></tr>';
    }).join('');
  }).catch(()=>{$('aclBody').innerHTML='<tr><td colspan="6" style="color:#f38ba8;text-align:center">Kunne ikke hente ACL-regler</td></tr>'});
}
function startAclCountdown(remainingMs){
  stopAclCountdown();
  let remaining=Math.max(0,Math.round(remainingMs/1000));
  const render=()=>{
    const m=Math.floor(remaining/60),s=remaining%60;
    $('aclCountdown').textContent=m+':'+(s<10?'0':'')+s;
  };
  render();
  aclCountdownTimer=setInterval(()=>{
    remaining--;
    if(remaining<=0){loadAcl();return}
    render();
  },1000);
}
function stopAclCountdown(){
  if(aclCountdownTimer){clearInterval(aclCountdownTimer);aclCountdownTimer=null}
}
function doAclToggle(){
  const want=$('aclToggle').checked;
  api('/api/acl',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:want})}).then(r=>r.json()).then(d=>{
    showAlert('aclAlert','ok',d.message||'ACL-status opdateret');
    loadAcl();
  }).catch(e=>{$('aclToggle').checked=!want;showAlert('aclAlert','err','Fejl: '+e.message)});
}
function clearAclForm(){
  aclEditingIndex=null;
  $('aclFormTitle').textContent='Tilføj regel';
  $('aclFormSubmitBtn').textContent='Tilføj regel';
  $('aclFormAction').value='deny';
  $('aclFormCidr').value='';
  $('aclFormService').value='http';
}
function editAclRule(index,action,cidr,service){
  aclEditingIndex=index;
  $('aclFormTitle').textContent='Redigér regel #'+index;
  $('aclFormSubmitBtn').textContent='Gem ændringer';
  $('aclFormAction').value=action;
  $('aclFormCidr').value=cidr;
  $('aclFormService').value=service;
  $('aclFormCidr').scrollIntoView({behavior:'smooth',block:'center'});
}
// FEAT-399/401-følgefejl: en Bloker-regel der matcher afsenderens EGEN IP
// for HTTP kan reelt låse en selv ude (bekræftet brugerrapport) —
// server-svaret kan inkludere "self_match_warning":true, vis det tydeligt
// FØR man fortsætter med et nyt login (i stedet for først at opdage det der).
function showAclResult(d,okMsg){
  if(d.self_match_warning){
    showAlert('aclAlert','err','⚠ ADVARSEL: denne regel BLOKERER din EGEN nuværende IP for HTTP — du risikerer at logge dig selv ude! '+(d.message||''));
  }else{
    showAlert('aclAlert','ok',d.message||okMsg);
  }
}
function submitAclRule(){
  const action=$('aclFormAction').value;
  const cidr=$('aclFormCidr').value.trim();
  const service=$('aclFormService').value;
  if(!cidr){showAlert('aclAlert','err','IP/CIDR er påkrævet');return}
  const body=JSON.stringify({cidr:cidr,service:service,action:action,enabled:true});
  const url=(aclEditingIndex===null)?'/api/acl/rules':('/api/acl/rules/'+aclEditingIndex);
  api(url,{method:'POST',headers:{'Content-Type':'application/json'},body:body}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(d=>{
    showAclResult(d,aclEditingIndex===null?'Regel tilføjet':'Regel opdateret');
    clearAclForm();
    loadAcl();
  }).catch(e=>showAlert('aclAlert','err','Fejl: '+e.message));
}
function toggleAclRule(index,enabled){
  api('/api/acl/rules/'+index,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled})}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(d=>{
    showAclResult(d,'Regel opdateret');
    loadAcl();
  }).catch(e=>showAlert('aclAlert','err','Fejl: '+e.message));
}
function moveAclRule(fromIndex,toIndex){
  api('/api/acl/rules/'+fromIndex+'/move',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({to_index:toIndex})}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(d=>{
    showAlert('aclAlert','ok',d.message||'Regel flyttet');
    loadAcl();
  }).catch(e=>showAlert('aclAlert','err','Fejl: '+e.message));
}
function deleteAclRule(index){
  askConfirm('Slet ACL-regel','Slet regel #'+index+'?',()=>{
    api('/api/acl/rules/'+index,{method:'DELETE'}).then(r=>{
      if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
      return r.json();
    }).then(d=>{
      showAlert('aclAlert','ok',d.message||'Regel slettet');
      if(aclEditingIndex===index)clearAclForm();
      loadAcl();
    }).catch(e=>showAlert('aclAlert','err','Fejl: '+e.message));
  });
}
function confirmAclChange(){
  api('/api/acl/confirm',{method:'POST'}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(d=>{
    showAlert('aclAlert','ok',d.message||'ACL-ændring bekræftet');
    loadAcl();
  }).catch(e=>showAlert('aclAlert','err','Fejl: '+e.message));
}

// --- WiFi ---
function toggleWifiStaticFields(){$('wifiStaticFields').style.display=$('wifiDhcp').checked?'none':'block'}
function loadWifi(){
  api('/api/wifi').then(r=>r.json()).then(d=>{
    const c=d.config||{};
    $('wifiEnabled').checked=!!c.enabled;
    $('wifiSsid').value=c.ssid||'';
    $('wifiDhcp').checked=!!c.dhcp;
    $('wifiPowerSave').checked=!!c.power_save;
    $('wifiIp').value=c.static_ip||'';
    $('wifiGw').value=c.static_gateway||'';
    $('wifiNm').value=c.static_netmask||'';
    $('wifiDns').value=c.static_dns||'';
    toggleWifiStaticFields();
  }).catch(()=>{});
}
function saveWifi(){
  const body={enabled:$('wifiEnabled').checked,ssid:$('wifiSsid').value,dhcp:$('wifiDhcp').checked,power_save:$('wifiPowerSave').checked};
  if($('wifiPass').value)body.password=$('wifiPass').value;
  if(!$('wifiDhcp').checked){body.static_ip=$('wifiIp').value;body.static_gateway=$('wifiGw').value;body.static_netmask=$('wifiNm').value;body.static_dns=$('wifiDns').value;}
  api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{$('wifiPass').value='';showAlert('wifiAlert','warn','WiFi-config opdateret i RAM. Kræver Save + Genstart for at træde i kraft.')})
  .catch(e=>showAlert('wifiAlert','err','Fejl: '+e.message));
}

// --- Ethernet ---
function toggleEthStaticFields(){$('ethStaticFields').style.display=$('ethDhcp').checked?'none':'block'}
function loadEthernet(){
  api('/api/ethernet').then(r=>r.json()).then(d=>{
    const c=d.config||{};
    $('ethEnabled').checked=!!c.enabled;
    $('ethDhcp').checked=!!c.dhcp;
    $('ethIp').value=c.static_ip||'';
    $('ethGw').value=c.static_gateway||'';
    $('ethNm').value=c.static_netmask||'';
    $('ethDns').value=c.static_dns||'';
    $('ethHostname').value=c.hostname||'';
    toggleEthStaticFields();
  }).catch(()=>{});
}
function saveEthernet(){
  const body={enabled:$('ethEnabled').checked,dhcp:$('ethDhcp').checked,hostname:$('ethHostname').value};
  if(!$('ethDhcp').checked){body.static_ip=$('ethIp').value;body.static_gateway=$('ethGw').value;body.static_netmask=$('ethNm').value;body.static_dns=$('ethDns').value;}
  api('/api/ethernet',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('ethAlert','warn','Ethernet-config opdateret i RAM. Kræver Save + Genstart for at træde i kraft.'))
  .catch(e=>showAlert('ethAlert','err','Fejl: '+e.message));
}

// --- Hostname ---
function loadHostname(){
  api('/api/hostname').then(r=>r.json()).then(d=>{$('deviceHostname').value=d.hostname||''}).catch(()=>{});
}
function saveHostname(){
  const h=$('deviceHostname').value.trim();
  if(!h){showAlert('hostnameAlert','err','Hostname må ikke være tomt');return}
  api('/api/hostname',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hostname:h})}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('hostnameAlert','warn','Hostname opdateret i RAM. Kræver Save + Genstart.'))
  .catch(e=>showAlert('hostnameAlert','err','Fejl: '+e.message));
}

// --- Modbus Slave (RTU) ---
function loadModbusSlave(){
  api('/api/modbus/slave').then(r=>r.json()).then(d=>{
    const c=d.config||{};
    $('mbsId').value=c.slave_id||1;
    $('mbsBaud').value=c.baudrate||9600;
    $('mbsParity').value=c.parity||'none';
    $('mbsStop').value=c.stop_bits||1;
    $('mbsIfd').value=c.inter_frame_delay_ms||0;
  }).catch(()=>{});
}
function saveModbusSlave(){
  const body={slave_id:parseInt($('mbsId').value),baudrate:parseInt($('mbsBaud').value),parity:$('mbsParity').value,stop_bits:parseInt($('mbsStop').value),inter_frame_delay_ms:parseInt($('mbsIfd').value)};
  api('/api/modbus/slave',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('mbsAlert','warn','Slave-config opdateret i RAM. Kræver Save + Genstart for at træde i kraft.'))
  .catch(e=>showAlert('mbsAlert','err','Fejl: '+e.message));
}

// --- Modbus Master (RTU) ---
function loadModbusMaster(){
  api('/api/modbus/master').then(r=>r.json()).then(d=>{
    const c=d.config||{};
    $('mbmEnabled').checked=!!c.enabled;
    $('mbmBaud').value=c.baudrate||9600;
    $('mbmParity').value=c.parity||'none';
    $('mbmStop').value=c.stop_bits||1;
    $('mbmTimeout').value=c.timeout_ms||1000;
    $('mbmIfd').value=c.inter_frame_delay_ms||0;
    $('mbmMaxReq').value=c.max_requests_per_cycle||5;
    $('mbmCacheTtl').value=c.cache_ttl_ms||0;
  }).catch(()=>{});
}
function saveModbusMaster(){
  const body={enabled:$('mbmEnabled').checked,baudrate:parseInt($('mbmBaud').value),parity:$('mbmParity').value,stop_bits:parseInt($('mbmStop').value),timeout_ms:parseInt($('mbmTimeout').value),inter_frame_delay_ms:parseInt($('mbmIfd').value),max_requests_per_cycle:parseInt($('mbmMaxReq').value),cache_ttl_ms:parseInt($('mbmCacheTtl').value)};
  api('/api/modbus/master',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('mbmAlert','ok','Master-config opdateret og delvist anvendt straks — baud/parity/stopbits kræver dog Save + Genstart for at overleve reboot.'))
  .catch(e=>showAlert('mbmAlert','err','Fejl: '+e.message));
}

// --- Modbus Transceiver ---
function loadTransceiver(){
  api('/api/modbus/slave').then(r=>r.json()).then(d=>{
    const x=d.transceiver||{};
    $('xcvrMode').value=x.mode||'slave';
    $('xcvrSlaveUart').value=(x.slave_uart!=null?x.slave_uart:2);
    $('xcvrMasterUart').value=(x.master_uart!=null?x.master_uart:2);
  }).catch(()=>{});
}
function saveTransceiver(){
  const body={transceiver:{mode:$('xcvrMode').value,slave_uart:parseInt($('xcvrSlaveUart').value),master_uart:parseInt($('xcvrMasterUart').value)}};
  api('/api/modbus/slave',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('xcvrAlert','warn','Transceiver-config opdateret i RAM. Kræver Save + Genstart for at træde i kraft.'))
  .catch(e=>showAlert('xcvrAlert','err','Fejl: '+e.message));
}

// --- HTTP Server + Legacy Auth ---
function loadHttpAuth(){
  api('/api/config').then(r=>r.json()).then(d=>{
    const h=d.http||{};
    $('httpEnabled').checked=!!h.enabled;
    $('httpPort').value=h.port||80;
    $('httpApiEnabled').checked=!!h.api_enabled;
    $('httpAuthEnabled').checked=!!h.auth_enabled;
    $('httpAuthMode').value=h.auth_mode||'bearer';
    $('httpUser').value=h.username||'';
  }).catch(()=>{});
}
function saveHttpAuth(){
  const body={
    enabled:$('httpEnabled').checked,
    port:parseInt($('httpPort').value)||80,
    api_enabled:$('httpApiEnabled').checked,
    auth_enabled:$('httpAuthEnabled').checked,
    auth_mode:$('httpAuthMode').value
  };
  if($('httpUser').value)body.username=$('httpUser').value;
  if($('httpPass').value)body.password=$('httpPass').value;
  api('/api/http',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{$('httpPass').value='';showAlert('httpAuthAlert','warn','HTTP-indstillinger opdateret i RAM. Husk Save for at overleve reboot.')})
  .catch(e=>showAlert('httpAuthAlert','err','Fejl: '+e.message));
}

// --- Rate-Limit ---
function loadRateLimit(){
  api('/api/system/rate-limit').then(r=>r.json()).then(d=>{
    $('rateLimitEnabled').checked=!!d.enabled;
  }).catch(()=>{});
}
function saveRateLimit(){
  const body={enabled:$('rateLimitEnabled').checked};
  api('/api/system/rate-limit',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{showAlert('rateLimitAlert','ok','Rate-limit '+($('rateLimitEnabled').checked?'aktiveret':'deaktiveret')+' (ikke gemt ved reboot).')})
  .catch(e=>{loadRateLimit();showAlert('rateLimitAlert','err','Fejl: '+e.message)});
}

// --- SSE Server-indstillinger ---
function loadSseSettings(){
  api('/api/config').then(r=>r.json()).then(d=>{
    const s=d.sse||{};
    $('sseEnabled').checked=!!s.enabled;
    $('ssePort').value=s.port||8081;
    $('sseMaxClients').value=s.max_clients||3;
    $('sseCheckInterval').value=s.check_interval_ms||200;
    $('sseHeartbeat').value=s.heartbeat_ms||15000;
  }).catch(()=>{});
}
function saveSseSettings(){
  const body={sse:{
    enabled:$('sseEnabled').checked,
    port:parseInt($('ssePort').value),
    max_clients:parseInt($('sseMaxClients').value),
    check_interval_ms:parseInt($('sseCheckInterval').value),
    heartbeat_ms:parseInt($('sseHeartbeat').value)
  }};
  api('/api/http',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('sseSettingsAlert','warn','SSE-indstillinger opdateret i RAM. Kræver Save + Genstart for at træde i kraft.'))
  .catch(e=>showAlert('sseSettingsAlert','err','Fejl: '+e.message));
}

// --- Telnet ---
function loadTelnet(){
  api('/api/telnet').then(r=>r.json()).then(d=>{
    $('telnetEnabled').checked=!!d.enabled;
    $('telnetPort').value=d.port||23;
    $('telnetUser').value=d.username||'';
  }).catch(()=>{});
}
function saveTelnet(){
  const body={enabled:$('telnetEnabled').checked,port:parseInt($('telnetPort').value)||23};
  if($('telnetUser').value!=='')body.username=$('telnetUser').value;
  if($('telnetPass').value)body.password=$('telnetPass').value;
  api('/api/telnet',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{$('telnetPass').value='';showAlert('telnetAlert','warn','Telnet-config opdateret i RAM. Kræver Save + Genstart.')})
  .catch(e=>showAlert('telnetAlert','err','Fejl: '+e.message));
}

// --- NTP ---
function loadNtp(){
  api('/api/ntp').then(r=>r.json()).then(d=>{
    $('ntpEnabled').checked=!!d.enabled;
    $('ntpServer').value=d.server||'';
    $('ntpTz').value=d.timezone||'';
    $('ntpInterval').value=d.sync_interval_min||60;
  }).catch(()=>{});
}
function saveNtp(){
  const body={enabled:$('ntpEnabled').checked,server:$('ntpServer').value,timezone:$('ntpTz').value,sync_interval_min:parseInt($('ntpInterval').value)||60};
  api('/api/ntp',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('ntpAlert','ok','NTP-config opdateret og anvendt straks (kræver ikke reboot).'))
  .catch(e=>showAlert('ntpAlert','err','Fejl: '+e.message));
}

// --- Analog I/O Kalibrering (kun ES32D26) ---
function loadAnalog(){
  api('/api/analog').then(r=>{
    if(r.status===404){$('analogCard').style.display='none';return null}
    return r.json();
  }).then(d=>{
    if(!d)return;
    const rows=[];
    (d.ai_voltage||[]).forEach(c=>rows.push({ch:c.channel,en:c.enabled,sc:c.scale,off:c.offset,val:(c.value/100).toFixed(2)+' V'}));
    (d.ai_current||[]).forEach(c=>rows.push({ch:c.channel,en:c.enabled,sc:c.scale,off:c.offset,val:(c.value/100).toFixed(2)+' mA'}));
    (d.ao||[]).forEach(c=>rows.push({ch:c.channel,en:c.enabled,sc:c.scale,off:c.offset,val:(c.setpoint/100).toFixed(2),mode:c.mode}));
    $('analogBody').innerHTML=rows.map(r=>
      '<tr><td>'+r.ch+'</td>'+
      '<td><input type="checkbox" id="an_en_'+r.ch+'" '+(r.en?'checked':'')+'></td>'+
      '<td>'+(r.mode?('<select id="an_mode_'+r.ch+'" style="padding:3px 5px;background:#1e1e2e;border:1px solid #45475a;border-radius:3px;color:#cdd6f4;font-size:11px"><option value="voltage"'+(r.mode==='voltage'?' selected':'')+'>Spænding</option><option value="current"'+(r.mode==='current'?' selected':'')+'>Strøm</option></select>'):'-')+'</td>'+
      '<td><input type="number" step="0.0001" id="an_sc_'+r.ch+'" value="'+r.sc+'" style="width:80px;padding:3px 5px;background:#1e1e2e;border:1px solid #45475a;border-radius:3px;color:#cdd6f4;font-size:11px"></td>'+
      '<td><input type="number" step="0.0001" id="an_of_'+r.ch+'" value="'+r.off+'" style="width:80px;padding:3px 5px;background:#1e1e2e;border:1px solid #45475a;border-radius:3px;color:#cdd6f4;font-size:11px"></td>'+
      '<td>'+r.val+'</td>'+
      '<td><button class="btn btn-sm btn-success" onclick="saveAnalogChannel(\''+r.ch+'\')">Gem</button></td></tr>'
    ).join('');
  }).catch(()=>{$('analogBody').innerHTML='<tr><td colspan="7" style="color:#f38ba8;text-align:center">Kunne ikke hente analog-status</td></tr>'});
}
function saveAnalogChannel(ch){
  const body={channel:ch,enabled:$('an_en_'+ch).checked,scale:parseFloat($('an_sc_'+ch).value),offset:parseFloat($('an_of_'+ch).value)};
  const modeSel=$('an_mode_'+ch);
  if(modeSel)body.mode=modeSel.value;
  api('/api/analog',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(r.status===403)throw new Error('Ingen skriveadgang (privilege read)');
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(d=>showAlert('analogAlert','ok',(d.note||'Gemt')+' ('+ch+')'))
  .catch(e=>showAlert('analogAlert','err','Fejl ('+ch+'): '+e.message));
}

// === User Badge ===
function toggleUserMenu(){var m=document.getElementById('userMenu');m.classList.toggle('show')}
document.addEventListener('click',function(e){var b=document.getElementById('userBtn');var m=document.getElementById('userMenu');if(b&&m&&!b.contains(e.target)&&!m.contains(e.target))m.classList.remove('show')});
function updateUserBadge(){
// BUG-393b: route through the same queuedFetch() throttle every other
// request on this page uses — a plain unqueued fetch() here could exceed
// the device's max_open_sockets alongside the page's own burst of queued
// calls and lose the race (BUG-369's original heap-crash class of bug, on
// this page it just silently left the badge unset instead).
queuedFetch('/api/user/me',{}).then(function(r){return r.json()}).then(function(d){
if(d.authenticated){document.getElementById('userName').textContent=d.username;document.getElementById('userDot').className='dot dot-on';document.getElementById('umUser').textContent=d.username;document.getElementById('umRoles').textContent=d.roles||'all';document.getElementById('umPriv').textContent=d.privilege||'rw';document.getElementById('umMode').textContent=d.mode||'legacy';document.getElementById('umLogout').style.display='block'}
else{document.getElementById('userName').textContent='Ikke logget ind';document.getElementById('userDot').className='dot dot-off';document.getElementById('umLogout').style.display='none'}
}).catch(function(){})
}
function doLogout(){
// BUG-393: cookie is HttpOnly, always POST unconditionally — server reads
// it itself and clears it in the response either way. Reload afterward to
// reliably stop this page's polling and blank any already-rendered data.
fetch('/api/logout',{method:'POST'}).catch(()=>{}).then(()=>{location.reload();});}
updateUserBadge();
