# Provider/model response size validation

The asset manifest supplies the exact expected size. Previously a server could
return HTTP 200 with an incompatible Content-Length after ignoring a Range
request; the downloader would first discard the existing resumable prefix and
only check the received size after the transfer. A streamed response without a
length could also write beyond the manifest size until the much larger global
download cap was reached.

Full-response metadata is now checked before a saved prefix is reset. Incoming
body chunks and the final buffered bytes are limited by both the manifest size
(when present) and the global cap, using subtraction-safe arithmetic. A server
that legitimately ignores Range can still send a complete response of the
declared length; unknown-length streams remain supported but are bounded.
Checksums and the final exact-size check remain required before publication.

`build/download-size-tests.log` records 21 passing ModelStore/ProviderStore
tests, including explicit equal, short, long, unknown-length, bounded-write and
overflow controls. Four optional live/release-asset tests were skipped.

`build/download-real-tls-test.log` records the integration test with a local
HTTPS proxy and a test-only certificate. The production downloader connects to
the allowed `github.com` host through the loopback proxy with normal TLS checks.
An untrusted certificate is rejected and preserves the three-byte partial. The
test explicitly adds its certificate to the test process's trusted roots and
repeats the request: a server that ignores `Range` and reports an incorrect
Content-Length leaves that partial intact; a server reporting the correct
length downloads and verifies the complete object. Both trusted requests are
checked to contain the expected `Range: bytes=3-` header. The certificate and
private key are publicly committed test fixtures, never product credentials.
The integration test requires Python and skips when it is unavailable. It does
not yet prove process-restart recovery, slow transfers or mirror fallback.
