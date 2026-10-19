

Componentes

# vending_machine (Núcleo / Orquestador)
- Función: Instancia el bucle de eventos (vm_event_loop), registra los manejadores de eventos centralizados y coordina las acciones del sistema.

- Responsabilidad: Recibe solicitudes de selección de producto, ordena el cobro al servidor y actúa según la respuesta (despachar producto o notificar error).

## vm_ui (Interfaz de Usuario)
- Función: Gestiona las entradas del usuario (pantalla táctil, botones).

- Responsabilidad: Emite un evento en el vm_event_loop cuando el usuario selecciona un ítem para comprar.

## comm (Módulo de Comunicaciones)
- Función: Administra una conexión TCP/Socket en segundo plano ejecutándose en su propia tarea de FreeRTOS (comm_task).

- Responsabilidad: Procesa las solicitudes de pago de forma sincrónica hacia el servidor y publica el resultado (OK o FAILED) de vuelta al bucle de eventos




## Ejemplo de flujo de eventos: seleccion y pago de producto
```C

[Usuario] ──(Selecciona ID 42)──> [vm_ui]
                                    │
                                    ├──(Emite UI_EVENT_PRODUCT_SELECTED)
                                    ▼
                         [vending_machine]
                                    │
                                    ├──(Llama comm_start_payment_by_item)
                                    ▼
                                 [comm]
                                    │
                          ┌─────────┴─────────┐
                   (Conexión Socket al Servidor)
                          └─────────┬─────────┘
                                    │
            ┌───────────────────────┼───────────────────────┐
            ▼                       ▼                       ▼
     [Respuesta: OK]        [Respuesta: REJECT]     [Sin conexión / Timeout]
            │                       │                       │
 (Emite COMM_EVENT_OK)   (Emite COMM_EVENT_FAILED) (Emite COMM_EVENT_FAILED)
            │                       │                       │
            └───────────────────────┼───────────────────────┘
                                    ▼
                         [vending_machine]
                                    │
                       ┌────────────┴────────────┐
                       ▼                         ▼
            (Despacha Producto)         (Muestra Error en UI)

```


# Comunicación con el servidor

La máquina mantiene una **conexión TCP persistente** con el servidor (`server/`).
Cada mensaje es un objeto **JSON en una sola línea** terminada en `\n`.

```
ESP → {"type":"hello","device_id":"vm-01"}        SRV → {"type":"hello_ack"}
ESP → {"type":"ping"}                              SRV → {"type":"pong"}
ESP → {"type":"pay_req","req_id":7,"item_id":42}   SRV → {"type":"pay_resp","req_id":7,"status":"ok"}
                                                   SRV → {"type":"pay_resp","req_id":7,"status":"rejected","reason":"no_stock"}
```

- `req_id` permite descartar una respuesta que llega tarde (después de un timeout).
- Si no hay tráfico, la máquina envía `ping` cada `COMM_PING_INTERVAL_MS`. Si no recibe `pong`, cierra y reconecta (evento `COMM_EVENT_DISCONNECTED`).
- El servidor cierra la conexión si no recibe nada en 30 s.

## Eventos de COMM

| Evento | Datos | Cuándo |
|---|---|---|
| `COMM_EVENT_CONNECTED` | - | Se recibió `hello_ack` |
| `COMM_EVENT_DISCONNECTED` | - | Se perdió la conexión |
| `COMM_EVENT_PAYMENT_OK` | `comm_payment_result_t` | Servidor aprobó el pago |
| `COMM_EVENT_PAYMENT_FAILED` | `comm_payment_result_t` | Rechazo (`no_stock`, `unknown_item`), `timeout` o `disconnected` |

## Estados de la máquina

`OFFLINE` → (conectado) → `IDLE` → (selección) → `PAYING` → (pago ok) → `DISPENSING` → `IDLE`.
Las selecciones fuera de `IDLE` se ignoran.

# Configuración y compilación (ESP-IDF 6.1)

```bash
idf.py set-target esp32s3
idf.py menuconfig
#   Example Connection Configuration  -> SSID y password del Wi-Fi
#   Vending Machine - Comunicaciones  -> IP de la PC con el servidor, puerto, device id
#   Vending Machine - Interfaz        -> simulación de selecciones (activada por defecto)
idf.py build flash monitor
```

# Servidor (Python + FastAPI)

Un solo proceso levanta el servidor TCP de las máquinas (puerto **8080**) y la API REST (puerto **8000**).

```bash
cd server
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000
```

- API: `GET /products`, `PUT /products/{id}` (ej. `{"stock": 5}`), `GET /devices`, `GET /transactions`. Documentación en http://localhost:8000/docs
- Simular una máquina sin hardware: `.venv/bin/python tools/fake_esp32.py --items 42 3 999`
- Tests: `.venv/bin/pytest`
- Variables de entorno: `TCP_HOST` (por defecto `0.0.0.0`), `TCP_PORT` (por defecto `8080`)

# Próximo paso: TLS

El transporte (`components/comm/comm_transport.c`) ya tiene la rama TLS con `esp_tls`:
1. Generar el certificado: `openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes`
2. Copiar `server.crt` a `components/comm/certs/server.crt` y activar `COMM_USE_TLS` en menuconfig.
3. En el servidor, pasar un `ssl.SSLContext` a `asyncio.start_server` (pendiente).
