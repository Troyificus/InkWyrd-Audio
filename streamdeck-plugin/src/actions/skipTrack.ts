import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

@action({ UUID: "com.inkwyrd.audiodeck.skiptrack" })
export class SkipTrack extends SingletonAction {
	async onKeyDown(ev: KeyDownEvent): Promise<void> {
		sendCommand({ command: "skipTrack" });
		await ev.action.showOk();
	}
}
