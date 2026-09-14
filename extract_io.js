
// BUG-393: login now uses a server-set, HttpOnly session cookie
// (Set-Cookie from POST /api/login) instead of a Bearer token this page
// has to remember itself — the browser sends it automatically on every
// same-origin request. No client-side token state left to lose, which is
// what made BUG-392/389/389b (all iOS-Safari-specific breakage) possible
// in the first place.
let pendingConfirmFn=null;
function $(id){return document.getElementById(id)}

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
    loadAll();
  }).catch(e=>{
    $('loginErr').style.display='block';
    $('loginErr').textContent=(e.message==='acl')
      ? 'Denne IP-adresse er blokeret af IP ACL\'en. Enheden retter en ny, ubekræftet regel automatisk inden for 5 minutter — prøv igen om lidt.'
      : 'Forkert brugernavn eller adgangskode';
  });
}
$('authPass').addEventListener('keydown',e=>{if(e.key==='Enter')doLogin()});

// BUG-369c-mønster: hårdt loft på samtidige forbindelser.
// BUG-394b: denne blok SKAL staa foer det foerste updateUserBadge()/loadAll()
// bootstrap-kald nedenfor — de bruger queuedFetch(), som laeser
// _apiFetchQueue/_apiFetchActive. Disse er `let`/`const` (ikke hoisted som
// funktionserklaeringer), saa et kald FOeR denne blok udfoeres rammer
// midlertidig-dod-zone (TDZ): "Cannot access '_apiFetchQueue' before
// initialization". Fejlen kastes inde i queuedFetch()'s Promise-executor,
// hvilket goer den om til et rejected promise i stedet for en synkron
// exception — resten af scriptet koerer videre uden fejl i konsollen, men
// updateUserBadge()/loadAll()'s stille .catch(()=>{}) sluger fejlen, saa
// badge og alt andet data-load ved sideindlaesning fejler HELT usynligt.
// Ramte tidligere kun /io (og /system, /logs — samme fejl der) fordi
// BUG-393b flyttede updateUserBadge() til FOeRST i bootstrap-raekkefoelgen
// uden at tjekke at koeens deklarationer stod foer det nye kaldested.
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
  return queuedFetch(url,opts).then(r=>{if(r.status===401){$('loginModal').classList.add('show');}return r;});
}

// BUG-393: used to proactively probe /api/status first (with its own
// network-vs-401 distinction to get wrong, see BUG-389/389b). Simpler and
// structurally immune to that bug class: just start loading — if not
// logged in (or the cookie's session expired), the first real request
// 401s and api()'s own handling below shows the modal.
// BUG-393b: updateUserBadge() called FIRST, not after loadAll() — if any
// of loadAll()'s many calls (buildCards/loadCounter/loadTimer/etc) ever
// throws synchronously, a later statement in this same script would never
// run, silently leaving the badge on its default "not logged in" text.
// BUG-396: samme TDZ-moenster som BUG-394b, fundet ved en efterfoelgende
// systemgennemgang. loadAll() -> buildCards() -> counterCardHtml()/
// timerCardHtml() laeser `const HW_MODES`/`EDGE_TYPES`/`TIMER_MODES`, som
// stod deklareret LAENGERE NEDE i scriptet (ved COUNTERS/TIMERS-sektionerne).
// Modsat updateUserBadge()s version (fanget af et .catch() paa et Promise)
// er dette et RENT SYNKRONT kald — kastede en ufanget "Cannot access
// 'HW_MODES' before initialization", som stoppede HELE resten af scriptet
// paa stedet: hverken counters, timere, GPIO-mappings, heartbeat, version-
// footer ELLER click-outside-lukker-menu-listeneren (linje 281, staar efter
// dette kald) blev nogensinde koert ved sideindlaesning. Flyttet hertil,
// foer bootstrap-kaldet.
const HW_MODES=[['sw','Software (poll discrete input)'],['sw_isr','Software-interrupt (GPIO)'],['hw','Hardware PCNT (GPIO)']];
const EDGE_TYPES=[['rising','Stigende flanke'],['falling','Faldende flanke'],['both','Begge flanker']];
const TIMER_MODES=[['ONESHOT','Oneshot (3 faser)'],['MONOSTABLE','Monostable (enkelt-puls)'],['ASTABLE','Astable (blink)'],['INPUT_TRIGGERED','Input-triggered']];
updateUserBadge();
loadAll();

// --- Confirm Dialog ---
function askConfirm(title,msg,fn){$('confirmTitle').textContent=title;$('confirmMsg').textContent=msg;pendingConfirmFn=fn;$('confirmDlg').classList.add('show');}
function closeConfirm(){$('confirmDlg').classList.remove('show');pendingConfirmFn=null}
function confirmAction(){var fn=pendingConfirmFn;closeConfirm();if(fn)fn()}

// --- Alert helper ---
function showAlert(id,type,msg){const el=$(id);el.className='alert alert-'+type;el.textContent=msg;el.style.display='block';setTimeout(()=>{el.style.display='none'},8000);}

// === User Badge ===
function toggleUserMenu(){$('userMenu').classList.toggle('show')}
document.addEventListener('click',function(e){const b=$('userBtn'),m=$('userMenu');if(b&&m&&!b.contains(e.target)&&!m.contains(e.target))m.classList.remove('show')});
function updateUserBadge(){
  // BUG-393b: route through the same queuedFetch() throttle every other
  // request on this page uses — a plain unqueued fetch() here could exceed
  // the device's max_open_sockets alongside loadAll()'s own burst of
  // queued calls and lose the race (BUG-369's original heap-crash class of
  // bug, on this page it just silently left the badge unset instead).
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
// COUNTERS
// ============================================================
// BUG-396: HW_MODES/EDGE_TYPES flyttet til foer bootstrap-kaldet ovenfor (TDZ-fix).
function counterCardHtml(id){
  return '<div class="card" style="grid-column:1/-1">'+
  '<h2><span class="status-dot" id="c'+id+'_dot"></span>Counter '+id+'</h2>'+
  '<label class="chk"><input type="checkbox" id="c'+id+'_enabled"> Aktiveret</label>'+
  '<div class="fld-row">'+
  '<div class="fld"><label>Hardware-tilstand</label><select id="c'+id+'_hwmode" onchange="updateCounterModeUI('+id+')">'+
  HW_MODES.map(m=>'<option value="'+m[0]+'">'+m[1]+'</option>').join('')+'</select></div>'+
  '<div class="fld"><label>Flanke</label><select id="c'+id+'_edge">'+EDGE_TYPES.map(m=>'<option value="'+m[0]+'">'+m[1]+'</option>').join('')+'</select></div>'+
  '</div>'+
  '<div class="fld cmode" data-counter="'+id+'" data-mode="sw"><label>Discrete Input-indeks (poll)</label><input type="number" id="c'+id+'_pin_sw" min="0" max="255"></div>'+
  '<div class="fld cmode" data-counter="'+id+'" data-mode="sw_isr"><label>GPIO-pin (interrupt)</label><input type="number" id="c'+id+'_pin_isr" min="0" max="39"></div>'+
  '<div class="fld cmode" data-counter="'+id+'" data-mode="hw"><label>GPIO-pin (PCNT hardware)</label><input type="number" id="c'+id+'_pin_hw" min="0" max="39"></div>'+
  '<div class="fld-row">'+
  '<div class="fld"><label>Prescaler</label><input type="number" id="c'+id+'_prescaler" min="1"></div>'+
  '<div class="fld"><label>Bit-bredde</label><select id="c'+id+'_bitwidth"><option value="8">8</option><option value="16">16</option><option value="32">32</option><option value="64">64</option></select></div>'+
  '</div>'+
  '<div class="fld-row">'+
  '<div class="fld"><label>Skalafaktor</label><input type="number" id="c'+id+'_scale" step="0.0001"></div>'+
  '<div class="fld"><label>Retning (visning)</label><select id="c'+id+'_direction"><option value="up">Op</option><option value="down">Ned</option></select></div>'+
  '</div>'+
  '<label class="chk"><input type="checkbox" id="c'+id+'_debounce_en" onchange="$(\'c'+id+'_debounce_ms\').disabled=!this.checked"> Debounce</label>'+
  '<div class="fld"><label>Debounce (ms)</label><input type="number" id="c'+id+'_debounce_ms" min="0"></div>'+
  '<div class="subhdr">Compare (tærskel-udløser)</div>'+
  '<label class="chk"><input type="checkbox" id="c'+id+'_cmp_en"> Compare aktiveret</label>'+
  '<div class="fld-row">'+
  '<div class="fld"><label>Sammenlign</label><select id="c'+id+'_cmp_mode"><option value="0">&gt;=</option><option value="1">&gt;</option><option value="2">==</option></select></div>'+
  '<div class="fld"><label>Kilde</label><select id="c'+id+'_cmp_source"><option value="0">Raw</option><option value="1">Prescaled</option><option value="2">Scaled</option></select></div>'+
  '</div>'+
  '<div class="fld"><label>Tærskelværdi</label><input type="number" id="c'+id+'_cmp_value" min="0"></div>'+
  '<label class="chk"><input type="checkbox" id="c'+id+'_reset_on_read"> Nulstil ved læsning</label>'+
  '<div class="subhdr">Register-mapping (auto-tildelt, read-only)</div>'+
  '<div class="info-grid">'+
  '<span class="lbl">Værdi (skaleret)</span><span class="val" id="c'+id+'_reg_value">-</span>'+
  '<span class="lbl">Raw/prescaled</span><span class="val" id="c'+id+'_reg_raw">-</span>'+
  '<span class="lbl">Frekvens</span><span class="val" id="c'+id+'_reg_freq">-</span>'+
  '<span class="lbl">Kontrol</span><span class="val" id="c'+id+'_reg_ctrl">-</span>'+
  '</div>'+
  '<div class="subhdr">Live-værdier</div>'+
  '<div class="info-grid">'+
  '<span class="lbl">Rå tælling</span><span class="val" id="c'+id+'_live_raw">-</span>'+
  '<span class="lbl">Skaleret værdi</span><span class="val" id="c'+id+'_live_value">-</span>'+
  '<span class="lbl">Frekvens (Hz)</span><span class="val" id="c'+id+'_live_freq">-</span>'+
  '<span class="lbl">Status</span><span class="val" id="c'+id+'_live_status">-</span>'+
  '</div>'+
  '<div class="btn-row">'+
  '<button class="btn btn-success btn-sm" onclick="saveCounter('+id+')">Gem</button>'+
  '<button class="btn btn-primary btn-sm" onclick="counterAction('+id+',\'start\')">Start</button>'+
  '<button class="btn btn-warn btn-sm" onclick="counterAction('+id+',\'stop\')">Stop</button>'+
  '<button class="btn btn-danger btn-sm" onclick="counterAction('+id+',\'reset\')">Reset</button>'+
  '<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="loadCounter('+id+')">Opdater</button>'+
  '</div>'+
  '<div class="alert" id="c'+id+'_alert"></div>'+
  '</div>';
}

function updateCounterModeUI(id){
  const mode=$('c'+id+'_hwmode').value;
  document.querySelectorAll('.cmode[data-counter="'+id+'"]').forEach(el=>{el.hidden=(el.dataset.mode!==mode);});
}

function loadCounter(id){
  api('/api/counters/'+id).then(r=>r.json()).then(d=>{
    $('c'+id+'_enabled').checked=!!d.enabled;
    const modeMap={SW:'sw',SW_ISR:'sw_isr',HW_PCNT:'hw',DISABLED:'sw'};
    $('c'+id+'_hwmode').value=modeMap[d.mode]||'sw';
    updateCounterModeUI(id);
    if(d.edge_type!=null)$('c'+id+'_edge').value=['rising','falling','both'][d.edge_type]||'rising';
    if(d.prescaler!=null)$('c'+id+'_prescaler').value=d.prescaler;
    if(d.bit_width!=null)$('c'+id+'_bitwidth').value=d.bit_width;
    if(d.scale_factor!=null)$('c'+id+'_scale').value=d.scale_factor;
    if(d.direction!=null)$('c'+id+'_direction').value=d.direction===1?'down':'up';
    $('c'+id+'_debounce_en').checked=!!d.debounce_enabled;
    $('c'+id+'_debounce_ms').disabled=!d.debounce_enabled;
    if(d.debounce_ms!=null)$('c'+id+'_debounce_ms').value=d.debounce_ms;
    if(d.input_dis!=null)$('c'+id+'_pin_sw').value=d.input_dis;
    if(d.interrupt_pin!=null)$('c'+id+'_pin_isr').value=d.interrupt_pin;
    if(d.hw_gpio!=null)$('c'+id+'_pin_hw').value=d.hw_gpio;
    $('c'+id+'_cmp_en').checked=!!d.compare_enabled;
    if(d.compare_mode!=null)$('c'+id+'_cmp_mode').value=d.compare_mode;
    if(d.compare_source!=null)$('c'+id+'_cmp_source').value=d.compare_source;
    if(d.compare_value!=null)$('c'+id+'_cmp_value').value=d.compare_value;
    $('c'+id+'_reset_on_read').checked=!!d.reset_on_read;
    $('c'+id+'_reg_value').textContent=d.value_reg!=null?('HR'+d.value_reg):'-';
    $('c'+id+'_reg_raw').textContent=d.raw_reg!=null?('HR'+d.raw_reg):'-';
    $('c'+id+'_reg_freq').textContent=d.freq_reg!=null?('HR'+d.freq_reg):'-';
    $('c'+id+'_reg_ctrl').textContent=d.ctrl_reg!=null?('HR'+d.ctrl_reg):'-';
    $('c'+id+'_live_raw').textContent=d.raw!=null?d.raw:'-';
    $('c'+id+'_live_value').textContent=d.value!=null?d.value:'-';
    $('c'+id+'_live_freq').textContent=d.frequency!=null?d.frequency:'-';
    const running=!!d.running;
    $('c'+id+'_dot').className='status-dot'+(running?' on':'');
    let statusTxt=running?'Kører':'Stoppet';
    if(d.overflow)statusTxt+=' · Overflow';
    if(d.compare_triggered)statusTxt+=' · Compare udløst';
    $('c'+id+'_live_status').textContent=statusTxt;
  }).catch(()=>showAlert('c'+id+'_alert','err','Kunne ikke hente counter '+id));
}

function saveCounter(id){
  const hwmode=$('c'+id+'_hwmode').value;
  const body={
    enabled:$('c'+id+'_enabled').checked,
    hw_mode:hwmode,
    edge:$('c'+id+'_edge').value,
    direction:$('c'+id+'_direction').value,
    prescaler:parseInt($('c'+id+'_prescaler').value)||1,
    bit_width:parseInt($('c'+id+'_bitwidth').value),
    scale_factor:parseFloat($('c'+id+'_scale').value)||1,
    debounce_ms:$('c'+id+'_debounce_en').checked?(parseInt($('c'+id+'_debounce_ms').value)||0):0,
    compare_enabled:$('c'+id+'_cmp_en').checked,
    compare_mode:parseInt($('c'+id+'_cmp_mode').value),
    compare_source:parseInt($('c'+id+'_cmp_source').value),
    compare_value:parseInt($('c'+id+'_cmp_value').value)||0,
    reset_on_read:$('c'+id+'_reset_on_read').checked
  };
  if(hwmode==='sw')body.input_dis=parseInt($('c'+id+'_pin_sw').value)||0;
  if(hwmode==='sw_isr')body.interrupt_pin=parseInt($('c'+id+'_pin_isr').value)||0;
  if(hwmode==='hw')body.hw_gpio=parseInt($('c'+id+'_pin_hw').value)||0;
  api('/api/counters/'+id,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(()=>{showAlert('c'+id+'_alert','ok','Counter '+id+' gemt');loadCounter(id);})
  .catch(e=>showAlert('c'+id+'_alert','err','Fejl: '+e.message));
}

function counterAction(id,action){
  const body=action==='start'?{running:true}:action==='stop'?{running:false}:{reset:true};
  api('/api/counters/'+id+'/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{showAlert('c'+id+'_alert','ok','Kommando sendt');setTimeout(()=>loadCounter(id),300);})
  .catch(e=>showAlert('c'+id+'_alert','err','Fejl: '+e.message));
}

// ============================================================
// TIMERS
// ============================================================
// BUG-396: TIMER_MODES flyttet til foer bootstrap-kaldet ovenfor (TDZ-fix).
function timerCardHtml(id){
  return '<div class="card" style="grid-column:1/-1">'+
  '<h2><span class="status-dot" id="t'+id+'_dot"></span>Timer '+id+'</h2>'+
  '<label class="chk"><input type="checkbox" id="t'+id+'_enabled"> Aktiveret</label>'+
  '<div class="fld"><label>Mode</label><select id="t'+id+'_mode" onchange="updateTimerModeUI('+id+')">'+
  TIMER_MODES.map(m=>'<option value="'+m[0]+'">'+m[1]+'</option>').join('')+'</select></div>'+

  '<div class="tmode" data-timer="'+id+'" data-mode="ONESHOT">'+
  '<div class="fld-row"><div class="fld"><label>Fase 1 varighed (ms)</label><input type="number" id="t'+id+'_p1dur" min="0"></div>'+
  '<div class="fld"><label>Fase 1 niveau</label><select id="t'+id+'_p1out"><option value="0">Lav</option><option value="1">Høj</option></select></div></div>'+
  '<div class="fld-row"><div class="fld"><label>Fase 2 varighed (ms)</label><input type="number" id="t'+id+'_p2dur" min="0"></div>'+
  '<div class="fld"><label>Fase 2 niveau</label><select id="t'+id+'_p2out"><option value="0">Lav</option><option value="1">Høj</option></select></div></div>'+
  '<div class="fld-row"><div class="fld"><label>Fase 3 varighed (ms)</label><input type="number" id="t'+id+'_p3dur" min="0"></div>'+
  '<div class="fld"><label>Fase 3 niveau</label><select id="t'+id+'_p3out"><option value="0">Lav</option><option value="1">Høj</option></select></div></div>'+
  '</div>'+

  '<div class="tmode" data-timer="'+id+'" data-mode="MONOSTABLE">'+
  '<div class="fld"><label>Pulsvarighed (ms)</label><input type="number" id="t'+id+'_pulsedur" min="0"></div>'+
  '</div>'+

  '<div class="tmode" data-timer="'+id+'" data-mode="ASTABLE">'+
  '<div class="fld-row"><div class="fld"><label>ON-varighed (ms)</label><input type="number" id="t'+id+'_ondur" min="0"></div>'+
  '<div class="fld"><label>OFF-varighed (ms)</label><input type="number" id="t'+id+'_offdur" min="0"></div></div>'+
  '<div class="fld-row"><div class="fld"><label>ON-niveau</label><select id="t'+id+'_astp1out"><option value="1">Høj</option><option value="0">Lav</option></select></div>'+
  '<div class="fld"><label>OFF-niveau</label><select id="t'+id+'_astp2out"><option value="0">Lav</option><option value="1">Høj</option></select></div></div>'+
  '</div>'+

  '<div class="tmode" data-timer="'+id+'" data-mode="INPUT_TRIGGERED">'+
  '<div class="fld"><label>Coil-indeks (trigger-input, 0-255)</label><input type="number" id="t'+id+'_indis" min="0" max="255"></div>'+
  '<div class="fld-row"><div class="fld"><label>Forsinkelse (ms)</label><input type="number" id="t'+id+'_delay" min="0"></div>'+
  '<div class="fld"><label>Triggerkant</label><select id="t'+id+'_trigedge"><option value="1">Stigende</option><option value="0">Faldende</option></select></div></div>'+
  '<div class="fld"><label>Output-niveau ved trigger</label><select id="t'+id+'_intp1out"><option value="1">Høj</option><option value="0">Lav</option></select></div>'+
  '</div>'+

  '<div class="fld"><label>Output-coil</label><input type="number" id="t'+id+'_outcoil" min="0" max="65535"></div>'+
  '<div class="info-grid"><span class="lbl">Kontrolregister</span><span class="val" id="t'+id+'_reg_ctrl">-</span></div>'+
  '<div class="subhdr">Live-status</div>'+
  '<div class="info-grid">'+
  '<span class="lbl">Output</span><span class="val" id="t'+id+'_live_output">-</span>'+
  '<span class="lbl">Status</span><span class="val" id="t'+id+'_live_status">-</span>'+
  '</div>'+
  '<div class="btn-row">'+
  '<button class="btn btn-success btn-sm" onclick="saveTimer('+id+')">Gem</button>'+
  '<button class="btn btn-primary btn-sm" onclick="timerAction('+id+',\'start\')">Start</button>'+
  '<button class="btn btn-warn btn-sm" onclick="timerAction('+id+',\'stop\')">Stop</button>'+
  '<button class="btn btn-danger btn-sm" onclick="timerAction('+id+',\'reset\')">Reset</button>'+
  '<button class="btn btn-sm" style="background:#45475a;color:#cdd6f4" onclick="loadTimer('+id+')">Opdater</button>'+
  '</div>'+
  '<div class="alert" id="t'+id+'_alert"></div>'+
  '</div>';
}

function updateTimerModeUI(id){
  const mode=$('t'+id+'_mode').value;
  document.querySelectorAll('.tmode[data-timer="'+id+'"]').forEach(el=>{el.hidden=(el.dataset.mode!==mode);});
}

function loadTimer(id){
  api('/api/timers/'+id).then(r=>r.json()).then(d=>{
    $('t'+id+'_enabled').checked=!!d.enabled;
    $('t'+id+'_mode').value=d.mode||'ONESHOT';
    updateTimerModeUI(id);
    if(d.phase1_duration_ms!=null)$('t'+id+'_p1dur').value=d.phase1_duration_ms;
    if(d.phase2_duration_ms!=null)$('t'+id+'_p2dur').value=d.phase2_duration_ms;
    if(d.phase3_duration_ms!=null)$('t'+id+'_p3dur').value=d.phase3_duration_ms;
    if(d.pulse_duration_ms!=null)$('t'+id+'_pulsedur').value=d.pulse_duration_ms;
    if(d.on_duration_ms!=null)$('t'+id+'_ondur').value=d.on_duration_ms;
    if(d.off_duration_ms!=null)$('t'+id+'_offdur').value=d.off_duration_ms;
    if(d.input_dis!=null)$('t'+id+'_indis').value=d.input_dis;
    if(d.delay_ms!=null)$('t'+id+'_delay').value=d.delay_ms;
    if(d.trigger_edge!=null)$('t'+id+'_trigedge').value=d.trigger_edge?1:0;
    if(d.output_coil!=null&&d.output_coil!==0xFFFF)$('t'+id+'_outcoil').value=d.output_coil;
    const p1=d.phase1_output_state?1:0, p2=d.phase2_output_state?1:0, p3=d.phase3_output_state?1:0;
    if($('t'+id+'_p1out'))$('t'+id+'_p1out').value=p1;
    if($('t'+id+'_p2out'))$('t'+id+'_p2out').value=p2;
    if($('t'+id+'_p3out'))$('t'+id+'_p3out').value=p3;
    if($('t'+id+'_astp1out'))$('t'+id+'_astp1out').value=p1;
    if($('t'+id+'_astp2out'))$('t'+id+'_astp2out').value=p2;
    if($('t'+id+'_intp1out'))$('t'+id+'_intp1out').value=p1;
    $('t'+id+'_reg_ctrl').textContent=d.ctrl_reg!=null?('HR'+d.ctrl_reg):'-';
    $('t'+id+'_live_output').textContent=d.output!=null?(d.output?'Høj':'Lav'):'-';
    const running=!!d.running;
    $('t'+id+'_dot').className='status-dot'+(running?' on':'');
    $('t'+id+'_live_status').textContent=running?('Kører (fase '+(d.current_phase!=null?d.current_phase:'-')+')'):'Stoppet';
  }).catch(()=>showAlert('t'+id+'_alert','err','Kunne ikke hente timer '+id));
}

function saveTimer(id){
  const mode=$('t'+id+'_mode').value;
  const body={enabled:$('t'+id+'_enabled').checked,mode:mode};
  const coilVal=$('t'+id+'_outcoil').value;
  if(coilVal!=='')body.output_coil=parseInt(coilVal);
  if(mode==='ONESHOT'){
    body.phase1_duration_ms=parseInt($('t'+id+'_p1dur').value)||0;
    body.phase2_duration_ms=parseInt($('t'+id+'_p2dur').value)||0;
    body.phase3_duration_ms=parseInt($('t'+id+'_p3dur').value)||0;
    body.phase1_output_state=$('t'+id+'_p1out').value==='1';
    body.phase2_output_state=$('t'+id+'_p2out').value==='1';
    body.phase3_output_state=$('t'+id+'_p3out').value==='1';
  }else if(mode==='MONOSTABLE'){
    body.pulse_duration_ms=parseInt($('t'+id+'_pulsedur').value)||0;
  }else if(mode==='ASTABLE'){
    body.on_duration_ms=parseInt($('t'+id+'_ondur').value)||0;
    body.off_duration_ms=parseInt($('t'+id+'_offdur').value)||0;
    body.phase1_output_state=$('t'+id+'_astp1out').value==='1';
    body.phase2_output_state=$('t'+id+'_astp2out').value==='1';
  }else if(mode==='INPUT_TRIGGERED'){
    body.input_dis=parseInt($('t'+id+'_indis').value)||0;
    body.delay_ms=parseInt($('t'+id+'_delay').value)||0;
    body.trigger_edge=$('t'+id+'_trigedge').value==='1';
    body.phase1_output_state=$('t'+id+'_intp1out').value==='1';
  }
  api('/api/timers/'+id,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(()=>{showAlert('t'+id+'_alert','ok','Timer '+id+' gemt');loadTimer(id);})
  .catch(e=>showAlert('t'+id+'_alert','err','Fejl: '+e.message));
}

function timerAction(id,action){
  const body=action==='start'?{running:true}:action==='stop'?{running:false}:{reset:true};
  api('/api/timers/'+id+'/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>{showAlert('t'+id+'_alert','ok','Kommando sendt');setTimeout(()=>loadTimer(id),300);})
  .catch(e=>showAlert('t'+id+'_alert','err','Fejl: '+e.message));
}

// ============================================================
// GPIO STATISK MAPPING
// ============================================================
function pinLabel(pin){
  if(pin>=101&&pin<=108)return 'DI'+(pin-100)+' ('+pin+')';
  if(pin>=201&&pin<=208)return 'DO'+(pin-200)+' ('+pin+')';
  return 'GPIO'+pin;
}
function updateGpioNewDirUI(){
  const isInput=$('gpioNewDir').value==='input';
  $('gpioNewRegWrap').hidden=!isInput;
  $('gpioNewCoilWrap').hidden=isInput;
}
function loadGpioMappings(){
  api('/api/gpio').then(r=>r.json()).then(d=>{
    const rows=Array.isArray(d.gpios)?d.gpios:[];
    if(rows.length===0){
      $('gpioBody').innerHTML='<tr><td colspan="4" style="color:#6c7086;text-align:center">Ingen GPIO-mappings konfigureret</td></tr>';
      return;
    }
    $('gpioBody').innerHTML=rows.map(r=>{
      const bound=(r.direction==='input')?('DI '+r.register):(r.coil!=null?('Coil '+r.coil):'-');
      return '<tr><td>'+pinLabel(r.pin)+'</td><td>'+(r.direction||'-')+'</td><td>'+bound+'</td>'+
        '<td><button class="btn btn-sm btn-danger" onclick="deleteGpioMapping('+r.pin+')">Slet</button></td></tr>';
    }).join('');
  }).catch(()=>{$('gpioBody').innerHTML='<tr><td colspan="4" style="color:#6c7086;text-align:center">Kunne ikke hente GPIO-mappings</td></tr>';});
}
function createGpioMapping(){
  const pin=parseInt($('gpioNewPin').value);
  const dir=$('gpioNewDir').value;
  const body={direction:dir};
  if(dir==='input')body.register=parseInt($('gpioNewReg').value)||0;
  else body.coil=parseInt($('gpioNewCoil').value)||0;
  api('/api/gpio/'+pin+'/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>{
    if(!r.ok)return r.json().then(j=>{throw new Error(j.error||('HTTP '+r.status))});
    return r.json();
  }).then(()=>{showAlert('gpioAlert','ok','GPIO-mapping oprettet');loadGpioMappings();})
  .catch(e=>showAlert('gpioAlert','err','Fejl: '+e.message));
}
function deleteGpioMapping(pin){
  askConfirm('Slet GPIO-mapping','Slet mappingen for pin '+pinLabel(pin)+'?',()=>{
    api('/api/gpio/'+pin,{method:'DELETE'}).then(r=>{
      if(!r.ok)throw new Error('HTTP '+r.status);
      return r.json();
    }).then(()=>{showAlert('gpioAlert','ok','Mapping slettet');loadGpioMappings();})
    .catch(e=>showAlert('gpioAlert','err','Fejl: '+e.message));
  });
}
function loadHeartbeat(){
  api('/api/gpio/2/heartbeat').then(r=>r.json()).then(d=>{
    $('hbEnabled').checked=!d.gpio2_user_mode;
  }).catch(()=>{});
}
function saveHeartbeat(){
  const enabled=$('hbEnabled').checked;
  api('/api/gpio/2/heartbeat',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled})}).then(r=>{
    if(!r.ok)throw new Error('HTTP '+r.status);
    return r.json();
  }).then(()=>showAlert('hbAlert','ok','GPIO2-tilstand opdateret'))
  .catch(e=>{loadHeartbeat();showAlert('hbAlert','err','Fejl: '+e.message);});
}

// ============================================================
// BOOTSTRAP
// ============================================================
function buildCards(){
  let ch='',th='';
  for(let i=1;i<=4;i++){ch+=counterCardHtml(i);th+=timerCardHtml(i);}
  $('counterCards').innerHTML=ch;
  $('timerCards').innerHTML=th;
  $('counterCards').style.display='contents';
  $('timerCards').style.display='contents';
  for(let i=1;i<=4;i++){updateCounterModeUI(i);updateTimerModeUI(i);}
  updateGpioNewDirUI();
}
function loadAll(){
  buildCards();
  for(let i=1;i<=4;i++){loadCounter(i);loadTimer(i);}
  loadGpioMappings();
  loadHeartbeat();
  api('/api/version').then(r=>r.json()).then(d=>{$('footVer').textContent='v'+(d.firmware_version||'-');}).catch(()=>{});
}
