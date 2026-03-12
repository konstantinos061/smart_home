from fastapi import FastAPI

from app.api.routes import router
from app.services.bootstrap import init_db

app = FastAPI(title='Smart Home IoT Platform Starter')
app.include_router(router)


@app.on_event('startup')
def startup_event() -> None:
    init_db()
