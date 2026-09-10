"""
Spike, round 3: actually mute the local Discord client, then put it back.

Round 2 proved the owner can be granted rpc + rpc.voice.write and get an
OAuth code. This closes the loop: code -> access token -> AUTHENTICATE
over the RPC socket -> read the current voice settings -> mute -> restore
whatever was there before.

Everything has to happen in one run because the OAuth code is single-use
and expires within about a minute.

SECRETS: the client secret is read from a file whose path is passed in
(default G:\\Inkwyrd-Audio\\.secrets\\discord_client_secret.txt, which is
gitignored). It is never printed, never logged, and never sent anywhere
except Discord's own token endpoint. Same for the access token - only its
length is reported.

RESTORE: the user's existing mute/deaf state is read BEFORE anything is
changed and put back at the end, including if a step fails partway. An
app that leaves someone muted after a test is worse than one that never
muted them at all.
"""

import json
import struct
import sys
import time
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

PIPE = r'\\.\pipe\discord-ipc-0'
CLIENT_ID = "1543399962723745792"       # public application id, not a secret
REDIRECT_URI = "https://inkwyrd.com/rpc"  # registered; see below
SCOPES = ["rpc", "rpc.voice.write"]
TOKEN_URL = "https://discord.com/api/v10/oauth2/token"

DEFAULT_SECRET_PATH = r"G:\Inkwyrd-Audio\.secrets\discord_client_secret.txt"


def log(message):
    print(message, flush=True)


def send(pipe, op, payload):
    data = json.dumps(payload).encode("utf-8")
    pipe.write(struct.pack("<II", op, len(data)) + data)
    pipe.flush()


def recv(pipe):
    header = pipe.read(8)
    if not header or len(header) < 8:
        return None, {}
    op, length = struct.unpack("<II", header)
    body = pipe.read(length) if length else b""
    try:
        return op, json.loads(body.decode("utf-8"))
    except Exception:
        return op, {"raw": body[:400].decode("utf-8", "replace")}


def command(pipe, cmd, args=None):
    send(pipe, 1, {"cmd": cmd, "args": args or {}, "nonce": str(uuid.uuid4())})
    op, payload = recv(pipe)
    if payload.get("evt") == "ERROR":
        data = payload.get("data") or {}
        raise RuntimeError(f"{cmd} failed: code={data.get('code')} "
                            f"message={data.get('message')!r}")
    return payload.get("data") or {}


def exchange_code_for_token(code, client_secret):
    """Standard OAuth2 authorization_code exchange.

    redirect_uri IS required here, unlike the RPC AUTHORIZE call - this
    is the ordinary HTTP token endpoint, not the IPC transport, and OAuth
    requires it to match the one the authorization was issued against.
    AUTHORIZE defaulted to the application's first registered URI, so
    that's the value to send.
    """
    body = urllib.parse.urlencode({
        "client_id": CLIENT_ID,
        "client_secret": client_secret,
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
    }).encode("utf-8")

    # The User-Agent is NOT optional. Without one, urllib sends
    # "Python-urllib/3.x" and Cloudflare - not Discord - rejects the
    # request with HTTP 403 "error code: 1010", which looks like a
    # credentials or scope problem and isn't one. Discord's API
    # documentation requires a descriptive agent; this is that.
    request = urllib.request.Request(
        TOKEN_URL, data=body,
        headers={
            "Content-Type": "application/x-www-form-urlencoded",
            "Accept": "application/json",
            "User-Agent": "InkwyrdAudio (https://github.com/Troyificus/InkWyrd-Audio, 0.1)",
        })

    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace")[:400]
        raise RuntimeError(f"token exchange HTTP {e.code}: {detail}")


def main():
    secret_path = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SECRET_PATH)
    if not secret_path.is_file():
        log(f"RESULT: FAILED - no client secret file at {secret_path}")
        return 1

    client_secret = secret_path.read_text(encoding="utf-8").strip()
    if not client_secret:
        log(f"RESULT: FAILED - {secret_path} is empty")
        return 1
    log(f"read client secret from {secret_path} ({len(client_secret)} chars)")

    with open(PIPE, "r+b", buffering=0) as pipe:
        # 1. HANDSHAKE
        send(pipe, 0, {"v": 1, "client_id": CLIENT_ID})
        op, payload = recv(pipe)
        if payload.get("evt") == "ERROR":
            log(f"RESULT: FAILED - handshake refused: {payload.get('data')}")
            return 1
        user = (payload.get("data") or {}).get("user") or {}
        log(f"handshake OK, connected as {user.get('username')}")

        # 2. AUTHORIZE - no redirect_uri, see CLAUDE.md
        log("requesting authorisation (a consent dialog may appear)...")
        data = command(pipe, "AUTHORIZE", {"client_id": CLIENT_ID, "scopes": SCOPES})
        code = data.get("code")
        if not code:
            log(f"RESULT: FAILED - no code in AUTHORIZE reply: {json.dumps(data)[:200]}")
            return 1
        log(f"got OAuth code ({len(code)} chars)")

        # 3. Exchange for an access token
        token_data = exchange_code_for_token(code, client_secret)
        access_token = token_data.get("access_token")
        if not access_token:
            log(f"RESULT: FAILED - no access_token: {json.dumps(token_data)[:200]}")
            return 1
        log(f"exchanged for access token ({len(access_token)} chars, "
            f"scope={token_data.get('scope')!r}, "
            f"expires_in={token_data.get('expires_in')}s, "
            f"refresh_token={'yes' if token_data.get('refresh_token') else 'no'})")

        # 4. AUTHENTICATE the RPC connection with it
        auth = command(pipe, "AUTHENTICATE", {"access_token": access_token})
        app = auth.get("application") or {}
        log(f"RPC connection authenticated as application {app.get('name')!r}")

        # 5. Read the CURRENT state before changing anything
        before = command(pipe, "GET_VOICE_SETTINGS")
        was_muted = before.get("mute")
        was_deafened = before.get("deaf")
        log(f"current voice settings: mute={was_muted} deaf={was_deafened}")

        try:
            # 6. Mute
            after = command(pipe, "SET_VOICE_SETTINGS", {"mute": True})
            log(f"SET_VOICE_SETTINGS mute=True -> mute is now {after.get('mute')}")
            log(">>> check Discord now - you should show as muted <<<")
            time.sleep(4)
        finally:
            # 7. Put it back, even if something above went wrong
            restored = command(pipe, "SET_VOICE_SETTINGS", {"mute": bool(was_muted)})
            log(f"restored mute={restored.get('mute')} (was {was_muted})")

    log("\nRESULT: WORKS END TO END - Inkwyrd can mute and unmute the local "
        "Discord client on its own.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
