"""Estado en memoria: productos, maquinas conectadas y transacciones.

Todo se accede desde el mismo event loop de asyncio (servidor TCP y endpoints
async de FastAPI), por lo que no hace falta sincronizacion adicional.
"""

from dataclasses import dataclass, field
from datetime import datetime, timezone


def _now() -> datetime:
    return datetime.now(timezone.utc)


@dataclass
class Product:
    id: int
    name: str
    price: float
    stock: int


@dataclass
class Device:
    device_id: str
    address: str
    connected_at: datetime = field(default_factory=_now)
    last_seen: datetime = field(default_factory=_now)


@dataclass
class Transaction:
    device_id: str
    req_id: int
    item_id: int
    status: str
    reason: str | None
    price: float | None
    timestamp: datetime = field(default_factory=_now)


class Store:
    def __init__(self) -> None:
        self.products: dict[int, Product] = {}
        self.devices: dict[str, Device] = {}
        self.transactions: list[Transaction] = []

    def seed(self) -> None:
        for p in (
            Product(1, "Agua", 1.00, 10),
            Product(2, "Gaseosa", 1.50, 5),
            Product(3, "Papas", 2.00, 0),
            Product(42, "Chocolate", 1.25, 3),
        ):
            self.products[p.id] = p

    def process_payment(self, device_id: str, req_id: int, item_id: int) -> Transaction:
        """Aprueba el pago si el producto existe y tiene stock; descuenta una unidad."""
        product = self.products.get(item_id)
        if product is None:
            tx = Transaction(device_id, req_id, item_id, "rejected", "unknown_item", None)
        elif product.stock <= 0:
            tx = Transaction(device_id, req_id, item_id, "rejected", "no_stock", product.price)
        else:
            product.stock -= 1
            tx = Transaction(device_id, req_id, item_id, "ok", None, product.price)
        self.transactions.append(tx)
        return tx


store = Store()
store.seed()
