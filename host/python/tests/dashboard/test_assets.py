# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

from pathlib import Path

from picopwm.dashboard import app


def test_source_dashboard_serves_canonical_assets():
    root = Path(__file__).resolve().parents[4]
    application = app.create_app()
    try:
        with application.test_client() as http:
            for route, path in {
                "/": "host/web/src/index.html",
                "/favicon.svg": "host/web/src/favicon.svg",
                "/css/dashboard.css": "host/web/src/css/dashboard.css",
                "/css/direct.css": "host/direct/src/css/direct.css",
                "/js/app.mjs": "host/web/src/js/app.mjs",
                "/js/view.mjs": "host/direct/src/js/view.mjs",
                "/js/device.mjs": "host/direct/src/js/device.mjs",
            }.items():
                with http.get(route) as response:
                    assert response.status_code == 200
                    assert response.data == (root / path).read_bytes()
    finally:
        application.extensions["picopwm_boards"].close()

