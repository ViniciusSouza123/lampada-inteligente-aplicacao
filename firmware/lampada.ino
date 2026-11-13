/*
 * LAMPADA INTELIGENTE - ESP32 DevKit V1 + Arduino IoT Cloud + Backend (InfluxDB)
 *
 * Variaveis da nuvem (thingProperties.h):
 *   comando (String, leitura/escrita) | LED (String) | sistema (String)
 *   temperatura (float) | luminosidade (bool: true = claro)
 *
 * Comandos aceitos no painel (maiuscula/minuscula tanto faz):
 *   Ligar, Desligar, Vermelho, Verde, Azul,
 *   Ativar Temperatura, Desativar Temperatura,
 *   Ligar LDR, Desligar LDR,
 *   Ativar Buzzer, Desativar Buzzer
 *
 * Antes de usar: troque BACKEND_URL e BACKEND_API_KEY pelos seus valores.
 * Serial Monitor: 115200 baud
 */

#include "thingProperties.h"
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ---------- BACKEND (API que grava no InfluxDB) ----------
// O IP do computador pode mudar se o roteador reiniciar: confira com "ipconfig" antes de apresentar.
#define BACKEND_URL      "http://IP_DO_COMPUTADOR:8000/leituras"
#define BACKEND_API_KEY  "COLOQUE_A_CHAVE_DA_API_AQUI"
#define DISPOSITIVO      "lampada-esp32"
#define ENVIO_MS         5000     // envia uma leitura a cada 5 s

// ---------- PINOS ----------
#define PIN_DHT     21
#define PIN_LDR     22
#define PIN_BOTAO   18
#define PIN_POT     34
#define PIN_BUZZER  2    // buzzer PASSIVO
#define PIN_R       4
#define PIN_G       23
#define PIN_B       27

// ---------- CONFIGURACOES ----------
#define DHTTYPE DHT11
#define LED_ANODO_COMUM false   // LED ligado ao GND = catodo comum -> false
#define LDR_INVERTER true       // seu modulo: saida 1 = escuro. true -> 1 = CLARO, 0 = ESCURO
#define LED_LIGA_COM_LUZ true   // true: LED liga com luz e apaga no escuro | false: o contrario

#define TEMP_MAX 30.0           // buzzer toca e LED desliga se temp >= TEMP_MAX ou < TEMP_MIN
#define TEMP_MIN 0.0
#define FREQ_ALARME 1000
#define TEMPO_COR_MS 1000       // tempo que o LED mostra a cor dos comandos Vermelho/Verde/Azul

// ---------- BUZZER PASSIVO (funciona no core ESP32 2.x e 3.x) ----------
#define CH_BUZZER 0
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define BUZZER_INIT()  ledcAttachChannel(PIN_BUZZER, 2000, 8, CH_BUZZER)
  #define BUZZER_TOM(f)  ledcWriteTone(PIN_BUZZER, f)
#else
  #define BUZZER_INIT()  do { ledcSetup(CH_BUZZER, 2000, 8); ledcAttachPin(PIN_BUZZER, CH_BUZZER); } while (0)
  #define BUZZER_TOM(f)  ledcWriteTone(CH_BUZZER, f)
#endif

DHT dht(PIN_DHT, DHTTYPE);

// ---------- ESTADO ----------
volatile unsigned long ultimoBotaoMs = 0;
volatile bool botaoEvento = false;

bool sistemaLigado = true;       // controlado pelo botao fisico
bool ledLigado = false;
bool buzzerTocando = false;
const char* corAtual = "--";
float tempAtual = NAN;

// controlados pelos comandos do painel
bool temperaturaAtiva = true;
bool detectorAtivo = true;       // LDR ligado/desligado
bool buzzerAtivo = true;
bool modoManual = false;         // true apos "Ligar" ou "Desligar"
bool ledManual = false;
uint8_t corTemp = 0;             // 0 = nenhuma, 1 = vermelho, 2 = verde, 3 = azul
unsigned long tCorTemp = 0;

bool ntpIniciado = false;

unsigned long tDht = 0;
unsigned long tPrint = 0;
unsigned long tNuvem = 0;
unsigned long tEnvio = 0;

// ---------- INTERRUPCAO DO BOTAO (com debounce) ----------
void IRAM_ATTR isrBotao() {
  unsigned long agora = millis();
  if (agora - ultimoBotaoMs > 200) {
    botaoEvento = true;
    ultimoBotaoMs = agora;
  }
}

// ---------- LED RGB ----------
void setLed(bool r, bool g, bool b) {
  if (LED_ANODO_COMUM) { r = !r; g = !g; b = !b; }
  digitalWrite(PIN_R, r);
  digitalWrite(PIN_G, g);
  digitalWrite(PIN_B, b);
}

// ---------- BUZZER ----------
void bip(int ms, int freq) {
  BUZZER_TOM(freq);
  delay(ms);
  BUZZER_TOM(0);
}

// ---------- LDR: true = claro, false = escuro ----------
bool lerLdrClaro() {
  bool v = digitalRead(PIN_LDR);
  return LDR_INVERTER ? !v : v;
}

// ---------- COMANDOS DO PAINEL ----------
void onComandoChange() {
  String cmd = comando;
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  Serial.print(">>> COMANDO RECEBIDO: ");
  Serial.println(cmd);

  if (cmd == "ligar")                      { modoManual = true; ledManual = true; }
  else if (cmd == "desligar")              { modoManual = true; ledManual = false; }
  else if (cmd == "vermelho")              { corTemp = 1; tCorTemp = millis(); }
  else if (cmd == "verde")                 { corTemp = 2; tCorTemp = millis(); }
  else if (cmd == "azul")                  { corTemp = 3; tCorTemp = millis(); }
  else if (cmd == "ativar temperatura")    { temperaturaAtiva = true; tDht = 0; }
  else if (cmd == "desativar temperatura") { temperaturaAtiva = false; }
  else if (cmd == "ligar ldr" || cmd == "ativar detector")       { detectorAtivo = true; modoManual = false; }
  else if (cmd == "desligar ldr" || cmd == "desativar detector") { detectorAtivo = false; }
  else if (cmd == "ativar buzzer")         { buzzerAtivo = true; }
  else if (cmd == "desativar buzzer")      { buzzerAtivo = false; }
  else Serial.println("Comando desconhecido.");

  comando = "";   // limpa para aceitar o mesmo comando de novo
}

// ---------- ATUALIZA OS TEXTOS DO PAINEL ----------
void atualizarNuvem() {
  String s = String("SISTEMA: ") + (sistemaLigado ? "ON" : "OFF") +
             " | BUZZER: " + (!buzzerAtivo ? "DESATIVADO" : (buzzerTocando ? "ON" : "OFF"));
  if (!temperaturaAtiva) s += " | TEMPERATURA: DESATIVADA";
  if (!detectorAtivo)    s += " | LDR: DESATIVADO";
  if (s != sistema) sistema = s;

  String l = ledLigado ? (String("LIGADO - ") + corAtual) : String("DESLIGADO");
  if (l != LED) LED = l;
}

// ---------- ENVIO PARA O BACKEND (API -> InfluxDB) ----------
void enviarParaBackend() {
  if (WiFi.status() != WL_CONNECTED) return;

  String json = "{\"dispositivo\":\"" DISPOSITIVO "\"";

  time_t epoch = time(nullptr);
  if (epoch > 1600000000) {            // so envia timestamp se o relogio ja sincronizou (NTP)
    json += ",\"timestamp\":" + String((unsigned long)epoch);
  }
  if (sistemaLigado && temperaturaAtiva && !isnan(tempAtual)) {
    json += ",\"temperatura\":" + String(tempAtual, 1);
  }
  if (sistemaLigado && detectorAtivo) {
    json += ",\"luminosidade\":" + String(lerLdrClaro() ? 1 : 0);
  }
  json += ",\"led_ligado\":" + String(ledLigado ? 1 : 0);
  json += ",\"cor\":\"" + String(ledLigado ? corAtual : "DESLIGADO") + "\"";
  json += ",\"sistema_ligado\":" + String(sistemaLigado ? 1 : 0);
  json += ",\"buzzer_tocando\":" + String(buzzerTocando ? 1 : 0);
  json += "}";

  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(2000);
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", BACKEND_API_KEY);
  int codigo = http.POST(json);
  http.end();

  Serial.print("[BACKEND] Envio: ");
  if (codigo == 200) {
    Serial.println("OK (200)");
  } else if (codigo < 0) {
    Serial.print("FALHOU, sem conexao (codigo ");
    Serial.print(codigo);
    Serial.println(") - confira o IP, o firewall e se o Docker esta rodando");
  } else {
    Serial.print("FALHOU, o servidor respondeu ");
    Serial.println(codigo);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_POT, INPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);   // botao entre o pino e o GND
  attachInterrupt(digitalPinToInterrupt(PIN_BOTAO), isrBotao, FALLING);

  BUZZER_INIT();
  BUZZER_TOM(0);
  setLed(false, false, false);

  dht.begin();

  Serial.println();
  Serial.println("=== LAMPADA INTELIGENTE - ESP32 + ARDUINO IOT CLOUD + BACKEND ===");

  // Teste rapido na partida
  setLed(true, false, false);  delay(400);
  setLed(false, true, false);  delay(400);
  setLed(false, false, true);  delay(400);
  setLed(false, false, false);
  bip(200, 523);
  bip(200, 659);
  bip(200, 784);

  // Valores iniciais das variaveis da nuvem
  comando = "";
  LED = "DESLIGADO";
  sistema = "SISTEMA: ON | BUZZER: OFF";
  luminosidade = false;

  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  Serial.println("Conectando a nuvem... (o circuito ja funciona sem ela)");
  Serial.println("------------------------------------------------");
}

void loop() {
  ArduinoCloud.update();
  unsigned long agora = millis();

  // Hora real pela internet (NTP), usada no timestamp das leituras
  if (!ntpIniciado && WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    ntpIniciado = true;
  }

  // Fim da cor temporaria dos comandos Vermelho/Verde/Azul
  if (corTemp != 0 && agora - tCorTemp >= TEMPO_COR_MS) corTemp = 0;

  // Botao fisico: liga/desliga o sistema inteiro (sem barulho)
  if (botaoEvento) {
    botaoEvento = false;
    sistemaLigado = !sistemaLigado;
    if (sistemaLigado) {
      Serial.println(">>> SISTEMA LIGADO");
      tDht = 0;            // forca uma leitura nova do DHT11
    } else {
      corTemp = 0;
      tempAtual = NAN;
      Serial.println(">>> SISTEMA DESLIGADO");
    }
  }

  bool foraDaFaixa = false;   // temperatura >= TEMP_MAX ou < TEMP_MIN
  ledLigado = false;
  corAtual = "--";

  if (sistemaLigado) {
    // Sensor de temperatura (DHT11): no maximo 1 leitura a cada 2 s
    if (!temperaturaAtiva) {
      tempAtual = NAN;
    } else if (agora - tDht >= 2000) {
      tDht = agora;
      float t = dht.readTemperature();
      if (!isnan(t)) {
        tempAtual = t;
        temperatura = t;   // envia para a nuvem
      } else {
        tempAtual = NAN;
      }
    }

    foraDaFaixa = temperaturaAtiva && !isnan(tempAtual) &&
                  (tempAtual >= TEMP_MAX || tempAtual < TEMP_MIN);

    // Detector de luz (LDR)
    bool claro = false;
    if (detectorAtivo) {
      claro = lerLdrClaro();
      luminosidade = claro;   // envia para a nuvem
    }

    // O LED deve ligar?
    bool ledQuerLigar;
    if (modoManual)          ledQuerLigar = ledManual;
    else if (detectorAtivo)  ledQuerLigar = LED_LIGA_COM_LUZ ? claro : !claro;
    else                     ledQuerLigar = false;

    // LED RGB
    if (foraDaFaixa) {
      setLed(false, false, false);
    } else if (corTemp != 0) {
      if (corTemp == 1)      { setLed(true, false, false); corAtual = "VERMELHO"; }
      else if (corTemp == 2) { setLed(false, true, false); corAtual = "VERDE";    }
      else                   { setLed(false, false, true); corAtual = "AZUL";     }
      ledLigado = true;
    } else if (ledQuerLigar) {
      int pot = analogRead(PIN_POT);   // 0 a 4095
      if (pot < 1365)      { setLed(true, false, false); corAtual = "VERMELHO"; }
      else if (pot < 2730) { setLed(false, true, false); corAtual = "VERDE";    }
      else                 { setLed(false, false, true); corAtual = "AZUL";     }
      ledLigado = true;
    } else {
      setLed(false, false, false);
    }
  } else {
    setLed(false, false, false);
  }

  // Buzzer: toca com o sistema ligado, buzzer ativo e temperatura fora da faixa
  bool tocar = sistemaLigado && buzzerAtivo && foraDaFaixa;
  if (tocar && !buzzerTocando) {
    BUZZER_TOM(FREQ_ALARME);
    buzzerTocando = true;
  } else if (!tocar && buzzerTocando) {
    BUZZER_TOM(0);
    buzzerTocando = false;
  }

  // Atualiza os textos do painel a cada 0,5 s
  if (agora - tNuvem >= 500) {
    tNuvem = agora;
    atualizarNuvem();
  }

  // Envia as leituras ao backend a cada ENVIO_MS
  if (agora - tEnvio >= ENVIO_MS) {
    tEnvio = agora;
    enviarParaBackend();
  }

  // Relatorio no Monitor Serial a cada 1 s
  if (agora - tPrint >= 1000) {
    tPrint = agora;

    if (!sistemaLigado) {
      Serial.print("[DESLIGADO] Temp: -- | LDR: -- | LED RGB: DESLIGADO | Cor: -- | Buzzer: OFF");
    } else {
      Serial.print("[LIGADO] ");
      if (!temperaturaAtiva) {
        Serial.print("Temp: DESATIVADA");
      } else if (isnan(tempAtual)) {
        Serial.print("Temp: FALHA NA LEITURA (confira fios/pino 21)");
      } else {
        Serial.print("Temp: ");
        Serial.print(tempAtual, 1);
        Serial.print(" C");
      }
      Serial.print(" | LDR: ");
      if (!detectorAtivo) Serial.print("DESATIVADO");
      else Serial.print(lerLdrClaro() ? "1 (CLARO)" : "0 (ESCURO)");
      Serial.print(" | LED RGB: ");
      Serial.print(ledLigado ? "LIGADO" : "DESLIGADO");
      Serial.print(" | Cor: ");
      Serial.print(corAtual);
      Serial.print(" | Buzzer: ");
      Serial.print(!buzzerAtivo ? "DESATIVADO" : (buzzerTocando ? "ON" : "OFF"));
    }
    Serial.print(" | Nuvem: ");
    Serial.println(ArduinoCloud.connected() ? "CONECTADA" : "DESCONECTADA");
  }
}