import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

@action({ UUID: "com.inkwyrd.audiodeck.toggleshuffle" })
export class ToggleShuffle extends SingletonAction {
	async onKeyDown(ev: KeyDownEvent): Promise<void> {
		sendCommand({ command: "toggleShuffle" });
		await ev.action.showOk();
	}
}
