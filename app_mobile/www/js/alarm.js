/* ==========================================================================
   Módulo de Alarme, Toques Nativos do Celular e Notificações Fora do App
   - Acessa os toques pré-instalados no Android via ponte Java (window.ElosNative)
   - Permite alternar entre Som de Notificação (Mensagem) e Toque Inteiro (Chamada)
   - Permite pesquisar pelo nome do toque no celular + Seletor Nativo do Android
   - Controla se o Cartão RFID de Emergência deve ou não tocar o alarme no celular
   ========================================================================== */

const ElosAlarm = (function () {
  const LocalNotifications =
    window.capacitorLocalNotifications && window.capacitorLocalNotifications.LocalNotifications
      ? window.capacitorLocalNotifications.LocalNotifications
      : null;

  const CHANNEL_ID = 'elos_med_alarm_v2';

  // Preferências salvas no celular
  let soundCategory = localStorage.getItem('elos-sound-cat') || 'chamada'; // 'notificacao' | 'chamada'
  let selectedToneUri = localStorage.getItem('elos-tone-uri') || '';
  let selectedToneTitle = localStorage.getItem('elos-tone-title') || 'Padrão do Celular';
  let ringOnRfid = localStorage.getItem('elos-ring-rfid') === 'true';

  // Lista carregada dos toques pré-instalados no celular
  let phoneTonesList = [];
  let searchFilter = '';

  let audioCtx = null;
  let webFallbackInterval = null;
  let previewTimeout = null;

  function hasNativeBridge() {
    return typeof window.ElosNative !== 'undefined' && window.ElosNative !== null;
  }

  async function init() {
    // Sincroniza o checkbox do RFID e abas de som na interface
    const chkRfid = document.getElementById('chk_alarme_rfid');
    if (chkRfid) chkRfid.checked = ringOnRfid;

    updateSoundCategoryUI();
    updateSelectedToneLabel();
    loadPhoneTones();

    // Callback quando o usuário escolhe um toque pela janela nativa do sistema Android
    window.onNativeRingtonePicked = function (uri, title) {
      selectTone(uri, title, true);
    };

    if (LocalNotifications) {
      try {
        await LocalNotifications.requestPermissions();
        await LocalNotifications.createChannel({
          id: CHANNEL_ID,
          name: 'Alertas de Medicamentos E.L.O.S.',
          description: 'Notificações fora do aplicativo quando o remédio é liberado',
          importance: 5,
          visibility: 1,
          vibration: true,
          lights: true
        });
      } catch (e) {
        console.log('Init LocalNotifications:', e);
      }
    }
  }

  function setRingOnRfid(enabled) {
    ringOnRfid = !!enabled;
    localStorage.setItem('elos-ring-rfid', ringOnRfid ? 'true' : 'false');
    ElosUI.showToast(
      ringOnRfid
        ? '🔔 Alarme ativado também para liberações via Cartão RFID'
        : '🔕 Liberação por Cartão RFID ficará silenciosa no celular'
    );
  }

  function shouldRingOnRfid() {
    return ringOnRfid;
  }

  function setSoundCategory(cat) {
    soundCategory = cat === 'notificacao' ? 'notificacao' : 'chamada';
    localStorage.setItem('elos-sound-cat', soundCategory);
    stopPreview();
    updateSoundCategoryUI();
    loadPhoneTones();
  }

  function updateSoundCategoryUI() {
    const tabNotif = document.getElementById('tab_som_notif');
    const tabChamada = document.getElementById('tab_som_chamada');
    if (tabNotif) tabNotif.classList.toggle('active', soundCategory === 'notificacao');
    if (tabChamada) tabChamada.classList.toggle('active', soundCategory === 'chamada');
  }

  function updateSelectedToneLabel() {
    const el = document.getElementById('lbl_toque_atual');
    if (el) {
      const tipoTxt =
        soundCategory === 'chamada' ? '📞 Toque de Chamada' : '💬 Som de Mensagem';
      el.textContent = `${selectedToneTitle} (${tipoTxt})`;
    }
  }

  function loadPhoneTones() {
    phoneTonesList = [];
    if (hasNativeBridge()) {
      try {
        const jsonStr = window.ElosNative.listarToquesCelular(soundCategory);
        const parsed = JSON.parse(jsonStr || '[]');
        if (Array.isArray(parsed)) {
          phoneTonesList = parsed;
        }
      } catch (e) {
        console.error('Erro ao listar toques do celular:', e);
      }
    }

    // Sempre inclui a opção Padrão do Sistema no topo
    phoneTonesList.unshift({
      title:
        soundCategory === 'chamada'
          ? '⭐ Toque de Chamada Padrão do Celular'
          : '⭐ Som de Notificação Padrão do Celular',
      uri: ''
    });

    renderToneList();
  }

  function filterTones(query) {
    searchFilter = (query || '').trim().toLowerCase();
    renderToneList();
  }

  function renderToneList() {
    const ul = document.getElementById('lista_toques_celular');
    if (!ul) return;
    ul.innerHTML = '';

    const filtrados = phoneTonesList.filter((item) =>
      item.title.toLowerCase().includes(searchFilter)
    );

    if (filtrados.length === 0) {
      const li = document.createElement('li');
      li.className = 'empty';
      li.textContent = 'Nenhum toque encontrado com esse nome';
      ul.appendChild(li);
      return;
    }

    filtrados.forEach((item) => {
      const li = document.createElement('li');
      li.className = 'tone-item' + (item.uri === selectedToneUri ? ' active' : '');

      const btnSelect = document.createElement('button');
      btnSelect.type = 'button';
      btnSelect.className = 'tone-select-btn';
      btnSelect.innerHTML =
        (item.uri === selectedToneUri ? '🟢 ' : '🎵 ') +
        '<span>' +
        item.title +
        '</span>';
      btnSelect.onclick = () => selectTone(item.uri, item.title, true);

      const btnPlay = document.createElement('button');
      btnPlay.type = 'button';
      btnPlay.className = 'tone-play-btn';
      btnPlay.textContent = '▶️ Ouvir';
      btnPlay.onclick = (e) => {
        e.stopPropagation();
        selectTone(item.uri, item.title, true);
      };

      li.append(btnSelect, btnPlay);
      ul.appendChild(li);
    });
  }

  function selectTone(uri, title, playSample) {
    selectedToneUri = uri || '';
    selectedToneTitle = title || 'Padrão do Celular';
    localStorage.setItem('elos-tone-uri', selectedToneUri);
    localStorage.setItem('elos-tone-title', selectedToneTitle);
    updateSelectedToneLabel();
    renderToneList();

    if (playSample) {
      previewSelectedTone();
    }
  }

  function openSystemRingtonePicker() {
    if (hasNativeBridge()) {
      stopPreview();
      window.ElosNative.abrirSeletorDoSistema(soundCategory);
    } else {
      ElosUI.showToast('ℹ️ Seletor nativo disponível dentro do APK instalado no Android.');
    }
  }

  function previewSelectedTone() {
    stopPreview();
    if (hasNativeBridge()) {
      window.ElosNative.testarToque(selectedToneUri, false);
      previewTimeout = setTimeout(() => {
        try {
          window.ElosNative.pararToque();
        } catch (_) {}
      }, 4000);
    } else {
      playWebFallbackBeep();
    }
  }

  function stopPreview() {
    if (previewTimeout) {
      clearTimeout(previewTimeout);
      previewTimeout = null;
    }
    if (hasNativeBridge()) {
      try {
        window.ElosNative.pararToque();
      } catch (_) {}
    }
  }

  // Fallback sonoro caso aberto em navegador web comum
  function playWebFallbackBeep() {
    try {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      if (!AudioContextClass) return;
      if (!audioCtx) audioCtx = new AudioContextClass();
      if (audioCtx.state === 'suspended') audioCtx.resume();

      const now = audioCtx.currentTime;
      const notes = [587.33, 783.99, 880.0, 1174.66];
      notes.forEach((freq, idx) => {
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'triangle';
        osc.frequency.setValueAtTime(freq, now + idx * 0.16);
        gain.gain.setValueAtTime(0.001, now + idx * 0.16);
        gain.gain.exponentialRampToValueAtTime(0.45, now + idx * 0.16 + 0.03);
        gain.gain.exponentialRampToValueAtTime(0.001, now + idx * 0.16 + 0.14);
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start(now + idx * 0.16);
        osc.stop(now + idx * 0.16 + 0.15);
      });
    } catch (_) {}
  }

  // Sincroniza os horários programados com o agendador nativo do Android (para tocar fora do app!)
  async function syncScheduledNotifications(agendamentos) {
    const isCallMode = soundCategory === 'chamada';

    if (hasNativeBridge()) {
      try {
        window.ElosNative.limparAgendamentosNativos();
        (agendamentos || []).forEach((a, idx) => {
          if (a.restante > 1) {
            window.ElosNative.agendarNotificacaoEmSegundos(
              100 + idx,
              Math.floor(a.restante),
              '💊 Hora do Remédio! (E.L.O.S.)',
              `Dose liberada: ${a.desc}. Retire seu medicamento na caixa!`,
              selectedToneUri,
              isCallMode
            );
          }
        });
      } catch (e) {
        console.error('Erro ao agendar notificação nativa:', e);
      }
    }

    if (LocalNotifications) {
      try {
        const idsAntigos = [];
        for (let i = 100; i <= 120; i++) idsAntigos.push({ id: i });
        await LocalNotifications.cancel({ notifications: idsAntigos });

        const novas = (agendamentos || [])
          .filter((a) => a.restante > 2)
          .map((a, idx) => ({
            id: 100 + idx,
            title: '💊 Hora do Remédio! (E.L.O.S.)',
            body: `Dose liberada: ${a.desc}. Retire seu medicamento na caixa!`,
            channelId: CHANNEL_ID,
            schedule: { at: new Date(Date.now() + a.restante * 1000), allowWhileIdle: true }
          }));

        if (novas.length > 0) {
          await LocalNotifications.schedule({ notifications: novas });
        }
      } catch (_) {}
    }
  }

  // Dispara a Notificação Fora do App + Toque Pré-instalado do Celular + Vibração + Modal
  function triggerAlarm(descricaoDose, tituloPersonalizado) {
    stopPreview();
    const titulo = tituloPersonalizado || '💊 HORA DO REMÉDIO! (E.L.O.S.)';
    const desc = descricaoDose || 'Sua dose foi liberada na Caixa E.L.O.S.!';
    const isCallMode = soundCategory === 'chamada';

    // 1. Dispara Notificação Real do Sistema Android (aparece fora do app) + Toca o Toque do Celular + Vibra
    if (hasNativeBridge()) {
      try {
        window.ElosNative.dispararAlertaNativo(titulo, desc, selectedToneUri, isCallMode);
      } catch (e) {
        console.error('Erro no alerta nativo:', e);
      }
    } else {
      playWebFallbackBeep();
      if (isCallMode && !webFallbackInterval) {
        webFallbackInterval = setInterval(playWebFallbackBeep, 2000);
      }
    }

    // 2. Exibe o Modal de Alerta Visual para Foco (TDAH / PCD)
    const modal = document.getElementById('alarmModal');
    const modalDesc = document.getElementById('alarmModalDesc');
    const modalTime = document.getElementById('alarmModalTime');

    if (modalDesc) modalDesc.textContent = desc;
    if (modalTime) {
      const agora = new Date();
      modalTime.textContent = agora.toLocaleTimeString('pt-BR', {
        hour: '2-digit',
        minute: '2-digit'
      });
    }
    if (modal) modal.classList.remove('hidden');
  }

  function stopAlarm() {
    stopPreview();
    if (webFallbackInterval) {
      clearInterval(webFallbackInterval);
      webFallbackInterval = null;
    }
    if (hasNativeBridge()) {
      try {
        window.ElosNative.pararToque();
      } catch (_) {}
    }
    const modal = document.getElementById('alarmModal');
    if (modal) modal.classList.add('hidden');
  }

  return {
    init,
    setRingOnRfid,
    shouldRingOnRfid,
    setSoundCategory,
    filterTones,
    openSystemRingtonePicker,
    previewSelectedTone,
    stopPreview,
    triggerAlarm,
    stopAlarm,
    syncScheduledNotifications
  };
})();
