// Proves the plugin never replays key presses made while Inkwyrd Audio
// wasn't connected. That used to happen: presses were queued and all
// fired on the next connect - five Skips with the app closed skipped five
// tracks at the next launch.
//
// Runs the REAL client (src/audioAppClient.ts), bundled with a stand-in
// for the Stream Deck SDK, against a stand-in server on its own port, so
// it never touches a running copy of the app on 39231.
//
//   node test-no-replay.mjs
import { build } from "esbuild";
import { WebSocketServer } from "ws";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { resolve } from "node:path";

const PORT = 39299;
let failures = 0;
const check = (ok, what) => {
	console.log(`  ${ok ? "PASS" : "FAIL"}  ${what}`);
	if (!ok) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const dir = mkdtempSync(join(tmpdir(), "inkwyrd-noreplay-"));
const stub = join(dir, "streamdeck-stub.mjs");
writeFileSync(stub, "export default { logger: { info() {}, debug() {} } };\n");

// Inside node_modules rather than the temp folder: the bundle leaves
// `ws` external, and it has to be able to find it from where it sits.
const out = join("node_modules", ".inkwyrd-test", "client.mjs");
await build({
	entryPoints: ["src/audioAppClient.ts"],
	bundle: true,
	platform: "node",
	format: "esm",
	outfile: out,
	alias: { "@elgato/streamdeck": stub },
	external: ["ws"],
	logLevel: "silent",
});

process.env.INKWYRD_CONTROL_URL = `ws://127.0.0.1:${PORT}`;
const client = await import(pathToFileURL(resolve(out)).href);

// 1. No server: presses must be refused, not saved up.
const whileClosed = [
	client.sendCommand({ command: "skipTrack" }),
	client.sendCommand({ command: "skipTrack" }),
	client.sendCommand({ command: "triggerSoundboard", name: "Thunder" }),
];
check(whileClosed.every((sent) => sent === false),
	"presses while the app isn't running report failure, so the key can warn");

// 2. The "app" starts. Nothing pressed earlier may arrive.
const received = [];
const server = new WebSocketServer({ port: PORT, host: "127.0.0.1" });
server.on("connection", (ws) => ws.on("message", (m) => received.push(JSON.parse(m.toString()).command)));

// The client retries every 3 s; allow for one full retry, then a margin.
for (let i = 0; i < 50 && server.clients.size === 0; i++) await sleep(100);
check(server.clients.size === 1, "the client reconnects once the app is running");

await sleep(500);
check(received.length === 0,
	`nothing pressed while the app was closed is replayed on connect (got: ${JSON.stringify(received)})`);

// 3. Connected: a press goes through, once.
check(client.sendCommand({ command: "toggleMute" }) === true, "a press while connected reports success");
await sleep(300);
check(received.length === 1 && received[0] === "toggleMute", "and arrives exactly once");

for (const ws of server.clients) ws.terminate();
server.close();

console.log(failures === 0 ? "NO-REPLAY TEST PASSED" : `NO-REPLAY TEST FAILED (${failures})`);
process.exit(failures === 0 ? 0 : 1);
