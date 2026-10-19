"""API de la maquina expendedora.

Un solo proceso levanta:
  - El servidor TCP (socket) al que se conectan las maquinas (TCP_PORT, por defecto 8080)
  - La API REST de FastAPI para administrar/monitorear (puerto de uvicorn, por defecto 8000)

Ejecutar:  uvicorn app.main:app --host 0.0.0.0 --port 8000
"""

import logging
import os
from contextlib import asynccontextmanager
from dataclasses import asdict

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

from .store import store
from .tcp_server import VendingTCPServer

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s [%(name)s] %(message)s")

TCP_HOST = os.getenv("TCP_HOST", "0.0.0.0")
TCP_PORT = int(os.getenv("TCP_PORT", "8080"))


@asynccontextmanager
async def lifespan(app: FastAPI):
    tcp = VendingTCPServer(store, TCP_HOST, TCP_PORT)
    await tcp.start()
    app.state.tcp = tcp
    yield
    await tcp.stop()


app = FastAPI(title="Vending Machine API", lifespan=lifespan)


class ProductUpdate(BaseModel):
    name: str | None = None
    price: float | None = Field(default=None, ge=0)
    stock: int | None = Field(default=None, ge=0)


@app.get("/products")
async def list_products():
    return [asdict(p) for p in store.products.values()]


@app.put("/products/{product_id}")
async def update_product(product_id: int, update: ProductUpdate):
    product = store.products.get(product_id)
    if product is None:
        raise HTTPException(status_code=404, detail="Producto no encontrado")
    for key, value in update.model_dump(exclude_none=True).items():
        setattr(product, key, value)
    return asdict(product)


@app.get("/devices")
async def list_devices():
    return [asdict(d) for d in store.devices.values()]


@app.get("/transactions")
async def list_transactions():
    return [asdict(t) for t in store.transactions]
