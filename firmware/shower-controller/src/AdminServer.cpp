#include "AdminServer.h"

#include <WiFi.h>
#include <esp_task_wdt.h>
#include <SD.h>
#include <mbedtls/base64.h>

#include "Config.h"

namespace {
const char ADMIN_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Camp Shower Admin</title><style>
:root{color-scheme:light dark;--bg:#f3efe4;--ink:#1b2a28;--mut:#6b7c78;--card:#fff;--edge:#e2dccb;--line:#eee8d9;--teal:#0f6b5c;--teal2:#0b5447;--tealx:#b6f0dc;--sec:#e7efec;--in:#fbfaf6;--bd:#cfd8d5;--dng:#c0392b;--dngbg:#fde8e6;--warn:#b8621a;--warnbg:#fff1dc;--ok:#5ec89f;--bad:#e0493f;--or:#f28c28;--or2:#b8621a}
@media(prefers-color-scheme:dark){:root{--bg:#0d1917;--ink:#f3efe4;--mut:#9fb3ae;--card:#16302c;--edge:#08100f;--line:#254740;--sec:#234a43;--in:#0c1c19;--bd:#3a5c55;--dng:#ff9a92;--dngbg:#3a1512;--warn:#ffc85b;--warnbg:#3a2f0f}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.35 system-ui,-apple-system,sans-serif;-webkit-text-size-adjust:100%}
button{font:inherit;cursor:pointer;color:inherit}input,select{font:inherit}
.top{position:sticky;top:0;z-index:5;background:var(--teal);color:#fff;padding:14px 16px 12px;border-radius:0 0 22px 22px;box-shadow:0 8px 24px #0f6b5c44}
.wrap{max-width:720px;margin:0 auto}.row1{display:flex;align-items:center;justify-content:space-between;gap:10px}
.eyebrow{font-size:11px;font-weight:800;letter-spacing:.13em;text-transform:uppercase;color:var(--tealx)}.ttl{font-size:24px;font-weight:900;line-height:1.05}
.back{background:#ffffff22;border:0;color:#fff;border-radius:999px;padding:7px 13px;font-weight:800;font-size:13px;flex:none}
.live{margin-top:12px;background:var(--teal2);border-radius:12px;padding:10px 12px;display:flex;align-items:center;gap:10px;font-weight:700;font-size:14px}
.live .pulse{width:10px;height:10px;border-radius:50%;background:#5ef0b4;box-shadow:0 0 0 4px #5ef0b433;flex:none}
.live.warn{background:#7a4d00}.live.warn .pulse{background:#ffd166;box-shadow:0 0 0 4px #ffd16633}.live.bad{background:#7a1f16}.live.bad .pulse{background:#ff8a80;box-shadow:0 0 0 4px #ff8a8033}
.live small{display:block;font-weight:600;color:var(--tealx);font-size:12px}
.body{padding:16px 16px 90px}
.cta{width:100%;border:0;border-radius:14px;padding:15px;font-size:16px;font-weight:900;background:var(--or);color:#2b1600;box-shadow:0 6px 0 var(--or2);margin-bottom:16px}.cta:active{transform:translateY(3px);box-shadow:0 3px 0 var(--or2)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}@media(min-width:600px){.grid{grid-template-columns:repeat(3,1fr)}}
.tile{background:var(--card);border-radius:18px;padding:14px 14px 12px;min-height:118px;box-shadow:0 2px 0 var(--edge),0 10px 24px #0000000d;display:flex;flex-direction:column;justify-content:space-between;cursor:pointer;border:2px solid transparent}
.tile:hover{border-color:var(--teal)}.tile.static{cursor:default;min-height:0}.tile.static:hover{border-color:transparent}
.tile .ic{font-size:10.5px;font-weight:800;letter-spacing:.1em;color:var(--mut);text-transform:uppercase}
.tile .n{font-size:28px;font-weight:900;color:var(--teal);line-height:1;margin-top:8px;overflow-wrap:anywhere}.tile .n small{font-size:13px;color:var(--mut);margin-left:3px;font-weight:700}.tile .n.txt{font-size:18px}
.tile .l{font-size:12px;color:var(--mut);font-weight:700;margin-top:4px}.tile .l b{color:var(--ink)}
.tile.warn .n{color:var(--warn)}.tile.bad .n{color:var(--dng)}.tile.dim .n{color:var(--mut)}
.dots{display:flex;gap:4px;margin-top:8px}.dots i{width:9px;height:9px;border-radius:50%;background:var(--ok)}.dots i.bad{background:var(--bad)}.dots i.dim{background:var(--bd)}
.card{background:var(--card);border-radius:18px;padding:14px;box-shadow:0 2px 0 var(--edge),0 10px 24px #0000000d;margin-bottom:12px}.card.off{opacity:.5;pointer-events:none}
h3{margin:0 0 8px;font-size:15px;font-weight:900;display:flex;justify-content:space-between;align-items:baseline;gap:8px}h3 small{color:var(--mut);font-weight:600;font-size:12px;text-align:right}
.muted{color:var(--mut);font-size:12.5px}.field{display:flex;gap:8px;margin-top:8px}
input[type=text],input[type=number],input[type=password],select{flex:1;min-width:0;width:100%;padding:10px 11px;border:1.5px solid var(--bd);border-radius:10px;background:var(--in);color:var(--ink)}
.btn{border:0;border-radius:10px;padding:10px 13px;background:var(--teal);color:#fff;font-weight:800;font-size:13.5px;white-space:nowrap}
.btn.sec{background:var(--sec);color:var(--teal)}.btn.dng{background:var(--dngbg);color:var(--dng)}.btn.arm{background:var(--dng);color:#fff}.btn.sm{padding:6px 10px;font-size:12.5px}.btn[disabled]{opacity:.4;pointer-events:none}
.acts{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px;align-items:center}
.mem{display:flex;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--line);cursor:pointer}.mem:first-of-type{border-top:0}.mem.static{cursor:default}
.av{width:34px;height:34px;border-radius:50%;background:var(--sec);color:var(--teal);font-weight:900;display:flex;align-items:center;justify-content:center;flex:none;font-size:14px}.av.off{background:var(--warnbg);color:var(--warn)}
.who{flex:1;min-width:0}.who b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.who span{font-size:12px;color:var(--mut)}
.use{text-align:right;flex:none}.use b{display:block;color:var(--teal)}.use span{font-size:11.5px;color:var(--mut)}.chev{color:var(--bd);font-size:18px}
.editor{background:var(--in);border:1.5px solid var(--bd);border-radius:12px;padding:12px;margin:0 0 10px}
label{display:block;font-size:12px;font-weight:700;color:var(--mut);margin:8px 0 4px}.editor label:first-child{margin-top:0}
.sw{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:8px 0;cursor:pointer}
.tog{width:42px;height:24px;border-radius:12px;background:var(--teal);position:relative;flex:none}.tog::after{content:"";position:absolute;top:3px;right:3px;width:18px;height:18px;border-radius:50%;background:#fff}.tog.off{background:var(--bd)}.tog.off::after{right:auto;left:3px}
.range{width:100%;accent-color:var(--teal);margin:6px 0}.kv{display:grid;grid-template-columns:auto 1fr;gap:5px 12px;font-size:13px}.kv span:nth-child(odd){color:var(--mut)}
.seg{display:flex;gap:6px;margin:8px 0 4px}.seg i{flex:1;height:10px;border-radius:5px;background:var(--edge)}.seg i.on{background:var(--teal)}
.tag{display:inline-block;font-size:10.5px;font-weight:800;padding:2px 7px;border-radius:999px;background:var(--sec);color:var(--teal);white-space:nowrap}.tag.warn{background:var(--warnbg);color:var(--warn)}.tag.bad{background:var(--dngbg);color:var(--dng)}.tag.dim{background:var(--line);color:var(--mut)}
.pills{display:flex;gap:6px;flex-wrap:wrap;margin:6px 0 10px}
table{width:100%;border-collapse:collapse;font-size:13px}td,th{text-align:left;padding:7px 4px;border-bottom:1px solid var(--line)}th{color:var(--mut);font-size:11.5px;text-transform:uppercase;letter-spacing:.06em}td.r,th.r{text-align:right}td input{padding:8px}
.big{font-size:22px;font-weight:900;color:var(--teal);margin:4px 0}.big.warn{color:var(--warn)}.big.bad{color:var(--dng)}.big.dim{color:var(--mut)}
.steps{margin:6px 0 0;padding-left:18px;font-size:13px;color:var(--mut)}.steps li{margin:3px 0}.uid{font:11px ui-monospace,monospace;color:var(--mut);margin-top:6px}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);max-width:90vw;background:var(--teal2);color:#fff;padding:11px 16px;border-radius:12px;font-weight:700;font-size:14px;box-shadow:0 8px 24px #0006;z-index:9;opacity:0;transition:opacity .2s;pointer-events:none}#toast.on{opacity:1}#toast.bad{background:#7a1f16}
</style></head><body><div id="top" class="top"></div><div id="body" class="body wrap"></div><div id="toast"></div><script>
const $=s=>document.querySelector(s),esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])),F=(v,d)=>Number(v||0).toFixed(d),DS=['OPEN','IN USE','UNAVAILABLE'],LIM=[['Shower','shower'],['Water fill','water'],['RV fill','rv']];
const st={view:'home',sid:0,R:null,edit:null,edName:'',edAllowance:0,edEnabled:true,name:'',at:'',known:'1',L:null,pw:'',q:'',cf:null,vol:null};
let O=null,S=[],last='',lastHead='',busy=false,forceNext=false,down=false,lastOk=0,toastT=0;
const typing=()=>{const a=document.activeElement;return !!a&&/^(INPUT|SELECT|TEXTAREA)$/.test(a.tagName)&&a.type!='file'&&$('#body').contains(a)};
const nm=id=>(S.find(x=>x.id==id)||{}).name||'Station '+id,dur=s=>Math.floor(s/60)+':'+String(s%60).padStart(2,'0'),up=s=>{const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?h+'h '+m+'m':m+'m'};
const rtag=r=>`<span class="tag ${/LIMIT|HANDOFF/.test(r)?'warn':/TIMEOUT|REBOOT|ERROR|RECOVERY/.test(r)?'bad':''}">${esc(r)}</span>`;
const cbtn=(id,label,cls,x='',dis=false)=>`<button class="${cls}${st.cf===id?' arm':''}" data-a="cf" data-v="${id}" data-x="${x}"${dis?' disabled':''}>${st.cf===id?'Tap again to confirm':label}</button>`;
const tile=(n,u,l)=>`<div class="tile static"><div><div class="n">${n}${u?`<small>${u}</small>`:''}</div><div class="l">${l}</div></div></div>`;
const lim=s=>{const o={};LIM.forEach(([n,k])=>{o[k+'Gal']=s.limits[k].gal;o[k+'Min']=s.limits[k].min});return o};
function toast(m,bad,stick){const t=$('#toast');t.textContent=m;t.className='on'+(bad?' bad':'');clearTimeout(toastT);if(!stick)toastT=setTimeout(()=>t.className='',bad?5000:3000)}
async function post(path,data={}){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const j=await r.json();if(!r.ok)throw Error(j.message||'Request failed');return j}
async function cmd(action,extra={},station=st.sid||O.status.stationId){try{const j=await post('/api/command',{station,action,...extra});let m=j.message,ok=true;if(j.pending){toast('Sending to '+nm(station)+'…',false,true);m='';for(let i=0;i<13&&!m;i++){await new Promise(x=>setTimeout(x,300));const q=await(await fetch('/api/command?nonce='+j.nonce)).json();if(q.state=='done'){ok=q.ok;m=(q.ok?'':'Rejected: ')+(q.message||'')}else if(q.state!='pending')break}if(!m){ok=false;m='No answer from '+nm(station)}}toast(nm(station)+': '+m,!ok);refresh(true);return ok}catch(e){toast(e.message,true);return false}}
function stationState(x){const t=x.telemetry;return x.alarm?'DARK MID-SESSION':!x.online?'OFFLINE':t.calibrationActive?'CALIBRATING':(t.relay||{}).testActive?'RELAY TEST':t.enrollmentPending?'ENROLLING':t.session.active?'IN USE':DS[t.session.doorState]||'?'}
function stationCls(x){const t=x.telemetry,h=t.health;return x.alarm?'bad':!x.online?'dim':!(h.hub&&h.relay&&h.rfid&&h.sd)||t.session.doorState==2?'bad':t.session.active||t.calibrationActive||t.enrollmentPending||(t.relay||{}).testActive?'warn':''}
function head(){const s=O&&O.status,t={home:'Shower Admin',members:'Members',water:'Water use',camp:'Camp settings',station:nm(st.sid)}[st.view];let live;const al=S.filter(x=>x.alarm);
if(al.length)live=`<div class="live bad"><span class="pulse"></span><span>⚠ ${al.map(x=>esc(x.name)).join(' · ')} dark mid-session — CHECK WATER<small>${al.map(x=>`${esc(x.telemetry.session.name)||'session'} · ${F(x.telemetry.session.gallons,1)} gal when lost · silent ${up(x.lastSeenS)}`).join(' · ')}</small></span></div>`;
else if(!O)live=`<div class="live warn"><span class="pulse"></span><span>Connecting…<small>waiting for the controller</small></span></div>`;
else if(Date.now()-lastOk>7000)live=`<div class="live bad"><span class="pulse"></span><span>Controller not responding<small>retrying — last update ${Math.round((Date.now()-lastOk)/1000)} s ago</small></span></div>`;
else{const w=S.find(x=>x.online&&x.telemetry.enrollmentPending),act=S.filter(x=>x.online&&x.telemetry.session.active),on=S.filter(x=>x.online);let bad;
if(w)live=`<div class="live warn"><span class="pulse"></span><span>Waiting for ${esc(w.telemetry.pendingName)}'s wristband<small>tap it on ${esc(w.name)}'s reader now</small></span></div>`;
else if(act.length)live=`<div class="live"><span class="pulse"></span><span>${act.map(x=>`${esc(x.telemetry.session.name)} at ${esc(x.name)}`).join(' · ')}<small>${act.map(x=>`${F(x.telemetry.session.gallons,1)} of ${F(x.telemetry.session.limit,0)} gal${x.telemetry.session.pumpOn?'':' · water off'}`).join(' · ')}</small></span></div>`;
else if((bad=on.filter(x=>stationCls(x)=='bad')).length)live=`<div class="live warn"><span class="pulse"></span><span>${bad.map(x=>esc(x.name)).join(' · ')} need${bad.length==1?'s':''} attention<small>${bad.map(x=>{const h=x.telemetry.health;return [['hub',h.hub],['relay',h.relay],['RFID',h.rfid],['SD',h.sd]].filter(p=>!p[1]).map(p=>p[0]).join(', ')||'unavailable'}).join(' · ')} — tap the station tile</small></span></div>`;
else live=`<div class="live"><span class="pulse"></span><span>${on.every(x=>x.telemetry.session.doorState==0)?'All stations open':'Ready'}<small>${on.length} of ${S.length} station${S.length==1?'':'s'} online · tap a wristband to start</small></span></div>`}
return `<div class="wrap"><div class="row1"><div><div class="eyebrow">Camp shower${s?' · on '+esc(s.station):''}</div><div class="ttl">${esc(t)}</div></div>${st.view!='home'?'<button class="back" data-a="go" data-v="home">‹ Home</button>':''}</div>${live}</div>`}
function home(){const m=O.members,dis=m.filter(x=>!x.enabled).length,L=O.status.limits;let g=0,n=0;m.forEach(x=>{g+=x.networkGallons;n+=x.networkSessions});
return `<button class="cta" data-a="enroll">＋ Enroll a wristband</button><div class="grid">
<div class="tile" data-a="go" data-v="members"><div class="ic">Members</div><div><div class="n">${m.length}</div><div class="l">${dis?`<b>${dis} disabled</b>`:m.length?'all enabled':'none enrolled yet'}</div></div></div>
<div class="tile" data-a="go" data-v="water"><div class="ic">Water</div><div><div class="n">${F(g,1)}<small>gal</small></div><div class="l">${n} session${n==1?'':'s'} this burn</div></div></div>
${S.map(x=>{const t=x.telemetry,se=t.session,h=t.health,d=[h.hub,h.relay,h.rfid,h.sd].concat(t.features.music?[t.speaker=='connected']:[]);
const l=x.alarm?`<b>${esc(se.name)}</b> · ${F(se.gallons,1)} gal · dark ${up(x.lastSeenS)}`:!x.online?`last seen ${up(x.lastSeenS)} ago`:se.active?`<b>${esc(se.name)}</b> · ${F(se.gallons,1)} of ${F(se.limit,0)} gal`:t.enrollmentPending?`waiting for <b>${esc(t.pendingName)}</b>`:x.recent.length?`last <b>${F(x.recent[0].gallons,1)} gal</b> · up ${up(h.uptimeS)}`:`up ${up(h.uptimeS)} · no sessions yet`;
return `<div class="tile ${stationCls(x)}" data-a="go" data-v="station" data-i="${x.id}"><div class="ic">${esc(x.name)}${x.local?' · here':''}</div><div><div class="n txt">${stationState(x)}</div><div class="dots">${d.map(v=>`<i class="${!x.online?'dim':v?'':'bad'}"></i>`).join('')}</div><div class="l">${l}</div></div></div>`}).join('')}
<div class="tile" data-a="go" data-v="camp"><div class="ic">Camp settings</div><div><div class="n txt">${F(L.shower.gal,0)} · ${F(L.water.gal,0)} · ${F(L.rv.gal,0)} gal</div><div class="l">shower · water · RV limits</div></div></div></div>`}
function members(){const m=[...O.members].sort((a,b)=>a.name.localeCompare(b.name)),w=S.find(x=>x.online&&x.telemetry.enrollmentPending),q=st.q.trim().toLowerCase(),list=q?m.filter(x=>x.name.toLowerCase().includes(q)):m,at=st.at||O.status.stationId;
return `<div class="card"><h3>Enroll a wristband <small>${m.length} enrolled</small></h3>${w?`<div class="muted">Waiting for <b>${esc(w.telemetry.pendingName)}</b> — tap the wristband on <b>${esc(w.name)}</b>'s reader.</div><div class="acts"><button class="btn sec" data-a="cancel" data-v="${w.id}">Cancel enrollment</button></div>`:
`<label for="name">Member name</label><input id="name" type="text" maxlength="32" placeholder="e.g. Dusty River" value="${esc(st.name)}" data-k="name" autocomplete="off"><label for="at">Enroll on (the reader you are standing at)</label><div class="field" style="margin-top:0"><select id="at" data-k="at">${S.map(x=>`<option value="${x.id}"${x.id==at?' selected':''}>${esc(x.name)}${x.online?'':' (offline)'}</option>`).join('')}</select><button class="btn" data-a="arm">Enroll</button></div><div class="muted" style="margin-top:8px">Type a name, tap Enroll, then tap the new wristband on that station's reader.</div>`}</div>
<div class="card"><h3>Members <small>tap a member to edit</small></h3>${m.length>8?`<input id="q" type="text" placeholder="Search…" value="${esc(st.q)}" data-k="q" style="margin-bottom:6px">`:''}${list.length?list.map(memRow).join(''):`<div class="muted">No members ${q?'match':'enrolled yet'}.</div>`}</div>`}
function memRow(x){const open=st.edit===x.uid,row=`<div class="mem" data-a="edit" data-v="${esc(x.uid)}"><div class="av${x.enabled?'':' off'}">${esc((x.name||'?')[0].toUpperCase())}</div><div class="who"><b>${esc(x.name)}</b><span>${x.enabled?'':'disabled · '}${x.allowance>0?F(x.allowance,1)+' gal shower limit':'station limits'}</span></div><div class="use"><b>${F(x.networkGallons,1)} gal</b><span>${x.networkSessions} session${x.networkSessions==1?'':'s'}</span></div><span class="chev">${open?'⌄':'›'}</span></div>`;
if(!open)return row;return row+`<div class="editor"><label for="edName">Name</label><input id="edName" type="text" maxlength="32" value="${esc(st.edName)}" data-k="edName"><label for="edAllowance">Shower limit in gallons (0 = station limit)</label><input id="edAllowance" type="number" min="0" max="500" step="0.5" value="${esc(st.edAllowance)}" data-k="edAllowance">
<div class="sw" data-a="edToggle"><span><b>Can start sessions</b><br><span class="muted">Off keeps history but blocks the wristband</span></span><span class="tog${st.edEnabled?'':' off'}"></span></div>
<div class="muted">Showers ${F(x.showerGallons,1)} · water fill ${F(x.waterGallons,1)} · RV ${F(x.rvGallons,1)} gal</div><div class="uid">UID ${esc(x.uid)}</div>
<div class="acts"><button class="btn" data-a="save">Save</button><button class="btn sec" data-a="edit" data-v="${esc(x.uid)}">Cancel</button><span style="flex:1"></span>${cbtn('del','Delete','btn dng sm')}</div></div>`}
const recentTable=x=>x.recent.length?`<table><thead><tr><th>Member</th><th class="r">Gal</th><th class="r">Time</th><th class="r">Ended</th></tr></thead><tbody>${x.recent.map(r=>`<tr><td>${esc(r.name)}</td><td class="r">${F(r.gallons,2)}</td><td class="r">${dur(r.durationS)}</td><td class="r">${rtag(r.reason)}</td></tr>`).join('')}</tbody></table>`:'<div class="muted">No completed sessions</div>';
function water(){const m=[...O.members].sort((a,b)=>b.networkGallons-a.networkGallons);let g=0,n=0;m.forEach(x=>{g+=x.networkGallons;n+=x.networkSessions});
return `<div class="grid" style="margin-bottom:12px">${tile(F(g,1),'gal','total this burn')}${tile(n,'','sessions')}${tile(n?F(g/n,1):'—','gal','average per session')}</div>
<div class="card"><h3>By member <small>camp-wide gallons</small></h3><table><thead><tr><th>Member</th><th class="r">Shower</th><th class="r">Water</th><th class="r">RV</th><th class="r">Total</th></tr></thead><tbody>${m.length?m.map(x=>`<tr><td>${esc(x.name)}</td><td class="r">${F(x.showerGallons,1)}</td><td class="r">${F(x.waterGallons,1)}</td><td class="r">${F(x.rvGallons,1)}</td><td class="r"><b>${F(x.networkGallons,1)}</b></td></tr>`).join(''):'<tr><td colspan="5" class="muted">No members yet</td></tr>'}</tbody></table></div>
${S.map(x=>`<div class="card"><h3>${esc(x.name)} <small>recent sessions</small></h3>${recentTable(x)}</div>`).join('')}`}
function station(){const x=S.find(s=>s.id==st.sid);if(!x)return'<div class="card"><div class="muted">This station has not been heard on CampNet yet.</div><div class="acts"><button class="btn sec" data-a="go" data-v="home">Home</button></div></div>';
const t=x.telemetry,se=t.session,h=t.health,on=x.online,off=on?'':' off',loc=x.local,mus=t.features.music,spk=t.speaker=='connected',c=[];
c.push(`<div class="card"><h3>${esc(x.roleName)} station <small>${loc?'you are connected here':on?'online via CampNet':'offline · last seen '+up(x.lastSeenS)+' ago'}</small></h3><div class="big ${stationCls(x)}">${stationState(x)}</div>${se.active?`<div>${esc(se.name)} · <b>${F(se.gallons,2)}</b> of ${F(se.limit,1)} gal · water ${se.pumpOn?'<b>ON</b>':'off'}</div><div class="acts">${cbtn('end','End session','btn dng')}</div>`:`<div class="muted">${esc(t.message)}</div>`}</div>`);
c.push(`<div class="card${off}"><h3>Recent sessions <small>newest first</small></h3>${recentTable(x)}</div>`);
c.push(`<div class="card${off}"><h3>Flow calibration <small>${F(t.pulsesPerGallon,2)} pulses / gal</small></h3><ol class="steps"><li>Put the outlet in a known-volume container</li><li>Start dispensing (pump runs, 10 min max)</li><li>Enter the measured gallons and stop</li></ol><label for="known">Measured volume (gallons)</label><div class="field" style="margin-top:0"><input id="known" type="number" min="0.01" max="100" step="0.01" value="${esc(st.known)}" data-k="known"><button class="btn" data-a="calStart"${t.calibrationActive?' disabled':''}>Start</button><button class="btn sec" data-a="calStop"${t.calibrationActive?'':' disabled'}>Stop &amp; save</button></div><div class="muted" style="margin-top:8px">${esc(t.calibrationMessage)} · ${t.calibrationPulses} pulses</div></div>`);
if(mus){const vol=st.vol!=null?st.vol:t.speakerVolume,cal=t.musicCalibrationActive;
c.push(`<div class="card${off}"><h3>Speaker <small>${esc(t.speaker)} · ${esc(t.audioPlayback)}</small></h3><input class="range" type="range" min="0" max="100" step="1" value="${vol}" data-k="vol"><div class="muted">Volume <b id="volv">${vol}</b>% · channel 1 track ${t.audioFile?'ready':'missing'}</div><div class="acts"><button class="btn" data-a="tone"${spk?'':' disabled'}>Test tone</button><button class="btn" data-a="play"${spk&&t.audioFile?'':' disabled'}>Play ch. 1</button><button class="btn sec" data-a="stop">Stop</button><button class="btn sec" data-a="findSpeaker">Find speaker</button></div>${loc?`<label for="audioFile">Replace channel 1 (44.1 kHz stereo 16-bit PCM)</label><div class="field" style="margin-top:0"><input id="audioFile" type="file" style="flex:1;min-width:0"><button class="btn sec" data-a="upload">Upload</button></div>`:`<div class="muted" style="margin-top:8px">Audio upload is local only — join ${esc(x.name)}'s Wi-Fi to replace its track.</div>`}<div class="muted" style="margin-top:6px">Reconnects to a speaker named “Select 4 Go”.</div></div>`);
c.push(`<div class="card${off}"><h3>Music knob <small>raw ${t.musicKnobRaw} / 4095</small></h3><div class="seg">${t.musicPositions.map((p,i)=>`<i class="${i==t.musicChannel?'on':''}"></i>`).join('')}</div><div><b>${t.musicChannel} · ${esc(t.musicChannelName)}</b> <span class="tag${t.musicKnobCalibrated?'':' warn'}">${t.musicKnobCalibrated?'calibrated':'test thresholds'}</span></div>${cal?`<div class="muted" style="margin-top:8px">Calibrating — turn the knob to notch <b>${t.musicCalibrationNext}</b> and capture. Notch 0 is quiet, 1–9 are channels.</div><div class="acts"><button class="btn" data-a="musicCalCapture">Capture notch ${t.musicCalibrationNext}</button><button class="btn sec" data-a="musicCalCancel">Cancel</button></div>`:`<div class="muted" style="margin-top:8px">Captured: ${t.musicPositions.map((p,i)=>i+':'+(p==null?'—':p)).join(' · ')}</div><div class="acts"><button class="btn sec" data-a="musicCalStart">Recalibrate knob</button></div>`}</div>`)}
const rr=t.relay||{},rs=!!t.features.relayConfig,idle=!se.active&&!t.calibrationActive&&!rr.testActive,onCh=ch=>!!(rr.state&(1<<(4-ch)));
if(rs){const R=st.R&&st.R.sid==x.id?st.R:{pump:rr.pump,charger:rr.charger,accessory:rr.accessory},roles={};if(rr.pump)roles[rr.pump]='pump';if(rr.charger)roles[rr.charger]='charger';if(rr.accessory)roles[rr.accessory]='accessory';
const sel=(k,none)=>`<select data-k="R.${k}"${idle?'':' disabled'}>${none?`<option value="0"${R[k]==0?' selected':''}>Not assigned</option>`:''}${[1,2,3,4].map(n=>`<option value="${n}"${R[k]==n?' selected':''}>Relay ${n}</option>`).join('')}</select>`;
c.push(`<div class="card${off}"><h3>Relay &amp; power <small>${rr.testActive?'testing relay '+rr.testChannel:idle?'idle · commanded states':'busy — wait for idle'}</small></h3><div class="pills">${[1,2,3,4].map(ch=>`<span class="tag${onCh(ch)?'':' dim'}">${ch} · ${roles[ch]||'unassigned'} · ${onCh(ch)?'ON':'off'}</span>`).join('')}</div>
<div class="kv"><span>Pump</span>${sel('pump')}<span>Phone charger</span>${sel('charger',1)}<span>LED / speaker / display</span>${sel('accessory',1)}</div>
<div class="acts">${cbtn('relays','Save assignments','btn','',!idle)}${cbtn('acc',rr.accessoryEnabled?'Turn accessory off':'Turn accessory on','btn sec','',!idle)}</div>
<div class="muted" style="margin-top:8px">Saving briefly clears every output, then restores accessory power. Assigned roles must use different relays.</div>
<div class="acts">${[1,2,3,4].map(n=>cbtn('rt'+n,'Test '+n,'btn sec sm','',!idle||!h.relay)).join('')}<button class="btn dng sm" data-a="relayTestStop"${rr.testActive?'':' disabled'}>Stop test</button></div>
<div class="muted" style="margin-top:8px">A test energizes one physical relay for up to 5 s — it may run the pump or an unknown load. Outputs are commanded only; loads are not electrically monitored.</div></div>`)}
const pill=(l,ok)=>`<span class="tag${ok?'':' bad'}">${l} ${ok?'OK':'DOWN'}</span>`;
c.push(`<div class="card${off}"><h3>Controller health <small>up ${up(h.uptimeS)}</small></h3><div class="pills">${pill('Hub',h.hub)}${pill('Relay',h.relay)}${pill('RFID',h.rfid)}${pill('SD',h.sd)}${mus?`<span class="tag${spk?'':' warn'}">Speaker ${spk?'OK':esc(t.speaker)}</span>`:''}</div><div class="kv"><span>Heap</span><span>${Math.round(h.freeHeap/1024)} k free · low ${Math.round(h.minFreeHeap/1024)} k</span><span>Wi-Fi clients</span><span>${h.wifiClients}</span>${mus?`<span>Audio underruns</span><span>${h.audioUnderruns}</span>`:''}${rs?`<span>Charger</span><span>${rr.charger?(onCh(rr.charger)?'commanded on':'commanded off'):'not assigned'} · unmonitored</span><span>Accessory rail</span><span>${rr.accessory?(rr.accessoryEnabled?'enabled':'disabled')+' · '+(onCh(rr.accessory)?'commanded on':'commanded off'):'not assigned'} · unmonitored</span>`:''}${loc?`<span>Address</span><span>${esc(O.status.ip)} · ${esc(O.status.ssid)}</span>`:''}</div><div class="acts">${cbtn('reboot','Reboot controller','btn dng')}</div><div class="muted" style="margin-top:8px">Reboot stops the pump first and logs any open session as REBOOT.</div></div>`);
return c.join('')}
function camp(){const s=O.status,L=st.L||lim(s);
return `<div class="card"><h3>Station limits <small>per session · synced everywhere</small></h3><table><thead><tr><th>Kind</th><th>Gallons</th><th>Minutes</th></tr></thead><tbody>${LIM.map(([n,k])=>`<tr><td>${n}</td><td><input type="number" min="0.5" max="500" step="0.5" value="${esc(L[k+'Gal'])}" data-k="L.${k}Gal"></td><td><input type="number" min="1" max="180" value="${esc(L[k+'Min'])}" data-k="L.${k}Min"></td></tr>`).join('')}</tbody></table><div class="acts"><button class="btn" data-a="saveLimits">Save limits</button></div><div class="muted" style="margin-top:8px">A member's own shower limit overrides the shower value; fills always use the station limit. Gallons 0.5–500, minutes 1–180. Version ${s.limits.version}.</div></div>
<div class="card"><h3>CampNet <small>channel ${s.net.channel} · rx ${s.net.rx} · tx ${s.net.tx}${s.net.txFail?' · '+s.net.txFail+' failed':''}</small></h3>${S.map(x=>`<div class="mem static"><div class="av${x.online?'':' off'}">${x.id}</div><div class="who"><b>${esc(x.name)}${x.local?' · here':''}</b><span>${esc(x.roleName)}</span></div><div class="use"><b>${x.online?'online':'offline'}</b><span>${x.local?esc(s.ssid)+' · '+esc(s.ip):x.online?'via CampNet':'seen '+up(x.lastSeenS)+' ago'}</span></div></div>`).join('')}<div class="muted" style="margin-top:8px">Members v${s.membersVersion} · limits v${s.limits.version}</div></div>
${s.pagePassword?`<div class="card"><h3>Admin password</h3><label for="pw">New password (8–64 characters)</label><div class="field" style="margin-top:0"><input id="pw" type="password" minlength="8" maxlength="64" value="${esc(st.pw)}" data-k="pw"><button class="btn sec" data-a="password">Update</button></div><div class="muted" style="margin-top:8px">One password for every station; you will be asked to sign in again.</div></div>`:''}`}
function render(force){const h=head();if(force||h!==lastHead){$('#top').innerHTML=h;lastHead=h}if(!O)return;
if(!force&&(down||typing()))return;
const b=({members,water,camp,station}[st.view]||home)();if(!force&&b===last)return;
const a=document.activeElement,id=a&&a.id,ss=a&&a.selectionStart;$('#body').innerHTML=b;last=b;
if(id){const e=document.getElementById(id);if(e){e.focus();try{if(ss!=null)e.setSelectionRange(ss,ss)}catch(_){}}}}
function route(){const h=location.hash.slice(1).split('/');st.view=['members','water','camp','station'].includes(h[0])?h[0]:'home';st.sid=st.view=='station'?+h[1]||0:0;st.cf=null;st.R=null;window.scrollTo(0,0);render(true)}
const go=(v,id)=>{location.hash=v+(id?'/'+id:'')};
async function refresh(force){if(force)forceNext=true;if(busy)return;busy=true;const ctl=new AbortController(),tm=setTimeout(()=>ctl.abort(),5000),mv=O&&O.status?O.status.membersVersion:-1;try{const r=await fetch('/api/overview?membersVersion='+mv,{signal:ctl.signal});if(!r.ok)throw 0;const n=await r.json();if(n.members===null){if(!O||!Array.isArray(O.members))throw 0;n.members=O.members}O=n;S=O.stations;lastOk=Date.now();if(st.edit&&!O.members.some(x=>x.uid===st.edit))st.edit=null}catch(e){}finally{clearTimeout(tm);busy=false;const f=forceNext;forceNext=false;render(f)}}
const ACT={go:(v,el)=>go(v,el.dataset.i),enroll:()=>{go('members');setTimeout(()=>{const e=$('#name');e&&e.focus()},50)},
arm:async()=>{const n=st.name.trim();if(!n)return toast('Enter a member name first',true);document.activeElement&&document.activeElement.blur();if(await cmd('enroll',{name:n},+(st.at||O.status.stationId))){st.name='';render(true)}},
cancel:v=>cmd('cancel',{},+v),
edit:v=>{if(st.edit===v)st.edit=null;else{const x=O.members.find(m=>m.uid===v);if(!x)return;st.edit=v;st.edName=x.name;st.edAllowance=x.allowance;st.edEnabled=x.enabled}st.cf=null;render(true)},
edToggle:()=>{st.edEnabled=!st.edEnabled;render(true)},
save:async()=>{const n=st.edName.trim();if(!n)return toast('Name cannot be empty',true);try{await post('/api/member',{uid:st.edit,name:n,allowance:st.edAllowance||0,enabled:st.edEnabled?'1':'0'});st.edit=null;toast('Member saved');refresh(true)}catch(e){toast(e.message,true)}},
cf:(v,el)=>{if(st.cf===v){st.cf=null;render(true);CF[v](el.dataset.x)}else{st.cf=v;setTimeout(()=>{if(st.cf===v){st.cf=null;render(true)}},4000);render(true)}},
relayTestStop:()=>cmd('relayTestStop'),calStart:()=>cmd('calStart'),calStop:()=>cmd('calStop',{gallons:st.known}),tone:()=>cmd('tone'),play:()=>cmd('play'),stop:()=>cmd('stop'),findSpeaker:()=>cmd('findSpeaker'),musicCalStart:()=>cmd('musicCalStart'),musicCalCapture:()=>cmd('musicCalCapture'),musicCalCancel:()=>cmd('musicCalCancel'),
upload:async()=>{const f=$('#audioFile').files[0];if(!f)return toast('Choose a PCM file first',true);toast('Uploading '+f.name+'…',false,true);const b=new FormData();b.append('audio',f);try{const r=await fetch('/api/audio/upload',{method:'POST',body:b});const j=await r.json();toast(j.message||(r.ok?'Uploaded':'Upload failed'),!r.ok);refresh(true)}catch(e){toast('Upload failed',true)}},
saveLimits:async()=>{try{await post('/api/limits',st.L||lim(O.status));st.L=null;toast('Station limits saved');refresh(true)}catch(e){toast(e.message,true)}},
password:async()=>{try{await post('/api/password',{password:st.pw});toast('Password changed — sign in again');setTimeout(()=>location.reload(),1500)}catch(e){toast(e.message,true)}}};
const CF={relays:()=>{const x=S.find(s=>s.id==st.sid),rr=x.telemetry.relay,R=st.R&&st.R.sid==x.id?st.R:{pump:rr.pump,charger:rr.charger,accessory:rr.accessory},used=[R.pump,R.charger,R.accessory].filter(Boolean);if(!R.pump)return toast('Pump needs a relay',true);if(new Set(used).size!==used.length)return toast('Each assigned role must use a different relay',true);cmd('relayConfig',{pump:R.pump,charger:R.charger,accessory:R.accessory,accessoryEnabled:rr.accessoryEnabled?1:0}).then(ok=>{if(ok)st.R=null})},
acc:()=>{const rr=S.find(s=>s.id==st.sid).telemetry.relay;cmd('accessoryPower',{enabled:rr.accessoryEnabled?0:1})},del:async()=>{try{await post('/api/delete',{uid:st.edit});st.edit=null;toast('Registration deleted');refresh(true)}catch(e){toast(e.message,true)}},end:()=>cmd('endSession'),reboot:()=>cmd('reboot')};
for(const n of[1,2,3,4])CF['rt'+n]=()=>cmd('relayTestStart',{channel:n});
document.body.addEventListener('click',e=>{const el=e.target.closest('[data-a]');if(el&&ACT[el.dataset.a])ACT[el.dataset.a](el.dataset.v,el)});
document.body.addEventListener('input',e=>{const el=e.target,k=el.dataset.k;if(!k)return;const v=el.value;if(k.startsWith('L.')){st.L=st.L||lim(O.status);st.L[k.slice(2)]=v}else if(k.startsWith('R.')){const rr=S.find(s=>s.id==st.sid).telemetry.relay;st.R=st.R&&st.R.sid==st.sid?st.R:{sid:st.sid,pump:rr.pump,charger:rr.charger,accessory:rr.accessory};st.R[k.slice(2)]=+v}else st[k]=v;if(k=='vol')$('#volv').textContent=v;if(k=='q')render(true)});
document.body.addEventListener('change',e=>{const el=e.target,k=el.dataset.k;if(k=='at')st.at=el.value;if(k=='vol')cmd('volume',{volume:el.value}).then(()=>{st.vol=null;el.blur()})});
window.addEventListener('pointerdown',()=>down=true);window.addEventListener('pointerup',()=>down=false);window.addEventListener('pointercancel',()=>down=false);
window.addEventListener('hashchange',route);route();refresh(true);setInterval(refresh,2000);
</script></body></html>)HTML";

constexpr uint32_t TELEMETRY_REBUILD_MS = 250;
// Give the ACK a chance to leave the radio before a remotely requested reboot.
constexpr uint32_t REMOTE_REBOOT_DELAY_MS = 500;
constexpr uint8_t COMMAND_DRAIN_PER_LOOP = 4;

struct ActionName {
  const char* name;
  uint8_t action;
};
const ActionName ACTION_NAMES[] = {
    {"enroll", CampNet::CMD_ENROLL},
    {"cancel", CampNet::CMD_CANCEL_ENROLL},
    {"calStart", CampNet::CMD_CALIBRATION_START},
    {"calStop", CampNet::CMD_CALIBRATION_STOP},
    {"musicCalStart", CampNet::CMD_MUSIC_CAL_START},
    {"musicCalCapture", CampNet::CMD_MUSIC_CAL_CAPTURE},
    {"musicCalCancel", CampNet::CMD_MUSIC_CAL_CANCEL},
    {"tone", CampNet::CMD_AUDIO_TONE},
    {"play", CampNet::CMD_AUDIO_PLAY},
    {"stop", CampNet::CMD_AUDIO_STOP},
    {"volume", CampNet::CMD_AUDIO_VOLUME},
    {"findSpeaker", CampNet::CMD_SPEAKER_SEARCH},
    {"reboot", CampNet::CMD_REBOOT},
    {"endSession", CampNet::CMD_END_SESSION},
    {"relayConfig", CampNet::CMD_RELAY_CONFIG},
    {"accessoryPower", CampNet::CMD_ACCESSORY_POWER},
    {"relayTestStart", CampNet::CMD_RELAY_TEST_START},
    {"relayTestStop", CampNet::CMD_RELAY_TEST_STOP},
};

uint8_t actionFromName(const String& name) {
  for (const ActionName& entry : ACTION_NAMES) {
    if (name == entry.name) return entry.action;
  }
  return 0;
}

// Actions that need the speaker / music knob, absent on fill stations.
bool needsMusic(uint8_t action) {
  switch (action) {
    case CampNet::CMD_MUSIC_CAL_START:
    case CampNet::CMD_MUSIC_CAL_CAPTURE:
    case CampNet::CMD_MUSIC_CAL_CANCEL:
    case CampNet::CMD_AUDIO_TONE:
    case CampNet::CMD_AUDIO_PLAY:
    case CampNet::CMD_AUDIO_STOP:
    case CampNet::CMD_AUDIO_VOLUME:
    case CampNet::CMD_SPEAKER_SEARCH:
      return true;
    default:
      return false;
  }
}

bool parseVolume(const String& value, long& percent) {
  if (value.isEmpty() || value.length() > 3) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) return false;
  }
  percent = value.toInt();
  return percent >= 0 && percent <= 100;
}

bool parseRelayConfig(const String& value, SettingsStore::RelayConfig& config) {
  int fields[4] = {0};
  int start = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    const int comma = value.indexOf(',', start);
    if ((i < 3 && comma < 0) || (i == 3 && comma >= 0)) return false;
    const String part = comma < 0 ? value.substring(start) : value.substring(start, comma);
    if (part.length() != 1 || !isDigit(part[0])) return false;
    fields[i] = part.toInt();
    start = comma + 1;
  }
  config.pump = static_cast<uint8_t>(fields[0]);
  config.charger = static_cast<uint8_t>(fields[1]);
  config.accessory = static_cast<uint8_t>(fields[2]);
  config.accessoryEnabled = fields[3] == 1;
  return fields[3] <= 1 && SettingsStore::relayConfigValid(config);
}

// Bounded copy of a packet char array that may lack a terminator.
template <size_t N>
String field(const char (&text)[N]) {
  String out;
  out.reserve(N);
  for (size_t i = 0; i < N && text[i] != '\0'; ++i) out += text[i];
  return out;
}

const char* boolJson(bool value) { return value ? "true" : "false"; }
}  // namespace

AdminServer::AdminServer(MemberRegistry& registry, const PulseStorage& pulseStorage,
                         const SessionStorage& sessions, SettingsStore& settings,
                         SpeakerAudio& speakerAudio, const UsageLedger& ledger,
                         CampNetLink& net)
    : registry_(registry), pulseStorage_(pulseStorage), sessions_(sessions),
      settings_(settings), speakerAudio_(speakerAudio), ledger_(ledger), net_(net) {}

bool AdminServer::begin() {
  if (started_) return true;
  // CampNet owns the radio and brings up the soft-AP; this only serves HTTP.
  if (!net_.ready()) return false;
  // begin() is retried from the main loop if the AP fails at boot, so the
  // route table must only be registered once.
  if (!routesConfigured_) {
    const char* headers[] = {"Authorization"};
    server_.collectHeaders(headers, 1);
    configureRoutes();
    routesConfigured_ = true;
  }
  server_.begin();
  started_ = true;
  return true;
}

void AdminServer::handle() {
  if (started_) server_.handleClient();
  drainRemoteCommands();
  if (millis() - lastTelemetryMs_ >= TELEMETRY_REBUILD_MS) {
    lastTelemetryMs_ = millis();
    publishTelemetry();
  }
}

bool AdminServer::onTagScanned(const String& uid) {
  lastUid_ = uid;
  if (!enrollmentPending_) return false;
  const String name = pendingName_;
  const bool saved = registry_.upsert(uid.c_str(), name);
  if (saved) net_.markMembersDirty();
  enrollmentPending_ = false;
  pendingName_ = "";
  lastMessage_ = saved ? name + " enrolled" : "Enrollment save failed";
  return true;
}

String AdminServer::address() const { return started_ ? WiFi.softAPIP().toString() : String("offline"); }

bool AdminServer::takeCalibrationStartRequest() {
  const bool requested = calibrationStartRequested_;
  calibrationStartRequested_ = false;
  return requested;
}

bool AdminServer::takeCalibrationStopRequest(float& knownGallons) {
  if (!calibrationStopRequested_) return false;
  calibrationStopRequested_ = false;
  knownGallons = calibrationKnownGallons_;
  return true;
}

void AdminServer::reportCalibration(bool active, uint32_t pulses, const String& message) {
  calibrationActive_ = active;
  calibrationPulses_ = pulses;
  calibrationMessage_ = message;
}

bool AdminServer::takeMusicCalibrationStartRequest() {
  const bool requested = musicCalibrationStartRequested_;
  musicCalibrationStartRequested_ = false;
  return requested;
}

bool AdminServer::takeMusicCalibrationCaptureRequest() {
  const bool requested = musicCalibrationCaptureRequested_;
  musicCalibrationCaptureRequested_ = false;
  return requested;
}

bool AdminServer::takeMusicCalibrationCancelRequest() {
  const bool requested = musicCalibrationCancelRequested_;
  musicCalibrationCancelRequested_ = false;
  return requested;
}

void AdminServer::reportMusicKnob(uint16_t raw, int8_t channel,
                                  bool calibrationActive, uint8_t nextPosition,
                                  const String& message) {
  musicKnobRaw_ = raw;
  musicChannel_ = channel;
  musicCalibrationActive_ = calibrationActive;
  musicCalibrationNextPosition_ = nextPosition;
  musicCalibrationMessage_ = message;
}

void AdminServer::reportHardware(bool hubReady, bool relayReady, bool rfidReady) {
  hubReady_ = hubReady;
  relayReady_ = relayReady;
  rfidReady_ = rfidReady;
}

void AdminServer::reportRelays(uint8_t state, bool testActive, uint8_t testChannel) {
  relayState_ = state;
  relayTestActive_ = testActive;
  relayTestChannel_ = testActive ? testChannel : 0;
}

bool AdminServer::takeRelayPolicyApplyRequest() {
  const bool requested = relayPolicyApplyRequested_;
  relayPolicyApplyRequested_ = false;
  return requested;
}

bool AdminServer::takeRelayTestStartRequest(uint8_t& channel) {
  if (!relayTestStartRequested_) return false;
  relayTestStartRequested_ = false;
  channel = requestedRelayTestChannel_;
  return true;
}

bool AdminServer::takeRelayTestStopRequest() {
  const bool requested = relayTestStopRequested_;
  relayTestStopRequested_ = false;
  return requested;
}

void AdminServer::reportSession(const char* activeName, float sessionGallons,
                                float sessionLimit, bool pumpOn, uint8_t doorState) {
  strlcpy(activeName_, activeName ? activeName : "", sizeof(activeName_));
  sessionGallons_ = sessionGallons;
  sessionLimit_ = sessionLimit;
  pumpOn_ = pumpOn;
  doorState_ = doorState;
}

bool AdminServer::takeRebootRequest() {
  if (!rebootRequested_ || static_cast<int32_t>(millis() - rebootReadyMs_) < 0) return false;
  rebootRequested_ = false;
  return true;
}

bool AdminServer::takeSpeakerSearchRequest() {
  const bool requested = speakerSearchRequested_;
  speakerSearchRequested_ = false;
  return requested;
}

bool AdminServer::takeEndSessionRequest() {
  const bool requested = endSessionRequested_;
  endSessionRequested_ = false;
  return requested;
}

bool AdminServer::authorize() {
  // The Wi-Fi password is the gate; the page itself is open unless enabled.
  if (!Config::ADMIN_PAGE_PASSWORD) return true;
  String header = server_.header("Authorization");
  if (header.startsWith("Basic ")) {
    header.remove(0, 6);
    unsigned char decoded[128] = {0};
    size_t decodedLength = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decodedLength,
                              reinterpret_cast<const unsigned char*>(header.c_str()),
                              header.length()) == 0) {
      decoded[decodedLength] = 0;
      const String credentials(reinterpret_cast<char*>(decoded));
      const int colon = credentials.indexOf(':');
      if (colon > 0 && credentials.substring(0, colon) == Config::ADMIN_USERNAME &&
          settings_.verifyPassword(credentials.substring(colon + 1))) return true;
    }
  }
  server_.sendHeader("WWW-Authenticate", "Basic realm=\"Camp Shower Admin\"");
  server_.send(401, "application/json", "{\"ok\":false,\"message\":\"Authentication required\"}");
  return false;
}

void AdminServer::configureRoutes() {
  server_.on("/", HTTP_GET, [this]() { if (authorize()) server_.send_P(200, "text/html", ADMIN_PAGE); });
  server_.on("/api/status", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", statusJson()); });
  server_.on("/api/members", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", "{\"members\":" + membersJson() + "}"); });
  server_.on("/api/sessions", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", "{\"sessions\":" + sessionsJson() + "}"); });
  server_.on("/api/health", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", healthJson()); });
  server_.on("/api/stations", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", stationsJson()); });
  server_.on("/api/overview", HTTP_GET, [this]() {
    if (!authorize()) return;
    sendOverview();
  });
  server_.on("/api/command", HTTP_POST, [this]() { if (authorize()) handleCommandPost(); });
  server_.on("/api/command", HTTP_GET, [this]() { if (authorize()) handleCommandPoll(); });
  // Legacy per-action routes share the command implementations.
  server_.on("/api/reboot", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_REBOOT, "", 0.0F); });
  server_.on("/api/speaker/search", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_SPEAKER_SEARCH, "", 0.0F); });
  server_.on("/api/enroll", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_ENROLL, server_.arg("name"), 0.0F); });
  server_.on("/api/cancel", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_CANCEL_ENROLL, "", 0.0F); });
  server_.on("/api/member", HTTP_POST, [this]() { if (authorize()) updateMember(); });
  server_.on("/api/rename", HTTP_POST, [this]() { if (authorize()) renameMember(); });
  server_.on("/api/delete", HTTP_POST, [this]() { if (authorize()) deleteMember(); });
  if (Config::ADMIN_PAGE_PASSWORD) {
    server_.on("/api/password", HTTP_POST, [this]() { if (authorize()) changePassword(); });
  }
  server_.on("/api/calibration/start", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_CALIBRATION_START, "", 0.0F); });
  server_.on("/api/calibration/stop", HTTP_POST, [this]() {
    if (authorize()) sendAction(CampNet::CMD_CALIBRATION_STOP, "", server_.arg("gallons").toFloat());
  });
  server_.on("/api/music/calibration/start", HTTP_POST,
             [this]() { if (authorize()) sendAction(CampNet::CMD_MUSIC_CAL_START, "", 0.0F); });
  server_.on("/api/music/calibration/capture", HTTP_POST,
             [this]() { if (authorize()) sendAction(CampNet::CMD_MUSIC_CAL_CAPTURE, "", 0.0F); });
  server_.on("/api/music/calibration/cancel", HTTP_POST,
             [this]() { if (authorize()) sendAction(CampNet::CMD_MUSIC_CAL_CANCEL, "", 0.0F); });
  server_.on("/api/audio/tone", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_AUDIO_TONE, "", 0.0F); });
  server_.on("/api/audio/play", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_AUDIO_PLAY, "", 0.0F); });
  server_.on("/api/audio/stop", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_AUDIO_STOP, "", 0.0F); });
  server_.on("/api/audio/volume", HTTP_POST, [this]() {
    if (!authorize()) return;
    long percent = 0;
    if (!parseVolume(server_.arg("volume"), percent)) return sendJsonMessage(400, false, "Volume must be 0-100");
    sendAction(CampNet::CMD_AUDIO_VOLUME, "", static_cast<float>(percent));
  });
  server_.on("/api/limits", HTTP_POST, [this]() { if (authorize()) setRoleLimits(); });
  server_.on("/api/audio/upload", HTTP_POST,
             [this]() {
               if (!audioUploadAuthorized_) return;
               const bool ok = !audioUploadFailed_ && audioUploadBytes_ > 0;
               sendJsonMessage(ok ? 200 : 500, ok,
                               ok ? String("Uploaded ") + audioUploadBytes_ + " bytes" : "Audio upload failed");
               audioUploadAuthorized_ = false;
             },
             [this]() { handleAudioUpload(); });
  server_.onNotFound([this]() { if (authorize()) sendJsonMessage(404, false, "Not found"); });
}

void AdminServer::sendOverview() {
  // The dashboard polls every two seconds. Send one JSON component at a time
  // so we never retain stations + members + sessions plus a duplicate combined
  // body in internal heap. WebServer terminates the HTTP/1.1 chunked response
  // when sendContent("") is called.
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"status\":");
  server_.sendContent(statusJson());
  server_.sendContent(",\"health\":");
  server_.sendContent(healthJson());
  server_.sendContent(",\"members\":");
  // Member data is the largest part of the response and changes rarely. The
  // browser returns the version it already has; send JSON null when that cache
  // is current so the two-second status poll does not rebuild and transmit the
  // full camp roster every time.
  const bool membersCurrent = server_.hasArg("membersVersion") &&
                              server_.arg("membersVersion") == String(registry_.version());
  if (membersCurrent) {
    server_.sendContent("null");
  } else {
    server_.sendContent("[");
    String memberChunk;
    memberChunk.reserve(1024);
    for (size_t i = 0; i < registry_.count(); ++i) {
      String member = memberJson(i);
      if (memberChunk.length() + member.length() + 1 > 900 &&
          !memberChunk.isEmpty()) {
        server_.sendContent(memberChunk);
        memberChunk = "";
      }
      if (i) memberChunk += ',';
      memberChunk += member;
    }
    if (!memberChunk.isEmpty()) server_.sendContent(memberChunk);
    server_.sendContent("]");
  }
  server_.sendContent(",\"sessions\":");
  server_.sendContent(sessionsJson());
  server_.sendContent(",\"stations\":");
  server_.sendContent(stationsJson());
  server_.sendContent("}");
  server_.sendContent("");
}

// ---- Telemetry published over CampNet and rendered for every station ----

void AdminServer::publishTelemetry() {
  CampNet::TelemetryPacket telemetry;
  buildTelemetry(telemetry);
  net_.setLocalTelemetry(telemetry);

  const size_t count = sessions_.recentCount();
  const uint32_t newestEndMs = count ? sessions_.recentAt(0).endMs : 0;
  if (recentPublished_ && count == publishedRecentCount_ && newestEndMs == publishedRecentEndMs_) return;
  CampNet::RecentPacket recent;
  buildRecent(recent);
  net_.setLocalRecent(recent);
  recentPublished_ = true;
  publishedRecentCount_ = count;
  publishedRecentEndMs_ = newestEndMs;
}

void AdminServer::buildTelemetry(CampNet::TelemetryPacket& t) const {
  memset(&t, 0, sizeof(t));
  t.uptimeS = millis() / 1000UL;
  t.freeHeap = ESP.getFreeHeap();
  t.minFreeHeap = ESP.getMinFreeHeap();
  t.audioUnderruns = speakerAudio_.bufferUnderruns();
  t.calibrationPulses = calibrationPulses_;
  t.pulsesPerGallon = settings_.pulsesPerGallon();
  t.sessionGallons = sessionGallons_;
  t.sessionLimit = sessionLimit_;
  t.musicKnobRaw = musicKnobRaw_;
  const bool knobCalibrated = settings_.musicKnobCalibrated();
  if (knobCalibrated) {
    for (uint8_t i = 0; i < CampNet::MUSIC_POSITIONS && i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
      t.musicPositions[i] = settings_.musicKnobPosition(i);
    }
  }
  const bool sdOk = pulseStorage_.healthy() && sessions_.healthy() && settings_.healthy() && registry_.healthy();
  t.flags = (sdOk ? CampNet::TELEM_SD_OK : 0) | (hubReady_ ? CampNet::TELEM_HUB_OK : 0) |
            (relayReady_ ? CampNet::TELEM_RELAY_OK : 0) | (rfidReady_ ? CampNet::TELEM_RFID_OK : 0) |
            (calibrationActive_ ? CampNet::TELEM_CALIBRATION_ACTIVE : 0) |
            (speakerAudio_.connected() ? CampNet::TELEM_SPEAKER_CONNECTED : 0) |
            (speakerAudio_.fileAvailable() ? CampNet::TELEM_AUDIO_FILE : 0) |
            (musicCalibrationActive_ ? CampNet::TELEM_MUSIC_CAL_ACTIVE : 0);
  t.features = (Config::HAS_MUSIC ? CampNet::FEATURE_MUSIC : 0) |
               (Config::HAS_LED_STRIP ? CampNet::FEATURE_LEDS : 0) |
               (Config::HAS_DOOR_SIGN ? CampNet::FEATURE_DOOR_SIGN : 0) |
               (enrollmentPending_ ? CampNet::FEATURE_ENROLL_PENDING : 0) |
               (knobCalibrated ? CampNet::FEATURE_MUSIC_CALIBRATED : 0) |
               (pumpOn_ ? CampNet::FEATURE_PUMP_ON : 0) |
               CampNet::FEATURE_RELAY_CONFIG;
  t.doorState = doorState_;
  t.wifiClients = WiFi.softAPgetStationNum();
  t.speakerVolume = speakerAudio_.speakerVolumePercent();
  t.musicChannel = musicChannel_;
  t.musicCalNext = musicCalibrationNextPosition_;
  strlcpy(t.activeName, activeName_, sizeof(t.activeName));
  strlcpy(t.pendingName, pendingName_.c_str(), sizeof(t.pendingName));
  strlcpy(t.speaker, speakerAudio_.connectionLabel(), sizeof(t.speaker));
  strlcpy(t.playback, speakerAudio_.playbackLabel(), sizeof(t.playback));
  strlcpy(t.calibrationMessage, calibrationMessage_.c_str(), sizeof(t.calibrationMessage));
  strlcpy(t.message, lastMessage_.c_str(), sizeof(t.message));
  const SettingsStore::RelayConfig& relayConfig = settings_.relayConfig();
  t.relayState = relayState_;
  t.pumpRelay = relayConfig.pump;
  t.chargerRelay = relayConfig.charger;
  t.accessoryRelay = relayConfig.accessory;
  t.relayFlags = (relayConfig.accessoryEnabled ? CampNet::RELAY_ACCESSORY_ENABLED : 0) |
                 (relayTestActive_ ? CampNet::RELAY_TEST_ACTIVE : 0);
  t.relayTestChannel = relayTestChannel_;
}

void AdminServer::buildRecent(CampNet::RecentPacket& r) const {
  memset(&r, 0, sizeof(r));
  const size_t count = sessions_.recentCount();
  r.count = static_cast<uint8_t>(count < CampNet::RECENT_ENTRIES_PER_PACKET ? count : CampNet::RECENT_ENTRIES_PER_PACKET);
  for (uint8_t i = 0; i < r.count; ++i) {
    const SessionStorage::Record& record = sessions_.recentAt(i);
    CampNet::RecentEntry& entry = r.entries[i];
    entry.uidLen = CampNet::uidFromHex(record.uid, entry.uid);
    entry.gallons = record.gallons;
    const uint32_t seconds = (record.endMs - record.startMs) / 1000UL;
    entry.durationS = static_cast<uint16_t>(seconds > 65535UL ? 65535UL : seconds);
    entry.reason = CampNet::sessionReasonCode(record.reason);
  }
}

String AdminServer::telemetryJson(const CampNet::TelemetryPacket& t) const {
  const bool calibrated = t.features & CampNet::FEATURE_MUSIC_CALIBRATED;
  const uint8_t safeChannel =
      t.musicChannel >= 0 && t.musicChannel < Config::MUSIC_KNOB_POSITION_COUNT
          ? static_cast<uint8_t>(t.musicChannel) : 0;
  String body;
  body.reserve(1280);
  body += "{\"calibrationActive\":"; body += boolJson(t.flags & CampNet::TELEM_CALIBRATION_ACTIVE);
  body += ",\"calibrationPulses\":" + String(t.calibrationPulses);
  body += ",\"calibrationMessage\":\"" + jsonEscape(field(t.calibrationMessage)) + "\"";
  body += ",\"pulsesPerGallon\":" + String(t.pulsesPerGallon, 4);
  body += ",\"speaker\":\"" + jsonEscape(field(t.speaker)) + "\"";
  body += ",\"audioPlayback\":\"" + jsonEscape(field(t.playback)) + "\"";
  body += ",\"audioFile\":"; body += boolJson(t.flags & CampNet::TELEM_AUDIO_FILE);
  body += ",\"speakerVolume\":" + String(t.speakerVolume);
  body += ",\"musicKnobRaw\":" + String(t.musicKnobRaw);
  body += ",\"musicChannel\":" + String(t.musicChannel);
  body += ",\"musicChannelName\":\"" + jsonEscape(Config::MUSIC_CHANNEL_NAMES[safeChannel]) + "\"";
  body += ",\"musicKnobCalibrated\":"; body += boolJson(calibrated);
  body += ",\"musicCalibrationActive\":"; body += boolJson(t.flags & CampNet::TELEM_MUSIC_CAL_ACTIVE);
  body += ",\"musicCalibrationNext\":" + String(t.musicCalNext);
  body += ",\"musicPositions\":[";
  for (uint8_t i = 0; i < CampNet::MUSIC_POSITIONS; ++i) {
    if (i) body += ',';
    if (calibrated) body += String(t.musicPositions[i]);
    else body += "null";
  }
  body += "],\"enrollmentPending\":"; body += boolJson(t.features & CampNet::FEATURE_ENROLL_PENDING);
  body += ",\"pendingName\":\"" + jsonEscape(field(t.pendingName)) + "\"";
  body += ",\"message\":\"" + jsonEscape(field(t.message)) + "\"";
  body += ",\"features\":{\"music\":"; body += boolJson(t.features & CampNet::FEATURE_MUSIC);
  body += ",\"leds\":"; body += boolJson(t.features & CampNet::FEATURE_LEDS);
  body += ",\"doorSign\":"; body += boolJson(t.features & CampNet::FEATURE_DOOR_SIGN);
  body += ",\"relayConfig\":"; body += boolJson(t.features & CampNet::FEATURE_RELAY_CONFIG);
  body += "},\"relay\":{\"state\":" + String(t.relayState);
  body += ",\"pump\":" + String(t.pumpRelay);
  body += ",\"charger\":" + String(t.chargerRelay);
  body += ",\"accessory\":" + String(t.accessoryRelay);
  body += ",\"accessoryEnabled\":"; body += boolJson(t.relayFlags & CampNet::RELAY_ACCESSORY_ENABLED);
  body += ",\"testActive\":"; body += boolJson(t.relayFlags & CampNet::RELAY_TEST_ACTIVE);
  body += ",\"testChannel\":" + String(t.relayTestChannel);
  body += "},\"health\":{\"uptimeS\":" + String(t.uptimeS);
  body += ",\"freeHeap\":" + String(t.freeHeap);
  body += ",\"minFreeHeap\":" + String(t.minFreeHeap);
  body += ",\"wifiClients\":" + String(t.wifiClients);
  body += ",\"hub\":"; body += boolJson(t.flags & CampNet::TELEM_HUB_OK);
  body += ",\"relay\":"; body += boolJson(t.flags & CampNet::TELEM_RELAY_OK);
  body += ",\"rfid\":"; body += boolJson(t.flags & CampNet::TELEM_RFID_OK);
  body += ",\"sd\":"; body += boolJson(t.flags & CampNet::TELEM_SD_OK);
  body += ",\"audioUnderruns\":" + String(t.audioUnderruns);
  body += "},\"session\":{\"active\":"; body += boolJson(t.activeName[0] != '\0');
  body += ",\"name\":\"" + jsonEscape(field(t.activeName)) + "\"";
  body += ",\"gallons\":" + String(t.sessionGallons, 3);
  body += ",\"limit\":" + String(t.sessionLimit, 2);
  body += ",\"pumpOn\":"; body += boolJson(t.features & CampNet::FEATURE_PUMP_ON);
  body += ",\"doorState\":" + String(t.doorState) + "}}";
  return body;
}

String AdminServer::recentJson(const CampNet::RecentPacket& r) const {
  const uint8_t count = r.count < CampNet::RECENT_ENTRIES_PER_PACKET ? r.count : CampNet::RECENT_ENTRIES_PER_PACKET;
  String body;
  body.reserve(count * 110 + 4);
  body += '[';
  for (uint8_t i = 0; i < count; ++i) {
    if (i) body += ',';
    const CampNet::RecentEntry& entry = r.entries[i];
    char hex[CampNet::UID_BYTES * 2 + 1];
    CampNet::uidToHex(entry.uid, entry.uidLen, hex);
    const char* name = registry_.nameFor(hex);
    body += "{\"name\":\"" + jsonEscape(name ? name : "Deleted member");
    body += "\",\"uid\":\"" + String(hex);
    body += "\",\"gallons\":" + String(entry.gallons, 4);
    body += ",\"durationS\":" + String(entry.durationS);
    body += ",\"reason\":\"" + String(CampNet::sessionReasonName(entry.reason)) + "\"}";
  }
  body += ']';
  return body;
}

String AdminServer::stationsJson() const {
  String body;
  body.reserve(1920 * (net_.peerCount() + 1) + 4);
  body += '[';
  bool first = true;
  CampNet::TelemetryPacket telemetry;
  CampNet::RecentPacket recent;
  for (uint8_t id = 1; id <= CampNet::MAX_STATIONS; ++id) {
    const bool local = id == Config::STATION_ID_VALUE;
    const CampNetLink::Peer& peer = net_.peer(id);
    if (!local && !peer.seen) continue;
    if (local) {
      buildTelemetry(telemetry);
      buildRecent(recent);
    } else {
      const CampNetLink::RemoteTelemetry& remote = net_.telemetry(id);
      if (remote.valid) memcpy(&telemetry, &remote.packet, sizeof(telemetry));
      else memset(&telemetry, 0, sizeof(telemetry));
      const CampNetLink::RemoteRecent& remoteRecent = net_.recent(id);
      if (remoteRecent.valid) memcpy(&recent, &remoteRecent.packet, sizeof(recent));
      else memset(&recent, 0, sizeof(recent));
    }
    if (!first) body += ',';
    first = false;
    const uint8_t role = local ? Config::STATION_ROLE_VALUE : peer.role;
    body += "{\"id\":" + String(id) + ",\"name\":\"" + String(Config::STATION_NAMES[id]) + "\"";
    body += ",\"role\":" + String(role) + ",\"roleName\":\"" + String(CampNet::roleName(role)) + "\"";
    body += ",\"local\":"; body += boolJson(local);
    body += ",\"online\":"; body += boolJson(local || net_.peerOnline(id));
    body += ",\"alarm\":"; body += boolJson(!local && net_.peerDeadmanAlarm(id));
    body += ",\"lastSeenS\":" + String(local ? 0UL : (millis() - peer.lastSeenMs) / 1000UL);
    body += ",\"telemetry\":";
    body += telemetryJson(telemetry);
    body += ",\"recent\":";
    body += recentJson(recent);
    body += '}';
  }
  body += ']';
  return body;
}

// ---- Station actions: one implementation per action, local or remote ----

int AdminServer::runAction(uint8_t action, const String& text, float value, String& message) {
  if (!Config::HAS_MUSIC && needsMusic(action)) {
    message = "Not available on a fill station";
    return 501;
  }
  switch (action) {
    case CampNet::CMD_ENROLL: {
      String name = text;
      name.trim();
      if (!registry_.healthy()) { message = "Member storage unavailable"; return 503; }
      if (name.isEmpty() || name.length() > 32) { message = "Name must be 1-32 characters"; return 400; }
      pendingName_ = name;
      enrollmentPending_ = true;
      lastMessage_ = "Waiting for wristband";
      message = "Tap wristband on reader";
      return 200;
    }
    case CampNet::CMD_CANCEL_ENROLL:
      enrollmentPending_ = false;
      pendingName_ = "";
      lastMessage_ = "Enrollment cancelled";
      message = lastMessage_;
      return 200;
    case CampNet::CMD_CALIBRATION_START:
      calibrationStartRequested_ = true;
      calibrationMessage_ = "Starting…";
      message = "Calibration requested";
      return 200;
    case CampNet::CMD_CALIBRATION_STOP:
      if (!(value > 0.0F)) { message = "Enter a known volume"; return 400; }
      calibrationKnownGallons_ = value;
      calibrationStopRequested_ = true;
      message = "Stop requested";
      return 200;
    case CampNet::CMD_MUSIC_CAL_START:
      musicCalibrationStartRequested_ = true;
      musicCalibrationMessage_ = "Starting...";
      message = "Music knob calibration requested";
      return 200;
    case CampNet::CMD_MUSIC_CAL_CAPTURE:
      if (!musicCalibrationActive_) { message = "Start music knob calibration first"; return 409; }
      musicCalibrationCaptureRequested_ = true;
      message = "Position capture requested";
      return 200;
    case CampNet::CMD_MUSIC_CAL_CANCEL:
      musicCalibrationCancelRequested_ = true;
      message = "Music knob calibration cancelled";
      return 200;
    case CampNet::CMD_AUDIO_TONE: {
      const bool started = speakerAudio_.playTestTone();
      message = started ? "Test tone started" : "Speaker not connected";
      return started ? 200 : 409;
    }
    case CampNet::CMD_AUDIO_PLAY: {
      const bool started = speakerAudio_.playSong();
      message = started ? "Song started" : "Connect speaker and upload audio first";
      return started ? 200 : 409;
    }
    case CampNet::CMD_AUDIO_STOP:
      speakerAudio_.stop();
      message = "Audio stopped";
      return 200;
    case CampNet::CMD_AUDIO_VOLUME: {
      if (value < 0.0F || value > 100.0F) { message = "Volume must be 0-100"; return 400; }
      const uint8_t percent = static_cast<uint8_t>(value);
      if (!settings_.setSpeakerVolumePercent(percent)) { message = "Could not save volume to SD card"; return 503; }
      speakerAudio_.setSpeakerVolumePercent(percent);
      message = String("Speaker volume set to ") + percent + "%";
      return 200;
    }
    case CampNet::CMD_SPEAKER_SEARCH:
      speakerSearchRequested_ = true;
      message = "Searching for speaker";
      return 200;
    case CampNet::CMD_REBOOT:
      rebootRequested_ = true;
      rebootReadyMs_ = millis();
      message = "Rebooting";
      return 200;
    case CampNet::CMD_END_SESSION:
      if (activeName_[0] == '\0') { message = "No active session"; return 409; }
      endSessionRequested_ = true;
      lastMessage_ = "Session end requested";
      message = "Ending session";
      return 200;
    case CampNet::CMD_RELAY_CONFIG: {
      if (activeName_[0] != '\0' || calibrationActive_ || relayTestActive_) {
        message = "Relay configuration requires an idle station";
        return 409;
      }
      SettingsStore::RelayConfig config;
      if (!parseRelayConfig(text, config)) {
        message = "Pump must use 1-4; auxiliary relays use 0-4 and assignments must be unique";
        return 400;
      }
      if (!settings_.setRelayConfig(config)) {
        message = "Could not save relay configuration to SD card";
        return 503;
      }
      relayPolicyApplyRequested_ = true;
      lastMessage_ = relayReady_ ? "Relay configuration saved" : "Relay configuration saved; awaiting module";
      message = lastMessage_;
      return 200;
    }
    case CampNet::CMD_ACCESSORY_POWER: {
      if (activeName_[0] != '\0' || calibrationActive_ || relayTestActive_) {
        message = "Accessory power changes require an idle station";
        return 409;
      }
      if (value != 0.0F && value != 1.0F) {
        message = "Accessory state must be 0 or 1";
        return 400;
      }
      const bool enabled = value == 1.0F;
      if (!settings_.setAccessoryEnabled(enabled)) {
        message = "Could not save accessory power setting to SD card";
        return 503;
      }
      relayPolicyApplyRequested_ = true;
      lastMessage_ = enabled ? "Accessory rail enabled" : "Accessory rail disabled";
      message = lastMessage_;
      return 200;
    }
    case CampNet::CMD_RELAY_TEST_START: {
      const uint8_t channel = static_cast<uint8_t>(value);
      if (activeName_[0] != '\0' || calibrationActive_ || relayTestActive_) {
        message = "Relay testing requires an idle station";
        return 409;
      }
      if (!relayReady_) { message = "Relay module unavailable"; return 503; }
      if (channel < 1 || channel > 4) { message = "Relay channel must be 1-4"; return 400; }
      requestedRelayTestChannel_ = channel;
      relayTestStartRequested_ = true;
      relayTestActive_ = true;
      relayTestChannel_ = channel;
      message = String("Testing relay ") + channel + " for 5 seconds";
      return 200;
    }
    case CampNet::CMD_RELAY_TEST_STOP:
      relayTestStopRequested_ = true;
      message = relayTestActive_ ? "Stopping relay test" : "Relay outputs restored";
      return 200;
    default:
      message = "Unknown action";
      return 400;
  }
}

void AdminServer::sendAction(uint8_t action, const String& text, float value) {
  String message;
  const int code = runAction(action, text, value, message);
  sendJsonMessage(code, code == 200, message);
}

void AdminServer::drainRemoteCommands() {
  CampNetLink::IncomingCommand command;
  for (uint8_t n = 0; n < COMMAND_DRAIN_PER_LOOP && net_.takeIncomingCommand(command); ++n) {
    const uint8_t argLen = command.argLen < CampNet::COMMAND_ARG_BYTES ? command.argLen : CampNet::COMMAND_ARG_BYTES;
    String text;
    float value = 0.0F;
    switch (command.action) {
      case CampNet::CMD_ENROLL:
        text.reserve(argLen);
        for (uint8_t i = 0; i < argLen && command.args[i] != '\0'; ++i) text += static_cast<char>(command.args[i]);
        break;
      case CampNet::CMD_CALIBRATION_STOP:
        if (argLen >= sizeof(float)) memcpy(&value, command.args, sizeof(float));
        break;
      case CampNet::CMD_AUDIO_VOLUME:
        value = argLen >= 1 ? static_cast<float>(command.args[0]) : -1.0F;
        break;
      case CampNet::CMD_RELAY_CONFIG:
        if (argLen >= 4) {
          text = String(command.args[0]) + ',' + String(command.args[1]) + ',' +
                 String(command.args[2]) + ',' + String(command.args[3]);
        }
        break;
      case CampNet::CMD_ACCESSORY_POWER:
      case CampNet::CMD_RELAY_TEST_START:
        value = argLen >= 1 ? static_cast<float>(command.args[0]) : -1.0F;
        break;
      default:
        break;
    }
    String message;
    const int code = runAction(command.action, text, value, message);
    if (code == 200 && command.action == CampNet::CMD_REBOOT) rebootReadyMs_ = millis() + REMOTE_REBOOT_DELAY_MS;
    const uint8_t status = code == 200 ? CampNet::ACK_OK : code == 501 ? CampNet::ACK_UNSUPPORTED : CampNet::ACK_REJECTED;
    net_.respondToCommand(command, status, message.c_str());
    Serial.printf("[ADMIN] remote command %u from station %u -> %d %s\n",
                  command.action, command.fromStation, code, message.c_str());
  }
}

void AdminServer::handleCommandPost() {
  const uint8_t action = actionFromName(server_.arg("action"));
  if (action == 0) return sendJsonMessage(400, false, "Unknown action");
  const long station = server_.arg("station").toInt();
  if (station < 1 || station > CampNet::MAX_STATIONS) return sendJsonMessage(400, false, "Unknown station");

  String text = server_.arg("name");
  text.trim();
  float value = 0.0F;
  uint8_t args[CampNet::COMMAND_ARG_BYTES] = {0};
  uint8_t argLen = 0;
  switch (action) {
    case CampNet::CMD_ENROLL:
      if (text.isEmpty() || text.length() > 32) return sendJsonMessage(400, false, "Name must be 1-32 characters");
      argLen = static_cast<uint8_t>(text.length());
      memcpy(args, text.c_str(), argLen);
      break;
    case CampNet::CMD_CALIBRATION_STOP:
      value = server_.arg("gallons").toFloat();
      if (!(value > 0.0F)) return sendJsonMessage(400, false, "Enter a known volume");
      memcpy(args, &value, sizeof(value));
      argLen = sizeof(value);
      break;
    case CampNet::CMD_AUDIO_VOLUME: {
      long percent = 0;
      if (!parseVolume(server_.arg("volume"), percent)) return sendJsonMessage(400, false, "Volume must be 0-100");
      value = static_cast<float>(percent);
      args[0] = static_cast<uint8_t>(percent);
      argLen = 1;
      break;
    }
    case CampNet::CMD_RELAY_CONFIG: {
      SettingsStore::RelayConfig config;
      text = server_.arg("pump") + ',' + server_.arg("charger") + ',' +
             server_.arg("accessory") + ',' + server_.arg("accessoryEnabled");
      if (!parseRelayConfig(text, config)) {
        return sendJsonMessage(400, false,
                               "Pump must use 1-4; auxiliary relays use 0-4 and assignments must be unique");
      }
      args[0] = config.pump;
      args[1] = config.charger;
      args[2] = config.accessory;
      args[3] = config.accessoryEnabled ? 1 : 0;
      argLen = 4;
      break;
    }
    case CampNet::CMD_ACCESSORY_POWER: {
      const String enabled = server_.arg("enabled");
      if (enabled != "0" && enabled != "1") return sendJsonMessage(400, false, "Accessory state must be 0 or 1");
      value = enabled == "1" ? 1.0F : 0.0F;
      args[0] = enabled == "1" ? 1 : 0;
      argLen = 1;
      break;
    }
    case CampNet::CMD_RELAY_TEST_START: {
      const long channel = server_.arg("channel").toInt();
      if (channel < 1 || channel > 4) return sendJsonMessage(400, false, "Relay channel must be 1-4");
      value = static_cast<float>(channel);
      args[0] = static_cast<uint8_t>(channel);
      argLen = 1;
      break;
    }
    default:
      break;
  }
  if (station == Config::STATION_ID_VALUE) return sendAction(action, text, value);

  const uint32_t nonce = net_.sendCommand(static_cast<uint8_t>(station), action, args, argLen);
  if (nonce == 0) return sendJsonMessage(503, false, "Station link unavailable");
  server_.send(202, "application/json",
               String("{\"ok\":true,\"pending\":true,\"nonce\":") + nonce + '}');
}

void AdminServer::handleCommandPoll() {
  const uint32_t nonce = strtoul(server_.arg("nonce").c_str(), nullptr, 10);
  const CampNetLink::CommandResult result = net_.commandResult(nonce);
  using State = CampNetLink::CommandResult::State;
  const char* state = result.state == State::Pending ? "pending"
                    : result.state == State::Done ? "done"
                    : result.state == State::Timeout ? "timeout" : "unknown";
  String message = field(result.message);
  if (result.state == State::Done && message.isEmpty()) {
    message = result.status == CampNet::ACK_OK ? "Done"
            : result.status == CampNet::ACK_UNSUPPORTED ? "Not supported on that station"
            : result.status == CampNet::ACK_UNAUTHORIZED ? "Station rejected the request as unauthorized"
            : "Rejected";
  }
  String body;
  body.reserve(160);
  body += String("{\"state\":\"") + state + "\",\"ok\":";
  body += boolJson(result.state == State::Done && result.status == CampNet::ACK_OK);
  body += ",\"status\":" + String(result.status);
  body += ",\"message\":\"" + jsonEscape(message) + "\"}";
  server_.send(200, "application/json", body);
}

// ---- Legacy JSON views ----

String AdminServer::sessionsJson() const {
  String body;
  body.reserve(sessions_.recentCount() * 128 + 4);
  body += '[';
  for (size_t i = 0; i < sessions_.recentCount(); ++i) {
    if (i) body += ',';
    const auto& record = sessions_.recentAt(i);
    const char* name = registry_.nameFor(record.uid);
    body += "{\"uid\":\"" + jsonEscape(record.uid) + "\",\"name\":\"";
    body += jsonEscape(name ? name : "Deleted member");
    body += "\",\"gallons\":" + String(record.gallons, 4);
    body += ",\"durationMs\":" + String(record.endMs - record.startMs);
    body += ",\"reason\":\"" + jsonEscape(record.reason) + "\"}";
  }
  body += ']';
  return body;
}

String AdminServer::healthJson() const {
  String body;
  body.reserve(256);
  body += "{\"uptimeMs\":" + String(millis());
  body += ",\"freeHeap\":" + String(ESP.getFreeHeap());
  body += ",\"minFreeHeap\":" + String(ESP.getMinFreeHeap());
  body += ",\"maxAllocHeap\":" + String(ESP.getMaxAllocHeap());
  body += ",\"freePsram\":" + String(ESP.getFreePsram());
  body += ",\"wifiClients\":" + String(WiFi.softAPgetStationNum());
  body += ",\"hub\":" + String(hubReady_ ? "true" : "false");
  body += ",\"relay\":" + String(relayReady_ ? "true" : "false");
  body += ",\"rfid\":" + String(rfidReady_ ? "true" : "false");
  body += ",\"sd\":" + String((pulseStorage_.healthy() && sessions_.healthy() &&
                               settings_.healthy() && registry_.healthy())
                                  ? "true" : "false");
  body += ",\"audioUnderruns\":" + String(speakerAudio_.bufferUnderruns());
  body += '}';
  return body;
}

String AdminServer::statusJson() const {
  String body;
  body.reserve(1024);
  body += "{\"station\":\"" + String(Config::STATION_NAME) + "\",\"ip\":\"" + address();
  body += "\",\"stationId\":" + String(Config::STATION_ID_VALUE);
  body += ",\"role\":" + String(Config::STATION_ROLE_VALUE);
  body += ",\"roleName\":\"" + String(CampNet::roleName(Config::STATION_ROLE_VALUE)) + "\"";
  body += ",\"ssid\":\"" + String(Config::WIFI_AP_NAME) + "\"";
  body += ",\"pagePassword\":" + String(Config::ADMIN_PAGE_PASSWORD ? "true" : "false");
  body += ",\"features\":{\"music\":" + String(Config::HAS_MUSIC ? "true" : "false");
  body += ",\"leds\":" + String(Config::HAS_LED_STRIP ? "true" : "false");
  body += ",\"doorSign\":" + String(Config::HAS_DOOR_SIGN ? "true" : "false") + "}";
  static const char* const limitKeys[CampNet::ROLE_COUNT] = {"shower", "water", "rv"};
  body += ",\"limits\":{";
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    body += String("\"") + limitKeys[role] + "\":{\"gal\":" + String(settings_.roleLimits(role).gallons, 1);
    body += ",\"min\":" + String(settings_.roleLimits(role).minutes) + "},";
  }
  body += "\"version\":" + String(settings_.limitsVersion()) + "}";
  body += ",\"membersVersion\":" + String(registry_.version());
  body += ",\"net\":{\"ready\":" + String(net_.ready() ? "true" : "false");
  body += ",\"channel\":" + String(CampNet::CHANNEL);
  body += ",\"rx\":" + String(net_.rxPackets()) + ",\"rxDropped\":" + String(net_.rxDropped());
  body += ",\"tx\":" + String(net_.txPackets()) + ",\"txFail\":" + String(net_.txFailures()) + "}";
  body += ",\"peers\":[";
  bool firstPeer = true;
  for (uint8_t id = 1; id <= CampNet::MAX_STATIONS; ++id) {
    const CampNetLink::Peer& peer = net_.peer(id);
    if (!peer.seen) continue;
    if (!firstPeer) body += ',';
    firstPeer = false;
    body += "{\"id\":" + String(id) + ",\"name\":\"" + String(Config::STATION_NAMES[id]) + "\"";
    body += ",\"role\":\"" + String(CampNet::roleName(peer.role)) + "\"";
    body += ",\"online\":" + String(net_.peerOnline(id) ? "true" : "false");
    body += ",\"lastSeenS\":" + String((millis() - peer.lastSeenMs) / 1000UL);
    body += ",\"state\":\"" + String(CampNet::doorStateName(peer.doorState)) + "\"";
    body += ",\"membersVersion\":" + String(peer.membersVersion);
    body += ",\"limitsVersion\":" + String(peer.limitsVersion) + "}";
  }
  body += "]";
  body += ",\"enrollmentPending\":" + String(enrollmentPending_ ? "true" : "false");
  body += ",\"pendingName\":\"" + jsonEscape(pendingName_) + "\",\"lastUid\":\"" + jsonEscape(lastUid_);
  body += "\",\"message\":\"" + jsonEscape(lastMessage_) + "\",\"pulsesPerGallon\":" + String(settings_.pulsesPerGallon(), 4);
  body += ",\"calibrationActive\":" + String(calibrationActive_ ? "true" : "false");
  body += ",\"calibrationPulses\":" + String(calibrationPulses_);
  body += ",\"calibrationMessage\":\"" + jsonEscape(calibrationMessage_) + "\"";
  body += ",\"speaker\":\"" + String(speakerAudio_.connectionLabel()) + "\"";
  body += ",\"audioPlayback\":\"" + String(speakerAudio_.playbackLabel()) + "\"";
  body += ",\"audioFile\":" + String(speakerAudio_.fileAvailable() ? "true" : "false");
  body += ",\"speakerVolume\":" + String(speakerAudio_.speakerVolumePercent());
  body += ",\"musicKnobRaw\":" + String(musicKnobRaw_);
  body += ",\"musicChannel\":" + String(musicChannel_);
  const uint8_t safeMusicChannel =
      musicChannel_ >= 0 && musicChannel_ < Config::MUSIC_KNOB_POSITION_COUNT
          ? static_cast<uint8_t>(musicChannel_)
          : 0;
  body += ",\"musicChannelName\":\"" +
          jsonEscape(Config::MUSIC_CHANNEL_NAMES[safeMusicChannel]) + "\"";
  body += ",\"musicKnobCalibrated\":" + String(settings_.musicKnobCalibrated() ? "true" : "false");
  body += ",\"musicCalibrationActive\":" + String(musicCalibrationActive_ ? "true" : "false");
  body += ",\"musicCalibrationNext\":" + String(musicCalibrationNextPosition_);
  body += ",\"musicCalibrationMessage\":\"" + jsonEscape(musicCalibrationMessage_) + "\"";
  body += ",\"musicPositions\":[";
  for (uint8_t i = 0; i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
    if (i) body += ',';
    if (settings_.musicKnobCalibrated()) body += String(settings_.musicKnobPosition(i));
    else body += "null";
  }
  body += "]}";
  return body;
}

String AdminServer::memberJson(size_t index) const {
  String body;
  body.reserve(224);
  const char* uid = registry_.uidAt(index);
  if (uid == nullptr) return "{}";
  body += "{\"uid\":\"" + jsonEscape(uid) + "\",\"name\":\"" + jsonEscape(registry_.nameAt(index));
  body += "\",\"allowance\":" + String(registry_.allowanceAt(index), 3);
  body += ",\"enabled\":" + String(registry_.enabledAt(index) ? "true" : "false");
  const float local = sessions_.gallonsFor(uid);
  body += ",\"gallons\":" + String(local, 4);
  body += ",\"sessions\":" + String(sessions_.sessionsFor(uid));
  float byRole[CampNet::ROLE_COUNT];
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    byRole[role] = ledger_.remoteGallonsByRole(uid, role);
  }
  byRole[Config::STATION_ROLE_VALUE] += local;
  body += ",\"networkGallons\":" + String(local + ledger_.remoteGallonsFor(uid), 4);
  body += ",\"networkSessions\":" + String(sessions_.sessionsFor(uid) + ledger_.remoteSessionsFor(uid));
  body += ",\"showerGallons\":" + String(byRole[CampNet::ROLE_SHOWER], 4);
  body += ",\"waterGallons\":" + String(byRole[CampNet::ROLE_WATER_FILL], 4);
  body += ",\"rvGallons\":" + String(byRole[CampNet::ROLE_RV_FILL], 4);
  body += ",\"pulses\":" + String(static_cast<unsigned long long>(pulseStorage_.totalFor(uid))) + '}';
  return body;
}

String AdminServer::membersJson() const {
  String body;
  body.reserve(registry_.count() * 192 + 4);
  body += '[';
  for (size_t i = 0; i < registry_.count(); ++i) {
    if (i) body += ',';
    body += memberJson(i);
  }
  body += ']';
  return body;
}

// ---- Camp-wide (member / limits / password) endpoints ----

void AdminServer::renameMember() { server_.sendHeader("Location", "/api/member"); updateMember(); }
void AdminServer::updateMember() {
  const String uid = server_.arg("uid"), name = server_.arg("name");
  const float allowance = server_.arg("allowance").toFloat();
  const bool enabled = server_.arg("enabled") == "1";
  if (!registry_.update(uid.c_str(), name, allowance, enabled)) return sendJsonMessage(400, false, "Member update failed");
  net_.markMembersDirty();
  lastMessage_ = "Member updated"; sendJsonMessage(200, true, lastMessage_);
}
void AdminServer::deleteMember() { if (!registry_.remove(server_.arg("uid").c_str())) return sendJsonMessage(404, false, "Registration not found"); net_.markMembersDirty(); lastMessage_ = "Registration deleted"; sendJsonMessage(200, true, lastMessage_); }
void AdminServer::setRoleLimits() {
  static const char* const keys[CampNet::ROLE_COUNT][2] = {
      {"showerGal", "showerMin"}, {"waterGal", "waterMin"}, {"rvGal", "rvMin"}};
  SettingsStore::RoleLimits limits[CampNet::ROLE_COUNT];
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    const String gallons = server_.arg(keys[role][0]);
    const String minutes = server_.arg(keys[role][1]);
    if (gallons.isEmpty() || minutes.isEmpty()) return sendJsonMessage(400, false, "Fill in every limit");
    limits[role].gallons = gallons.toFloat();
    const long parsedMinutes = minutes.toInt();
    limits[role].minutes = static_cast<uint16_t>(constrain(parsedMinutes, 0L, 65535L));
  }
  if (!SettingsStore::limitsValid(limits)) {
    return sendJsonMessage(400, false, "Gallons must be 0.5-500 and minutes 1-180");
  }
  if (!settings_.setRoleLimits(limits)) return sendJsonMessage(503, false, "Could not save limits to SD card");
  net_.markLimitsDirty();
  lastMessage_ = "Station limits saved";
  sendJsonMessage(200, true, lastMessage_);
}
void AdminServer::changePassword() {
  if (!settings_.setPassword(server_.arg("password"))) return sendJsonMessage(400, false, "Password must be 8-64 characters");
  // CampNet syncs the new salted hash so one password opens every station.
  net_.markAuthDirty();
  sendJsonMessage(200, true, "Password changed");
}

void AdminServer::handleAudioUpload() {
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    audioUploadAuthorized_ = authorize();
    audioUploadFailed_ = !audioUploadAuthorized_;
    audioUploadBytes_ = 0;
    if (!audioUploadAuthorized_) return;
    // The multipart body is parsed synchronously inside handleClient(), so the
    // main loop (session limits, relay-test timeout, WDT feed) is frozen for
    // the whole transfer. Only accept uploads while the station is idle.
    if (activeName_[0] != '\0' || calibrationActive_ || relayTestActive_) {
      audioUploadFailed_ = true;
      return;
    }
    speakerAudio_.stop();
    SD.remove(Config::AUDIO_PATH);
    audioUploadFile_ = SD.open(Config::AUDIO_PATH, FILE_WRITE);
    audioUploadFailed_ = !audioUploadFile_;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!audioUploadAuthorized_ || audioUploadFailed_) return;
    esp_task_wdt_reset();  // multi-MB PCM uploads outlast the 20 s loop watchdog
    const size_t written = audioUploadFile_.write(upload.buf, upload.currentSize);
    audioUploadBytes_ += written;
    if (written != upload.currentSize) audioUploadFailed_ = true;
  } else if (upload.status == UPLOAD_FILE_END) {
    if (audioUploadFile_) audioUploadFile_.close();
    if (audioUploadFailed_) SD.remove(Config::AUDIO_PATH);
    speakerAudio_.refreshFileAvailability();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (audioUploadFile_) audioUploadFile_.close();
    SD.remove(Config::AUDIO_PATH);
    audioUploadFailed_ = true;
    speakerAudio_.refreshFileAvailability();
  }
}

void AdminServer::sendJsonMessage(int code, bool ok, const String& message) { server_.send(code, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + jsonEscape(message) + "\"}"); }
String AdminServer::jsonEscape(const String& value) { String escaped; escaped.reserve(value.length() + 8); for (size_t i=0;i<value.length();++i){const char c=value[i]; if(c=='"'||c=='\\')escaped+='\\'; if(c=='\n')escaped+="\\n"; else if(c>=32)escaped+=c;} return escaped; }
