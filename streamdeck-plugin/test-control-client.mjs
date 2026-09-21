import WebSocket from "ws";

const ws = new WebSocket("ws://127.0.0.1:39231");

ws.on("open", () => {
	console.log("[test-client] connected");
	const cmd = process.argv[2];
	if (cmd === "mute") ws.send(JSON.stringify({ command: "toggleMute" }));
	else if (cmd === "skip") ws.send(JSON.stringify({ command: "skipTrack" }));
	else if (cmd === "shuffle") ws.send(JSON.stringify({ command: "toggleShuffle" }));
	else if (cmd === "stopall") ws.send(JSON.stringify({ command: "stopAllSounds" }));
	else if (cmd === "fadeout") ws.send(JSON.stringify({ command: "fadeOutMusic" }));
	else if (cmd === "soundboard") ws.send(JSON.stringify({ command: "triggerSoundboard", name: process.argv[3] }));
	setTimeout(() => { ws.close(); process.exit(0); }, 500);
});

ws.on("error", (err) => {
	console.error("[test-client] error:", err.message);
	process.exit(1);
});
