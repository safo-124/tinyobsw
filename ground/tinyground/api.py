from fastapi import FastAPI

app = FastAPI(title="tinyobsw ground segment")


@app.get("/api/health")
def health():
    return {"status": "ok"}
