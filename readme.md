

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