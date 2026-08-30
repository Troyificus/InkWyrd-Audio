import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

interface SoundboardSettings {
	soundName?: string;
	[key: string]: string | undefined; // satisfies the SDK's JsonObject constraint
}

@action({ UUID: "com.inkwyrd.audiodeck.soundboard" })
export class Soundboard extends SingletonAction<SoundboardSettings> {
	async onKeyDown(ev: KeyDownEvent<SoundboardSettings>): Promise<void> {
		const soundName = ev.payload.settings.soundName;
		if (!soundName) {
			await ev.action.showAlert();
			return;
		}
		sendCommand({ command: "triggerSoundboard", name: soundName });
		await ev.action.showOk();
	}
}
