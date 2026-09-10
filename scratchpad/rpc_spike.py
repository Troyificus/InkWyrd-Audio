"""
Spike, round 2: which AUTHORIZE payload shape does Discord's RPC socket
actually accept?

Round 1 passed {client_id, scopes, redirect_uri} and was refused with
'Redirect URI cannot be used in the RPC OAuth2 Authorization flow', even
with an exactly-matching registered URI and a freshly restarted client.

Two things from Discord Userdoccers' RPC reference say that payload may
simply be the wrong shape rather than the request being forbidden:

  * redirect_uri is documented as "only applicable if using the ws
    transport" - this is the IPC (named pipe) transport, where it may not
    belong at all;
  * there is a `response_type` argument ("must be code") that round 1
    never sent;
  * when redirect_uri IS accepted, omitting it defaults to "the first
    registered redirect URI for the application" - which reframes round
    1's very first error. `Missing "redirect_uri" in request` arrived
    when the application had NO registered redirects, so there was
    nothing to default to. That is not the same as "this field is
    required".

So: try the shapes in order, cheapest first, and stop at the first one
that doesn't come back as an error. A refusal returns immediately; a
SUCCESS puts a consent dialog in front of the user and blocks, which is
the outcome we want and is why this stops rather than continuing.

Still needs no client secret - the AUTHORIZE response settles the
question before any token exchange.
"""

import json
import struct
import sys
import uuid
from pathlib import Path

PIPE = r'\\.\pipe\discord-ipc-0'
CLIENT_ID = "1543399962723745792"   # public application id, not a secret
SCOPES = ["rpc", "rpc.voice.write"]
REGISTERED_URI = sys.argv[1] if len(sys.argv) > 1 else "https://inkwyrd.com/rpc"

LOG = Path(__file__).with_name("rpc_spike2_result.txt")

ATTEMPTS = [
    ("no redirect_uri (default to first registered)",
     {"client_id": CLIENT_ID, "scopes": SCOPES}),

    ("response_type=code, no redirect_uri",
     {"client_id": CLIENT_ID, "scopes": SCOPES, "response_type": "code"}),

    ("response_type=code + redirect_uri",
     {"client_id": CLIENT_ID, "scopes": SCOPES, "response_type": "code",
      "redirect_uri": REGISTERED_URI}),

    # 'rpc' alone, in case rpc.voice.write is the part being refused and
    # the redirect-URI message is just the generic OAuth2 error text.
    ("rpc scope only, no redirect_uri",
     {"client_id": CLIENT_ID, "scopes": ["rpc"]}),
]


def log(message):
    print(message, flush=True)
    with LOG.open("a", encoding="utf-8") as f:
        f.write(message + "\n")


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


def attempt(label, args):
    """Fresh connection per attempt - a refused AUTHORIZE may or may not
    leave the socket in a state that accepts another, and that isn't
    what's being tested here."""
    log(f"\n--- {label} ---")
    log(f"    args: {json.dumps({k: v for k, v in args.items() if k != 'client_id'})}")

    with open(PIPE, "r+b", buffering=0) as pipe:
        send(pipe, 0, {"v": 1, "client_id": CLIENT_ID})
        op, payload = recv(pipe)
        if payload.get("evt") == "ERROR":
            log(f"    handshake refused: {payload.get('data')}")
            return False

        send(pipe, 1, {"cmd": "AUTHORIZE", "args": args, "nonce": str(uuid.uuid4())})
        log("    >>> if a consent dialog appears in Discord, click Authorize <<<")

        op, payload = recv(pipe)
        data = payload.get("data") or {}

        if payload.get("evt") == "ERROR":
            log(f"    REFUSED - code={data.get('code')} message={data.get('message')!r}")
            return False

        code = data.get("code")
        if code:
            # Deliberately truncated - short-lived, but no reason for it
            # to sit in a log in full.
            log(f"    AUTHORISED - got an OAuth code ({len(code)} chars, starts {code[:6]}...)")
            return True

        log(f"    UNEXPECTED - {json.dumps(payload)[:400]}")
        return False


def main():
    LOG.write_text("", encoding="utf-8")
    log(f"registered redirect URI under test: {REGISTERED_URI}")

    for label, args in ATTEMPTS:
        try:
            if attempt(label, args):
                log("\nRESULT: ACCEPTED. The owner can grant rpc/rpc.voice.write "
                    "without whitelisting, using this payload shape.")
                return 0
        except Exception as e:
            log(f"    transport error: {e!r}")

    log("\nRESULT: every payload shape was refused.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
