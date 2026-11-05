import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ElosDeviceState,
  ElosEventPayload,
  MainTab,
  PhoneTone,
  RfidLogEntry,
  SoundCategory
} from './types/elos';
import { bluetoothService } from './services/bluetoothService';
import { alarmService } from './services/alarmService';
import { TabAgendamento } from './components/TabAgendamento';
import { TabRfid } from './components/TabRfid';
import { TabNotificacoes } from './components/TabNotificacoes';
import { AlarmModal } from './components/AlarmModal';
import {
  IconBell,
  IconBluetooth,
  IconClock,
  IconMoon,
  IconRfid,
  IconSun
} from './components/Icons';

export const App: React.FC = () => {
  // Navegação principal por abas (renderização condicional estrita: apenas 1 aba na tela!)
  const [activeTab, setActiveTab] = useState<MainTab>('agendamento');
  const [theme, setTheme] = useState<'light' | 'dark'>(() => {
    return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
  });

  // Estado da conexão BLE e da Caixa E.L.O.S.
  const [connected, setConnected] = useState(false);
  const [deviceState, setDeviceState] = useState<ElosDeviceState>({
    ocupados: 0,
    total: 7,
    agendamentos: []
  });
  const [elapsedSeconds, setElapsedSeconds] = useState(0);
  const lastSyncMsRef = useRef<number>(Date.now());
  const lastTriggeredKeyRef = useRef<string | null>(null);
  const lastAlarmTimeRef = useRef<number>(0);

  // Estado RFID
  const [ringOnRfid, setRingOnRfid] = useState<boolean>(() => {
    return localStorage.getItem('elos-ring-rfid') === 'true';
  });
  const [rfidHistory, setRfidHistory] = useState<RfidLogEntry[]>([]);

  // Estado de Toques do Celular & Notificações
  const [soundCategory, setSoundCategory] = useState<SoundCategory>(() => {
    return localStorage.getItem('elos-sound-cat') === 'notificacao' ? 'notificacao' : 'chamada';
  });
  const [selectedToneUri, setSelectedToneUri] = useState<string>(() => {
    return localStorage.getItem('elos-tone-uri') || '';
  });
  const [selectedToneTitle, setSelectedToneTitle] = useState<string>(() => {
    return localStorage.getItem('elos-tone-title') || 'Padrão do Sistema';
  });
  const [phoneTones, setPhoneTones] = useState<PhoneTone[]>([]);

  // Estado do Modal de Alarme e Toast
  const [alarmModal, setAlarmModal] = useState<{
    visible: boolean;
    title: string;
    description: string;
    timeText: string;
  }>({
    visible: false,
    title: '',
    description: '',
    timeText: ''
  });
  const [toastMsg, setToastMsg] = useState<string | null>(null);
  const toastTimerRef = useRef<number | null>(null);

  const showToast = useCallback((msg: string) => {
    setToastMsg(msg);
    if (toastTimerRef.current) clearTimeout(toastTimerRef.current);
    toastTimerRef.current = window.setTimeout(() => setToastMsg(null), 2800);
  }, []);

  const triggerSafeAlarm = useCallback(
    (description: string, title = 'Hora do Remédio — E.L.O.S.') => {
      if (Date.now() - lastAlarmTimeRef.current < 3500) return;
      lastAlarmTimeRef.current = Date.now();

      alarmService.triggerAlarm(title, description, selectedToneUri, soundCategory);

      const nowStr = new Date().toLocaleTimeString('pt-BR', {
        hour: '2-digit',
        minute: '2-digit'
      });
      setAlarmModal({
        visible: true,
        title,
        description,
        timeText: nowStr
      });
    },
    [selectedToneUri, soundCategory]
  );

  // Carrega a lista de toques pré-instalados no celular sempre que a categoria muda
  useEffect(() => {
    const list = alarmService.fetchPhoneTones(soundCategory);
    setPhoneTones(list);
  }, [soundCategory]);

  // Inicializa permissões e registra callback do seletor nativo do Android
  useEffect(() => {
    alarmService.initializePermissions();
    window.onNativeRingtonePicked = (uri: string, title: string) => {
      const cleanUri = uri || '';
      const cleanTitle = title || 'Padrão do Sistema';
      setSelectedToneUri(cleanUri);
      setSelectedToneTitle(cleanTitle);
      localStorage.setItem('elos-tone-uri', cleanUri);
      localStorage.setItem('elos-tone-title', cleanTitle);
      alarmService.previewTone(cleanUri);
    };
  }, []);

  // Escuta mensagens Bluetooth vindas do ESP32
  useEffect(() => {
    const onBleMessage = (line: string) => {
      try {
        const payload: ElosEventPayload = JSON.parse(line);

        if (payload.evento) {
          const timeStr = new Date().toLocaleTimeString('pt-BR', {
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
          });

          if (payload.evento === 'DISPENSADO') {
            const desc = payload.desc || 'Dose programada liberada na Caixa E.L.O.S.';
            showToast(`Dose liberada: ${desc}`);
            triggerSafeAlarm(desc, 'Hora do Remédio — E.L.O.S.');
          } else if (payload.evento === 'DISPENSADO_RFID') {
            const uid = payload.uid || 'Cartão';
            const entry: RfidLogEntry = {
              uid,
              status: 'Dose de emergência liberada',
              time: timeStr,
              success: true
            };
            setRfidHistory((prev) => [entry, ...prev.slice(0, 14)]);
            showToast(`Dose liberada via Tag RFID (${uid})`);

            if (localStorage.getItem('elos-ring-rfid') === 'true') {
              triggerSafeAlarm(
                `Dose de emergência liberada via Tag RFID (UID: ${uid}).`,
                'Liberação via Tag RFID — E.L.O.S.'
              );
            }
          } else if (payload.evento === 'VAZIO' || payload.evento === 'VAZIO_RFID') {
            if (payload.evento === 'VAZIO_RFID') {
              const uid = payload.uid || 'Cartão';
              setRfidHistory((prev) => [
                {
                  uid,
                  status: 'Tentativa com caixa vazia',
                  time: timeStr,
                  success: false
                },
                ...prev.slice(0, 14)
              ]);
            }
            showToast('Caixa vazia. Abasteça os compartimentos.');
            if (
              payload.evento === 'VAZIO' ||
              localStorage.getItem('elos-ring-rfid') === 'true'
            ) {
              triggerSafeAlarm(
                'Tentativa de liberar dose, porém a caixa está sem medicamentos.',
                'Alerta: Caixa Vazia — E.L.O.S.'
              );
            }
          } else if (payload.evento === 'FULL') {
            showToast('Limite máximo de 8 agendamentos atingido.');
          } else if (payload.evento === 'AGENDADO') {
            showToast('Horário programado e sincronizado.');
          }
          return;
        }

        if (typeof payload.ocupados === 'number') {
          const nextState: ElosDeviceState = {
            ocupados: payload.ocupados,
            total: payload.total ?? 7,
            agendamentos: payload.agendamentos || []
          };
          lastSyncMsRef.current = Date.now();
          setElapsedSeconds(0);
          setDeviceState(nextState);

          alarmService.syncScheduledAlarms(
            nextState.agendamentos,
            localStorage.getItem('elos-tone-uri') || '',
            localStorage.getItem('elos-sound-cat') === 'notificacao'
              ? 'notificacao'
              : 'chamada'
          );
        }
      } catch {
        // ignora linhas não-JSON
      }
    };

    bluetoothService.setListeners(onBleMessage, setConnected, showToast);
  }, [showToast, triggerSafeAlarm]);

  // Timer de 1 segundo para atualizar o contador regressivo sem recriar componentes
  useEffect(() => {
    const id = window.setInterval(() => {
      const elapsed = Math.floor((Date.now() - lastSyncMsRef.current) / 1000);
      setElapsedSeconds(elapsed);

      const items = deviceState.agendamentos || [];
      if (items.length > 0) {
        let nearest = items[0];
        for (const item of items) {
          if (item.restante < nearest.restante) nearest = item;
        }
        const rem = Math.max(0, nearest.restante - elapsed);
        if (rem <= 0) {
          const key = `${nearest.id}_${lastSyncMsRef.current}`;
          if (lastTriggeredKeyRef.current !== key) {
            lastTriggeredKeyRef.current = key;
            triggerSafeAlarm(nearest.desc, 'Hora do Remédio — E.L.O.S.');
          }
          if (Date.now() - lastSyncMsRef.current > 2500 && bluetoothService.isConnected()) {
            bluetoothService.sendCommand('STATUS');
          }
        }
      }
    }, 1000);

    return () => clearInterval(id);
  }, [deviceState.agendamentos, triggerSafeAlarm]);

  const handleToggleTheme = () => {
    const next = theme === 'dark' ? 'light' : 'dark';
    setTheme(next);
    document.documentElement.setAttribute('data-theme', next);
    localStorage.setItem('elos-theme', next);
  };

  const handleChangeTab = (tab: MainTab) => {
    if (tab !== 'notificacoes') {
      alarmService.stopPreview();
    }
    setActiveTab(tab);
  };

  const handleToggleRingOnRfid = (enabled: boolean) => {
    setRingOnRfid(enabled);
    localStorage.setItem('elos-ring-rfid', enabled ? 'true' : 'false');
    showToast(
      enabled
        ? 'Alarme ativado para liberações via Tag RFID.'
        : 'Liberações via Tag RFID silenciosas no celular.'
    );
  };

  const handleChangeSoundCategory = (cat: SoundCategory) => {
    alarmService.stopPreview();
    setSoundCategory(cat);
    localStorage.setItem('elos-sound-cat', cat);
  };

  const handleSelectTone = (uri: string, title: string, preview: boolean) => {
    setSelectedToneUri(uri);
    setSelectedToneTitle(title);
    localStorage.setItem('elos-tone-uri', uri);
    localStorage.setItem('elos-tone-title', title);
    if (preview) {
      alarmService.previewTone(uri);
    }
  };

  const handleOpenSystemPicker = () => {
    const ok = alarmService.openAndroidRingtonePicker(soundCategory);
    if (!ok) {
      showToast('O seletor nativo abre diretamente no aplicativo instalado no Android.');
    }
  };

  return (
    <div className="app-shell">
      {/* CABEÇALHO SÓBRIO */}
      <header className="topbar">
        <div className="brand-group">
          <span className={`status-dot ${connected ? 'connected' : ''}`} />
          <div>
            <h1 className="brand-title">E.L.O.S.</h1>
            <span className="brand-subtitle">
              {connected ? 'Conectado via Bluetooth' : 'Caixa Desconectada'}
            </span>
          </div>
        </div>

        <div className="topbar-actions">
          <button
            type="button"
            className={`connect-btn ${connected ? 'connected' : ''}`}
            onClick={() => bluetoothService.toggleConnection()}
          >
            <IconBluetooth size={15} />
            <span>{connected ? 'Conectado' : 'Conectar Caixa'}</span>
          </button>

          <button
            type="button"
            className="theme-btn"
            onClick={handleToggleTheme}
            title="Alternar tema claro/escuro"
          >
            {theme === 'dark' ? <IconSun size={17} /> : <IconMoon size={17} />}
          </button>
        </div>
      </header>

      {/* BARRA DE NAVEGAÇÃO POR ABAS */}
      <nav className="tabs-bar" role="tablist">
        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'agendamento'}
          className={`tab-trigger ${activeTab === 'agendamento' ? 'active' : ''}`}
          onClick={() => handleChangeTab('agendamento')}
        >
          <IconClock size={16} />
          <span>Agendamento</span>
          <span className="tab-counter">{(deviceState.agendamentos || []).length}</span>
        </button>

        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'rfid'}
          className={`tab-trigger ${activeTab === 'rfid' ? 'active' : ''}`}
          onClick={() => handleChangeTab('rfid')}
        >
          <IconRfid size={16} />
          <span>Tag RFID</span>
        </button>

        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'notificacoes'}
          className={`tab-trigger ${activeTab === 'notificacoes' ? 'active' : ''}`}
          onClick={() => handleChangeTab('notificacoes')}
        >
          <IconBell size={16} />
          <span>Notificações</span>
        </button>
      </nav>

      {/* CONTEÚDO EXCLUSIVO DA ABA SELECIONADA */}
      <main className="app-container">
        {activeTab === 'agendamento' && (
          <TabAgendamento
            deviceState={deviceState}
            elapsedSeconds={elapsedSeconds}
            onSaveSchedule={(delay, repeat, desc) =>
              bluetoothService.sendCommand(`AGENDAR:${delay}:${repeat}:${desc}`)
            }
            onRemoveSchedule={(id) => bluetoothService.sendCommand(`REMOVER:${id}`)}
            onClearSchedules={() => {
              if (window.confirm('Deseja apagar todos os horários programados?')) {
                bluetoothService.sendCommand('LIMPAR');
              }
            }}
            onUpdateStock={async (total, ocupados) => {
              const ok = await bluetoothService.sendCommand(`ESTOQUE:${total}:${ocupados}`);
              if (ok) showToast('Estoque atualizado com sucesso.');
              return ok;
            }}
            onDispenseNow={() => bluetoothService.sendCommand('DISPENSAR')}
            showToast={showToast}
          />
        )}

        {activeTab === 'rfid' && (
          <TabRfid
            ringOnRfid={ringOnRfid}
            onToggleRingOnRfid={handleToggleRingOnRfid}
            lastRfidLog={rfidHistory[0] || null}
            rfidHistory={rfidHistory}
            onDispenseEmergency={() => bluetoothService.sendCommand('DISPENSAR')}
          />
        )}

        {activeTab === 'notificacoes' && (
          <TabNotificacoes
            soundCategory={soundCategory}
            selectedToneUri={selectedToneUri}
            selectedToneTitle={selectedToneTitle}
            tones={phoneTones}
            onChangeCategory={handleChangeSoundCategory}
            onSelectTone={handleSelectTone}
            onOpenSystemPicker={handleOpenSystemPicker}
            onTestAlarm={() =>
              triggerSafeAlarm(
                'Teste de notificação externa, vibração e toque selecionado.',
                'Teste de Alerta — E.L.O.S.'
              )
            }
          />
        )}
      </main>

      {/* MODAL DE ALERTA QUANDO O REMÉDIO É DISPENSADO */}
      <AlarmModal
        visible={alarmModal.visible}
        title={alarmModal.title}
        description={alarmModal.description}
        timeText={alarmModal.timeText}
        onConfirmStop={() => {
          alarmService.stopAlarm();
          setAlarmModal((prev) => ({ ...prev, visible: false }));
        }}
      />

      {toastMsg && <div className="toast-pill">{toastMsg}</div>}
    </div>
  );
};

