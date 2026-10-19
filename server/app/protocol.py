"""Protocolo maquina <-> servidor.

Cada mensaje es un objeto JSON en una sola linea terminada en '\\n'.
El campo "type" identifica el mensaje.

ESP -> SRV
    {"type":"hello","device_id":"vm-01"}
    {"type":"ping"}
    {"type":"pay_req","req_id":7,"item_id":42}

SRV -> ESP
    {"type":"hello_ack"}
    {"type":"pong"}
    {"type":"pay_resp","req_id":7,"status":"ok"}
    {"type":"pay_resp","req_id":7,"status":"rejected","reason":"no_stock"}
    {"type":"error","reason":"..."}
"""

import json
from typing import Annotated, Literal, Union

from pydantic import BaseModel, Field, TypeAdapter, ValidationError

MAX_LINE_BYTES = 512


class Hello(BaseModel):
    type: Literal["hello"]
    device_id: str = Field(min_length=1, max_length=32)


class Ping(BaseModel):
    type: Literal["ping"]


class PayReq(BaseModel):
    type: Literal["pay_req"]
    req_id: int = Field(ge=0)
    item_id: int = Field(ge=0)


IncomingMessage = Annotated[Union[Hello, Ping, PayReq], Field(discriminator="type")]
_incoming_adapter = TypeAdapter(IncomingMessage)


class ProtocolError(Exception):
    """Mensaje malformado o desconocido."""


def parse_line(line: bytes) -> Union[Hello, Ping, PayReq]:
    try:
        data = json.loads(line)
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise ProtocolError("invalid_json") from e
    try:
        return _incoming_adapter.validate_python(data)
    except ValidationError as e:
        raise ProtocolError("invalid_message") from e


def encode(msg: dict) -> bytes:
    return (json.dumps(msg, separators=(",", ":")) + "\n").encode()


def hello_ack() -> dict:
    return {"type": "hello_ack"}


def pong() -> dict:
    return {"type": "pong"}


def pay_resp(req_id: int, status: str, reason: str | None = None) -> dict:
    msg = {"type": "pay_resp", "req_id": req_id, "status": status}
    if reason is not None:
        msg["reason"] = reason
    return msg


def error(reason: str) -> dict:
    return {"type": "error", "reason": reason}
