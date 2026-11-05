export type MainTab = 'agendamento' | 'rfid' | 'notificacoes';
export type ScheduleMode = 'horario' | 'intervalo' | 'cronometro';
export type SoundCategory = 'notificacao' | 'chamada';

export interface ScheduleItem {
  id: number;
  restante: number;
  repete: number;
  desc: string;
}

export interface ElosDeviceState {
  ocupados: number;
  total: number;
  agendamentos: ScheduleItem[];
}

export interface ElosEventPayload {
  evento?: 'DISPENSADO' | 'DISPENSADO_RFID' | 'VAZIO' | 'VAZIO_RFID' | 'FULL' | 'AGENDADO';
  origem?: string;
  uid?: string;
  desc?: string;
  ok?: boolean;
  ocupados?: number;
  total?: number;
  agendamentos?: ScheduleItem[];
}

export interface PhoneTone {
  title: string;
  uri: string;
}

export interface RfidLogEntry {
  uid: string;
  status: string;
  time: string;
  success: boolean;
}

export interface ElosNativeBridge {
  listarToquesCelular(tipo: string): string;
  abrirSeletorDoSistema(tipo: string): void;
  testarToque(uriStr: string, modoChamada: boolean): void;
  pararToque(): void;
  dispararAlertaNativo(
    titulo: string,
    mensagem: string,
    uriStr: string,
    modoChamada: boolean
  ): void;
  agendarNotificacaoEmSegundos(
    id: number,
    segundos: number,
    titulo: string,
    mensagem: string,
    uriStr: string,
    modoChamada: boolean
  ): void;
  limparAgendamentosNativos(): void;
}

declare global {
  interface Window {
    ElosNative?: ElosNativeBridge;
    onNativeRingtonePicked?: (uri: string, title: string) => void;
  }
}

