import os
from typing import Any

import requests
from fastapi import HTTPException


CHIRPSTACK_BASE_URL = os.getenv('CHIRPSTACK_BASE_URL', 'http://10.0.0.1:8090').rstrip('/')
CHIRPSTACK_API_TOKEN = os.getenv(
    'CHIRPSTACK_API_TOKEN',
    'eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJjaGlycHN0YWNrIiwiaXNzIjoiY2hpcnBzdGFjayIsInN1YiI6ImE0MTlkYjdiLTJmZjMtNGFhNC05ZDI5LTFmODQ0YmM4ZDgwZCIsInR5cCI6ImtleSJ9.8BpDwgC9jgCmgvAQ7DBjUsjGWW2m9xv3zOrWBTE19xo',
)


def enqueue_device_queue_item(
    node_id: str,
    queue_item: dict[str, Any],
    *,
    flush_queue: bool,
) -> dict[str, Any]:
    chirpstack_body = {
        'flushQueue': flush_queue,
        'queueItem': {
            key: value for key, value in queue_item.items() if value is not None
        },
    }

    headers = {
        'Content-Type': 'application/json',
    }
    if CHIRPSTACK_API_TOKEN:
        headers['Authorization'] = f'Bearer {CHIRPSTACK_API_TOKEN}'

    chirpstack_url = f'{CHIRPSTACK_BASE_URL}/api/devices/{node_id}/queue'
    try:
        response = requests.post(
            chirpstack_url,
            json=chirpstack_body,
            headers=headers,
            timeout=15,
        )
    except requests.RequestException as exc:
        raise HTTPException(
            status_code=502,
            detail=f'Could not reach ChirpStack: {exc}',
        ) from exc

    if response.status_code >= 400:
        try:
            error_detail = response.json()
        except ValueError:
            error_detail = response.text

        raise HTTPException(
            status_code=502,
            detail={
                'message': 'ChirpStack queue request failed',
                'chirpstackStatusCode': response.status_code,
                'chirpstackUrl': chirpstack_url,
                'devEui': node_id,
                'chirpstackError': error_detail,
            },
        )

    try:
        chirpstack_response = response.json()
    except ValueError:
        chirpstack_response = {}

    return {
        'body': chirpstack_body,
        'chirpstackResponse': chirpstack_response,
    }
