import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

@action({ UUID: "com.inkwyrd.audiodeck.togglemute" })
export class ToggleMute extends SingletonAction {
	async onKeyDown(ev: KeyDownEvent): Promise<void> {
		sendCommand({ command: "toggleMute" });
		await ev.action.showOk();
	}
}
