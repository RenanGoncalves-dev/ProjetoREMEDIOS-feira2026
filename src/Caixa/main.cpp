#include <Arduino.h>
#include <WiFi.h>
//#include <esp_now.h>
#include <WebServer.h>
#include <Stepper.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* ssidAP = "GREENLANTERN 2532";
const char* passwordAP = "verde123";

uint8_t broadcastAddress[] = {0x84, 0xFC, 0xE6, 0x00, 0xD3, 0x38};

#define IN1 5
#define IN2 18
#define IN3 19
#define IN4 21
#define OLED_SDA 14
#define OLED_SCL 27

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Stepper myStepper(2048, IN1, IN3, IN2, IN4);
WebServer server(80);

typedef struct struct_message { int comando; } struct_message;
struct_message myData;
//esp_now_peer_info_t peerInfo;

// --- ESTOQUE ---
int capacidadeTotal = 7;
int slotsOcupados = 0;
bool emUso = false;

// --- AGENDAMENTOS ---
#define MAX_AGENDAMENTOS 8
struct Agendamento {
  unsigned long alvoMillis;
  unsigned long repeteMillis;
  bool ativo;
  String descricao;
};
Agendamento listaAgendamentos[MAX_AGENDAMENTOS];

// --- CONTROLE DO OLED (mensagens temporárias + tela principal) ---
bool msgAtiva = false;
unsigned long msgAte = 0;
unsigned long ultimoRefresh = 0;

// ======================= UTILIDADES =======================

void stopMotor() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

// Tempo restante em ms (aritmética com sinal: seguro contra estouro do millis)
long restanteMs(int i) {
  return (long)(listaAgendamentos[i].alvoMillis - millis());
}

// Índice do agendamento ativo mais próximo (-1 se não houver)
int proximoAgendamento() {
  int idx = -1;
  long menor = 0;
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (!listaAgendamentos[i].ativo) continue;
    long r = restanteMs(i);
    if (idx == -1 || r < menor) { idx = i; menor = r; }
  }
  return idx;
}

void formatarTempo(long ms, char* buf, size_t tam) {
  if (ms < 0) ms = 0;
  long s = ms / 1000;
  long h = s / 3600, m = (s % 3600) / 60, x = s % 60;
  if (h > 99) h = 99;
  snprintf(buf, tam, "%02ld:%02ld:%02ld", h, m, x);
}

// ======================= OLED =======================

void updateDisplay(String linha1, String linha2) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println(linha1);
  display.setTextSize(2);
  display.setCursor(0, 30);
  display.println(linha2);
  display.display();
}

// Mostra uma mensagem por "ms" milissegundos e depois volta à tela principal
void showMessage(String l1, String l2, unsigned long ms = 2500) {
  updateDisplay(l1, l2);
  msgAtiva = true;
  msgAte = millis() + ms;
}

// Tela principal: estoque + cronômetro regressivo da próxima dose
void telaPrincipal() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("IP:");
  display.print(WiFi.localIP());   // fica sempre visível (0.0.0.0 = sem Wi-Fi)

  display.setTextSize(2);
  display.setCursor(0, 12);
  display.print("Est:");
  display.print(slotsOcupados);
  display.print("/");
  display.print(capacidadeTotal);

  display.setTextSize(1);
  display.setCursor(0, 36);

  int p = proximoAgendamento();
  if (p < 0) {
    display.print("Sem agendamento");
    display.setTextSize(2);
    display.setCursor(0, 47);
    display.print("--:--:--");
  } else {
    char buf[12];
    formatarTempo(restanteMs(p), buf, sizeof(buf));
    display.print("Proxima dose em:");
    display.setTextSize(2);
    display.setCursor(0, 47);
    display.print(buf);
  }
  display.display();
}

// ======================= DISPENSADOR =======================

void acionarDispensador() {
  if (slotsOcupados > 0 && !emUso) {
    emUso = true;
    slotsOcupados--;

    updateDisplay("Liberando", "Remedio...");
    myStepper.step(276); // 45 graus (1/8 de volta)
    stopMotor();

    // myData.comando = 1;
    // esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

    showMessage("Dose liberada", String(slotsOcupados) + "/" + String(capacidadeTotal), 2500);
    emUso = false;
  } else if (slotsOcupados <= 0) {
    showMessage("ALERTA!", "Caixa Vazia!", 4000);
  }
}

// ======================= DASHBOARD WEB (guardado na Flash) =======================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#2563eb">
<title>E.L.O.S. Dashboard</title>
<script>
  // Aplica o tema antes de renderizar (evita "piscar" branco no modo escuro)
  (function(){
    var t = localStorage.getItem('elos-theme');
    if (!t) t = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    document.documentElement.setAttribute('data-theme', t);
  })();
</script>
<style>
  :root {
    color-scheme: light;
    --bg:#f1f5f9; --surface:#ffffff; --surface2:#f8fafc; --text:#0f172a; --muted:#64748b;
    --border:#e2e8f0; --primary:#2563eb; --on-primary:#ffffff; --soft:#dbeafe;
    --ok:#16a34a; --warn:#d97706; --danger:#dc2626; --shadow:0 4px 16px rgba(15,23,42,.08);
  }
  [data-theme="dark"] {
    color-scheme: dark;
    --bg:#0b1220; --surface:#131c2e; --surface2:#1a2540; --text:#e5e7eb; --muted:#94a3b8;
    --border:#263350; --primary:#60a5fa; --on-primary:#0b1220; --soft:#1e3a5f;
    --ok:#4ade80; --warn:#fbbf24; --danger:#f87171; --shadow:0 4px 16px rgba(0,0,0,.5);
  }
  * { box-sizing: border-box; }
  html { -webkit-text-size-adjust: 100%; }
  body {
    margin: 0; background: var(--bg); color: var(--text);
    font-family: system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif; line-height: 1.4;
    padding: env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left);
    transition: background .25s, color .25s;
  }
  header {
    position: sticky; top: 0; z-index: 10; display: flex; align-items: center; justify-content: space-between;
    gap: 12px; padding: 12px 16px; background: var(--surface); border-bottom: 1px solid var(--border);
  }
  header h1 { font-size: 1.15rem; margin: 0; display: flex; align-items: center; gap: 8px; }
  .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--danger); display: inline-block; }
  .dot.on { background: var(--ok); }
  .icon-btn {
    border: 1px solid var(--border); background: var(--surface2); color: var(--text); width: 44px; height: 44px;
    border-radius: 12px; font-size: 1.25rem; cursor: pointer;
  }
  main { max-width: 960px; margin: 0 auto; padding: 16px; display: grid; gap: 16px; grid-template-columns: 1fr; }
  @media (min-width: 800px) {
    main { grid-template-columns: 1fr 1fr; align-items: start; }
    .full { grid-column: 1 / -1; }
  }
  .card { background: var(--surface); border: 1px solid var(--border); border-radius: 16px; padding: 18px; box-shadow: var(--shadow); }
  .card h2 { font-size: 1rem; margin: 0 0 14px; }
  .muted { color: var(--muted); font-size: .85rem; }

  /* Próxima dose */
  .hero { text-align: center; background: linear-gradient(135deg, var(--soft), var(--surface)); }
  .countdown { font-size: clamp(2.4rem, 12vw, 3.6rem); font-weight: 700; font-variant-numeric: tabular-nums; color: var(--primary); letter-spacing: 1px; margin: 4px 0; }

  /* Estoque */
  .stock-num { font-size: 2.6rem; font-weight: 700; text-align: center; }
  .bar { height: 12px; border-radius: 99px; background: var(--surface2); border: 1px solid var(--border); overflow: hidden; margin: 8px 0 4px; }
  .bar > div { height: 100%; width: 0; background: var(--ok); transition: width .4s, background .4s; }

  /* Formulários */
  label { display: block; font-size: .85rem; font-weight: 600; margin: 12px 0 6px; }
  input, select {
    width: 100%; padding: 12px; font-size: 16px; /* 16px evita zoom automático no iOS */
    border-radius: 10px; border: 1px solid var(--border); background: var(--surface2); color: var(--text);
  }
  input:focus, select:focus { outline: 2px solid var(--primary); outline-offset: 1px; }
  .row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .check { display: flex; align-items: center; gap: 8px; margin-top: 12px; font-weight: 500; }
  .check input { width: 20px; height: 20px; }
  .hidden { display: none; }

  .btn {
    width: 100%; min-height: 48px; margin-top: 14px; border: 0; border-radius: 12px; cursor: pointer;
    font-size: 1rem; font-weight: 700; color: var(--on-primary); background: var(--primary);
  }
  .btn:active { transform: scale(.98); opacity: .85; }
  .btn.ok { background: var(--ok); }
  .btn.warn { background: var(--warn); }
  .btn.danger { background: var(--danger); color: #fff; }
  .btn.ghost { background: var(--surface2); color: var(--text); border: 1px solid var(--border); }

  /* Lista */
  ul { list-style: none; margin: 0; padding: 0; }
  li { display: flex; align-items: center; gap: 10px; padding: 12px 0; border-bottom: 1px dashed var(--border); }
  li:last-child { border-bottom: 0; }
  li .info { flex: 1; min-width: 0; }
  li .info strong { display: block; overflow-wrap: anywhere; }
  li .time { font-variant-numeric: tabular-nums; font-weight: 700; color: var(--primary); }
  li .del { width: 40px; height: 40px; border-radius: 10px; border: 1px solid var(--border); background: var(--surface2); color: var(--danger); font-size: 1rem; cursor: pointer; }
  .empty { color: var(--muted); font-style: italic; justify-content: center; }

  /* Aviso (toast) */
  #toast {
    position: fixed; left: 50%; bottom: calc(20px + env(safe-area-inset-bottom)); transform: translate(-50%, 30px);
    background: var(--text); color: var(--bg); padding: 12px 18px; border-radius: 12px; opacity: 0;
    pointer-events: none; transition: .3s; max-width: 90vw; text-align: center; z-index: 20;
  }
  #toast.show { opacity: 1; transform: translate(-50%, 0); }
</style>
</head>
<body>
<header>
  <h1><span id="conn" class="dot" title="Conexão com a placa"></span> Painel E.L.O.S.</h1>
  <button class="icon-btn" id="themeBtn" onclick="toggleTheme()" aria-label="Alternar tema">🌙</button>
</header>

<main>
  <!-- PRÓXIMA DOSE -->
  <section class="card hero full">
    <div class="muted">Próxima dose em</div>
    <div class="countdown" id="proxTempo">--:--:--</div>
    <div class="muted" id="proxDesc">Nenhum agendamento ativo</div>
  </section>

  <!-- ESTOQUE -->
  <section class="card">
    <h2>📦 Estoque</h2>
    <div class="stock-num"><span id="eOcup">-</span> / <span id="eTotal">-</span></div>
    <div class="bar"><div id="eBar"></div></div>
    <div class="muted" style="text-align:center">Ocupados / Capacidade total</div>

    <div class="row">
      <div><label for="inp_total">Capacidade</label><input type="number" id="inp_total" min="1" inputmode="numeric"></div>
      <div><label for="inp_ocupados">Abastecidos</label><input type="number" id="inp_ocupados" min="0" placeholder="Ex: 7" inputmode="numeric"></div>
    </div>
    <button class="btn warn" onclick="atualizarEstoque()">🔄 Atualizar estoque</button>
    <button class="btn" id="btnDispense" onclick="dispensar()">💊 Dispensar uma dose agora</button>
  </section>

  <!-- AGENDAR -->
  <section class="card">
    <h2>⏰ Agendar nova dose</h2>
    <label for="modo">Tipo de agendamento</label>
    <select id="modo" onchange="mudaModo()">
      <option value="horario">Horário exato (ex: 18:15)</option>
      <option value="cronometro">Cronômetro (daqui a X tempo)</option>
      <option value="intervalo">Intervalo (a cada X horas)</option>
    </select>

    <div id="div_horario">
      <label for="h_time">Hora</label>
      <input type="time" id="h_time">
      <label class="check"><input type="checkbox" id="h_diario"> Repetir todos os dias</label>
    </div>
    <div id="div_cronometro" class="hidden">
      <div class="row">
        <div><label for="c_horas">Horas</label><input type="number" id="c_horas" min="0" max="99" placeholder="0" inputmode="numeric"></div>
        <div><label for="c_mins">Minutos</label><input type="number" id="c_mins" min="0" max="59" placeholder="0" inputmode="numeric"></div>
      </div>
    </div>
    <div id="div_intervalo" class="hidden">
      <label for="i_horas">Repetir a cada (horas)</label>
      <input type="number" id="i_horas" min="1" max="99" placeholder="Ex: 8" inputmode="numeric">
    </div>
    <button class="btn ok" onclick="salvarAgendamento()">➕ Adicionar à lista</button>
  </section>

  <!-- LISTA -->
  <section class="card full">
    <h2>📋 Agendamentos ativos</h2>
    <ul id="lista"><li class="empty">Carregando...</li></ul>
    <button class="btn danger" id="btnLimpar" onclick="limparAgendamentos()">🗑️ Limpar todos</button>
  </section>
</main>

<div id="toast" role="status"></div>

<script>
  const $ = id => document.getElementById(id);
  let dados = { ocupados: 0, total: 0, agendamentos: [] };
  let ultimaLeitura = Date.now();
  let totalInicializado = false;

  /* ---------- Tema ---------- */
  function atualizaIconeTema() {
    $('themeBtn').textContent = document.documentElement.getAttribute('data-theme') === 'dark' ? '☀️' : '🌙';
  }
  function toggleTheme() {
    const novo = document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', novo);
    localStorage.setItem('elos-theme', novo);
    atualizaIconeTema();
  }
  atualizaIconeTema();

  /* ---------- Utilidades ---------- */
  let toastTimer;
  function aviso(msg) {
    const t = $('toast'); t.textContent = msg; t.classList.add('show');
    clearTimeout(toastTimer); toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
  }
  function fmt(s) {
    s = Math.max(0, Math.floor(s));
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), x = s % 60;
    return [h, m, x].map(n => String(n).padStart(2, '0')).join(':');
  }

  /* ---------- Comunicação com o ESP32 ---------- */
  async function carregar() {
    try {
      const r = await fetch('/status', { cache: 'no-store' });
      dados = await r.json();
      ultimaLeitura = Date.now();
      $('conn').classList.add('on');
      if (!totalInicializado) { $('inp_total').value = dados.total; totalInicializado = true; }
    } catch (e) {
      $('conn').classList.remove('on');
    }
    desenhar();
  }

  /* ---------- Desenho da tela (roda a cada segundo, usando contagem local) ---------- */
  function desenhar() {
    const passou = (Date.now() - ultimaLeitura) / 1000;

    // Estoque
    $('eOcup').textContent = dados.ocupados;
    $('eTotal').textContent = dados.total;
    const pct = dados.total > 0 ? (dados.ocupados / dados.total) * 100 : 0;
    const bar = $('eBar');
    bar.style.width = pct + '%';
    bar.style.background = pct <= 15 ? 'var(--danger)' : pct <= 40 ? 'var(--warn)' : 'var(--ok)';

    // Lista com cronômetro regressivo
    const ul = $('lista');
    ul.innerHTML = '';
    const itens = dados.agendamentos || [];
    let menor = null;

    if (itens.length === 0) {
      const li = document.createElement('li');
      li.className = 'empty'; li.textContent = 'Nenhum agendamento programado';
      ul.appendChild(li);
    }
    itens.forEach(a => {
      const resta = Math.max(0, a.restante - passou);
      if (menor === null || resta < menor.resta) menor = { resta, desc: a.desc };

      const li = document.createElement('li');
      const info = document.createElement('div'); info.className = 'info';
      const nome = document.createElement('strong'); nome.textContent = a.desc;
      const sub = document.createElement('span'); sub.className = 'muted';
      sub.textContent = a.repete > 0 ? 'Repete a cada ' + fmt(a.repete) : 'Dose única';
      info.append(nome, sub);

      const tempo = document.createElement('span'); tempo.className = 'time'; tempo.textContent = fmt(resta);

      const del = document.createElement('button'); del.className = 'del'; del.textContent = '✕';
      del.setAttribute('aria-label', 'Remover agendamento'); del.onclick = () => remover(a.id);

      li.append(info, tempo, del);
      ul.appendChild(li);
    });

    // Destaque da próxima dose
    $('proxTempo').textContent = menor ? fmt(menor.resta) : '--:--:--';
    $('proxDesc').textContent = menor ? menor.desc : 'Nenhum agendamento ativo';

    // Se o tempo zerou, busca o novo estado da placa
    if (menor && menor.resta <= 0 && Date.now() - ultimaLeitura > 2000) carregar();
  }

  setInterval(desenhar, 1000);
  setInterval(carregar, 5000);
  carregar();

  /* ---------- Ações ---------- */
  async function dispensar() {
    try {
      const txt = await (await fetch('/dispensar')).text();
      aviso(txt === 'Vazio' ? '⚠️ A caixa está VAZIA!' : '💊 Dose liberada!');
    } catch (e) { aviso('Erro ao conectar com a placa.'); }
    carregar();
  }

  async function atualizarEstoque() {
    const t = $('inp_total').value, o = $('inp_ocupados').value;
    if (o === '' || t === '') return aviso('⚠️ Preencha os dois valores!');
    try {
      await fetch(`/estoque?total=${t}&ocupados=${o}`);
      $('inp_ocupados').value = '';
      aviso('✅ Estoque atualizado!');
    } catch (e) { aviso('Erro ao conectar com a placa.'); }
    carregar();
  }

  async function limparAgendamentos() {
    if (!confirm('Deseja apagar todos os horários agendados?')) return;
    try { await fetch('/limpar'); aviso('🗑️ Agendamentos apagados.'); } catch (e) { aviso('Erro ao conectar.'); }
    carregar();
  }

  async function remover(id) {
    try { await fetch('/remover?id=' + id); aviso('Agendamento removido.'); } catch (e) { aviso('Erro ao conectar.'); }
    carregar();
  }

  function mudaModo() {
    ['horario', 'cronometro', 'intervalo'].forEach(m => $('div_' + m).classList.add('hidden'));
    $('div_' + $('modo').value).classList.remove('hidden');
  }

  async function salvarAgendamento() {
    const modo = $('modo').value;
    let delay = 0, repetir = 0, desc = '';

    if (modo === 'horario') {
      const t = $('h_time').value;
      if (!t) return aviso('⚠️ Preencha o horário!');
      const p = t.split(':');
      const agora = new Date();
      const alvo = new Date(agora.getFullYear(), agora.getMonth(), agora.getDate(), +p[0], +p[1], 0);
      if (alvo <= agora) alvo.setDate(alvo.getDate() + 1);
      delay = Math.floor((alvo - agora) / 1000);
      if ($('h_diario').checked) { repetir = 86400; desc = 'Todos os dias às ' + t; }
      else desc = 'Às ' + t;
    } else if (modo === 'cronometro') {
      const h = parseInt($('c_horas').value || 0), m = parseInt($('c_mins').value || 0);
      if (h === 0 && m === 0) return aviso('⚠️ Insira um tempo!');
      delay = h * 3600 + m * 60;
      desc = 'Daqui a ' + h + 'h ' + m + 'm';
    } else {
      const h = parseInt($('i_horas').value || 0);
      if (h <= 0) return aviso('⚠️ Preencha as horas!');
      delay = h * 3600; repetir = h * 3600;
      desc = 'A cada ' + h + ' horas';
    }

    try {
      const msg = await (await fetch(`/agendar?delay=${delay}&repetir=${repetir}&desc=${encodeURIComponent(desc)}`)).text();
      aviso(msg === 'FULL' ? '⚠️ Limite máximo atingido!' : msg === 'OK' ? '✅ Dose adicionada!' : '⚠️ Configuração inválida.');
    } catch (e) { aviso('Erro ao conectar com a placa.'); }
    carregar();
  }
</script>
</body>
</html>
)rawliteral";

// ======================= HANDLERS =======================

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleStatus() {
  int p = proximoAgendamento();
  String json = "{\"ocupados\":" + String(slotsOcupados) +
                ",\"total\":" + String(capacidadeTotal) +
                ",\"proximo\":" + String(p >= 0 ? restanteMs(p) / 1000 : -1) +
                ",\"agendamentos\":[";
  bool primeiro = true;
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (!listaAgendamentos[i].ativo) continue;
    long r = restanteMs(i);
    if (r < 0) r = 0;
    if (!primeiro) json += ",";
    json += "{\"id\":" + String(i) +
            ",\"desc\":\"" + listaAgendamentos[i].descricao + "\"" +
            ",\"restante\":" + String(r / 1000) +
            ",\"repete\":" + String(listaAgendamentos[i].repeteMillis / 1000) + "}";
    primeiro = false;
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleEstoque() {
  if (server.hasArg("total") && server.hasArg("ocupados")) {
    int total = server.arg("total").toInt();
    int ocup = server.arg("ocupados").toInt();
    if (total < 1) total = 1;
    if (ocup < 0) ocup = 0;
    if (ocup > total) ocup = total;
    capacidadeTotal = total;
    slotsOcupados = ocup;

    Serial.println("[SISTEMA] Estoque reabastecido!");
    showMessage("Estoque:", String(slotsOcupados) + "/" + String(capacidadeTotal), 2000);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Faltam parametros");
  }
}

void handleDispense() {
  if (slotsOcupados > 0 && !emUso) {
    acionarDispensador();
    server.send(200, "text/plain", "OK");
  } else {
    showMessage("ALERTA!", "Caixa Vazia!", 4000);
    server.send(400, "text/plain", "Vazio");
  }
}

void handleLimpar() {
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) listaAgendamentos[i].ativo = false;
  Serial.println("[SISTEMA] Todos os agendamentos foram apagados.");
  server.send(200, "text/plain", "OK");
}

void handleRemover() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 0 && id < MAX_AGENDAMENTOS) {
      listaAgendamentos[id].ativo = false;
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "ID invalido");
}

void handleAgendar() {
  if (server.hasArg("delay") && server.hasArg("repetir") && server.hasArg("desc")) {
    int slotLivre = -1;
    for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
      if (!listaAgendamentos[i].ativo) { slotLivre = i; break; }
    }
    if (slotLivre == -1) {
      server.send(200, "text/plain", "FULL");
      return;
    }

    unsigned long delaySegundos = server.arg("delay").toInt();
    unsigned long repSegundos = server.arg("repetir").toInt();
    if (delaySegundos == 0) {
      server.send(200, "text/plain", "INVALIDO");
      return;
    }

    // Sanitiza a descrição para não quebrar o JSON
    String descricao = server.arg("desc");
    descricao.replace("\"", "'");
    descricao.replace("\\", "/");
    if (descricao.length() > 40) descricao = descricao.substring(0, 40);

    listaAgendamentos[slotLivre].alvoMillis = millis() + (delaySegundos * 1000UL);
    listaAgendamentos[slotLivre].repeteMillis = repSegundos * 1000UL;
    listaAgendamentos[slotLivre].descricao = descricao;
    listaAgendamentos[slotLivre].ativo = true;

    showMessage("Agendado:", descricao.length() > 10 ? descricao.substring(0, 10) : descricao, 2000);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Faltam parametros");
  }
}

// ======================= SETUP / LOOP =======================

void setup() {
  Serial.begin(115200);
  delay(2000);

  for (int i = 0; i < MAX_AGENDAMENTOS; i++) listaAgendamentos[i].ativo = false;

  myStepper.setSpeed(12);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("Erro OLED");

  updateDisplay("Conectando", "Wi-Fi...");
  Serial.println("\n[SISTEMA] Conectando ao Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssidAP, passwordAP);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("[SISTEMA] Wi-Fi Conectado com sucesso!");
  Serial.print("\n[SISTEMA] === ACESSE O PAINEL NO NAVEGADOR NESTE IP: ");
  Serial.print(WiFi.localIP());
  Serial.println(" ===");

  // if (esp_now_init() == ESP_OK) {
  //   esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  //   memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  //   peerInfo.channel = 0;
  //   peerInfo.encrypt = false;
  //   esp_now_add_peer(&peerInfo);
  // }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/dispensar", handleDispense);
  server.on("/agendar", handleAgendar);
  server.on("/remover", handleRemover);
  server.on("/limpar", handleLimpar);
  server.on("/estoque", handleEstoque);
  server.begin();

  stopMotor();
  // Mostra o IP por alguns segundos e depois entra na tela principal
  showMessage("Acesse o painel:", WiFi.localIP().toString(), 5000);
}

void loop() {
  server.handleClient();

  // --- Disparo dos agendamentos ---
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (listaAgendamentos[i].ativo && restanteMs(i) <= 0) {
      if (slotsOcupados > 0) {
        acionarDispensador();
        if (listaAgendamentos[i].repeteMillis > 0) {
          listaAgendamentos[i].alvoMillis = millis() + listaAgendamentos[i].repeteMillis;
        } else {
          listaAgendamentos[i].ativo = false;
        }
      } else {
        showMessage("ALERTA!", "Sem Remedio", 4000);
        listaAgendamentos[i].ativo = false;
      }
    }
  }

  // --- OLED: fim da mensagem temporária e atualização do cronômetro (1x por segundo) ---
  if (msgAtiva && (long)(millis() - msgAte) >= 0) msgAtiva = false;

  if (!msgAtiva && millis() - ultimoRefresh >= 1000) {
    ultimoRefresh = millis();
    telaPrincipal();
  }
}
