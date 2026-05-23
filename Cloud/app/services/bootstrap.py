from sqlalchemy import text

from ..db import engine
from ..models import Base


def init_db() -> None:
    with engine.begin() as conn:
        conn.execute(text('CREATE EXTENSION IF NOT EXISTS timescaledb'))
        Base.metadata.create_all(bind=conn)
        conn.execute(text("SELECT create_hypertable('telemetry', 'time', if_not_exists => TRUE);"))
        conn.execute(text("SELECT create_hypertable('node_status_events', 'time', if_not_exists => TRUE);"))
