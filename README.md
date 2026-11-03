# 💊 E.L.O.S. — Ecossistema Inteligente de Dispensação de Medicamentos

Sistema embarcado e aplicativo móvel de dispensação automatizada de medicamentos, projetado com foco em **acessibilidade e suporte a Pessoas com Deficiência (PCD)** no âmbito do **NAPNE — Instituto Federal do Espírito Santo (IFES)**.

O ecossistema **E.L.O.S.** opera de forma **100% Offline**, eliminando a dependência de roteadores Wi-Fi ou conexão com a internet por meio de comunicação direta via **Bluetooth Low Energy (BLE)** com o smartphone (`.apk`) e rádio **ESP-NOW** para integração com dispositivos vestíveis (Pulseira de Alerta).

> ⚠️ **Status:** Protótipo funcional em desenvolvimento.
>
> Este projeto tem caráter experimental e de pesquisa aplicada, não devendo ser utilizado para dispensação clínica real sem validação médica, mecânica, eletrônica e de segurança.

---

## 🎯 Objetivo do Ecossistema

O **E.L.O.S.** busca oferecer autonomia, segurança e praticidade no gerenciamento diário de medicamentos para pessoas PCD, idosos e cuidadores, resolvendo problemas críticos de dispensadores convencionais:

1. **Operação 100% Independente de Internet/Wi-Fi:** A caixa nunca trava nem deixa de dispensar um medicamento caso a internet da residência caia ou o equipamento seja transportado em viagens.
2. **Controle pelo Celular via Bluetooth (`.apk`):** Permite cadastrar horários, intervalos, cronômetros e controlar o estoque de comprimidos diretamente pelo aplicativo Android.
3. **Persistência em Memória Não-Volátil (Flash NVS):** O estoque configurado fica salvo na memória interna do microcontrolador mesmo em caso de queda de energia.
4. **Preparado para Alerta Sensorial (Pulseira Inteligente):** Arquitetura pronta para acionar uma pulseira vibratória/sonora via protocolo **ESP-NOW** no momento exato em que a dose é liberada.

---

## 🏗️ Arquitetura de Comunicação (100% Offline)

```text
 ┌─────────────────────────┐          Bluetooth BLE           ┌──────────────────────────┐
 │    APLICATIVO MOBILE    │ ◄──────────────────────────────► │    CAIXA INTELIGENTE     │
 │   ELOS Caixa (Android)  │   Nordic UART Service (GATT)     │      ESP32 DevKit V1     │
 │  • Agenda horários      │   • Envia comandos (RX)          │  • Motor de Passo 28BYJ  │
 │  • Controla estoque     │   • Recebe JSON em tempo real    │  • Visor OLED SSD1306    │
 │  • Disparo manual       │                                  │  • Memória Flash (NVS)   │
 └─────────────────────────┘                                  └────────────┬─────────────┘
                                                                           │
                                                                 Protocolo │ ESP-NOW
                                                                (Canal Fixo│Offline)
                                                                           ▼
                                                              ┌──────────────────────────┐
                                                              │   PULSEIRA DE SUPORTE    │
                                                              │   Seeed XIAO ESP32-C3    │
                                                              │  • Alerta sonoro/vibrac. │
                                                              │    para suporte PCD      │
                                                              └──────────────────────────┘
```

---

## 📱 Aplicativo Mobile (`ELOS Caixa`) — Tecnologias e Arquitetura

O aplicativo Android ([`app_mobile/ELOS_Caixa.apk`](./app_mobile/ELOS_Caixa.apk)) foi estruturado utilizando uma **arquitetura híbrida modular em camadas**, combinando a agilidade e acessibilidade de interfaces modernas com acesso nativo ao hardware Bluetooth do sistema operacional Android.

### 🛠️ Stack Tecnológica do Aplicativo

| Camada | Tecnologia Utilizada | Papel no Projeto |
| :--- | :--- | :--- |
| **Runtime Nativo & Container** | **Capacitor 6** (`@capacitor/core`, `@capacitor/android`) | Encapsula a aplicação em um WebView nativo de alto desempenho no Android (`.apk`), gerenciando ciclo de vida, permissões de sistema (`AndroidManifest.xml`) e ponte JavaScript ↔ Java/Kotlin. |
| **Comunicação Bluetooth Nativa** | **`@capacitor-community/bluetooth-le`** | Plugin nativo em Kotlin que acessa diretamente as APIs `BluetoothAdapter`, `BluetoothLeScanner` e `BluetoothGatt` do Android (com *fallback* automático para **Web Bluetooth API** em navegadores). |
| **Protocolo de Rádio** | **BLE GATT — Nordic UART Service (NUS)** | Padrão industrial de comunicação serial sobre Bluetooth Low Energy (`Service: 6e400001-...`), com fragmentação automática de pacotes (*chunking* de 20 bytes) para compatibilidade universal de MTU. |
| **Arquitetura de Código (Frontend)** | **JavaScript ES6+ Modular ( Padrão IIFE / MVC )** | Código desacoplado em 3 módulos independentes (`bluetooth.js`, `ui.js`, `app.js`), sem peso de frameworks externos, garantindo inicialização instantânea (< 100 ms) e baixo consumo de RAM. |
| **Interface & Acessibilidade** | **HTML5 Semântico + CSS3 Custom Properties** | Design System próprio com **Modo Claro/Escuro (Dark Mode)** automático, alto contraste, prevenção de zoom indesejado e botões com área mínima de toque de `48px` (diretrizes de acessibilidade para PCD). |
| **Compilação & CI/CD** | **Gradle 8 + Java JDK 17 + GitHub Actions** | Pipeline automatizado ([`.github/workflows/build-apk.yml`](./.github/workflows/build-apk.yml)) que sincroniza os módulos e compila o binário `.apk` na nuvem a cada atualização do repositório. |

---

### 🧩 Estrutura Modular do Aplicativo (`app_mobile/`)

O código-fonte do aplicativo foi organizado de forma limpa e separada por responsabilidades:

```text
app_mobile/
├── www/                        # Código-fonte Modular da Aplicação
│   ├── index.html              # Marcação semântica HTML5 e atributos de acessibilidade (ARIA)
│   ├── css/
│   │   └── styles.css          # Design System, variáveis de tema (Light/Dark) e Grid responsivo
│   ├── js/
│   │   ├── bluetooth.js        # Módulo ElosBluetooth: Conexão BLE em 4 estágios, GATT RX/TX e buffers
│   │   ├── ui.js               # Módulo ElosUI: Manipulação de DOM, barra de estoque, temas e Toasts
│   │   └── app.js              # Módulo ElosApp: Regras de negócio, cálculo de agendamentos e estado
│   └── vendor/
│       ├── capacitor.js        # Bridge nativa do Capacitor Core
│       └── ble-plugin.js       # Cliente oficial Bluetooth Low Energy
├── android/                    # Projeto Nativo Android (Gradle, MainActivity.java, AndroidManifest.xml)
├── capacitor.config.json       # Configuração do pacote Android (com.elos.caixa)
├── package.json                # Gerenciamento de dependências Node/Capacitor
├── README_APK.md               # Guia rápido de uso e referência de comandos BLE
└── ELOS_Caixa.apk              # Instalador Android compilado pronto para uso
```

### 🔄 Conexão Inteligente em 4 Estágios (`bluetooth.js`)
Para garantir que qualquer usuário consiga conectar o celular à caixa sem dificuldades técnicas, o módulo `ElosBluetooth` executa automaticamente 4 etapas ao tocar em **Conectar Caixa**:
1. **Dispositivos GATT Ativos (`getConnectedDevices`):** Verifica se o Android já mantém um link GATT ativo com a `ELOS_Caixa`.
2. **Dispositivos Pareados no Sistema (`getBondedDevices`):** Se o usuário já pareou a `ELOS_Caixa` nas configurações do Android, o aplicativo conecta diretamente pelo endereço MAC sem exigir nova busca.
3. **Auto-Discovery BLE (`requestLEScan`):** Realiza uma varredura rápida de 3,5 segundos procurando pelo nome `ELOS_Caixa` ou pelo UUID do serviço e conecta sozinho ao encontrá-la.
4. **Lista Manual de Fallback (`requestDevice`):** Caso haja múltiplas caixas próximas, exibe a lista seletora nativa do Android.

---

### 📡 Protocolo de Comandos Bluetooth (App ↔ ESP32)

A comunicação utiliza comandos em texto terminados por `\n` na característica **RX** (`6e400002-b5a3-f393-e0a9-e50e24dcca9e`) e respostas em **JSON** notificadas pela característica **TX** (`6e400003-b5a3-f393-e0a9-e50e24dcca9e`). Os mesmos comandos também funcionam via **Monitor Serial USB (115200 baud)** para testes de bancada:

| Ação | Comando Enviado (`RX`) | Exemplo | Resposta da Caixa (`TX`) |
| :--- | :--- | :--- | :--- |
| **Sincronizar Estado** | `STATUS` | `STATUS` | JSON completo com estoque e agendamentos |
| **Dispensar 1 Dose** | `DISPENSAR` | `DISPENSAR` | `{"evento":"DISPENSADO","ok":true}` + JSON de status |
| **Atualizar Estoque** | `ESTOQUE:<total>:<ocup>` | `ESTOQUE:7:5` | Salva na Flash NVS e retorna JSON de status |
| **Agendar Dose** | `AGENDAR:<delay>:<rep>:<desc>` | `AGENDAR:28800:28800:A cada 8 horas` | `{"evento":"AGENDADO","ok":true}` + JSON de status |
| **Remover Horário** | `REMOVER:<id>` | `REMOVER:0` | Desativa o slot `id` e retorna JSON de status |
| **Limpar Agenda** | `LIMPAR` | `LIMPAR` | Apaga todos os agendamentos e retorna JSON |

---

## 🔩 Hardware e Pinagem

### Componentes Principais

| Componente | Função no Ecossistema | Quantidade |
| :--- | :--- | :---: |
| **ESP32 DevKit V1** | Controlador principal da Caixa (BLE + Controle de Motor + NVS) | 1 |
| **Display OLED 0.96" I2C (SSD1306)** | Exibição local de status Bluetooth, estoque e cronômetro regressivo | 1 |
| **Motor de Passo 28BYJ-48 + Driver ULN2003** | Tracionamento do carrossel rotativo de medicamentos | 1 |
| **Seeed Studio XIAO ESP32-C3** *(Em integração)* | Controlador compacto da Pulseira Inteligente | 1 |
| **Estrutura Mecânica Impressa em 3D** | Carrossel de compartimentos e carenagem da Caixa/Pulseira | 1 |

### Mapa de Pinos (Caixa — ESP32 DevKit V1)

| Pino ESP32 | Periférico Conectado | Função |
| :---: | :---: | :--- |
| `GPIO 5` | ULN2003 `IN1` | Bobina 1 do Motor de Passo |
| `GPIO 18` | ULN2003 `IN2` | Bobina 2 do Motor de Passo |
| `GPIO 19` | ULN2003 `IN3` | Bobina 3 do Motor de Passo |
| `GPIO 21` | ULN2003 `IN4` | Bobina 4 do Motor de Passo |
| `GPIO 14` | OLED `SDA` | Barramento de Dados I2C do Visor |
| `GPIO 27` | OLED `SCL` | Clock I2C do Visor (Endereço `0x3C`) |

---

## 💻 Firmware do ESP32 (PlatformIO)

O firmware em C++ ([`src/Caixa/main.cpp`](./src/Caixa/main.cpp)) utiliza multi-ambientes no [`platformio.ini`](./platformio.ini) (`[env:caixa]` e `[env:pulseira]`):

### Bibliotecas Utilizadas
* `BLEDevice.h` / `BLEServer.h` / `BLE2902.h` *(Nativas do core Arduino-ESP32 para servidor BLE UART)*
* `Preferences.h` *(Nativa para armazenamento não-volátil NVS na memória Flash)*
* `esp_now.h` *(Nativa para comunicação direta com a Pulseira)*
* `arduino-libraries/Stepper@^1.1.3` *(Controle do motor de passo 28BYJ-48)*
* `adafruit/Adafruit GFX Library@^1.11.9` & `adafruit/Adafruit SSD1306@^2.5.9` *(Interface gráfica no visor OLED)*

### Chaves de Configuração Rápida (`src/Caixa/main.cpp`)
No topo do arquivo [`src/Caixa/main.cpp`](./src/Caixa/main.cpp), duas constantes permitem adaptar o comportamento do firmware sem alterar a lógica do programa:
* `#define USAR_BLUETOOTH_CLASSICO 0`: Mantém `0` para **BLE (Bluetooth Low Energy)** ou `1` para **Bluetooth Clássico SPP (`BluetoothSerial.h`)**.
* `#define ATIVAR_PULSEIRA 0`: Mantém `0` enquanto a pulseira está em modelagem 3D; basta alterar para `1` para ativar os disparos `ESP-NOW` na função `notificarPulseira()`.

---

## 🛠️ Como Compilar e Executar

### 1. Gravar o Firmware na Caixa (ESP32)
Com o VS Code e a extensão **PlatformIO** instalados:
```bash
# Compilar o ambiente da Caixa
pio run -e caixa

# Enviar para a placa ESP32 via USB
pio run -e caixa --target upload

# Abrir Monitor Serial (115200 baud)
pio device monitor
```

### 2. Instalar ou Recompilar o Aplicativo Android (`.apk`)
* **Para instalar diretamente:** Envie o arquivo [`app_mobile/ELOS_Caixa.apk`](./app_mobile/ELOS_Caixa.apk) para o smartphone Android e execute a instalação.
* **Para modificar o aplicativo e gerar um novo `.apk`:**
  1. Edite os módulos em `app_mobile/www/` (`index.html`, `css/styles.css`, `js/ui.js`, `js/bluetooth.js`, `js/app.js`).
  2. Sincronize com o projeto Android:
     ```bash
     cd app_mobile
     npx cap sync android
     ```
  3. Faça o `git push origin main` — o **GitHub Actions** compilará automaticamente o novo `app_mobile/ELOS_Caixa.apk` na nuvem, bastando rodar `git pull` para recebê-lo pronto.

---

## 📁 Estrutura Geral do Projeto

```text
Projeto_ELOS/
├── .github/
│   └── workflows/
│       └── build-apk.yml          # Pipeline CI/CD que compila o APK Android automaticamente
├── app_mobile/                    # Aplicativo Mobile Android (Capacitor + BLE)
│   ├── www/                       # Módulos Frontend (HTML5, CSS3, JS ES6+)
│   ├── android/                   # Projeto Nativo Android (Gradle)
│   ├── README_APK.md              # Documentação do protocolo Bluetooth
│   └── ELOS_Caixa.apk             # Instalador Android compilado
├── backups/
│   └── Caixa_main_wifi_backup.cpp # Backup histórico da primeira versão com WebServer Wi-Fi
├── src/
│   ├── Caixa/
│   │   └── main.cpp               # Firmware principal da Caixa Inteligente (BLE + OLED + NVS)
│   └── Pulseira/
│       └── main.cpp               # Firmware base da Pulseira (XIAO ESP32-C3 + ESP-NOW)
├── platformio.ini                 # Configuração dos ambientes [env:caixa] e [env:pulseira]
└── README.md                      # Documentação principal do ecossistema E.L.O.S.
```

---

## 🏫 Contexto Institucional e Autoria

* **Projeto:** E.L.O.S. — Dispenser Automatizado de Medicamentos  
* **Instituição:** Instituto Federal do Espírito Santo — **IFES**  
* **Núcleo de Suporte:** **NAPNE** (Núcleo de Atendimento às Pessoas com Necessidades Específicas)  
* **Autor:** **Renan Bernardo Gonçalves** — *Desenvolvimento de hardware, firmware embarcado, aplicativo móvel e integração mecânica/eletrônica.*
