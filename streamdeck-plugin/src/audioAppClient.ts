import WebSocket from "ws";
import streamDeck from "@elgato/streamdeck";

// InkwyrdAudioApp's local control server - see src/app/ControlServer.h in
// the main repo. Fixed loopback-only port, no auth: nothing outside this
// machine can reach it, and there's nothing sensitive to protect locally.
//
// INKWYRD_CONTROL_URL exists only for test-no-replay.mjs, which has to
// run its own stand-in server without touching a real running app.
// Stream Deck never sets it.
const CONTROL_SERVER_URL = process.env.INKWYRD_CONTROL_URL ?? "ws://127.0.0.1:39231";
const RECONNECT_DELAY_MS = 3000;

type Command = Record<string, unknown>;
type MessageHandler = (msg: Record<string, unknown>) => void;

let socket: WebSocket | null = null;
let connecting = false;
const messageHandlers: MessageHandler[] = [];

function connect(): void {
	if (socket !== null || connecting) return;
	connecting = true;

	const ws = new WebSocket(CONTROL_SERVER_URL);

	ws.on("open", () => {
		connecting = false;
		socket = ws;
		streamDeck.logger.info("Connected to Inkwyrd Audio control server");
	});

	ws.on("close", () => {
		socket = null;
		connecting = false;
		// InkwyrdAudioApp might not be running yet, or was just closed -
		// keep quietly retrying rather than giving up on the connection.
		setTimeout(connect, RECONNECT_DELAY_MS);
	});

	ws.on("error", (err: Error) => {
		streamDeck.logger.debug(`Inkwyrd Audio control connection error: ${err.message}`);
	});

	ws.on("message", (data: WebSocket.RawData) => {
		try {
			const msg = JSON.parse(data.toString());
			for (const handler of messageHandlers) handler(msg);
		} catch {
			// ignore malformed messages
		}
	});
}

export function onMessage(handler: MessageHandler): void {
	messageHandlers.push(handler);
}

// Sends now or not at all, and says which.
//
// This used to QUEUE presses made while the app wasn't connected and
// replay every one of them the moment it connected - so five presses of
// Skip with the app closed skipped five tracks on the next launch, and
// any soundboard keys pressed in the meantime all fired at once into the
// call. A key press is a request for something to happen NOW; one that
// can't happen now must not happen later by surprise. Returns false so
// the key can show a warning instead of a tick.
export function sendCommand(command: Command): boolean {
	if (socket !== null && socket.readyState === WebSocket.OPEN) {
		socket.send(JSON.stringify(command));
		return true;
	}

	streamDeck.logger.info(`Inkwyrd Audio isn't connected - dropped ${String(command.command)}`);
	connect(); // may already be retrying; this just makes sure
	return false;
}

connect();
