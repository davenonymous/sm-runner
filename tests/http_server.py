# Copyright 2026 davenonymous
#
# The local HTTP server of the SteamWorks passthrough suites (run.sh): the
# files of a folder (GET and HEAD), and two redirects to its hello.json, one
# on the same host (/moved) and one to the host "localhost" (/elsewhere),
# which the suites do not allow. A POST to any path is answered 201 with
# {"created": true}; a DELETE with the Content-Type and Content-Length it
# came with, as text. It listens on 127.0.0.1 on a free port and writes the
# port into PORTFILE once it listens.
#
#   python3 http_server.py FOLDER PORTFILE
import functools
import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    redirects = {
        "/moved": lambda port: "/hello.json",
        "/elsewhere": lambda port: f"http://localhost:{port}/hello.json",
    }

    def do_GET(self):
        target = self.redirects.get(self.path)
        if target is None:
            super().do_GET()
            return
        self.send_response(302)
        self.send_header("Location", target(self.server.server_port))
        self.send_header("Content-Length", "0")
        self.end_headers()

    def answer(self, status, content_type, body):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        self.answer(201, "application/json", '{"created": true}')

    def do_DELETE(self):
        received = [
            f"{name.lower()}: {self.headers.get(name, 'none')}" for name in ("Content-Type", "Content-Length")
        ]
        self.answer(200, "text/plain", "; ".join(received))

    def log_message(self, format, *args):
        pass


def main():
    folder, port_file = sys.argv[1], sys.argv[2]
    handler = functools.partial(Handler, directory=folder)
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
        with open(port_file + ".partial", "w") as out:
            out.write(str(server.server_port))
        os.replace(port_file + ".partial", port_file)
        server.serve_forever()


if __name__ == "__main__":
    main()
