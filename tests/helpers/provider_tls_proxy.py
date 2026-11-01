"""Loopback HTTPS proxy for ModelStore integration tests; fixture keys are public."""
import socket
import ssl
import sys

cert, key, mode = sys.argv[1:]
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert, key)
with socket.socket() as listener:
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(20)
    print(listener.getsockname()[1], flush=True)
    for index in range(2 if mode == "short-range" else 1):
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(20)
            request = b""
            while b"\r\n\r\n" not in request:
                chunk = connection.recv(4096)
                if not chunk:
                    sys.exit(1)
                request += chunk
            if not request.startswith(b"CONNECT github.com:"):
                sys.exit(2)
            connection.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            try:
                with context.wrap_socket(connection, server_side=True) as secure:
                    request = b""
                    while b"\r\n\r\n" not in request:
                        chunk = secure.recv(4096)
                        if not chunk:
                            sys.exit(3)
                        request += chunk
                    print(request.decode("latin-1").replace("\r", "\\r").replace("\n", "\\n"), flush=True)
                    if mode == "short-range":
                        body = b"re" if index == 0 else b"ct"
                        content_range = b"bytes 3-4/7" if index == 0 else b"bytes 5-6/7"
                        response = b"HTTP/1.1 206 Partial Content\r\nContent-Range: " + content_range + b"\r\n"
                    else:
                        body = b"incorrect" if mode == "wrong-length" else b"correct"
                        response = b"HTTP/1.1 200 OK\r\n"
                    secure.sendall(response + b"Content-Length: " + str(len(body)).encode()
                                   + b"\r\nConnection: close\r\n\r\n" + body)
            except (ssl.SSLError, ConnectionError):
                if mode != "untrusted":
                    raise
