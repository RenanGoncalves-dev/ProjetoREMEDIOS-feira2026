/* ==========================================================================
   Controlador Principal da Aplicação (Estado, Eventos, RFID, Alarme e Regras)
   ========================================================================== */

const ElosApp = (function () {
  let state = { ocupados: 0, total: 7, agendamentos: [] };
  let lastSyncTimestamp = Date.now();
  let isTotalInitialized = false;
  let lastTriggeredScheduleKey = null;
  let lastAlarmTriggerMs = 0;

  function triggerSafeAlarm(desc, titulo) {
    if (Date.now() - lastAlarmTriggerMs < 3500) return;
    lastAlarmTriggerMs = Date.now();
    ElosAlarm.triggerAlarm(desc, titulo);
  }

  function handleIncomingPayload(line) {
    try {
      const json = JSON.parse(line);
      if (json.evento) {
        if (json.evento === 'DISPENSADO') {
          const msgDose = json.desc || 'Dose liberada na Caixa E.L.O.S.';
          ElosUI.showToast('Dose liberada: ' + msgDose);
          triggerSafeAlarm(msgDose, 'Hora do Remédio — E.L.O.S.');
        } else if (json.evento === 'DISPENSADO_RFID') {
          const uidInfo = json.uid ? ` (UID: ${json.uid})` : '';
          ElosUI.updateRfidStatus(json.uid || '', 'Dose de emergência liberada');
          ElosUI.showToast(`Dose liberada via Tag RFID${uidInfo}`);
          // Toca o alarme no celular APENAS se o usuário ativou a opção na aba Tag RFID
          if (ElosAlarm.shouldRingOnRfid()) {
            triggerSafeAlarm(
              `Dose de emergência liberada via Tag RFID${uidInfo}.`,
              'Liberação via Tag RFID — E.L.O.S.'
            );
          }
        } else if (json.evento === 'VAZIO' || json.evento === 'VAZIO_RFID') {
          if (json.evento === 'VAZIO_RFID') {
            ElosUI.updateRfidStatus(json.uid || '', 'Tentativa com caixa vazia');
          }
          ElosUI.showToast('Caixa vazia. Abasteça os compartimentos.');
          if (json.evento === 'VAZIO' || ElosAlarm.shouldRingOnRfid()) {
            triggerSafeAlarm(
              'Tentativa de liberação de dose, porém a caixa está sem medicamentos.',
              'Alerta: Caixa Vazia — E.L.O.S.'
            );
          }
        } else if (json.evento === 'FULL') {
          ElosUI.showToast('Limite de 8 agendamentos atingido.');
        } else if (json.evento === 'AGENDADO') {
          ElosUI.showToast('Horário salvo e sincronizado.');
        }
        return;
      }

      if (typeof json.ocupados === 'number') {
        state = json;
        lastSyncTimestamp = Date.now();
        if (!isTotalInitialized) {
          ElosUI.$('inp_total').value = state.total;
          isTotalInitialized = true;
        }
        ElosAlarm.syncScheduledNotifications(state.agendamentos || []);
        tick();
      }
    } catch (e) {
      console.log('Mensagem recebida da Caixa:', line);
    }
  }

  function tick() {
    const elapsedSeconds = (Date.now() - lastSyncTimestamp) / 1000;
    const nearest = ElosUI.renderDashboard(state, elapsedSeconds, (id) => {
      ElosBluetooth.sendCommand('REMOVER:' + id);
    });

    if (nearest && nearest.remaining <= 0) {
      const triggerKey = `${nearest.id}_${lastSyncTimestamp}`;
      if (lastTriggeredScheduleKey !== triggerKey) {
        lastTriggeredScheduleKey = triggerKey;
        triggerSafeAlarm(nearest.desc, 'Hora do Remédio — E.L.O.S.');
      }
      if (Date.now() - lastSyncTimestamp > 2500 && ElosBluetooth.isConnected()) {
        ElosBluetooth.sendCommand('STATUS');
      }
    }
  }

  function dispenseNow() {
    ElosBluetooth.sendCommand('DISPENSAR');
  }

  function updateStock() {
    const total = ElosUI.$('inp_total').value;
    const ocupados = ElosUI.$('inp_ocupados').value;
    if (ocupados === '' || total === '') {
      ElosUI.showToast('Informe a capacidade total e as doses abastecidas.');
      return;
    }
    ElosBluetooth.sendCommand(`ESTOQUE:${total}:${ocupados}`).then((ok) => {
      if (ok) {
        ElosUI.$('inp_ocupados').value = '';
        ElosUI.showToast('Estoque atualizado.');
      }
    });
  }

  function clearSchedules() {
    if (!confirm('Deseja remover todos os horários agendados?')) return;
    ElosBluetooth.sendCommand('LIMPAR');
  }

  function saveSchedule() {
    const form = ElosUI.getScheduleFormValues();
    let delay = 0;
    let repeat = 0;
    let timeText = '';

    if (form.mode === 'horario') {
      const hh = String(form.clockHour).padStart(2, '0');
      const mm = String(form.clockMin).padStart(2, '0');
      const now = new Date();
      const target = new Date(
        now.getFullYear(),
        now.getMonth(),
        now.getDate(),
        form.clockHour,
        form.clockMin,
        0
      );
      if (target <= now) target.setDate(target.getDate() + 1);
      delay = Math.floor((target - now) / 1000);
      if (delay < 1) delay = 1;

      if (form.repeatDaily) {
        repeat = 86400;
        timeText = `Diário às ${hh}:${mm}`;
      } else {
        timeText = `Às ${hh}:${mm}`;
      }
    } else if (form.mode === 'intervalo') {
      delay = form.intervalHours * 3600;
      repeat = form.intervalHours * 3600;
      timeText = `A cada ${form.intervalHours}h`;
    } else {
      delay = form.timerHours * 3600 + form.timerMins * 60;
      if (delay <= 0) {
        ElosUI.showToast('Selecione um tempo maior que zero.');
        return;
      }
      timeText =
        form.timerHours > 0
          ? `Em ${form.timerHours}h ${form.timerMins}m`
          : `Em ${form.timerMins} min`;
    }

    const finalDesc = form.medName ? `${form.medName} (${timeText})` : timeText;

    ElosBluetooth.sendCommand(`AGENDAR:${delay}:${repeat}:${finalDesc}`).then((ok) => {
      if (ok) {
        ElosUI.clearMedicineName();
      }
    });
  }

  function init() {
    ElosUI.updateThemeIcon();
    ElosUI.refreshClockDisplays();
    ElosAlarm.init();
    ElosBluetooth.onMessage(handleIncomingPayload);
    ElosBluetooth.onConnectionChange((connected) => {
      ElosUI.setConnectionStatus(connected);
    });
    setInterval(tick, 1000);
  }

  return {
    init,
    dispenseNow,
    updateStock,
    clearSchedules,
    saveSchedule
  };
})();

document.addEventListener('DOMContentLoaded', ElosApp.init);
