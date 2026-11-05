import { LocalNotifications } from '@capacitor/local-notifications';
import { PhoneTone, ScheduleItem, SoundCategory } from '../types/elos';

const CHANNEL_ID = 'elos_med_alarm_v2';

class AlarmService {
  private audioCtx: AudioContext | null = null;
  private webLoopTimer: number | null = null;
  private previewTimer: number | null = null;

  public hasNativeBridge(): boolean {
    return typeof window !== 'undefined' && typeof window.ElosNative !== 'undefined' && window.ElosNative !== null;
  }

  public async initializePermissions(): Promise<void> {
    try {
      await LocalNotifications.requestPermissions();
      await LocalNotifications.createChannel({
        id: CHANNEL_ID,
        name: 'Alertas de Medicamentos E.L.O.S.',
        description: 'Notificações de liberação de medicamentos da Caixa E.L.O.S.',
        importance: 5,
        visibility: 1,
        vibration: true,
        lights: true
      });
    } catch {
      // ignora em ambiente web desktop
    }
  }

  public fetchPhoneTones(category: SoundCategory): PhoneTone[] {
    const list: PhoneTone[] = [
      {
        title:
          category === 'chamada'
            ? 'Padrão do Sistema (Toque de Chamada)'
            : 'Padrão do Sistema (Som de Notificação)',
        uri: ''
      }
    ];

    if (this.hasNativeBridge() && window.ElosNative) {
      try {
        const raw = window.ElosNative.listarToquesCelular(category);
        const parsed = JSON.parse(raw || '[]');
        if (Array.isArray(parsed)) {
          for (const item of parsed) {
            if (item && typeof item.title === 'string' && typeof item.uri === 'string') {
              list.push({ title: item.title, uri: item.uri });
            }
          }
        }
      } catch (err) {
        console.error('Erro ao consultar toques do Android:', err);
      }
    }

    return list;
  }

  public openAndroidRingtonePicker(category: SoundCategory): boolean {
    this.stopPreview();
    if (this.hasNativeBridge() && window.ElosNative) {
      window.ElosNative.abrirSeletorDoSistema(category);
      return true;
    }
    return false;
  }

  public previewTone(uri: string): void {
    this.stopPreview();
    if (this.hasNativeBridge() && window.ElosNative) {
      try {
        window.ElosNative.testarToque(uri, false);
        this.previewTimer = window.setTimeout(() => {
          try {
            window.ElosNative?.pararToque();
          } catch {
            // ignora
          }
        }, 4000);
      } catch {
        this.playWebFallbackBeep();
      }
    } else {
      this.playWebFallbackBeep();
    }
  }

  public stopPreview(): void {
    if (this.previewTimer !== null) {
      clearTimeout(this.previewTimer);
      this.previewTimer = null;
    }
    if (this.hasNativeBridge() && window.ElosNative) {
      try {
        window.ElosNative.pararToque();
      } catch {
        // ignora
      }
    }
  }

  public triggerAlarm(
    title: string,
    description: string,
    toneUri: string,
    category: SoundCategory
  ): void {
    this.stopPreview();
    const isCallMode = category === 'chamada';

    if (this.hasNativeBridge() && window.ElosNative) {
      try {
        window.ElosNative.dispararAlertaNativo(title, description, toneUri, isCallMode);
        return;
      } catch (err) {
        console.error('Falha no disparo nativo:', err);
      }
    }

    this.playWebFallbackBeep();
    if (isCallMode && this.webLoopTimer === null) {
      this.webLoopTimer = window.setInterval(() => this.playWebFallbackBeep(), 2000);
    }
  }

  public stopAlarm(): void {
    this.stopPreview();
    if (this.webLoopTimer !== null) {
      clearInterval(this.webLoopTimer);
      this.webLoopTimer = null;
    }
    if (this.hasNativeBridge() && window.ElosNative) {
      try {
        window.ElosNative.pararToque();
      } catch {
        // ignora
      }
    }
  }

  public async syncScheduledAlarms(
    schedules: ScheduleItem[],
    toneUri: string,
    category: SoundCategory
  ): Promise<void> {
    const isCallMode = category === 'chamada';

    if (this.hasNativeBridge() && window.ElosNative) {
      try {
        window.ElosNative.limparAgendamentosNativos();
        schedules.forEach((item, idx) => {
          if (item.restante > 1) {
            window.ElosNative?.agendarNotificacaoEmSegundos(
              100 + idx,
              Math.floor(item.restante),
              'Hora do Remédio — E.L.O.S.',
              `Dose liberada: ${item.desc}. Retire seu medicamento na caixa.`,
              toneUri,
              isCallMode
            );
          }
        });
      } catch (err) {
        console.error('Erro ao sincronizar agendamentos nativos:', err);
      }
    }

    try {
      const oldIds = Array.from({ length: 20 }, (_, i) => ({ id: 100 + i }));
      await LocalNotifications.cancel({ notifications: oldIds });

      const pending = schedules
        .filter((s) => s.restante > 2)
        .map((s, idx) => ({
          id: 100 + idx,
          title: 'Hora do Remédio — E.L.O.S.',
          body: `Dose liberada: ${s.desc}. Retire seu medicamento na caixa.`,
          channelId: CHANNEL_ID,
          schedule: {
            at: new Date(Date.now() + s.restante * 1000),
            allowWhileIdle: true
          }
        }));

      if (pending.length > 0) {
        await LocalNotifications.schedule({ notifications: pending });
      }
    } catch {
      // ignora em navegador desktop
    }
  }

  private playWebFallbackBeep(): void {
    try {
      const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      if (!AudioCtx) return;
      if (!this.audioCtx) this.audioCtx = new AudioCtx();
      if (this.audioCtx.state === 'suspended') this.audioCtx.resume();

      const now = this.audioCtx.currentTime;
      const notes = [523.25, 659.25, 783.99, 1046.5];
      notes.forEach((freq, idx) => {
        if (!this.audioCtx) return;
        const osc = this.audioCtx.createOscillator();
        const gain = this.audioCtx.createGain();
        osc.type = 'sine';
        osc.frequency.setValueAtTime(freq, now + idx * 0.14);
        gain.gain.setValueAtTime(0.001, now + idx * 0.14);
        gain.gain.exponentialRampToValueAtTime(0.35, now + idx * 0.14 + 0.03);
        gain.gain.exponentialRampToValueAtTime(0.001, now + idx * 0.14 + 0.12);
        osc.connect(gain);
        gain.connect(this.audioCtx.destination);
        osc.start(now + idx * 0.14);
        osc.stop(now + idx * 0.14 + 0.13);
      });
    } catch {
      // ignora
    }
  }
}

export const alarmService = new AlarmService();

