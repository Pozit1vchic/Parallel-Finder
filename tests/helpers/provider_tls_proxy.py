"""One-request loopback HTTPS proxy for ModelStore's real TLS test.

The certificate and private key are public test fixtures, never production keys.
"""
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
                body = b"incorrect" if mode == "wrong-length" else b"correct"
                secure.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: "
                               + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body)
        except (ssl.SSLError, ConnectionError):
            if mode != "untrusted":
                raise
