import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

// Fades the music out over whatever the Player's Fade out slider says,
// then stops it - the same as the Player's own Fade out button. Leaves
// the soundboard alone; that is the Killswitch's job.
@action({ UUID: "com.inkwyrd.audiodeck.fadeoutmusic" })
export class FadeOutMusic extends SingletonAction {
	async onKeyDown(ev: KeyDownEvent): Promise<void> {
		sendCommand({ command: "fadeOutMusic" });
		await ev.action.showOk();
	}
}
