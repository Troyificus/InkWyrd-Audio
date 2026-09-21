import { action, KeyDownEvent, SingletonAction } from "@elgato/streamdeck";
import { sendCommand } from "../audioAppClient";

interface SceneSettings {
	sceneName?: string;
	[key: string]: string | undefined; // satisfies the SDK's JsonObject constraint
}

// Switches Inkwyrd Audio to a scene: its playlist, its looping ambience,
// and its volume if it sets one. The app works out what actually needs to
// change, so pressing the scene that's already in effect is safe - it
// puts back anything that has drifted, like a loop the Killswitch cut.
@action({ UUID: "com.inkwyrd.audiodeck.scene" })
export class SceneKey extends SingletonAction<SceneSettings> {
	async onKeyDown(ev: KeyDownEvent<SceneSettings>): Promise<void> {
		const sceneName = ev.payload.settings.sceneName;
		if (!sceneName) {
			await ev.action.showAlert();
			return;
		}

		// A tick only when the app really got it; the warning triangle
		// when it isn't running - see sendCommand.
		if (sendCommand({ command: "activateScene", name: sceneName })) await ev.action.showOk();
		else await ev.action.showAlert();
	}
}
