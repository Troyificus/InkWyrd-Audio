import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

// Fades the music out over whatever the Player's Fade out slider says,
// then stops it - the same as the Player's own Fade out button. Leaves
// the soundboard alone; that is the Killswitch's job.
@action({ UUID: "com.inkwyrd.audiodeck.fadeoutmusic" })
export class FadeOutMusic extends SingletonAction {
	async onKeyDown(ev: KeyDownEvent): Promise<void> {
		// A tick only when the app really got it; the warning triangle
		// when it isn't running - see sendCommand.
		if (sendCommand({ command: "fadeOutMusic" })) await ev.action.showOk();
		else await ev.action.showAlert();
	}
}
