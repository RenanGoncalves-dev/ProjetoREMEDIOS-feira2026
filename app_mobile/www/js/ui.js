/* ==========================================================================
   Camada de Interface de Usuário (UI) — DOM, Temas e Componentes Visuais
   ========================================================================== */

const ElosUI = (function () {
  const $ = (id) => document.getElementById(id);
  let toastTimer = null;

  function updateThemeIcon() {
    const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
    $('themeBtn').textContent = isDark ? '☀️' : '🌙';
  }

  function toggleTheme() {
    const nextTheme = document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
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

  function switchScheduleMode() {
    const selected = $('modo').value;
    ['horario', 'cronometro', 'intervalo'].forEach((m) => {
      $('div_' + m).classList.toggle('hidden', m !== selected);
    });
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
        nearest = { remaining, desc: item.desc };
      }

      const li = document.createElement('li');

      const info = document.createElement('div');
      info.className = 'info';

      const title = document.createElement('strong');
      title.textContent = item.desc;

      const sub = document.createElement('span');
      sub.className = 'muted';
      sub.textContent =
        item.repete > 0 ? 'Repete a cada ' + formatDuration(item.repete) : 'Dose única';

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
    $('proxDesc').textContent = nearest ? nearest.desc : 'Nenhum agendamento ativo';

    return nearest;
  }

  return {
    $,
    updateThemeIcon,
    toggleTheme,
    showToast,
    setConnectionStatus,
    switchScheduleMode,
    renderDashboard
  };
})();

