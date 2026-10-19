# ProjetoFeira
# ELOS — Dispenser Automatizado de Medicamentos

Sistema experimental de dispensação automatizada de medicamentos desenvolvido para o **NAPNE — Instituto Federal do Espírito Santo (IFES)**.

O projeto utiliza um **ESP32** para controlar um mecanismo de posicionamento baseado em motor de passo, permitindo selecionar diferentes compartimentos de medicamentos de forma automatizada.

> ⚠️ **Status:** Protótipo em desenvolvimento.
>
> Este projeto é experimental e não deve ser utilizado para dispensação real de medicamentos sem validação técnica, mecânica, eletrônica e de segurança adequada.

---

## 🎯 Objetivo

O ELOS tem como objetivo desenvolver um protótipo de baixo custo capaz de auxiliar no gerenciamento e na dispensação programada de medicamentos.

A proposta inicial utiliza um sistema rotativo com múltiplos compartimentos, permitindo que o motor posicione o compartimento desejado na região de saída.

O projeto também serve como base para futuras versões com maior precisão, sensores, interface de usuário e arquitetura modular.

---

## ⚙️ Funcionamento

O sistema utiliza um **ESP32 DevKit V1** como controlador principal.

O motor de passo **28BYJ-48**, acionado através do driver **ULN2003**, movimenta o mecanismo responsável pelo posicionamento dos compartimentos.

A ativação pode ser realizada através de:

- 🌐 Interface Web hospedada pelo ESP32
- 🔘 Botão físico
- 🔊 Aviso sonoro através de buzzer

Fluxo básico:

```text
Usuário
   │
   ├── Interface Web
   │
   └── Botão físico
          │
          ▼
        ESP32
          │
          ▼
       ULN2003
          │
          ▼
      28BYJ-48
          │
          ▼
   Compartimento
```

---

## 🔩 Hardware

### Componentes principais

| Componente | Quantidade |
|---|---:|
| ESP32 DevKit V1 | 1 |
| Motor de passo 28BYJ-48 | 1 |
| Driver ULN2003 | 1 |
| Buzzer ativo 5V | 1 |
| Botão push-button | 1 |
| Estrutura mecânica impressa em 3D | 1 |

---

## 🔌 Pinagem

### Motor de passo / ULN2003

| ESP32 | ULN2003 |
|---|---|
| GPIO 5 | IN1 |
| GPIO 18 | IN2 |
| GPIO 19 | IN3 |
| GPIO 21 | IN4 |

### Interface

| Componente | GPIO |
|---|---:|
| Botão | GPIO 4 |
| Buzzer | GPIO 15 |

O botão utiliza `INPUT_PULLUP`, portanto o acionamento ocorre em nível lógico **LOW**.

---

## 💻 Software

O firmware foi desenvolvido utilizando:

- C++
- Arduino Framework
- PlatformIO
- ESP32
- Wi-Fi
- WebServer
- Stepper Library

### Dependência

```ini
arduino-libraries/Stepper@^1.1.3
```

As bibliotecas nativas do framework Arduino-ESP32, como `WiFi.h` e `WebServer.h`, não precisam ser instaladas separadamente.

---

## 🛠️ Compilação

### Clonar o projeto

```bash
git clone https://github.com/RenanGoncalves-dev/ELOSproject_ESP32.git
```

### Abrir no VS Code

Abra a pasta:

```text
ELOSproject_ESP32
```

com o Visual Studio Code e a extensão PlatformIO instalada.

### Compilar

```bash
pio run
```

### Enviar para o ESP32

Conecte o ESP32 via USB e execute:

```bash
pio run --target upload
```

### Monitor serial

```bash
pio device monitor
```

O monitor serial utiliza:

```text
115200 baud
```

---

## 🌐 Configuração Wi-Fi

As credenciais de Wi-Fi devem ser configuradas diretamente no firmware:

```cpp
const char* ssid = "SEU_NOME_DE_REDE";
const char* password = "SUA_SENHA_WIFI";
```

### ⚠️ Segurança

**Não publique suas credenciais reais no GitHub.**

Para versões futuras, a configuração será separada do código principal através de um sistema próprio de configuração.

---

## 📐 Posicionamento

O protótipo possui atualmente **8 compartimentos**.

A primeira implementação utiliza uma quantidade definida de passos do motor para realizar o deslocamento entre compartimentos.

Durante os testes mecânicos foi observado que o valor teórico:

```text
2048 / 8 = 256 passos
```

não representa necessariamente o deslocamento físico ideal do mecanismo.

Por isso, o projeto está passando por uma etapa de **calibração mecânica e eletrônica**.

Um dos valores testados experimentalmente foi aproximadamente:

```text
254 passos por compartimento
```

Esse valor ainda deve ser validado após a correção mecânica do acoplamento entre o eixo do motor e o mecanismo.

---

## ⚠️ Limitações atuais

O protótipo ainda possui algumas limitações:

- Não possui sensor de posição HOME
- O posicionamento depende da contagem de passos
- Pode ocorrer perda de referência caso o motor perca passos
- A calibração depende do mecanismo mecânico
- O acoplamento entre motor e mecanismo ainda está sendo aprimorado
- Não existe atualmente um sensor para confirmar que o medicamento foi realmente dispensado
- O sistema ainda não possui gerenciamento completo de horários e doses
- A interface Web ainda está em estágio inicial

Essas limitações fazem parte do processo de desenvolvimento do protótipo.

---

## 🚧 Próximos passos

### Curto prazo

- [ ] Finalizar calibração do mecanismo
- [ ] Corrigir definitivamente o acoplamento do eixo
- [ ] Testar diferentes velocidades do motor
- [ ] Melhorar a interface Web
- [ ] Organizar o código em módulos
- [ ] Realizar testes de repetibilidade

### Médio prazo

- [ ] Adicionar sensor de posição HOME
- [ ] Adicionar sensores de confirmação de dispensação
- [ ] Implementar controle de horários
- [ ] Adicionar RTC
- [ ] Melhorar o sistema de alertas
- [ ] Criar sistema de configuração das doses

### Longo prazo

- [ ] Desenvolver arquitetura modular
- [ ] Permitir expansão do número de medicamentos
- [ ] Criar módulos independentes de armazenamento
- [ ] Desenvolver sistema de identificação dos módulos
- [ ] Melhorar a precisão mecânica
- [ ] Avaliar motores e sistemas de transmissão mais adequados
- [ ] Desenvolver uma versão mais robusta para pesquisa/TCC

---

## 🧩 Arquitetura futura

Uma das principais evoluções planejadas para o ELOS é a criação de uma arquitetura **modular**.

A ideia é permitir que novos compartimentos ou módulos de medicamentos possam ser adicionados ao sistema sem a necessidade de reconstruir todo o equipamento.

Conceito:

```text
                 ┌──────────────────┐
                 │   CONTROLADOR    │
                 │      ESP32       │
                 └────────┬─────────┘
                          │
             ┌────────────┼────────────┐
             │            │            │
             ▼            ▼            ▼
        ┌─────────┐  ┌─────────┐  ┌─────────┐
        │ Módulo  │  │ Módulo  │  │ Módulo  │
        │   01    │  │   02    │  │   03    │
        └─────────┘  └─────────┘  └─────────┘
             │            │            │
             └────────────┴────────────┘
                          │
                          ▼
                   Área de saída
```

Essa arquitetura poderá permitir maior escalabilidade, manutenção simplificada e adaptação do equipamento para diferentes necessidades.

---

## 📁 Estrutura do projeto

```text
ELOSproject_ESP32/
│
├── src/
│   └── main.cpp
│
├── include/
│
├── lib/
│
├── test/
│
├── docs/
│
├── .gitignore
├── platformio.ini
└── README.md
```

---

## 🔬 Desenvolvimento

O projeto está sendo desenvolvido de forma incremental, começando por um protótipo funcional e evoluindo gradualmente para uma arquitetura mais robusta.

As etapas atuais envolvem principalmente:

1. Desenvolvimento do firmware
2. Testes do motor de passo
3. Calibração do posicionamento
4. Desenvolvimento mecânico
5. Integração entre hardware e software
6. Testes de repetibilidade
7. Evolução para uma arquitetura modular

---

## 🏫 Contexto

**Projeto:** ELOS — Dispenser Automatizado de Medicamentos  
**Instituição:** Instituto Federal do Espírito Santo — IFES  
**Aplicação:** NAPNE  
**Plataforma:** ESP32  
**Status:** Protótipo em desenvolvimento

---

## 👨‍💻 Autor

**Renan Bernardo Gonçalves**

Desenvolvimento de hardware, firmware, integração eletrônica e desenvolvimento do protótipo.

---

## 📄 Licença

Este projeto está em desenvolvimento para fins educacionais, experimentais e de pesquisa.

A licença definitiva do projeto será definida conforme sua evolução e finalidade de distribuição.
