/* ==========================================================================
   Camada de Interface de Usuário (UI) — DOM, Relógio Acessível PCD/TDAH e Temas
   ========================================================================== */

const ElosUI = (function () {
  const $ = (id) => document.getElementById(id);
  let toastTimer = null;

  // Estado interno do Seletor de Horário Acessível
  let activeMode = 'horario'; // 'horario' | 'intervalo' | 'cronometro'
  let clockHour = 8;
  let clockMin = 0;
  let intervalHours = 8;
  let timerHours = 0;
  let timerMins = 15;

  function updateThemeIcon() {
    const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
    $('themeBtn').textContent = isDark ? '☀️' : '🌙';
  }

  function toggleTheme() {
    const nextTheme =
      document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', nextTheme);
    localStorage.setItem('elos-theme', nextTheme);
    updateThemeIcon();
  }

  function showToast(msg) {
    const toast = $('toast');
    toast.textContent = msg;
    toast.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove('show'), 2800);
  }

  function formatDuration(seconds) {
    seconds = Math.max(0, Math.floor(seconds));
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return [h, m, s].map((n) => String(n).padStart(2, '0')).join(':');
  }

  function setConnectionStatus(connected) {
    if (connected) {
      $('conn').classList.add('on');
      $('btBtn').textContent = '🟢 Conectado';
      $('btBtn').classList.add('connected');
      showToast('✅ Conectado à ELOS_Caixa!');
    } else {
      $('conn').classList.remove('on');
      $('btBtn').textContent = '🔵 Conectar Caixa';
      $('btBtn').classList.remove('connected');
      showToast('🔴 Caixa desconectada.');
    }
  }

  /* ---------- Controles do Novo Seletor Acessível ---------- */
  function refreshClockDisplays() {
    $('disp_hora').textContent = String(clockHour).padStart(2, '0');
    $('disp_min').textContent = String(clockMin).padStart(2, '0');
    $('disp_intervalo').textContent = String(intervalHours) + 'h';
    $('disp_c_hora').textContent = String(timerHours).padStart(2, '0');
    $('disp_c_min').textContent = String(timerMins).padStart(2, '0');
  }

  function selectMode(mode) {
    activeMode = mode;
    ['horario', 'intervalo', 'cronometro'].forEach((m) => {
      $('div_' + m).classList.toggle('hidden', m !== mode);
      $('tab_' + m).classList.toggle('active', m === mode);
    });
  }

  function stepClock(deltaH, deltaM) {
    clockHour = (clockHour + deltaH + 24) % 24;
    clockMin = (clockMin + deltaM + 60) % 60;
    refreshClockDisplays();
  }

  function setClockPreset(h, m, labelSugerido) {
    clockHour = h;
    clockMin = m;
    const inpNome = $('inp_nome_remedio');
    if (inpNome && !inpNome.value.trim()) {
      inpNome.placeholder = 'Ex: Dose da ' + labelSugerido;
    }
    refreshClockDisplays();
    showToast(`🕒 Horário ajustado para ${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`);
  }

  function stepInterval(deltaH) {
    intervalHours = Math.max(1, Math.min(48, intervalHours + deltaH));
    refreshClockDisplays();
  }

  function setIntervalPreset(h) {
    intervalHours = h;
    refreshClockDisplays();
    showToast(`🔁 Intervalo de ${h} em ${h} horas selecionado`);
  }

  function stepTimer(deltaH, deltaM) {
    timerHours = Math.max(0, Math.min(99, timerHours + deltaH));
    timerMins = (timerMins + deltaM + 60) % 60;
    if (timerHours === 0 && timerMins === 0) timerMins = 1;
    refreshClockDisplays();
  }

  function setTimerPreset(h, m) {
    timerHours = h;
    timerMins = m;
    refreshClockDisplays();
    showToast(`⏳ Cronômetro ajustado para ${h > 0 ? h + 'h ' : ''}${m} min`);
  }

  function getScheduleFormValues() {
    const medName = ($('inp_nome_remedio').value || '').trim();
    const repeatDaily = $('h_diario').checked;
    return {
      mode: activeMode,
      medName,
      clockHour,
      clockMin,
      repeatDaily,
      intervalHours,
      timerHours,
      timerMins
    };
  }

  function clearMedicineName() {
    $('inp_nome_remedio').value = '';
  }

  function renderDashboard(state, elapsedSeconds, onRemoveSchedule) {
    // 1. Indicador de Estoque
    $('eOcup').textContent = state.ocupados;
    $('eTotal').textContent = state.total;
    const pct = state.total > 0 ? (state.ocupados / state.total) * 100 : 0;
    const bar = $('eBar');
    bar.style.width = pct + '%';
    bar.style.background =
      pct <= 15 ? 'var(--danger)' : pct <= 40 ? 'var(--warn)' : 'var(--ok)';

    // 2. Lista de Agendamentos e Próxima Dose
    const ul = $('lista');
    ul.innerHTML = '';
    const items = state.agendamentos || [];
    let nearest = null;

    if (items.length === 0) {
      const li = document.createElement('li');
      li.className = 'empty';
      li.textContent = 'Nenhum agendamento programado';
      ul.appendChild(li);
    }

    items.forEach((item) => {
      const remaining = Math.max(0, item.restante - elapsedSeconds);
      if (nearest === null || remaining < nearest.remaining) {
        nearest = { id: item.id, remaining, desc: item.desc };
      }

      const li = document.createElement('li');

      const info = document.createElement('div');
      info.className = 'info';

      const title = document.createElement('strong');
      title.textContent = '💊 ' + item.desc;

      const sub = document.createElement('span');
      sub.className = 'muted';
      sub.textContent =
        item.repete > 0 ? '🔁 Repete a cada ' + formatDuration(item.repete) : '1️⃣ Dose única';

      info.append(title, sub);

      const timeEl = document.createElement('span');
      timeEl.className = 'time';
      timeEl.textContent = formatDuration(remaining);

      const delBtn = document.createElement('button');
      delBtn.className = 'del';
      delBtn.textContent = '✕';
      delBtn.setAttribute('aria-label', 'Remover agendamento');
      delBtn.onclick = () => onRemoveSchedule(item.id);

      li.append(info, timeEl, delBtn);
      ul.appendChild(li);
    });

    $('proxTempo').textContent = nearest ? formatDuration(nearest.remaining) : '--:--:--';
    $('proxDesc').textContent = nearest ? '💊 ' + nearest.desc : 'Nenhum agendamento ativo';

    return nearest;
  }

  return {
    $,
    updateThemeIcon,
    toggleTheme,
    showToast,
    setConnectionStatus,
    refreshClockDisplays,
    selectMode,
    stepClock,
    setClockPreset,
    stepInterval,
    setIntervalPreset,
    stepTimer,
    setTimerPreset,
    getScheduleFormValues,
    clearMedicineName,
    renderDashboard
  };
})();
