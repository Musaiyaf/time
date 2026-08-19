#pragma once
#include <Arduino.h>

// Self-contained setup page (no external CSS/JS/CDN - the ESP32 has no
// internet access while a phone is joined to its setup Access Point).
// Placeholders %NTP1% %NTP2% %TZ% %BANNER% %STATUS% are substituted by
// web_portal.cpp before the page is served.
const char PAGE_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Clock Setup</title>
<style>
  :root{
    --bg:#12161c; --card:#1b212a; --line:#2a323d;
    --date:#1b6fd1; --week:#2ea359; --doy:#e0852d; --wifi:#1ba8a3;
    --text:#eef1f5; --muted:#8f9aa8; --accent:#ff5a5a;
  }
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);
       font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;
       padding:16px;}
  h1{font-size:1.25rem;margin:0 0 4px}
  .sub{color:var(--muted);font-size:.85rem;margin-bottom:16px}
  .banner{background:var(--date);color:#fff;padding:10px 14px;border-radius:10px;
          margin-bottom:16px;font-size:.9rem}
  .card{background:var(--card);border:1px solid var(--line);border-radius:14px;
        padding:16px;margin-bottom:14px}
  .card h2{font-size:.95rem;margin:0 0 10px;color:var(--muted);
           text-transform:uppercase;letter-spacing:.04em}
  .net{display:flex;align-items:center;justify-content:space-between;
       padding:10px 8px;border-bottom:1px solid var(--line);cursor:pointer;
       border-radius:8px}
  .net:last-child{border-bottom:none}
  .net:hover{background:#232b36}
  .net.sel{background:#22344a;outline:1px solid var(--date)}
  .net .ssid{font-weight:600}
  .net .meta{color:var(--muted);font-size:.8rem;display:flex;gap:8px;align-items:center}
  .bars{display:inline-flex;gap:2px;align-items:flex-end;height:12px}
  .bars i{display:block;width:3px;background:#3a4453}
  .bars i.on{background:var(--wifi)}
  .bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}
  .bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:13px}
  button, .btn{width:100%;padding:12px;border:none;border-radius:10px;
       background:var(--date);color:#fff;font-size:1rem;font-weight:600;
       cursor:pointer;margin-top:4px}
  button.secondary{background:transparent;border:1px solid var(--line);color:var(--text)}
  button.danger{background:var(--accent)}
  button:active{opacity:.85}
  .rescan{background:transparent;border:1px solid var(--line);color:var(--muted);
          width:auto;padding:6px 12px;font-size:.8rem;font-weight:500;float:right}
  label{display:block;font-size:.8rem;color:var(--muted);margin:10px 0 4px}
  input[type=text],input[type=password]{
       width:100%;padding:11px;border-radius:9px;border:1px solid var(--line);
       background:#11161d;color:var(--text);font-size:1rem}
  .pwrow{position:relative}
  .pwrow span{position:absolute;right:12px;top:11px;color:var(--muted);
       font-size:.75rem;cursor:pointer;user-select:none}
  details summary{cursor:pointer;color:var(--muted);font-size:.85rem;margin-top:6px}
  #status{margin-top:10px;font-size:.85rem;color:var(--muted);white-space:pre-line}
  .badge-row{display:flex;gap:6px;margin:10px 0}
  .badge{flex:1;text-align:center;padding:8px 4px;border-radius:8px;font-size:.75rem;
        font-weight:700;color:#fff}
  .footer{text-align:center;color:var(--muted);font-size:.75rem;margin-top:18px}
</style>
</head>
<body>
  <h1>ESP32 Grid Clock</h1>
  <div class="sub">WiFi &amp; time setup</div>

  %BANNER%

  <div class="card">
    <h2>WiFi Network <button class="rescan" onclick="scan()">Rescan</button></h2>
    <div id="nets"><div class="sub">Scanning...</div></div>
  </div>

  <div class="card">
    <h2>Connect</h2>
    <form id="f" onsubmit="return save(event)">
      <label>SSID</label>
      <input type="text" id="ssid" name="ssid" autocomplete="off" required>
      <label>Password</label>
      <div class="pwrow">
        <input type="password" id="pass" name="pass" autocomplete="off">
        <span onclick="togglePw()">SHOW</span>
      </div>

      <details>
        <summary>Advanced: time zone &amp; NTP</summary>
        <label>POSIX time zone string</label>
        <input type="text" id="tz" name="tz" value="%TZ%">
        <label>Primary NTP server</label>
        <input type="text" id="ntp1" name="ntp1" value="%NTP1%">
        <label>Secondary NTP server</label>
        <input type="text" id="ntp2" name="ntp2" value="%NTP2%">
      </details>

      <button type="submit">Save &amp; Connect</button>
    </form>
    <div id="status">%STATUS%</div>
  </div>

  <button class="secondary danger" onclick="resetWifi()">Forget saved WiFi</button>

  <div class="footer">ESP32-S3 Grid Clock</div>

<script>
function bars(rssi){
  var n = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  var h='';
  for(var i=1;i<=4;i++) h += '<i class="'+(i<=n?'on':'')+'"></i>';
  return '<span class="bars">'+h+'</span>';
}
function scan(){
  document.getElementById('nets').innerHTML = '<div class="sub">Scanning...</div>';
  fetch('/scan').then(r=>r.json()).then(list=>{
    if(!list.length){
      document.getElementById('nets').innerHTML = '<div class="sub">No networks found. Try Rescan.</div>';
      return;
    }
    list.sort((a,b)=>b.rssi-a.rssi);
    var html='';
    list.forEach(function(n,i){
      html += '<div class="net" onclick="pick(this)" data-ssid="'+escapeHtml(n.ssid)+'">'+
        '<span class="ssid">'+escapeHtml(n.ssid)+(n.secure?' &#128274;':'')+'</span>'+
        '<span class="meta">'+n.rssi+' dBm '+bars(n.rssi)+'</span></div>';
    });
    document.getElementById('nets').innerHTML = html;
  }).catch(()=>{
    document.getElementById('nets').innerHTML = '<div class="sub">Scan failed. Try Rescan.</div>';
  });
}
function escapeHtml(s){
  return s.replace(/[&<>"']/g, function(c){
    return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];
  });
}
function pick(el){
  document.querySelectorAll('.net').forEach(n=>n.classList.remove('sel'));
  el.classList.add('sel');
  document.getElementById('ssid').value = el.getAttribute('data-ssid');
  document.getElementById('pass').focus();
}
function togglePw(){
  var p = document.getElementById('pass');
  p.type = p.type === 'password' ? 'text' : 'password';
}
function save(e){
  e.preventDefault();
  var st = document.getElementById('status');
  st.textContent = 'Saving and connecting... the clock will restart.\nIf it can\'t reach that network within ~15s it will fall back to this setup page.';
  var body = new URLSearchParams(new FormData(document.getElementById('f')));
  fetch('/save', {method:'POST', body:body})
    .then(()=>{ st.textContent += '\nDone. You can close this page.'; })
    .catch(()=>{ st.textContent = 'Save failed, please retry.'; });
  return false;
}
function resetWifi(){
  if(!confirm('Forget saved WiFi and restart into setup mode?')) return;
  fetch('/resetwifi').then(()=>{
    document.getElementById('status').textContent = 'WiFi cleared. Restarting...';
  });
}
scan();
</script>
</body>
</html>
)rawliteral";
