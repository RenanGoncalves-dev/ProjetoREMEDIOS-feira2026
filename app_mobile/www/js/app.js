/* ==========================================================================
   Controlador Principal da Aplicação (Estado, Eventos e Regras de Negócio)
   ========================================================================== */

const ElosApp = (function () {
  let state = { ocupados: 0, total: 7, agendamentos: [] };
  let lastSyncTimestamp = Date.now();
  let isTotalInitialized = false;

  function handleIncomingPayload(line) {
    try {
      const json = JSON.parse(line);
      if (json.evento) {
        if (json.evento === 'DISPENSADO') ElosUI.showToast('💊 Dose liberada!');
        else if (json.evento === 'VAZIO') ElosUI.showToast('⚠️ A caixa está VAZIA!');
        else if (json.evento === 'FULL') ElosUI.showToast('⚠️ Limite de 8 agendamentos atingido!');
        else if (json.evento === 'AGENDADO') ElosUI.showToast('✅ Dose agendada com sucesso!');
        return;
      }

      if (typeof json.ocupados === 'number') {
        state = json;
        lastSyncTimestamp = Date.now();
        if (!isTotalInitialized) {
          ElosUI.$('inp_total').value = state.total;
          isTotalInitialized = true;
        }
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

    // Quando o cronômetro regressivo zera, solicita atualização automática ao ESP32
    if (
      nearest &&
      nearest.remaining <= 0 &&
      Date.now() - lastSyncTimestamp > 2500 &&
      ElosBluetooth.isConnected()
    ) {
      ElosBluetooth.sendCommand('STATUS');
    }
  }

  function dispenseNow() {
    ElosBluetooth.sendCommand('DISPENSAR');
  }

  function updateStock() {
    const total = ElosUI.$('inp_total').value;
    const ocupados = ElosUI.$('inp_ocupados').value;
    if (ocupados === '' || total === '') {
      ElosUI.showToast('⚠️ Preencha os dois valores!');
      return;
    }
    ElosBluetooth.sendCommand(`ESTOQUE:${total}:${ocupados}`).then((ok) => {
      if (ok) {
        ElosUI.$('inp_ocupados').value = '';
        ElosUI.showToast('✅ Estoque atualizado!');
      }
    });
  }

  function clearSchedules() {
    if (!confirm('Deseja apagar todos os horários agendados?')) return;
    ElosBluetooth.sendCommand('LIMPAR');
  }

  function saveSchedule() {
    const mode = ElosUI.$('modo').value;
    let delay = 0;
    let repeat = 0;
    let desc = '';

    if (mode === 'horario') {
      const timeVal = ElosUI.$('h_time').value;
      if (!timeVal) {
        ElosUI.showToast('⚠️ Preencha o horário!');
        return;
      }
      const parts = timeVal.split(':');
      const now = new Date();
      const target = new Date(
        now.getFullYear(),
        now.getMonth(),
        now.getDate(),
        +parts[0],
        +parts[1],
        0
      );
      if (target <= now) target.setDate(target.getDate() + 1);
      delay = Math.floor((target - now) / 1000);
      if (ElosUI.$('h_diario').checked) {
        repeat = 86400;
        desc = 'Todos os dias às ' + timeVal;
      } else {
        desc = 'Às ' + timeVal;
      }
    } else if (mode === 'cronometro') {
      const h = parseInt(ElosUI.$('c_horas').value || 0, 10);
      const m = parseInt(ElosUI.$('c_mins').value || 0, 10);
      if (h === 0 && m === 0) {
        ElosUI.showToast('⚠️ Insira um tempo!');
        return;
      }
      delay = h * 3600 + m * 60;
      desc = 'Daqui a ' + h + 'h ' + m + 'm';
    } else {
      const h = parseInt(ElosUI.$('i_horas').value || 0, 10);
      if (h <= 0) {
        ElosUI.showToast('⚠️ Preencha as horas!');
        return;
      }
      delay = h * 3600;
      repeat = h * 3600;
      desc = 'A cada ' + h + ' horas';
    }

    ElosBluetooth.sendCommand(`AGENDAR:${delay}:${repeat}:${desc}`);
  }

  function init() {
    ElosUI.updateThemeIcon();
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

