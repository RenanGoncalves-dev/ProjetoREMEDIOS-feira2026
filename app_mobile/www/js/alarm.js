/* ==========================================================================
   Módulo de Alarme Multi-Sensorial e Notificações Android (Suporte PCD/TDAH)
   - Notificações Nativas na Barra do Android (@capacitor/local-notifications)
   - Alarme Sonoro Contínuo Offline (Web Audio API)
   - Vibração Tátil + Aviso por Voz (Web Speech API / TTS em Português)
   - Modal de Tela Cheia para Foco de Atenção (TDAH / Baixa Visão)
   ========================================================================== */

const ElosAlarm = (function () {
  const LocalNotifications =
    window.capacitorLocalNotifications && window.capacitorLocalNotifications.LocalNotifications
      ? window.capacitorLocalNotifications.LocalNotifications
      : null;

  const CHANNEL_ID = 'elos_med_alarm_v1';
  let audioCtx = null;
  let alarmInterval = null;
  let isRinging = false;

  async function init() {
    if (!LocalNotifications) return;
    try {
      await LocalNotifications.requestPermissions();
      await LocalNotifications.createChannel({
        id: CHANNEL_ID,
        name: 'Alarmes de Medicamentos E.L.O.S.',
        description: 'Avisos sonoros e notificações na hora de tomar o remédio',
        importance: 5, // Importância Máxima (aparece por cima da tela e toca som)
        visibility: 1,
        vibration: true,
        lights: true
      });

      // Quando o usuário toca na notificação do Android, abre o modal de confirmação
      LocalNotifications.addListener('localNotificationActionPerformed', (notificationAction) => {
        const n = notificationAction.notification;
        triggerAlarm(n.body || 'Sua dose programada foi liberada na Caixa E.L.O.S.!', false);
      });
    } catch (e) {
      console.log('Inicialização de notificações:', e);
    }
  }

  // Toca uma sequência melódica de alerta médico usando Web Audio API (100% offline)
  function playBeepSequence() {
    try {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      if (!AudioContextClass) return;
      if (!audioCtx) audioCtx = new AudioContextClass();
      if (audioCtx.state === 'suspended') audioCtx.resume();

      const now = audioCtx.currentTime;
      const notes = [587.33, 783.99, 880.0, 1174.66]; // Ré5, Sol5, Lá5, Ré6 (som claro e chamativo)

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
    } catch (e) {
      console.log('Erro ao tocar som:', e);
    }

    // Vibra o celular (padrão tátil forte)
    if (navigator.vibrate) {
      try {
        navigator.vibrate([400, 150, 400, 150, 600]);
      } catch (_) {}
    }
  }

  // Fala em voz alta para acessibilidade (Deficiência Visual / TDAH)
  function speakAlert(texto) {
    if (!('speechSynthesis' in window)) return;
    try {
      window.speechSynthesis.cancel();
      const msg = new SpeechSynthesisUtterance(
        'Atenção! Hora do seu remédio. ' + texto + '. Retire sua dose na caixa ELOS.'
      );
      msg.lang = 'pt-BR';
      msg.rate = 1.0;
      msg.pitch = 1.05;
      window.speechSynthesis.speak(msg);
    } catch (_) {}
  }

  // Dispara notificação imediata na barra de status do Android
  async function sendInstantNotification(titulo, corpo) {
    if (!LocalNotifications) return;
    try {
      await LocalNotifications.schedule({
        notifications: [
          {
            id: Math.floor(Date.now() % 100000) + 1000,
            title: titulo,
            body: corpo,
            channelId: CHANNEL_ID,
            schedule: { at: new Date(Date.now() + 300), allowWhileIdle: true }
          }
        ]
      });
    } catch (e) {
      console.log('Erro ao enviar notificação imediata:', e);
    }
  }

  // Sincroniza os agendamentos da Caixa com os Alarmes Locais do Android
  // (Garante que o celular toque e notifique na hora certa mesmo se sair de perto do Bluetooth!)
  async function syncScheduledNotifications(agendamentos) {
    if (!LocalNotifications) return;
    try {
      // Remove notificações agendadas anteriores (IDs 100 a 120)
      const idsAntigos = [];
      for (let i = 100; i <= 120; i++) idsAntigos.push({ id: i });
      await LocalNotifications.cancel({ notifications: idsAntigos });

      if (!agendamentos || agendamentos.length === 0) return;

      const novasNotificacoes = agendamentos
        .filter((a) => a.restante > 2)
        .map((a, idx) => {
          const disparoEm = new Date(Date.now() + a.restante * 1000);
          return {
            id: 100 + idx,
            title: '💊 Hora do Remédio! (Caixa E.L.O.S.)',
            body: `Dose liberada: ${a.desc}. Retire seu medicamento na caixa!`,
            channelId: CHANNEL_ID,
            schedule: {
              at: disparoEm,
              allowWhileIdle: true
            }
          };
        });

      if (novasNotificacoes.length > 0) {
        await LocalNotifications.schedule({ notifications: novasNotificacoes });
      }
    } catch (e) {
      console.log('Erro ao sincronizar notificações agendadas:', e);
    }
  }

  // Dispara o Alarme Completo (Som + Vibração + Voz + Notificação + Tela Cheia)
  function triggerAlarm(descricaoDose, enviarNotificacaoBarra = true) {
    const desc = descricaoDose || 'Dose liberada na Caixa E.L.O.S.';

    if (enviarNotificacaoBarra) {
      sendInstantNotification(
        '💊 REMÉDIO LIBERADO! (E.L.O.S.)',
        `${desc} — Hora de tomar o seu medicamento!`
      );
    }

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

    if (isRinging) return;
    isRinging = true;

    playBeepSequence();
    speakAlert(desc);

    alarmInterval = setInterval(() => {
      playBeepSequence();
    }, 2000);
  }

  function stopAlarm() {
    isRinging = false;
    if (alarmInterval) {
      clearInterval(alarmInterval);
      alarmInterval = null;
    }
    if (navigator.vibrate) {
      try {
        navigator.vibrate(0);
      } catch (_) {}
    }
    if ('speechSynthesis' in window) {
      try {
        window.speechSynthesis.cancel();
      } catch (_) {}
    }
    const modal = document.getElementById('alarmModal');
    if (modal) modal.classList.add('hidden');
  }

  return {
    init,
    triggerAlarm,
    stopAlarm,
    syncScheduledNotifications
  };
})();

