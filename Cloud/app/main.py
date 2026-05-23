from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .api.routes import router
from .services.bootstrap import init_db

app = FastAPI(title='Smart Home IoT Platform Starter')

app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        'http://localhost',
        'http://localhost:5173',
        'http://127.0.0.1',
        'http://127.0.0.1:5173',
    ],
    allow_credentials=True,
    allow_methods=['*'],
    allow_headers=['*'],
)

app.include_router(router)


@app.on_event('startup')
def startup_event() -> None:
    init_db()
