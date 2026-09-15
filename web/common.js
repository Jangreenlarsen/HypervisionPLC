/* AUTO-EXTRACTED shared JS -- see scripts/gzip_web_assets.py.
   Do not duplicate these functions back into individual pages. */

function _dashFetchPump(){
  while(_dashFetchActive<DASH_FETCH_MAX_CONCURRENT && _dashFetchQueue.length>0){
    const job=_dashFetchQueue.shift();
    _dashFetchActive++;
    fetch(job.url,job.opts).then(r=>{_dashFetchActive--;_dashFetchPump();job.resolve(r);})
      .catch(e=>{_dashFetchActive--;_dashFetchPump();job.reject(e);});
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

function dot(el,on){el.className='dot '+(on?'dot-g':'dot-r')}

function badge(el,on,lbl){el.textContent=lbl||'';el.className='badge '+(on?'badge-ok':'badge-off')}

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

function syslogDetail(e){
  if(e.category==='regchange'){
    const kind=e.is_coil?'Coil':'HR';
    return kind+e.reg_addr+': '+e.old_value+' → '+e.new_value;
  }
  return e.message||'';
}

function _apiFetchPump(){
  while(_apiFetchActive<API_FETCH_MAX_CONCURRENT && _apiFetchQueue.length>0){
    const job=_apiFetchQueue.shift();
    _apiFetchActive++;
    fetch(job.url,job.opts).then(r=>{_apiFetchActive--;_apiFetchPump();job.resolve(r);})
      .catch(e=>{_apiFetchActive--;_apiFetchPump();job.reject(e);});
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
let _lastUserActivityMs=Date.now();
['mousedown','mousemove','keydown','touchstart','scroll'].forEach(evt=>{
  document.addEventListener(evt,()=>{_lastUserActivityMs=Date.now()},{passive:true});
});
setInterval(()=>{
  // 25 min, safely under the server's 30-min TTL so the renewal always
  // lands before expiry even with this check's own 5-min granularity.
  if(Date.now()-_lastUserActivityMs<25*60*1000){
    fetch('/api/status',{credentials:'same-origin'}).catch(()=>{});
  }
},5*60*1000);
