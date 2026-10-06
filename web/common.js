/* AUTO-EXTRACTED shared JS -- see scripts/gzip_web_assets.py.
   Do not duplicate these functions back into individual pages. */

function _dashFetchPump(){
  while(_dashFetchActive<DASH_FETCH_MAX_CONCURRENT && _dashFetchQueue.length>0){
    const job=_dashFetchQueue.shift();
    _dashFetchActive++;
    fetch(job.url,job.opts).then(r=>{_dashFetchActive--;_dashFetchPump();job.resolve(r);})
      .catch(e=>{
        _dashFetchActive--;
        // BUG-475: samme GET-retry som _apiFetchPump (lukket keep-alive-socket)
        const m=((job.opts&&job.opts.method)||'GET').toUpperCase();
        if(m==='GET'&&(job.retries||0)<2){job.retries=(job.retries||0)+1;setTimeout(()=>{_dashFetchQueue.unshift(job);_dashFetchPump();},150*job.retries);}
        else job.reject(e);
        _dashFetchPump();
      });
  }
}

function dashFetch(url,opts){
  return new Promise((resolve,reject)=>{
    _dashFetchQueue.push({url,opts,resolve,reject});
    _dashFetchPump();
  });
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

function fmtResetReason(v){return(v!=null&&v>=0&&v<_resetReasons.length)?_resetReasons[v]:'-'}

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

function fmtRate(v){return v!=null?v.toFixed(1)+'/s':'-'}

function histMinMax(hist){
  if(!hist||hist.length===0)return'-';
  let mn=Infinity,mx=-Infinity;
  for(const v of hist){if(v<mn)mn=v;if(v>mx)mx=v;}
  return 'min '+fmtRate(mn)+' / max '+fmtRate(mx);
}

// v7.9.68.6: null-guarded -- a caller's target element can legitimately be
// absent (e.g. a customized dashboard layout with a card removed/hidden).
// Throwing here used to silently abort whatever caller-side cleanup ran
// AFTER the dot()/badge() call in the same function -- see
// _expCheckSequential()'s base case in dashboard.html, where that skipped
// _expCycleRunning=false and permanently stalled the 30s auto-refresh.
function dot(el,on){if(!el)return;el.className='dot '+(on?'dot-g':'dot-r')}

function badge(el,on,lbl){if(!el)return;el.textContent=lbl||'';el.className='badge '+(on?'badge-ok':'badge-off')}

function $(id){return document.getElementById(id)}

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

function addAlarm(key,value,text,lowerIsWorse){
  alarmCurrentValues[key]=value;
  const acked=alarmAck[key];
  if(acked!=null){
    const stillAcked=lowerIsWorse?(value>=acked):(value<=acked);
    if(stillAcked)return;
  }
  alarms.push(text);
}

function checkAlarms(m){
  lastMetricsRaw=m;
  alarms=[];
  alarmCurrentValues={};
  const heap=g(m,'esp32_heap_free_bytes');
  if(heap!=null&&heap<30000)addAlarm('heap',heap,'Heap kritisk lav: '+fmtKB(heap),true);
  const heapMin=g(m,'esp32_heap_min_free_bytes');
  if(heapMin!=null&&heapMin<20000)addAlarm('heapMin',heapMin,'Heap minimum nået: '+fmtKB(heapMin),true);
  const msCrc=g(m,'modbus_slave_crc_errors_total');
  if(msCrc!=null&&msCrc>100)addAlarm('msCrc',msCrc,'Modbus Slave CRC fejl: '+fmtN(msCrc),false);
  const mmTo=g(m,'modbus_master_timeout_errors_total');
  if(mmTo!=null&&mmTo>50)addAlarm('mmTo',mmTo,'Modbus Master timeouts: '+fmtN(mmTo),false);
  const stOvr=g(m,'st_logic_cycle_overruns');
  const stCyc=g(m,'st_logic_total_cycles');
  const stOvrPct=(stCyc!=null&&stCyc>100)?(stOvr/stCyc*100):0;
  if(stOvr!=null&&stOvrPct>5)addAlarm('stOvr',stOvr,'ST Logic overruns: '+fmtN(stOvr)+' ('+stOvrPct.toFixed(1)+'%)',false);
  const authFail=g(m,'http_auth_failures_total');
  if(authFail!=null&&authFail>20)addAlarm('authFail',authFail,'HTTP auth failures: '+fmtN(authFail),false);
  // FEAT-456: expansion boards der er offline (expansion_board_online == 0)
  const expOff=gAll(m,'expansion_board_online').filter(e=>e.value===0).map(e=>'#'+e.labels.board);
  if(expOff.length)addAlarm('expOff',expOff.length,'Expansion board offline: '+expOff.join(', '),false);

  const bar=$('alarmBar');
  if(alarms.length){
    bar.style.display='flex';
    bar.innerHTML='<span>ALARM: '+alarms.join(' | ')+'</span>'
      +'<button onclick="ackAlarmBar()" title="Afstem — skjul indtil det bliver værre" style="background:#1e1e2e;color:#f38ba8;border:none;border-radius:3px;padding:2px 10px;font-size:11px;font-weight:600;cursor:pointer;flex-shrink:0;margin-left:12px">Afstem ✕</button>';
  }else{
    bar.style.display='none';
  }
}

function ackAlarmBar(){
  for(const k in alarmCurrentValues){alarmAck[k]=alarmCurrentValues[k];}
  try{sessionStorage.setItem('hfplc_alarm_ack',JSON.stringify(alarmAck));}catch(e){}
  if(lastMetricsRaw)checkAlarms(lastMetricsRaw);
}

function analogRows(list,unit){
      let h='<table class="tbl"><tr><th>Kanal</th><th>Værdi</th><th>Min/Maks/Snit</th><th>Trend</th></tr>';
      for(const d of list){
        const ch=d.labels.channel;
        const hv=history.analog[ch]||[d.value/100];
        const mn=Math.min(...hv),mx=Math.max(...hv),avg=hv.reduce((a,b)=>a+b,0)/hv.length;
        h+='<tr><td>'+ch.toUpperCase()+'</td><td>'+(d.value/100).toFixed(2)+' '+unit+'</td>'
          +'<td style="font-size:9px;color:#6c7086">'+mn.toFixed(2)+' / '+mx.toFixed(2)+' / '+avg.toFixed(2)+'</td>'
          +'<td><svg id="spark_'+ch+'" width="80" height="20" viewBox="0 0 200 24" preserveAspectRatio="none"></svg></td></tr>';
      }
      return h+'</table>';
    }

function mbActTimeString(a){
  if(a.epoch_s&&a.epoch_s>0){
    const d=new Date(a.epoch_s*1000);
    const pad=n=>String(n).padStart(2,'0');
    return pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());
  }
  const s=Math.floor(a.timestamp_ms/1000);
  return String(Math.floor(s/3600)).padStart(2,'0')+':'+String(Math.floor((s%3600)/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0');
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

// BUG-435: escaper ALT tekst fra serveren der indsaettes som HTML. Logposter
// indeholder bl.a. det brugernavn et FEJLET login forsoegte med — uden
// escaping kunne en uautentificeret angriber faa kode til at koere i en admins browser.
// FEAT-440: rødt banner øverst, mens den indloggede bruger har fabrikkens
// standard-adgangskode. Fanger svaret fra /api/user/me, som alle login-sider
// allerede henter — ingen ekstra forespørgsler mod enheden.
function hfDefaultPwBanner(d){
  if(!d||!(d.default_password||d.any_default_password)||document.getElementById('hfDefPw'))return;
  const b=document.createElement('div');
  b.id='hfDefPw';
  b.style.cssText='background:#f38ba8;color:#1e1e2e;padding:6px 14px;font-size:13px;font-weight:600;text-align:center';
  b.innerHTML=(d.default_password?'⚠ Du bruger fabrikkens standard-adgangskode.':'⚠ En eller flere konti bruger stadig fabrikkens standard-adgangskode.')+' Skift den under <a href="/system" style="color:#1e1e2e;text-decoration:underline">System → Brugerstyring</a>.';
  document.body.insertBefore(b,document.body.firstChild);
}
(function(){
  if(!window.fetch)return;
  const _f=window.fetch;
  window.fetch=function(u){
    const p=_f.apply(this,arguments);
    if(String(u&&u.url||u).indexOf('/api/user/me')>=0||String(u&&u.url||u).indexOf('/api/login')>=0){
      p.then(r=>r.ok?r.clone().json():null).then(hfDefaultPwBanner).catch(()=>{});
    }
    return p;
  };
})();
function escHtml(s){return s==null?'':String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}

function syslogDetail(e){
  if(e.category==='regchange'){
    const kind=e.is_coil?'Coil':'HR';
    return escHtml(kind+e.reg_addr+': '+e.old_value+' → '+e.new_value);
  }
  return escHtml(e.message||'');
}

function _apiFetchPump(){
  while(_apiFetchActive<API_FETCH_MAX_CONCURRENT && _apiFetchQueue.length>0){
    const job=_apiFetchQueue.shift();
    _apiFetchActive++;
    fetch(job.url,job.opts).then(r=>{_apiFetchActive--;_apiFetchPump();job.resolve(r);})
      .catch(e=>{
        _apiFetchActive--;
        // BUG-475: serveren lukker den ældste keep-alive-socket ved fuldt loft
        // (lru_purge) — et GET sendt på netop den giver "NetworkError/Failed to
        // fetch". GET er idempotent: prøv igen (op til 2 gange), POST aldrig.
        const m=((job.opts&&job.opts.method)||'GET').toUpperCase();
        if(m==='GET'&&(job.retries||0)<2){job.retries=(job.retries||0)+1;setTimeout(()=>{_apiFetchQueue.unshift(job);_apiFetchPump();},150*job.retries);}
        else job.reject(e);
        _apiFetchPump();
      });
  }
}

function closeConfirm(){$('confirmDlg').classList.remove('show');pendingConfirmFn=null}

function confirmAction(){var fn=pendingConfirmFn;closeConfirm();if(fn)fn()}

function toggleUserMenu(){$('userMenu').classList.toggle('show')}

function queuedFetch(url,opts){
  return new Promise((resolve,reject)=>{
    _apiFetchQueue.push({url,opts,resolve,reject});
    _apiFetchPump();
  });
}

// --- Session keepalive (v7.9.68.5) ---
// The server session is a 30-min SLIDING idle timeout (rbac.cpp's
// RBAC_SESSION_TOKEN_TTL_MS) -- it already renews on every authenticated
// request. But a page with no periodic background polling (e.g. System,
// while reading/filling a form without submitting) can go long stretches
// making no requests at all, so the sliding window silently lapses even
// though the user is still there -- the next click then hits a 401 and
// the login modal reappears, despite the user having been "active" the
// whole time. This pings a cheap, already-authenticated endpoint on a
// timer, but ONLY while real user interaction (mouse/keyboard/touch/
// scroll) has been seen recently -- a genuinely idle tab still logs out
// after ~30 min, unchanged.
//
// BUG-427: two holes made users get logged out well before 30 min idle:
//  1. The session COOKIE was only ever set at login (Max-Age=1800) -- the
//     browser deleted it 30 min after LOGIN regardless of activity; pinging
//     /api/status renewed the server-side window but never the cookie.
//     Now POST /api/session/renew re-sets the cookie with a fresh Max-Age.
//  2. The 5-min setInterval restarted on every page navigation, so a user
//     switching pages more often than every 5 min never pinged at all.
//     Now a 60-s check with the last-renew time shared across pages/tabs
//     (localStorage) renews at most every 4 min, and only if there has been
//     real user activity since the last renew -- so an idle browser is
//     logged out ~25-30 min after the last activity, as intended.
let _lastUserActivityMs=Date.now();  // page load/navigation counts as activity
['mousedown','mousemove','keydown','touchstart','scroll'].forEach(evt=>{
  document.addEventListener(evt,()=>{_lastUserActivityMs=Date.now()},{passive:true});
});
function _sessLastRenew(){try{return parseInt(localStorage.getItem('hfplc_sess_renew')||'0',10)||0}catch(e){return 0}}
function _sessSetRenew(t){try{localStorage.setItem('hfplc_sess_renew',String(t))}catch(e){}}
let _sessRenewLocal=0;  // fallback if localStorage is unavailable
function _sessionKeepalive(){
  const now=Date.now();
  const last=Math.max(_sessLastRenew(),_sessRenewLocal);
  if(_lastUserActivityMs<=last)return;          // no activity since last renew -> let it idle out
  if(last&&now-last<4*60*1000)return;          // renewed recently (this or another page/tab); never -> renew now
  _sessRenewLocal=now;_sessSetRenew(now);
  fetch('/api/session/renew',{method:'POST',credentials:'same-origin'}).catch(()=>{});
}
setTimeout(_sessionKeepalive,5000);
setInterval(_sessionKeepalive,60*1000);

// === BUG-430: én fælles Save-knap ===
// Firmware sender "X-Config-Unsaved: 1|0" på alle API-svar (fingeraftryk af
// den kørende konfiguration mod den sidst gemte). fetch pakkes ind én gang,
// så både api()/queuedFetch og direkte fetch-kald opdaterer knappen.
var _cfgUnsaved=false;
function _setUnsaved(u){
  _cfgUnsaved=u;
  var b=document.getElementById('saveBtn');
  if(!b||b.classList.contains('saving'))return;
  b.classList.toggle('unsaved',u);
  b.innerHTML=u?'&#9679; Save':'&#128190; Save';
  b.title=u?'Der er ugemte ændringer — klik for at gemme til NVS':'Gem konfiguration til NVS';
}
if(!window._cfgFetchWrapped){
  window._cfgFetchWrapped=true;
  var _origFetch=window.fetch;
  window.fetch=function(){
    return _origFetch.apply(this,arguments).then(function(r){
      try{var h=r.headers&&r.headers.get&&r.headers.get('X-Config-Unsaved');if(h==='1'||h==='0')_setUnsaved(h==='1');}catch(e){}
      return r;
    });
  };
}
async function doGlobalSave(){
  var btn=document.getElementById('saveBtn');
  if(!btn)return;
  btn.classList.remove('unsaved');btn.classList.add('saving');btn.textContent='⏳ Gemmer...';
  var ok=false;
  try{
    var r=await fetch('/api/system/save',{method:'POST',credentials:'same-origin'});
    ok=r.ok;
    if(r.status===401){var lm=document.getElementById('loginModal');if(lm)lm.classList.add('show');}
  }catch(e){}
  btn.classList.remove('saving');btn.classList.add(ok?'saved':'save-err');
  btn.textContent=ok?'✅ Gemt!':'❌ Fejl';
  setTimeout(function(){btn.className='save-btn';_setUnsaved(ok?false:_cfgUnsaved);},2000);
  return ok;
}
