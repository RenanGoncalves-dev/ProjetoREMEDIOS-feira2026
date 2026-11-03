/* ==========================================================================
   Camada de Serviço Bluetooth Low Energy (BLE) — Protocolo Nordic UART (NUS)
   ========================================================================== */

const ElosBluetooth = (function () {
  const { BleClient } = capacitorCommunityBluetoothLe;

  // UUIDs padrão do Nordic UART Service (configurados no firmware ESP32)
  const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
  const CHAR_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // Escrita (App -> ESP32)
  const CHAR_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // Notificação (ESP32 -> App)

  let connectedDeviceId = null;
  let rxBuffer = '';
  let onMessageCallback = null;
  let onConnectionChangeCallback = null;

  function isConnected() {
    return connectedDeviceId !== null;
  }

  function handleDisconnect() {
    connectedDeviceId = null;
    rxBuffer = '';
    if (onConnectionChangeCallback) onConnectionChangeCallback(false);
  }

  function handleNotification(dataView) {
    const bytes = new Uint8Array(dataView.buffer, dataView.byteOffset, dataView.byteLength);
    const chunk = new TextDecoder().decode(bytes);
    rxBuffer += chunk;

    let newlineIdx;
    while ((newlineIdx = rxBuffer.indexOf('\n')) !== -1) {
      const line = rxBuffer.slice(0, newlineIdx).trim();
      rxBuffer = rxBuffer.slice(newlineIdx + 1);
      if (line && onMessageCallback) {
        onMessageCallback(line);
      }
    }
  }

  async function connectToDeviceId(deviceId, deviceName) {
    ElosUI.showToast('🔗 Conectando em ' + (deviceName || 'ELOS_Caixa') + '...');
    try {
      await BleClient.disconnect(deviceId);
    } catch (_) {}

    await BleClient.connect(deviceId, () => handleDisconnect());
    connectedDeviceId = deviceId;

    await BleClient.startNotifications(
      connectedDeviceId,
      SERVICE_UUID,
      CHAR_TX_UUID,
      (dataView) => handleNotification(dataView)
    );

    if (onConnectionChangeCallback) onConnectionChangeCallback(true);
    await sendCommand('STATUS');
  }

  async function toggleConnection() {
    if (connectedDeviceId) {
      try {
        await BleClient.disconnect(connectedDeviceId);
      } catch (_) {}
      handleDisconnect();
      return;
    }

    try {
      await BleClient.initialize();

      try {
        const enabled = await BleClient.isEnabled();
        if (!enabled) await BleClient.requestEnable();
      } catch (_) {}

      await BleClient.setDisplayStrings({
        scanning: 'Procurando ELOS_Caixa...',
        cancel: 'Cancelar',
        availableDevices: 'Toque em ELOS_Caixa para conectar:',
        noDeviceFound: 'Nenhum dispositivo encontrado (Ative o GPS/Localização)'
      });

      // Etapa 1: Verifica se já está conectado no sistema Android
      try {
        const connectedList = await BleClient.getConnectedDevices([SERVICE_UUID]);
        const foundConn = (connectedList || []).find(
          (d) => d.name && d.name.toUpperCase().includes('ELOS')
        );
        if (foundConn) {
          await connectToDeviceId(foundConn.deviceId, foundConn.name);
          return;
        }
      } catch (e) {
        console.log('getConnectedDevices:', e);
      }

      // Etapa 2: Verifica se já está pareado/salvo nas Configurações Bluetooth do Android
      try {
        const bondedList = await BleClient.getBondedDevices();
        const foundBonded = (bondedList || []).find(
          (d) => d.name && d.name.toUpperCase().includes('ELOS')
        );
        if (foundBonded) {
          try {
            await connectToDeviceId(foundBonded.deviceId, foundBonded.name);
            return;
          } catch (bondErr) {
            console.log('Conexão direta via pareado não respondeu, iniciando scan ativo...', bondErr);
          }
        }
      } catch (e) {
        console.log('getBondedDevices:', e);
      }

      // Etapa 3: Varredura ativa automática por 3.5s
      ElosUI.showToast('🔍 Buscando ELOS_Caixa automaticamente...');
      let autoDiscovered = null;
      try {
        await BleClient.requestLEScan({ allowDuplicates: false }, (res) => {
          const name = (res.localName || (res.device && res.device.name) || '').toUpperCase();
          const uuids = (res.uuids || []).map((u) => u.toLowerCase());
          if (name.includes('ELOS') || uuids.includes(SERVICE_UUID.toLowerCase())) {
            autoDiscovered = res.device;
          }
        });

        for (let t = 0; t < 35; t++) {
          if (autoDiscovered) break;
          await new Promise((r) => setTimeout(r, 100));
        }
        await BleClient.stopLEScan();
      } catch (_) {
        try {
          await BleClient.stopLEScan();
        } catch (_) {}
      }

      if (autoDiscovered) {
        await connectToDeviceId(autoDiscovered.deviceId, autoDiscovered.name || 'ELOS_Caixa');
        return;
      }

      // Etapa 4: Seletor manual de dispositivos BLE sem filtro restritivo
      const selectedDevice = await BleClient.requestDevice({
        optionalServices: [SERVICE_UUID]
      });

      await connectToDeviceId(selectedDevice.deviceId, selectedDevice.name || 'ELOS_Caixa');
    } catch (e) {
      console.error(e);
      ElosUI.showToast('⚠️ Não foi possível conectar. Verifique se a Caixa está ligada e o GPS ativo.');
    }
  }

  async function sendCommand(cmd) {
    if (!connectedDeviceId) {
      ElosUI.showToast('⚠️ Conecte na caixa via Bluetooth primeiro!');
      return false;
    }
    try {
      const bytes = new TextEncoder().encode(cmd + '\n');
      // Fragmentação em blocos de 20 bytes (compatível com o MTU mínimo BLE padrão)
      for (let i = 0; i < bytes.length; i += 20) {
        const slice = bytes.slice(i, i + 20);
        const dataView = new DataView(slice.buffer, slice.byteOffset, slice.byteLength);
        await BleClient.write(connectedDeviceId, SERVICE_UUID, CHAR_RX_UUID, dataView);
      }
      return true;
    } catch (e) {
      console.error(e);
      ElosUI.showToast('Erro ao enviar comando Bluetooth.');
      return false;
    }
  }

  return {
    isConnected,
    toggleConnection,
    sendCommand,
    onMessage: (cb) => { onMessageCallback = cb; },
    onConnectionChange: (cb) => { onConnectionChangeCallback = cb; }
  };
})();

