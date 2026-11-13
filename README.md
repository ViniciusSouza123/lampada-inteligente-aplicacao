# Lâmpada Inteligente — ESP32 + Arduino IoT Cloud + InfluxDB + Grafana

Projeto da AV2 de **Práticas Integradas: Camada de Aplicação** (SENAI CIMATEC).

Uma lâmpada inteligente (LED RGB) controlada localmente pela ESP32 e remotamente por um painel no Arduino IoT Cloud. A ESP32 também envia as leituras dos sensores para um backend próprio, que grava tudo em um banco de dados (InfluxDB). O histórico aparece em um dashboard no Grafana.

## Como funciona

```
ESP32 --Wi-Fi--> Arduino IoT Cloud   (painel ao vivo + comandos)
ESP32 --HTTP POST, a cada 5 s--> Backend (FastAPI) --> InfluxDB --> Grafana (histórico)
```

## Estrutura do repositório

- `firmware/lampada.ino`: código da ESP32
- `firmware/thingProperties.h`: variáveis do Arduino IoT Cloud (modelo, sem segredos)
- `backend/main.py`: API que recebe as leituras e grava no InfluxDB
- `backend/Dockerfile` e `backend/requirements.txt`: container do backend
- `docker-compose.yml`: sobe InfluxDB, Grafana e backend
- `.env.example`: modelo das senhas (copiar para `.env`)

## 1. Como montar o projeto (hardware)

Componentes: ESP32 DevKit V1, sensor DHT11 (módulo de 3 pinos com resistor embutido), módulo LDR com saída digital, botão, buzzer **passivo**, potenciômetro, LED RGB de **cátodo comum**, resistor (ex.: 220 Ω) no GND comum do LED, protoboard e jumpers.

| Componente | Pino da ESP32 | Observação |
|---|---|---|
| DHT11 (dado) | GPIO 21 | alimentar com 3,3 V |
| LDR (saída digital) | GPIO 22 | no módulo usado, saída 1 = escuro |
| Botão | GPIO 18 | entre o pino e o GND (usa `INPUT_PULLUP`) |
| Potenciômetro (central) | GPIO 34 | pino ADC1, funciona com o Wi-Fi ligado |
| Buzzer passivo | GPIO 2 | toca por frequência (PWM) |
| LED RGB vermelho | GPIO 4 | |
| LED RGB verde | GPIO 23 | |
| LED RGB azul | GPIO 27 | |
| LED RGB cátodo comum | GND | com resistor |

## 2. Firmware (ESP32 + Arduino IoT Cloud)

1. No Arduino IoT Cloud, crie um **Thing** com um dispositivo ESP32 (placa *ESP32 Dev Module*) e estas 5 variáveis, todas com atualização *On change*:

| Variável | Tipo | Permissão |
|---|---|---|
| `comando` | String | leitura e escrita |
| `LED` | String | só leitura |
| `sistema` | String | só leitura |
| `temperatura` | float | só leitura |
| `luminosidade` | bool | só leitura |

2. Na aba *Sketch Secrets*, preencha o nome e a senha do Wi-Fi (2,4 GHz) e a *Secret Key* do dispositivo.
3. Instale as bibliotecas **DHT sensor library** e **Adafruit Unified Sensor**.
4. Cole `firmware/lampada.ino` no sketch e troque, no topo do arquivo:
   - `BACKEND_URL` pelo IP do computador que roda o backend (ex.: `http://192.168.15.100:8000/leituras`);
   - `BACKEND_API_KEY` pelo valor de `API_KEY` do seu `.env`.
5. Grave na placa e abra o Serial Monitor em **115200**. A cada 5 s deve aparecer `[BACKEND] Envio: OK (200)`.
6. No Arduino IoT Cloud, crie um dashboard com: *Messenger* (`comando`), *Value* (`sistema` e `LED`), *Gauge* (`temperatura`) e *Status* (`luminosidade`).

### Comandos do painel (digitados no Messenger)

| Comando | Efeito |
|---|---|
| `Ligar` / `Desligar` | acende ou apaga o LED, ignorando a luz |
| `Vermelho` / `Verde` / `Azul` | LED nessa cor por 1 s |
| `Ligar LDR` / `Desligar LDR` | liga ou desliga o sensor de luz (ligar volta ao modo automático) |
| `Ativar Temperatura` / `Desativar Temperatura` | liga ou desliga o sensor de temperatura |
| `Ativar Buzzer` / `Desativar Buzzer` | permite ou impede o buzzer de tocar |

### Comportamento do circuito

- **LED:** liga quando há luz e apaga no escuro. A cor vem do potenciômetro (vermelho, verde ou azul, em três faixas).
- **Botão (interrupção externa):** liga ou desliga o sistema todo (LED, temperatura, LDR e buzzer).
- **Temperatura:** se for **30 °C ou mais**, ou **abaixo de 0 °C**, o buzzer toca e o LED desliga. O limite fica em `TEMP_MAX` no código.
- **Monitor Serial:** mostra temperatura, LDR, estado do LED, cor, estado do buzzer e conexão com a nuvem.

## 3. Como rodar o backend

Pré-requisito: **Docker Desktop** instalado e rodando.

1. Copie o modelo de senhas e edite os valores:

```
copy .env.example .env
notepad .env
```

Use senhas suas. O token do InfluxDB e a `API_KEY` devem ser textos longos e aleatórios. O arquivo `.env` **não é enviado ao GitHub**.

2. Suba os três serviços:

```
docker compose up -d
docker compose ps
```

Devem aparecer `lampada-influxdb`, `lampada-grafana` e `lampada-backend` como `Up`.

3. Teste a API:

```
Invoke-RestMethod http://localhost:8000/saude
```

Deve responder `status : ok`.

4. A ESP32 e o computador precisam estar **na mesma rede Wi-Fi**. Descubra o IP do computador com `ipconfig` (Endereço IPv4) e coloque no `BACKEND_URL` do firmware. Se o roteador reiniciar, o IP pode mudar.

### API do backend

| Rota | Método | Descrição |
|---|---|---|
| `/saude` | GET | confere se o backend está no ar |
| `/leituras` | POST | recebe uma leitura e grava no InfluxDB |

A rota `/leituras` exige o cabeçalho `x-api-key` com a chave da API (autenticação). Exemplo de corpo:

```json
{
  "dispositivo": "lampada-esp32",
  "timestamp": 1794442000,
  "temperatura": 26.5,
  "luminosidade": 1,
  "led_ligado": 1,
  "cor": "VERDE",
  "sistema_ligado": 1,
  "buzzer_tocando": 0
}
```

O `timestamp` (segundos desde 1970, UTC) vem da hora real da ESP32 (NTP). Se faltar, o backend usa a hora do servidor.

## 4. Banco de dados (InfluxDB 2)

- **Organização:** `senai` | **Bucket:** `lampada` | **Retenção:** 30 dias.
- **Autenticação:** usuário e senha do InfluxDB (no `.env`) e um **token de acesso**, usado pelo backend e pelo Grafana.
- O bucket, a retenção e o token são criados automaticamente na primeira subida pelo `docker-compose.yml`.
- **Medição:** `lampada` | **Tag:** `dispositivo` | **Campos:** `temperatura`, `luminosidade` (1 = claro), `led_ligado`, `cor`, `cor_codigo` (0 = desligado, 1 = vermelho, 2 = verde, 3 = azul), `sistema_ligado`, `buzzer_tocando`.
- Os dados ficam guardados em volumes do Docker e permanecem ao reiniciar o computador.

## 5. Grafana

Acesse `http://localhost:3000` com o usuário e a senha do `.env`.

**Fonte de dados:** InfluxDB, linguagem **Flux**, URL `http://influxdb:8086` (nome do serviço no Docker, não `localhost`), organização `senai`, bucket `lampada` e o token do InfluxDB. O *Basic auth* deve ficar desligado.

Dashboard **Lampada inteligente**, com atualização automática de **5 s**:

| Painel | Tipo | Campo |
|---|---|---|
| Temperatura (°C) | gráfico de linha | `temperatura` |
| Luminosidade | indicador (1 = CLARO, 0 = ESCURO) | `luminosidade` |
| Cor do LED | indicador (nome da cor) | `cor_codigo` |
| Botão (sistema) | indicador (LIGADO / DESLIGADO) | `sistema_ligado` |

Exemplo de consulta (painel de temperatura):

```
from(bucket: "lampada")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "lampada")
  |> filter(fn: (r) => r._field == "temperatura")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
```

## 6. Como consultar os dados salvos

**Pelo InfluxDB:** abra `http://localhost:8086`, entre com o usuário e a senha do `.env`, vá em **Data Explorer**, escolha o bucket `lampada`, a medição `lampada` e os campos desejados, e clique em **Submit**. Pelo **Script Editor**, dá para usar uma consulta Flux:

```
from(bucket: "lampada")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "lampada")
```

**Pelo Grafana:** abra o dashboard *Lampada inteligente* ou use **Explore** com a fonte `influxdb`.


## Decisões de projeto

- O LED liga com luz e apaga no escuro (configurável em `LED_LIGA_COM_LUZ`).
- O potenciômetro escolhe entre três cores puras: vermelho, verde e azul.
- A umidade do DHT11 não é enviada nem exibida.

## Segurança

O arquivo `.env` (senhas, token e chave da API) está no `.gitignore` e nunca deve ser enviado ao GitHub. No firmware, `BACKEND_URL` e `BACKEND_API_KEY` ficam com valores de exemplo no repositório.
