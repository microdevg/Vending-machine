# Vending Machine (ESP32-S3 + servidor Python)

Máquina expendedora basada en un **ESP32-S3** que cobra cada compra contra un **servidor Python** a través de un socket TCP.

- **Firmware** (ESP-IDF 6.1): se conecta al Wi-Fi, mantiene una conexión TCP permanente con el servidor y, cuando el usuario elige un producto, pide el cobro y despacha si el servidor lo aprueba.
- **Servidor** (`server/`, Python + FastAPI): acepta las conexiones de las máquinas, aprueba o rechaza los pagos según el stock y expone una API REST para administrar productos y ver transacciones.

Estado actual: comunicación **sin TLS** y **sin autenticación**, pensada para probar el flujo completo. El despacho físico todavía es un log (no acciona motores) y el servidor guarda todo en memoria.

## Tabla de contenidos
- [Arquitectura general](#arquitectura-general)
- [Firmware: componentes](#firmware-componentes)
- [Flujo de una compra](#flujo-de-una-compra)
- [La conexión con el servidor](#la-conexión-con-el-servidor)
- [Protocolo](#protocolo)
- [Estados de la máquina](#estados-de-la-máquina)
- [Simulación y datos iniciales](#simulación-y-datos-iniciales)
- [Servidor](#servidor)
- [Puesta en marcha](#puesta-en-marcha)
- [Problemas comunes](#problemas-comunes)
- [Próximos pasos](#próximos-pasos)

---

## Arquitectura general

```
 ┌──────────── ESP32-S3 ────────────┐                 ┌──────────── PC / Mac ─────────────┐
 │                                  │                 │                                   │
 │  vm_ui ─┐                        │   Wi-Fi + TCP   │  server/  (un solo proceso)       │
 │         ├─► vm_event_loop ◄─┐    │   puerto 8080   │   ├─ tcp_server.py  :8080 ◄── máquinas
 │  comm ──┘        │          │    │ ◄─────────────► │   ├─ main.py (FastAPI) :8000 ◄── admin (navegador/curl)
 │   │              ▼          │    │  JSON por línea │   └─ store.py (productos, stock,  │
 │   │      vending_machine ───┘    │                 │                transacciones)     │
 │   └─ comm_task (socket abierto)  │                 │                                   │
 └──────────────────────────────────┘                 └───────────────────────────────────┘
```

- La máquina y el servidor hablan **directamente por un socket TCP** (puerto 8080) que queda **siempre abierto**.
- La **API REST** (puerto 8000) es independiente: es HTTP normal, para que una persona consulte o modifique datos.
- Ambos puertos corren en el **mismo proceso** y comparten los mismos datos: un cambio de stock por REST se refleja en la próxima compra que llega por el socket.

---

## Firmware: componentes

El firmware se organiza alrededor de **un único bucle de eventos** (`vm_event_loop`). Los componentes no se llaman entre sí para avisar resultados: publican eventos en el bucle y `vending_machine` los procesa todos.

```
main/main.c              → app_main() llama a vm_init()
components/
  vending_machine/       → núcleo / orquestador
  vm_ui/                 → interfaz de usuario (por ahora simulada)
  comm/                  → comunicación con el servidor
    comm.c               → tarea comm_task: conexión, reconexión, ping, pagos
    comm_proto.c         → arma y parsea los mensajes JSON (cJSON)
    comm_transport.c     → TCP plano o TLS según menuconfig (basado en el componente conn de ejemplo)
```

### vending_machine (núcleo / orquestador)
- Inicializa NVS, el stack de red y el **Wi-Fi** (`example_connect()` de `protocol_examples_common`).
- Crea el bucle `vm_event_loop` y registra un handler para **todos** los eventos (`ESP_EVENT_ANY_BASE`).
- Inicializa `comm` y `vm_ui` pasándoles el handle del bucle.
- Mantiene el **estado de la máquina** (ver [Estados](#estados-de-la-máquina)) y decide: pedir cobro, despachar o notificar error.
- `vm_dispense()` es por ahora un stub que solo escribe en el log.

> El Wi-Fi publica sus eventos en el **bucle por defecto** (`esp_event_loop_create_default()`), que es distinto de `vm_event_loop`.

### vm_ui (interfaz de usuario)
- Expone `ui_select_product(id)`, que publica `UI_EVENT_PRODUCT_SELECTED` en el bucle. Es lo que llamarán los botones o la pantalla cuando existan.
- Mientras tanto, si `CONFIG_VM_UI_SIMULATE=y`, una tarea simula selecciones periódicas (ver [Simulación](#simulación-y-datos-iniciales)).

### comm (comunicaciones)
- Corre en su propia tarea de FreeRTOS (`comm_task`), que conecta, mantiene la sesión y reconecta si se cae.
- `comm_start_payment_by_item(id)` **no bloquea**: encola el pedido y el resultado llega después como evento. Devuelve error inmediato si no hay conexión (`ESP_ERR_INVALID_STATE`) o si ya hay un pago en curso (`ESP_ERR_TIMEOUT`).
- `comm_is_connected()` informa si la sesión con el servidor está activa.

| Evento | Datos | Cuándo |
|---|---|---|
| `COMM_EVENT_CONNECTED` | - | Se recibió `hello_ack` del servidor |
| `COMM_EVENT_DISCONNECTED` | - | Se perdió la conexión |
| `COMM_EVENT_PAYMENT_OK` | `comm_payment_result_t` | El servidor aprobó el pago |
| `COMM_EVENT_PAYMENT_FAILED` | `comm_payment_result_t` | Rechazo del servidor (`no_stock`, `unknown_item`) o falla local (`timeout`, `disconnected`) |

`comm_payment_result_t` contiene `item_id`, `req_id` y `reason` (texto del motivo).

---

## Flujo de una compra

```
ESP32                                                            Mac (servidor)
─────                                                            ──────────────
ui_simulation_task (o un botón)
  └ ui_select_product(42)
      └ evento UI_EVENT_PRODUCT_SELECTED ──► vm_event_loop
                                               │
vending_machine (handler)  ◄───────────────────┘
  ├ ¿estado == IDLE? si no, se ignora
  └ comm_start_payment_by_item(42) ──► cola payment_queue  → estado PAYING
                                         │
comm_task (conexión TCP ya abierta)  ◄───┘
  └ envía:  {"type":"pay_req","req_id":5,"item_id":42}\n ─────► tcp_server.py
                                                                 └ store.process_payment()
                                                                    stock 42: 3 → 2
  recibe:   {"type":"pay_resp","req_id":5,"status":"ok"}\n ◄────┘
  └ evento COMM_EVENT_PAYMENT_OK ──► vm_event_loop
                                       │
vending_machine (handler) ◄────────────┘
  └ vm_dispense(42) → estado DISPENSING → "Producto 42 despachado." → estado IDLE
```

Posibles resultados del pago:

| Resultado | Evento | Qué hace la máquina |
|---|---|---|
| `status: ok` | `COMM_EVENT_PAYMENT_OK` | Despacha y vuelve a `IDLE` |
| `status: rejected` (`no_stock`, `unknown_item`) | `COMM_EVENT_PAYMENT_FAILED` | Notifica el error y vuelve a `IDLE` |
| Sin respuesta en 5 s | `COMM_EVENT_PAYMENT_FAILED` (`timeout`) | Notifica el error y vuelve a `IDLE` |
| Se cae la conexión durante el pago | `COMM_EVENT_PAYMENT_FAILED` (`disconnected`) | Notifica el error y pasa a `OFFLINE` |

---

## La conexión con el servidor

La comunicación es **directa por un socket TCP que permanece abierto** mientras ambos lados estén vivos. No es HTTP ni pasa por intermediarios.

```
Arranque ESP32
   │
   ├─ Wi-Fi conectado (obtiene IP del router)
   ├─ connect() a CONFIG_COMM_HOST:8080    ← se abre el socket UNA vez
   ├─ hello → hello_ack                    ← la máquina queda registrada en el servidor
   │
   │   ┌──────────── conexión abierta ────────────┐
   │   │  pay_req / pay_resp  (cuando hay compra)  │
   │   │  ping / pong         (cada 10 s sin tráfico) │
   │   └──────────────────────────────────────────┘
   │
   ├─ ¿se cayó? → close() → COMM_EVENT_DISCONNECTED → estado OFFLINE
   └─ espera 3 s y vuelve a conectar (bucle infinito)
```

### Cuándo se cierra la conexión
- El servidor se apaga o reinicia: el `recv` del ESP32 detecta el cierre.
- El servidor no responde un `ping` en 5 s: cubre Wi-Fi caído o PC dormida, donde no llega ningún aviso.
- El servidor no recibe nada de la máquina en **30 s** y cierra de su lado (por eso el ESP32 envía ping cada 10 s).
- Falla un `send`.

En todos los casos la máquina pasa a `OFFLINE`, no acepta compras y reintenta hasta reconectar.

### Por qué una conexión persistente y no un socket por compra
1. **Se sabe si hay conexión antes de cobrar**: gracias al ping, la máquina detecta que el servidor no está y no acepta compras, en vez de descubrirlo al intentar cobrar.
2. **Compras más rápidas**: no hay que conectar en cada una; con TLS esto pesa más (el handshake cuesta cientos de ms).
3. **Canal bidireccional**: el servidor podría enviar mensajes a la máquina en cualquier momento (hoy solo responde, pero a futuro podría mandar `set_price`, `dispense`, etc.).

### Detalles de implementación
- TCP es un *stream*: un `recv` puede traer medio mensaje o varios juntos. Tanto `comm.c` (`feed_bytes`) como el servidor (`readline`) acumulan bytes hasta el `\n`.
- `comm_task` hace `recv` con timeout de 100 ms; en cada vuelta revisa la cola de pagos, los timeouts y si toca enviar ping.
- `req_id` es un contador que aumenta con cada pago. Si llega una respuesta con un `req_id` que ya no está pendiente (por ejemplo, después de un timeout), se descarta.

---

## Protocolo

Cada mensaje es un objeto **JSON en una sola línea** terminado en `\n`. El campo `type` identifica el mensaje. La definición está en [`server/app/protocol.py`](server/app/protocol.py) y su contraparte en C en [`components/comm/comm_proto.c`](components/comm/comm_proto.c).

| Dirección | Mensaje | Respuesta |
|---|---|---|
| ESP → SRV | `{"type":"hello","device_id":"vm-01"}` | `{"type":"hello_ack"}` |
| ESP → SRV | `{"type":"ping"}` | `{"type":"pong"}` |
| ESP → SRV | `{"type":"pay_req","req_id":7,"item_id":42}` | `{"type":"pay_resp","req_id":7,"status":"ok"}` |
| | | `{"type":"pay_resp","req_id":7,"status":"rejected","reason":"no_stock"}` |
| SRV → ESP | `{"type":"error","reason":"..."}` | - (mensaje inválido, falta `hello`, línea muy larga) |

Reglas:
- El primer mensaje debe ser `hello` (el servidor espera hasta 5 s). Antes de eso cualquier otro mensaje recibe `error: hello_required`.
- Una línea inválida no cierra la conexión: el servidor responde `error` y sigue.
- Largo máximo de línea: 512 bytes.

Motivos de rechazo (`reason`): `no_stock`, `unknown_item`. Motivos locales del ESP32: `timeout`, `disconnected`.

---

## Estados de la máquina

```
            COMM_EVENT_CONNECTED
 OFFLINE ───────────────────────► IDLE ◄─────────────────────────┐
    ▲                              │                              │
    │ COMM_EVENT_DISCONNECTED      │ UI_EVENT_PRODUCT_SELECTED    │
    │                              ▼                              │
    └──────────────────────────  PAYING ── PAYMENT_OK ──► DISPENSING
                                   │                              │
                                   └── PAYMENT_FAILED ──► IDLE    │
                                                                  │
                                       (despacho terminado) ──────┘
```

| Estado | Significado |
|---|---|
| `OFFLINE` | Sin conexión con el servidor: no se aceptan compras |
| `IDLE` | Lista para recibir compras |
| `PAYING` | Esperando la respuesta del servidor |
| `DISPENSING` | Entregando el producto |

- Las selecciones fuera de `IDLE` se ignoran (se registra un warning en el log).
- Cada cambio de estado publica `VM_EVENT_STATE_CHANGED` (datos: `vm_state_t`), para que una futura pantalla pueda mostrarlo.

---

## Simulación y datos iniciales

### Simulación de selecciones (ESP32)
Mientras no haya botones ni pantalla, `vm_ui` simula compras. Se configura en menuconfig → **Vending Machine - Interfaz de usuario (UI)**:

| Opción | Por defecto | Efecto |
|---|---|---|
| `CONFIG_VM_UI_SIMULATE` | `y` | Crea la tarea `ui_simulation_task` |
| `CONFIG_VM_UI_SIMULATE_INTERVAL_MS` | `10000` | Una selección cada 10 s |

La tarea recorre en ciclo los productos **42 → 1 → 3 → 999**. Con `CONFIG_VM_UI_SIMULATE=n` el código de simulación no se compila.

### Datos iniciales del servidor ("seed")
No hay semilla aleatoria: todo es determinístico. El servidor arranca con estos productos (`Store.seed()` en [`server/app/store.py`](server/app/store.py)):

| ID | Producto | Precio | Stock inicial |
|---|---|---|---|
| 1 | Agua | 1.00 | 10 |
| 2 | Gaseosa | 1.50 | 5 |
| 3 | Papas | 2.00 | **0** |
| 42 | Chocolate | 1.25 | 3 |

La lista de la simulación está elegida para cubrir todos los casos:
- **42** → aprobado 3 veces, después `no_stock`.
- **1** → aprobado.
- **3** → siempre `no_stock`.
- **999** → `unknown_item`, no existe.

Los datos viven **en memoria**: al reiniciar el servidor vuelven a estos valores. Para cambiarlos sin reiniciar: `PUT /products/{id}` (ver [Servidor](#servidor)).

### Qué se ve en el monitor del ESP32
```
VM_UI: Producto 42 seleccionado por el usuario. Publicando evento...
VENDING_MACHINE: Producto seleccionado: 42. Iniciando proceso de pago...
COMM: Enviando pedido de pago req 1 item 42
COMM: Pago APROBADO (req 1, item 42)
VENDING_MACHINE: Producto 42 despachado.
```
Y en el servidor: `[vm-01] pago req=1 item=42 -> ok`.

---

## Servidor

```
server/
  app/main.py          FastAPI + lifespan que arranca el servidor TCP
  app/tcp_server.py    servidor TCP (asyncio): hello, ping y pagos por conexión
  app/protocol.py      definición y validación de mensajes (pydantic)
  app/store.py         productos, máquinas conectadas y transacciones (en memoria)
  tools/fake_esp32.py  cliente que simula una máquina, para probar sin hardware
  tests/               pytest: protocolo y flujo de pago contra el servidor TCP
  requirements.txt
```

Lógica de pago (`Store.process_payment`): aprueba si el producto existe y tiene stock, y descuenta una unidad; si no, rechaza con el motivo. Cada intento queda registrado como transacción.

### API REST (puerto 8000)
Documentación interactiva en http://localhost:8000/docs

| Método | Ruta | Descripción |
|---|---|---|
| `GET` | `/products` | Lista productos con precio y stock |
| `PUT` | `/products/{id}` | Modifica `name`, `price` o `stock`. Ej: `{"stock": 5}` |
| `GET` | `/devices` | Máquinas conectadas ahora por el socket |
| `GET` | `/transactions` | Historial de pagos (aprobados y rechazados) |

### Variables de entorno
| Variable | Por defecto | Descripción |
|---|---|---|
| `TCP_HOST` | `0.0.0.0` | Interfaz donde escucha el socket de las máquinas |
| `TCP_PORT` | `8080` | Puerto del socket de las máquinas |

---

## Puesta en marcha

### 1. Servidor
```bash
cd server
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000
```
- Usar `--host 0.0.0.0` (no `127.0.0.1`) para que sea accesible desde la red.
- La primera vez, macOS pregunta si Python puede aceptar conexiones entrantes: responder **Permitir**.

Probar sin hardware:
```bash
.venv/bin/python tools/fake_esp32.py --items 42 3 999   # simula una máquina
.venv/bin/pytest                                         # tests
```

### 2. Averiguar la IP de la PC
El ESP32 se conecta a la **IP de la PC donde corre el servidor** en la red local:
```bash
ipconfig getifaddr en0      # macOS
```
La PC y el ESP32 deben estar en la **misma red**. El ESP32-S3 solo usa Wi-Fi de **2.4 GHz** y no funciona con redes con portal de login (universidades, hoteles); en ese caso un hotspot del teléfono es la opción más simple.

### 3. Activar ESP-IDF en la terminal
`idf.py` solo existe después de cargar el entorno, y hay que hacerlo en **cada terminal nueva**:
```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
```
Atajo opcional:
```bash
echo "alias get_idf='source ~/.espressif/tools/activate_idf_v6.1.sh'" >> ~/.zshrc
```

### 4. Configurar el firmware
```bash
idf.py set-target esp32s3     # solo la primera vez
idf.py menuconfig
```
| Menú | Opción | Valor |
|---|---|---|
| Example Connection Configuration | WiFi SSID / WiFi Password | Datos de la red |
| Vending Machine - Comunicaciones (COMM) | Host o IP del servidor | IP de la PC (paso 2) |
| Vending Machine - Comunicaciones (COMM) | Puerto TCP del servidor | `8080` |
| Vending Machine - Comunicaciones (COMM) | Identificador de la maquina | `vm-01` |
| Vending Machine - Interfaz de usuario (UI) | Simular selecciones | activado para pruebas |

En menuconfig: `S` guarda, `Q` sale. Alternativas:
- VS Code: `Cmd+Shift+P` → **ESP-IDF: SDK Configuration Editor (menuconfig)**.
- Editar `sdkconfig` directamente, por ejemplo `CONFIG_COMM_HOST="192.168.100.176"`.

`sdkconfig` está en `.gitignore`, así que las credenciales del Wi-Fi no se suben al repo. Los valores base del proyecto están en `sdkconfig.defaults`.

Otras opciones de **Vending Machine - Comunicaciones (COMM)**:

| Opción | Por defecto | Descripción |
|---|---|---|
| `COMM_USE_TLS` | `n` | Conexión cifrada (ver [Próximos pasos](#próximos-pasos)) |
| `COMM_RESPONSE_TIMEOUT_MS` | `5000` | Espera máxima de `hello_ack`, `pong` y `pay_resp` |
| `COMM_PING_INTERVAL_MS` | `10000` | Ping si no hubo tráfico (debe ser menor a 30 s) |
| `COMM_RECONNECT_DELAY_MS` | `3000` | Espera entre reintentos de conexión |

### 5. Compilar, flashear y monitorear
```bash
ls /dev/cu.*                                   # buscar el puerto de la placa
idf.py -p /dev/cu.usbmodemXXXX build flash monitor
```
Al arrancar deberían verse `got ip: ...` y luego `Sesión establecida con el servidor.`; en el servidor, `maquina 'vm-01' registrada`.

> La IP queda **compilada** en el firmware: si se cambia, hay que volver a compilar y flashear.

---

## Problemas comunes

| Síntoma | Causa probable | Solución |
|---|---|---|
| `command not found: idf.py` | ESP-IDF no activado en esa terminal | `source ~/.espressif/tools/activate_idf_v6.1.sh` |
| menuconfig no responde a las teclas | Se ejecutó en el panel *Output* de VS Code | Usar una terminal interactiva o el SDK Configuration Editor |
| `Found 0 serial ports` | La PC no ve la placa | Ver abajo |
| `Fallo al conectar TCP a ...` | IP incorrecta, servidor apagado o firewall | Revisar IP con `ipconfig getifaddr en0`, servidor en `0.0.0.0`, permitir Python en el firewall |
| El ESP32 no obtiene IP | Red de 5 GHz, contraseña incorrecta o red con portal | Usar red 2.4 GHz / hotspot, revisar SSID y password |
| Cambié de red y dejó de conectar | La IP de la PC cambió | Actualizar `CONFIG_COMM_HOST`, recompilar y flashear |

**La placa no aparece en `/dev/cu.*`:**
1. El cable puede ser **solo de carga**: probar con uno que transfiera datos.
2. Las DevKit ESP32-S3 suelen tener **dos conectores**: *UART* (aparece como `cu.usbserial-*` / `cu.wchusbserial*`) y *USB* nativo (`cu.usbmodem*`). Probar el otro.
3. Conectar directo a la PC, sin hub ni adaptador.
4. Forzar modo descarga: mantener **BOOT**, presionar y soltar **RST**, soltar **BOOT**.

**Probar que la red y el firewall dejan pasar conexiones** (sin la placa): con el servidor corriendo, abrir `http://<IP-de-la-PC>:8000/docs` desde el teléfono conectado a la misma red. Si abre, el ESP32 también va a poder llegar.

**Evitar cambiar la IP en cada red:** reservar una IP fija para la PC en el router (reserva DHCP) o, a futuro, usar un hostname con mDNS (`mipc.local`). `comm` ya acepta hostnames porque usa `getaddrinfo`.

---

## Próximos pasos

### TLS
El transporte ([`components/comm/comm_transport.c`](components/comm/comm_transport.c)) ya tiene la rama TLS con `esp_tls` y compila:
1. Generar el certificado (solo pruebas, no producción):
   ```bash
   openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
   ```
2. Copiar `server.crt` a `components/comm/certs/server.crt` y activar `COMM_USE_TLS` en menuconfig.
3. En el servidor, pasar un `ssl.SSLContext` a `asyncio.start_server` (pendiente).

### Pendientes
- **Despacho real**: `vm_dispense()` solo escribe en el log. Al conectar el motor/solenoide conviene sacarlo del handler de eventos para no bloquear el bucle.
- **UI real**: botones o pantalla que llamen a `ui_select_product(id)`; desactivar `CONFIG_VM_UI_SIMULATE`.
- **Persistencia**: el servidor guarda todo en memoria; pasar a una base de datos (por ejemplo SQLite).
- **Autenticación** de máquinas y de la API REST.
- **Pago real con Mercado Pago** (u otra pasarela). Hoy el "pago" solo verifica stock. La integración iría en el **servidor**, nunca en el ESP32, para que las credenciales (access token) no queden en el firmware:
  1. Llega `pay_req` → el servidor crea una orden de pago en Mercado Pago (por ejemplo un **QR**) y le envía a la máquina los datos para mostrarlo.
  2. El usuario paga desde su teléfono → Mercado Pago avisa al servidor por **webhook** (requiere una URL pública, por ejemplo con un túnel durante el desarrollo).
  3. El servidor confirma el pago, descuenta stock y recién ahí envía `pay_resp` con `status: ok`.

  Implica ampliar el protocolo (un mensaje intermedio tipo `pay_pending` con el QR), un timeout de pago mucho mayor que los 5 s actuales, cancelación si el usuario no paga y una pantalla en la máquina para mostrar el QR.
- **Mensajes del servidor a la máquina** (cambio de precios, catálogo) aprovechando el canal bidireccional.
