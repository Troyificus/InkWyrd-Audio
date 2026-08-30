import WebSocket from "ws";
import streamDeck from "@elgato/streamdeck";

// InkwyrdAudioApp's local control server - see src/app/ControlServer.h in
// the main repo. Fixed loopback-only port, no auth: nothing outside this
// machine can reach it, and there's nothing sensitive to protect locally.
const CONTROL_SERVER_URL = "ws://127.0.0.1:39231";
const RECONNECT_DELAY_MS = 3000;

type Command = Record<string, unknown>;
type MessageHandler = (msg: Record<string, unknown>) => void;

let socket: WebSocket | null = null;
let connecting = false;
const pendingSends: string[] = [];
const messageHandlers: MessageHandler[] = [];

function connect(): void {
	if (socket !== null || connecting) return;
	connecting = true;

	const ws = new WebSocket(CONTROL_SERVER_URL);

	ws.on("open", () => {
		connecting = false;
		socket = ws;
		streamDeck.logger.info("Connected to Inkwyrd Audio control server");
		while (pendingSends.length > 0) ws.send(pendingSends.shift() as string);
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

export function sendCommand(command: Command): void {
	const json = JSON.stringify(command);
	if (socket !== null && socket.readyState === WebSocket.OPEN) socket.send(json);
	else pendingSends.push(json);
	connect();
}

connect();
