from fastapi.testclient import TestClient
from tinyground.api import app


def test_health():
    assert TestClient(app).get("/api/health").json() == {"status": "ok"}
