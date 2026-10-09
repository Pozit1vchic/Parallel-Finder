# Discord Rich Presence

Parallel Finder uses Discord's documented local RPC-over-IPC transport through
Qt Network (`QLocalSocket`), without an SDK DLL, HTTP requests, account tokens,
or OAuth login. This deliberately replaces the proposed Social SDK integration:
the application's Developer Portal does not expose a Social SDK download.

Application ID: `1558099632775766167`. Uploaded artwork key: `parallel_finder`.
The separately uploaded invite cover is configured in the Developer Portal;
PF does not advertise multiplayer invitations or join secrets.

Rich Presence starts automatically on ordinary desktop launches. There is no
Parallel Finder switch or saved enable/disable preference; legacy preferences
are ignored. Visibility is controlled by Discord's activity-sharing settings.
Discord's desktop client must be running. Rich Presence does not grant Discord verification.

Only a fixed localized description, whole-run progress, pair count, stage start
time, artwork key and public release-page link are sent. File paths, video names,
thumbnails, people identities and recognition data are never included.

The service starts two seconds after desktop startup. Diagnostic/test/analysis
workers never start it. Connections, pipe reads and writes are asynchronous;
there are no blocking waits. It scans the ten Discord pipe slots with a pause
between sweeps, coalesces activity changes to at most one request per 15 seconds,
limits incoming frame size to 64 KiB, handles fragmented frames and ping/pong,
waits for READY and request acknowledgements, and reconnects on disconnect,
timeout or protocol failure. Rejected activity requests back off for 30 seconds.
Normal shutdown clears the activity and closes the connection.

Tests use a real local pipe server with synthetic Discord frames to exercise
handshake, fragmented READY, localized matcher/export activity, rate limiting,
ping/pong, clearing, invalid size, reconnection, missing
Discord and rejected requests. Real Discord rendering remains a separate manual
check requiring a running logged-in desktop client.

Reference: https://docs.discord.com/developers/topics/rpc
