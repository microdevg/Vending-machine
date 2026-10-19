import asyncio
import json

import pytest
import pytest_asyncio

from app import protocol
from app.store import Store
from app.tcp_server import VendingTCPServer


# ---------- protocolo ----------

def test_parse_valid_messages():
    assert isinstance(protocol.parse_line(b'{"type":"hello","device_id":"vm-01"}\n'), protocol.Hello)
    assert isinstance(protocol.parse_line(b'{"type":"ping"}'), protocol.Ping)
    msg = protocol.parse_line(b'{"type":"pay_req","req_id":7,"item_id":42}')
    assert isinstance(msg, protocol.PayReq) and msg.req_id == 7 and msg.item_id == 42


@pytest.mark.parametrize("line", [
    b"no es json",
    b'{"type":"desconocido"}',
    b'{"type":"pay_req","req_id":1}',
    b'{"type":"pay_req","req_id":-1,"item_id":1}',
])
def test_parse_invalid_messages(line):
    with pytest.raises(protocol.ProtocolError):
        protocol.parse_line(line)


def test_encode_is_single_line():
    assert protocol.encode(protocol.pay_resp(1, "ok")) == b'{"type":"pay_resp","req_id":1,"status":"ok"}\n'


# ---------- servidor TCP ----------

@pytest_asyncio.fixture
async def server():
    store = Store()
    store.seed()
    srv = VendingTCPServer(store, "127.0.0.1", 0)
    await srv.start()
    yield srv
    await srv.stop()


class Client:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer

    async def request(self, msg: dict) -> dict:
        self.writer.write((json.dumps(msg) + "\n").encode())
        await self.writer.drain()
        return json.loads(await asyncio.wait_for(self.reader.readline(), 2))

    async def close(self):
        self.writer.close()
        await self.writer.wait_closed()


async def connect(server) -> Client:
    r, w = await asyncio.open_connection("127.0.0.1", server.bound_port)
    return Client(r, w)


@pytest.mark.asyncio
async def test_hello_required_first(server):
    c = await connect(server)
    assert await c.request({"type": "ping"}) == {"type": "error", "reason": "hello_required"}
    await c.close()


@pytest.mark.asyncio
async def test_payment_flow(server):
    c = await connect(server)
    assert await c.request({"type": "hello", "device_id": "vm-test"}) == {"type": "hello_ack"}
    assert "vm-test" in server.store.devices
    assert await c.request({"type": "ping"}) == {"type": "pong"}

    # producto 42 tiene stock 3
    for req_id in (1, 2, 3):
        resp = await c.request({"type": "pay_req", "req_id": req_id, "item_id": 42})
        assert resp == {"type": "pay_resp", "req_id": req_id, "status": "ok"}
    resp = await c.request({"type": "pay_req", "req_id": 4, "item_id": 42})
    assert resp == {"type": "pay_resp", "req_id": 4, "status": "rejected", "reason": "no_stock"}

    resp = await c.request({"type": "pay_req", "req_id": 5, "item_id": 999})
    assert resp["status"] == "rejected" and resp["reason"] == "unknown_item"

    assert server.store.products[42].stock == 0
    assert len(server.store.transactions) == 5

    await c.close()
    await asyncio.sleep(0.05)
    assert "vm-test" not in server.store.devices


@pytest.mark.asyncio
async def test_invalid_json_keeps_connection(server):
    c = await connect(server)
    await c.request({"type": "hello", "device_id": "vm-test"})
    c.writer.write(b"basura\n")
    await c.writer.drain()
    assert json.loads(await c.reader.readline()) == {"type": "error", "reason": "invalid_json"}
    assert await c.request({"type": "ping"}) == {"type": "pong"}
    await c.close()


@pytest.mark.asyncio
async def test_fragmented_and_batched_messages(server):
    """TCP es un stream: el servidor debe tolerar mensajes partidos o juntos."""
    c = await connect(server)
    c.writer.write(b'{"type":"hel')
    await c.writer.drain()
    await asyncio.sleep(0.05)
    c.writer.write(b'lo","device_id":"vm"}\n{"type":"ping"}\n')
    await c.writer.drain()
    assert json.loads(await c.reader.readline()) == {"type": "hello_ack"}
    assert json.loads(await c.reader.readline()) == {"type": "pong"}
    await c.close()
