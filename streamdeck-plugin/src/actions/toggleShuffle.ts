import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

@action({ UUID: "com.inkwyrd.audiodeck.toggleshuffle" })
export class ToggleShuffle extends SingletonAction {
	async onKeyDown(ev: KeyDownEvent): Promise<void> {
		// A tick only when the app really got it; the warning triangle
		// when it isn't running - see sendCommand.
		if (sendCommand({ command: "toggleShuffle" })) await ev.action.showOk();
		else await ev.action.showAlert();
	}
}
