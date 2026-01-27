#!/usr/bin/env python3
"""Cross-Origin Isolation対応ローカル開発サーバー。

COOP/COEPヘッダを付与することで performance.now() の精度が
100us → 5us に向上し、C++側の計測値をより正確に表示できる。
"""
from http.server import HTTPServer, SimpleHTTPRequestHandler
import sys

class COIHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
print(f'Serving on http://localhost:{port} (Cross-Origin Isolated)')
HTTPServer(('', port), COIHandler).serve_forever()
