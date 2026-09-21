import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";

// Shown in Stream Deck as "Soundboard Killswitch". The UUID and the
// command name still say "stop all sounds": the UUID is what every key
// the user has already placed points at, so renaming it would orphan
// them, and the command name is kept so an older plugin build still
// talks to a newer app.
import { sendCommand } from "../audioAppClient";

@action({ UUID: "com.inkwyrd.audiodeck.stopallsounds" })
export class StopAllSounds extends SingletonAction {
	async onKeyDown(ev: KeyDownEvent): Promise<void> {
		sendCommand({ command: "stopAllSounds" });
		await ev.action.showOk();
	}
}
