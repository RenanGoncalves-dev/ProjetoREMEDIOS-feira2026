# 📱 Aplicativo Bluetooth E.L.O.S. & Guia de Comandos para APK

Este diretório contém a interface mobile Bluetooth (`app_mobile/index.html`) e a documentação completa do protocolo de comunicação entre o celular (`.apk`) e a **Caixa Inteligente (`ELOS_Caixa`)**.

---

## 🔌 Protocolo de Comandos Bluetooth (Texto / Serial / BLE)

A Caixa recebe comandos em texto simples (terminados por quebra de linha `\n`) e responde sempre com um JSON atualizado em uma única linha terminada por `\n`.

Você pode testar todos os comandos abaixo tanto pelo **Celular (Bluetooth)** quanto pelo **Monitor Serial USB (115200 baud)** no VS Code!

| Ação no App | Comando enviado pelo App | Exemplo | Resposta da Caixa |
| :--- | :--- | :--- | :--- |
| **Ler Estado Atual** | `STATUS` | `STATUS` | JSON com estoque e lista de agendamentos |
| **Dispensar 1 Dose** | `DISPENSAR` | `DISPENSAR` | `{"evento":"DISPENSADO","ok":true}` + JSON de status |
| **Atualizar Estoque** | `ESTOQUE:<total>:<ocupados>` | `ESTOQUE:7:5` | JSON de status atualizado (salvo na Flash) |
| **Agendar Dose** | `AGENDAR:<delaySeg>:<repeteSeg>:<desc>` | `AGENDAR:28800:28800:A cada 8 horas` | `{"evento":"AGENDADO","ok":true}` + JSON de status |
| **Remover Agendamento** | `REMOVER:<id>` | `REMOVER:0` | JSON de status atualizado |
| **Limpar Todos** | `LIMPAR` | `LIMPAR` | JSON de status atualizado |

### Formato do JSON de Status retornado pela Caixa:
```json
{
  "ocupados": 5,
  "total": 7,
  "proximo": 3590,
  "agendamentos": [
    {
      "id": 0,
      "desc": "Todos os dias às 18:00",
      "restante": 3590,
      "repete": 86400
    }
  ]
}
```

---

## 🛠️ Opções para usar no Celular / Gerar o `.apk`

### Opção 1: Usar o `app_mobile/index.html` (Pronto!)
O arquivo [`app_mobile/index.html`](./index.html) já possui **toda a interface do painel E.L.O.S.** conectando direto no Bluetooth (`ELOS_Caixa`) usando os UUIDs padrão UART BLE:
- **Service UUID:** `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- **RX Characteristic (Celular -> Caixa):** `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- **TX Characteristic (Caixa -> Celular):** `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

Como abrir no Android:
1. Como o GitHub do projeto já está configurado, basta ativar o **GitHub Pages** no repositório (ou abrir via servidor local/Capacitor) pelo Chrome no Android e tocar em **"Adicionar à tela inicial"** — ele instala como um aplicativo no Android e funciona sem precisar que a caixa tenha Wi-Fi!
2. Ou empacotar esse mesmo `index.html` em um arquivo `.apk` nativo usando **CapacitorJS** (`@capacitor/android` + `@capacitor-community/bluetooth-le`).

### Opção 2: Criar um `.apk` no MIT App Inventor / Kodular
Se preferir montar um `.apk` em blocos no **MIT App Inventor** ou **Kodular**:
1. No arquivo `src/Caixa/main.cpp` (linha 12), mude:
   ```cpp
   #define USAR_BLUETOOTH_CLASSICO 1
   ```
2. Pareie o celular com `ELOS_Caixa` nas configurações de Bluetooth do Android.
3. No MIT App Inventor, use o componente nativo `BluetoothClient.SendText("DISPENSAR\n")`, `BluetoothClient.SendText("ESTOQUE:7:7\n")`, etc.

---

## ⌚ Como ativar a Pulseira no futuro
No arquivo `src/Caixa/main.cpp` (linha 16), toda a lógica de envio para a pulseira já está pronta na função `notificarPulseira()`. Quando você terminar de modelar em 3D e programar a pulseira:
1. Mude `#define ATIVAR_PULSEIRA 0` para `#define ATIVAR_PULSEIRA 1`.
2. Confira o endereço MAC em `macPulseira[]`.

