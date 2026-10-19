"""Servidor TCP (sin TLS) para las maquinas expendedoras."""

import asyncio
import logging

from . import protocol
from .store import Device, Store, _now

log = logging.getLogger("tcp")

# Si la maquina no envia nada en este tiempo (ni ping) se cierra la conexion
IDLE_TIMEOUT_S = 30.0
# Tiempo maximo para recibir el "hello" despues de conectar
HELLO_TIMEOUT_S = 5.0


class VendingTCPServer:
    def __init__(self, store: Store, host: str, port: int) -> None:
        self.store = store
        self.host = host
        self.port = port
        self._server: asyncio.Server | None = None

    async def start(self) -> None:
        self._server = await asyncio.start_server(
            self._handle_client, self.host, self.port, limit=protocol.MAX_LINE_BYTES
        )
        addrs = ", ".join(str(s.getsockname()) for s in self._server.sockets)
        log.info("Servidor TCP escuchando en %s", addrs)

    async def stop(self) -> None:
        if self._server:
            self._server.close()
            await self._server.wait_closed()

    @property
    def bound_port(self) -> int:
        assert self._server is not None
        return self._server.sockets[0].getsockname()[1]

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        peer = writer.get_extra_info("peername")
        address = f"{peer[0]}:{peer[1]}" if peer else "?"
        log.info("[%s] conexion TCP abierta", address)
        device: Device | None = None

        async def send(msg: dict) -> None:
            writer.write(protocol.encode(msg))
            await writer.drain()

        try:
            while True:
                timeout = HELLO_TIMEOUT_S if device is None else IDLE_TIMEOUT_S
                try:
                    line = await asyncio.wait_for(reader.readline(), timeout)
                except asyncio.TimeoutError:
                    log.warning("[%s] timeout sin mensajes, cerrando", address)
                    break
                except (asyncio.LimitOverrunError, ValueError):
                    log.warning("[%s] linea demasiado larga, cerrando", address)
                    await send(protocol.error("line_too_long"))
                    break

                if not line:
                    break  # el cliente cerro la conexion
                if not line.strip():
                    continue

                log.debug("[%s] <- %s", address, line.strip())
                try:
                    msg = protocol.parse_line(line)
                except protocol.ProtocolError as e:
                    log.warning("[%s] mensaje invalido (%s): %r", address, e, line)
                    await send(protocol.error(str(e)))
                    continue

                if device is None:
                    if not isinstance(msg, protocol.Hello):
                        await send(protocol.error("hello_required"))
                        continue
                    device = Device(device_id=msg.device_id, address=address)
                    self.store.devices[device.device_id] = device
                    log.info("[%s] maquina '%s' registrada", address, device.device_id)
                    await send(protocol.hello_ack())
                    continue

                device.last_seen = _now()

                if isinstance(msg, protocol.Ping):
                    await send(protocol.pong())
                elif isinstance(msg, protocol.PayReq):
                    tx = self.store.process_payment(device.device_id, msg.req_id, msg.item_id)
                    log.info(
                        "[%s] pago req=%d item=%d -> %s %s",
                        device.device_id, msg.req_id, msg.item_id, tx.status, tx.reason or "",
                    )
                    await send(protocol.pay_resp(msg.req_id, tx.status, tx.reason))
                elif isinstance(msg, protocol.Hello):
                    await send(protocol.hello_ack())
        except (ConnectionResetError, BrokenPipeError):
            pass
        finally:
            if device is not None and self.store.devices.get(device.device_id) is device:
                del self.store.devices[device.device_id]
            log.info("[%s] conexion cerrada", address)
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionResetError, BrokenPipeError):
                pass
