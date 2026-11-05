import { BleClient } from '@capacitor-community/bluetooth-le';

const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const CHAR_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const CHAR_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

type MessageListener = (line: string) => void;
type ConnectionListener = (connected: boolean) => void;
type ToastListener = (message: string) => void;

class BluetoothService {
  private connectedDeviceId: string | null = null;
  private rxBuffer = '';
  private onMessageCb: MessageListener | null = null;
  private onConnectionCb: ConnectionListener | null = null;
  private onToastCb: ToastListener | null = null;

  public setListeners(
    onMessage: MessageListener,
    onConnection: ConnectionListener,
    onToast: ToastListener
  ): void {
    this.onMessageCb = onMessage;
    this.onConnectionCb = onConnection;
    this.onToastCb = onToast;
  }

  public isConnected(): boolean {
    return this.connectedDeviceId !== null;
  }

  private toast(msg: string): void {
    if (this.onToastCb) this.onToastCb(msg);
  }

  private handleDisconnect(): void {
    this.connectedDeviceId = null;
    this.rxBuffer = '';
    if (this.onConnectionCb) this.onConnectionCb(false);
    this.toast('Caixa desconectada.');
  }

  private handleNotification(dataView: DataView): void {
    const bytes = new Uint8Array(dataView.buffer, dataView.byteOffset, dataView.byteLength);
    const chunk = new TextDecoder().decode(bytes);
    this.rxBuffer += chunk;

    let newlineIdx: number;
    while ((newlineIdx = this.rxBuffer.indexOf('\n')) !== -1) {
      const line = this.rxBuffer.slice(0, newlineIdx).trim();
      this.rxBuffer = this.rxBuffer.slice(newlineIdx + 1);
      if (line && this.onMessageCb) {
        this.onMessageCb(line);
      }
    }
  }

  private async connectToDeviceId(deviceId: string, deviceName?: string): Promise<void> {
    this.toast(`Conectando a ${deviceName || 'ELOS_Caixa'}...`);
    try {
      await BleClient.disconnect(deviceId);
    } catch {
      // ignora se não estava conectado
    }

    await BleClient.connect(deviceId, () => this.handleDisconnect());
    this.connectedDeviceId = deviceId;

    await BleClient.startNotifications(
      this.connectedDeviceId,
      SERVICE_UUID,
      CHAR_TX_UUID,
      (dv) => this.handleNotification(dv)
    );

    if (this.onConnectionCb) this.onConnectionCb(true);
    this.toast('Conectado com sucesso à ELOS_Caixa.');
    await this.sendCommand('STATUS');
  }

  public async toggleConnection(): Promise<void> {
    if (this.connectedDeviceId) {
      try {
        await BleClient.disconnect(this.connectedDeviceId);
      } catch {
        // ignora erro de desconexão
      }
      this.handleDisconnect();
      return;
    }

    try {
      await BleClient.initialize();

      try {
        const enabled = await BleClient.isEnabled();
        if (!enabled) {
          await BleClient.requestEnable();
        }
      } catch {
        // requestEnable não suportado em iOS/Web
      }

      await BleClient.setDisplayStrings({
        scanning: 'Procurando ELOS_Caixa...',
        cancel: 'Cancelar',
        availableDevices: 'Selecione ELOS_Caixa para conectar:',
        noDeviceFound: 'Nenhum dispositivo encontrado (Ative a Localização/GPS)'
      });

      // 1. Verifica dispositivos já conectados no Android
      try {
        const connectedList = await BleClient.getConnectedDevices([SERVICE_UUID]);
        const foundConn = (connectedList || []).find(
          (d) => d.name && d.name.toUpperCase().includes('ELOS')
        );
        if (foundConn) {
          await this.connectToDeviceId(foundConn.deviceId, foundConn.name);
          return;
        }
      } catch {
        // segue para próxima etapa
      }

      // 2. Verifica dispositivos já pareados nas configurações do Android
      try {
        const bondedList = await BleClient.getBondedDevices();
        const foundBonded = (bondedList || []).find(
          (d) => d.name && d.name.toUpperCase().includes('ELOS')
        );
        if (foundBonded) {
          try {
            await this.connectToDeviceId(foundBonded.deviceId, foundBonded.name);
            return;
          } catch {
            // se fora de alcance ou trocou MAC, segue para scan ativo
          }
        }
      } catch {
        // segue para scan ativo
      }

      // 3. Varredura BLE ativa por até 3.2 segundos
      this.toast('Localizando ELOS_Caixa via Bluetooth...');
      let discoveredDevice: { deviceId: string; name?: string } | null = null;
      try {
        await BleClient.requestLEScan({ allowDuplicates: false }, (res) => {
          const name = (res.localName || res.device?.name || '').toUpperCase();
          const uuids = (res.uuids || []).map((u) => u.toLowerCase());
          if (name.includes('ELOS') || uuids.includes(SERVICE_UUID.toLowerCase())) {
            discoveredDevice = res.device;
          }
        });

        for (let i = 0; i < 32; i++) {
          if (discoveredDevice) break;
          await new Promise((r) => setTimeout(r, 100));
        }
        await BleClient.stopLEScan();
      } catch {
        try {
          await BleClient.stopLEScan();
        } catch {
          // ignora
        }
      }

      if (discoveredDevice) {
        const dev = discoveredDevice as { deviceId: string; name?: string };
        await this.connectToDeviceId(dev.deviceId, dev.name || 'ELOS_Caixa');
        return;
      }

      // 4. Seletor manual do sistema
      const selectedDevice = await BleClient.requestDevice({
        optionalServices: [SERVICE_UUID]
      });
      await this.connectToDeviceId(selectedDevice.deviceId, selectedDevice.name || 'ELOS_Caixa');
    } catch (err) {
      console.error('BLE Connection Error:', err);
      this.toast('Não foi possível conectar. Verifique se a caixa está ligada e o Bluetooth ativo.');
    }
  }

  public async sendCommand(cmd: string): Promise<boolean> {
    if (!this.connectedDeviceId) {
      this.toast('Conecte-se à caixa via Bluetooth antes de enviar comandos.');
      return false;
    }

    try {
      const bytes = new TextEncoder().encode(cmd + '\n');
      for (let i = 0; i < bytes.length; i += 20) {
        const slice = bytes.slice(i, i + 20);
        const dataView = new DataView(slice.buffer, slice.byteOffset, slice.byteLength);
        await BleClient.write(this.connectedDeviceId, SERVICE_UUID, CHAR_RX_UUID, dataView);
      }
      return true;
    } catch (err) {
      console.error('BLE Write Error:', err);
      this.toast('Falha ao enviar comando para a caixa.');
      return false;
    }
  }
}

export const bluetoothService = new BluetoothService();

