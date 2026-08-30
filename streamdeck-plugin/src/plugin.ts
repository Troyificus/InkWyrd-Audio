import streamDeck from "@elgato/streamdeck";

import { SkipTrack } from "./actions/skipTrack";
import { ToggleShuffle } from "./actions/toggleShuffle";
import { Soundboard } from "./actions/soundboard";
import { ToggleMute } from "./actions/toggleMute";

// Establishes the connection to InkwyrdAudioApp's local control server
// as a side effect of import - see audioAppClient.ts.
import "./audioAppClient";

streamDeck.actions.registerAction(new SkipTrack());
streamDeck.actions.registerAction(new ToggleShuffle());
streamDeck.actions.registerAction(new Soundboard());
streamDeck.actions.registerAction(new ToggleMute());

streamDeck.connect();
