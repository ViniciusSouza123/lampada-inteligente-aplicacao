import os
from datetime import datetime, timezone
from typing import Optional

from fastapi import FastAPI, Header, HTTPException
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS
from pydantic import BaseModel

INFLUX_URL = os.environ["INFLUX_URL"]
INFLUX_TOKEN = os.environ["INFLUX_TOKEN"]
INFLUX_ORG = os.environ["INFLUX_ORG"]
INFLUX_BUCKET = os.environ["INFLUX_BUCKET"]
API_KEY = os.environ["API_KEY"]

client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = client.write_api(write_options=SYNCHRONOUS)

app = FastAPI(title="API da Lampada Inteligente")

CODIGO_COR = {"DESLIGADO": 0, "VERMELHO": 1, "VERDE": 2, "AZUL": 3}


class Leitura(BaseModel):
    dispositivo: str = "lampada-esp32"
    timestamp: Optional[int] = None  # segundos desde 1970 (UTC); se faltar, usa a hora do servidor
    temperatura: Optional[float] = None
    luminosidade: Optional[int] = None  # 1 = claro, 0 = escuro
    led_ligado: Optional[int] = None  # 1 = ligado, 0 = desligado
    cor: Optional[str] = None  # VERMELHO, VERDE, AZUL ou DESLIGADO
    sistema_ligado: Optional[int] = None  # estado do botao: 1 = ligado, 0 = desligado
    buzzer_tocando: Optional[int] = None  # 1 = tocando, 0 = quieto


@app.get("/saude")
def saude():
    return {"status": "ok"}


@app.post("/leituras")
def receber_leitura(leitura: Leitura, x_api_key: str = Header(default="")):
    if x_api_key != API_KEY:
        raise HTTPException(status_code=401, detail="chave invalida")

    ponto = Point("lampada").tag("dispositivo", leitura.dispositivo)
    campos = 0

    if leitura.temperatura is not None:
        ponto.field("temperatura", float(leitura.temperatura))
        campos += 1
    if leitura.luminosidade is not None:
        ponto.field("luminosidade", int(leitura.luminosidade))
        campos += 1
    if leitura.led_ligado is not None:
        ponto.field("led_ligado", int(leitura.led_ligado))
        campos += 1
    if leitura.cor is not None:
        cor = leitura.cor.upper()
        ponto.field("cor", cor)
        ponto.field("cor_codigo", CODIGO_COR.get(cor, 0))
        campos += 2
    if leitura.sistema_ligado is not None:
        ponto.field("sistema_ligado", int(leitura.sistema_ligado))
        campos += 1
    if leitura.buzzer_tocando is not None:
        ponto.field("buzzer_tocando", int(leitura.buzzer_tocando))
        campos += 1

    if campos == 0:
        raise HTTPException(status_code=400, detail="nenhuma leitura enviada")

    if leitura.timestamp and leitura.timestamp > 1_600_000_000:
        ponto.time(leitura.timestamp, WritePrecision.S)
    else:
        ponto.time(datetime.now(timezone.utc), WritePrecision.NS)

    write_api.write(bucket=INFLUX_BUCKET, record=ponto)
    return {"status": "gravado", "campos": campos}