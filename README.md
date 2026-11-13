\# Lâmpada Inteligente — ESP32 + Arduino IoT Cloud + InfluxDB + Grafana



Projeto da AV2 de \*\*Práticas Integradas: Camada de Aplicação\*\* (SENAI CIMATEC).



Uma lâmpada inteligente (LED RGB) controlada localmente pela ESP32 e remotamente por um painel no Arduino IoT Cloud. A ESP32 também envia as leituras dos sensores para um backend próprio, que grava tudo em um banco de dados (InfluxDB). O histórico aparece em um dashboard no Grafana.



\## Como funciona



```

ESP32 --Wi-Fi--> Arduino IoT Cloud   (painel ao vivo + comandos)

ESP32 --HTTP POST, a cada 5 s--> Backend (FastAPI) --> InfluxDB --> Grafana (histórico)

```



\## Estrutura do repositório



```

firmware/

&#x20; lampada.ino            código da ESP32

&#x20; thingProperties.h      variáveis do Arduino IoT Cloud (modelo, sem segredos)

backend/

&#x20; main.py                API que recebe as leituras e grava no InfluxDB

&#x20; Dockerfile

&#x20; requirements.txt

docker-compose.yml       sobe InfluxDB + Grafana + backend

.env.example             modelo das senhas (copiar para .env)

```



\## 1. Como montar o projeto (hardware)



Componentes: ESP32 DevKit V1, sensor DHT11 (módulo de 3 pinos com resistor embutido), módulo LDR com saída digital, botão, buzzer \*\*passivo\*\*, potenciômetro, LED RGB de \*\*cátodo comum\*\*, resistor (ex.: 220 Ω) no GND comum do LED, protoboard e jumpers.



| Componente | Pino da ESP32 | Observação |

|---|---|---|

| DHT11 (dado) | GPIO 21 | alimentar com 3,3 V |

| LDR (saída digital) | GPIO 22 | no módulo usado, saída 1 = escuro |

| Botão | GPIO 18 | entre o pino e o GND (usa `INPUT\_PULLUP`) |

| Potenciômetro (central) | GPIO 34 | pino ADC1, funciona com o Wi-Fi ligado |

| Buzzer passivo | GPIO 2 | toca por frequência (PWM) |

| LED RGB vermelho | GPIO 4 | |

| LED RGB verde | GPIO 23 | |

| LED RGB azul | GPIO 27 | |

| LED RGB cátodo comum | GND | com resistor |



\## 2. Firmware (ESP32 + Arduino IoT Cloud)



1\. No Arduino IoT Cloud, crie um \*\*Thing\*\* com um dispositivo ESP32 (placa \*ESP32 Dev Module\*) e estas 5 variáveis, todas com atualização \*On change\*:



| Variável | Tipo | Permissão |

|---|---|---|

| `comando` | String | leitura e escrita |

| `LED` | String | só leitura |

| `sistema` | String | só leitura |

| `temperatura` | float | só leitura |

| `luminosidade` | bool | só leitura |



2\. Na aba \*Sketch Secrets\*, preencha o nome e a senha do Wi-Fi (2,4 GHz) e a \*Secret Key\* do dispositivo.

3\. Instale as bibliotecas \*\*DHT sensor library\*\* e \*\*Adafruit Unified Sensor\*\*.

4\. Cole `firmware/lampada.ino` no sketch e troque, no topo do arquivo:

&#x20;  - `BACKEND\_URL` pelo IP do computador que roda o backend (ex.: `http://192.168.15.100:8000/leituras`);

&#x20;  - `BACKEND\_API\_KEY` pelo valor de `API\_KEY` do seu `.env`.

5\. Grave na placa e abra o Serial Monitor em \*\*115200\*\*. A cada 5 s deve aparecer `\[BACKEND] Envio: OK (200)`.

6\. No Arduino IoT Cloud, crie um dashboard com: \*Messenger\* (`comando`), \*Value\* (`sistema` e `LED`), \*Gauge\* (`temperatura`) e \*Status\* (`luminosidade`).



\### Comandos do painel (digitados no Messenger)



| Comando | Efeito |

|---|---|

| `Ligar` / `Desligar` | acende ou apaga o LED, ignorando a luz |

| `Vermelho` / `Verde` / `Azul` | LED nessa cor por 1 s |

| `Ligar LDR` / `Desligar LDR` | liga ou desliga o sensor de luz (ligar volta ao modo automático) |

| `Ativar Temperatura` / `Desativar Temperatura` | liga ou desliga o sensor de temperatura |

| `Ativar Buzzer` / `Desativar Buzzer` | permite ou impede o buzzer de tocar |



\### Comportamento do circuito



\- \*\*LED:\*\* liga quando há luz e apaga no escuro. A cor vem do potenciômetro (vermelho, verde ou azul, em três faixas).

\- \*\*Botão (interrupção externa):\*\* liga ou desliga o sistema todo (LED, temperatura, LDR e buzzer).

\- \*\*Temperatura:\*\* se for \*\*30 °C ou mais\*\*, ou \*\*abaixo de 0 °C\*\*, o buzzer toca e o LED desliga. O limite fica em `TEMP\_MAX` no código.

\- \*\*Monitor Serial:\*\* mostra temperatura, LDR, estado do LED, cor, estado do buzzer e conexão com a nuvem.



\## 3. Como rodar o backend



Pré-requisito: \*\*Docker Desktop\*\* instalado e rodando.



1\. Copie o modelo de senhas e edite os valores:



```

copy .env.example .env

notepad .env

```



&#x20;  Use senhas suas. O token do InfluxDB e a `API\_KEY` devem ser textos longos e aleatórios. O arquivo `.env` \*\*não é enviado ao GitHub\*\*.



2\. Suba os três serviços:



```

docker compose up -d

docker compose ps

```



&#x20;  Devem aparecer `lampada-influxdb`, `lampada-grafana` e `lampada-backend` como `Up`.



3\. Teste a API:



```

Invoke-RestMethod http://localhost:8000/saude

```



&#x20;  Deve responder `status : ok`.



4\. A ESP32 e o computador precisam estar \*\*na mesma rede Wi-Fi\*\*. Descubra o IP do computador com `ipconfig` (Endereço IPv4) e coloque no `BACKEND\_URL` do firmware. Se o roteador reiniciar, o IP pode mudar.



\### API do backend



| Rota | Método | Descrição |

|---|---|---|

| `/saude` | GET | confere se o backend está no ar |

| `/leituras` | POST | recebe uma leitura e grava no InfluxDB |



A rota `/leituras` exige o cabeçalho `x-api-key` com a chave da API (autenticação). Exemplo de corpo:



```json

{

&#x20; "dispositivo": "lampada-esp32",

&#x20; "timestamp": 1794442000,

&#x20; "temperatura": 26.5,

&#x20; "luminosidade": 1,

&#x20; "led\_ligado": 1,

&#x20; "cor": "VERDE",

&#x20; "sistema\_ligado": 1,

&#x20; "buzzer\_tocando": 0

}

```



O `timestamp` (segundos desde 1970, UTC) vem da hora real da ESP32 (NTP). Se faltar, o backend usa a hora do servidor.



\## 4. Banco de dados (InfluxDB 2)



\- \*\*Organização:\*\* `senai` | \*\*Bucket:\*\* `lampada` | \*\*Retenção:\*\* 30 dias.

\- \*\*Autenticação:\*\* usuário e senha do InfluxDB (no `.env`) e um \*\*token de acesso\*\*, usado pelo backend e pelo Grafana.

\- O bucket, a retenção e o token são criados automaticamente na primeira subida pelo `docker-compose.yml`.

\- \*\*Medição:\*\* `lampada` | \*\*Tag:\*\* `dispositivo` | \*\*Campos:\*\* `temperatura`, `luminosidade` (1 = claro), `led\_ligado`, `cor`, `cor\_codigo` (0 = desligado, 1 = vermelho, 2 = verde, 3 = azul), `sistema\_ligado`, `buzzer\_tocando`.

\- Os dados ficam guardados em volumes do Docker e permanecem ao reiniciar o computador.



\## 5. Grafana



Acesse `http://localhost:3000` com o usuário e a senha do `.env`.



\*\*Fonte de dados:\*\* InfluxDB, linguagem \*\*Flux\*\*, URL `http://influxdb:8086` (nome do serviço no Docker, não `localhost`), organização `senai`, bucket `lampada` e o token do InfluxDB. O \*Basic auth\* deve ficar desligado.



Dashboard \*\*Lampada inteligente\*\*, com atualização automática de \*\*5 s\*\*:



| Painel | Tipo | Campo |

|---|---|---|

| Temperatura (°C) | gráfico de linha | `temperatura` |

| Luminosidade | indicador (1 = CLARO, 0 = ESCURO) | `luminosidade` |

| Cor do LED | indicador (nome da cor) | `cor\_codigo` |

| Botão (sistema) | indicador (LIGADO / DESLIGADO) | `sistema\_ligado` |



Exemplo de consulta (painel de temperatura):



```

from(bucket: "lampada")

&#x20; |> range(start: v.timeRangeStart, stop: v.timeRangeStop)

&#x20; |> filter(fn: (r) => r.\_measurement == "lampada")

&#x20; |> filter(fn: (r) => r.\_field == "temperatura")

&#x20; |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)

```



\## 6. Como consultar os dados salvos



\*\*Pelo InfluxDB:\*\* abra `http://localhost:8086`, entre com o usuário e a senha do `.env`, vá em \*\*Data Explorer\*\*, escolha o bucket `lampada`, a medição `lampada` e os campos desejados, e clique em \*\*Submit\*\*. Pelo \*\*Script Editor\*\*, dá para usar uma consulta Flux:



```

from(bucket: "lampada")

&#x20; |> range(start: -1h)

&#x20; |> filter(fn: (r) => r.\_measurement == "lampada")

```



\*\*Pelo Grafana:\*\* abra o dashboard \*Lampada inteligente\* ou use \*\*Explore\*\* com a fonte `influxdb`.



\## Dificuldades encontradas e soluções



\- \*\*Potenciômetro:\*\* os pinos GPIO 15 e 12 pertencem ao ADC2, que não lê com o Wi-Fi ligado. Solução: mudar para o GPIO 34 (ADC1).

\- \*\*Buzzer passivo:\*\* não toca com `digitalWrite`. Solução: gerar a frequência por PWM (LEDC).

\- \*\*LDR digital:\*\* o módulo manda 1 no escuro. Solução: inverter no código (`LDR\_INVERTER`).

\- \*\*LED RGB com um resistor só no GND:\*\* o vermelho "puxava" a corrente e o amarelo saía vermelho. Solução: usar só cores puras (vermelho, verde e azul).

\- \*\*Plano grátis do Arduino IoT Cloud:\*\* limite de 5 variáveis e sem API. Solução: reunir estados em textos (`sistema` e `LED`), receber todos os comandos em uma variável (`comando`) e guardar o histórico em um backend próprio com InfluxDB e Grafana.

\- \*\*Cloud Agent no Opera:\*\* não gravou na placa. Solução: usar o Edge.

\- \*\*Docker no Windows:\*\* a instalação exigiu o recurso \*Virtual Machine Platform\* (`wsl --install`).

\- \*\*Grafana dentro do Docker:\*\* `localhost` aponta para o próprio container. Solução: usar o nome do serviço, `http://influxdb:8086`.



\## Decisões de projeto



\- O LED liga com luz e apaga no escuro (configurável em `LED\_LIGA\_COM\_LUZ`).

\- O potenciômetro escolhe entre três cores puras: vermelho, verde e azul.

\- A umidade do DHT11 não é enviada nem exibida.



\## Segurança



O arquivo `.env` (senhas, token e chave da API) está no `.gitignore` e nunca deve ser enviado ao GitHub. No firmware, `BACKEND\_URL` e `BACKEND\_API\_KEY` ficam com valores de exemplo no repositório.

