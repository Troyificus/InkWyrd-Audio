#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "MasterEngine.h"
#include "NowPlayingDisplay.h"
#include "PlaylistEngine.h"

// The Player window's whole content: status/transport across the top,
// then the things that shape playback behaviour (crossfade/loop/fade-out)
// and master volume.
//
// The playlist library, the now-playing track list, Voice FX and the
// soundboard used to all be embedded here directly. They're now each
// their own DetachableWindow (LibraryWindow, PlaylistWindow, VoiceFxWindow,
// SoundboardWindow) - this component only holds the two callbacks that
// show/hide the latter two, fired from a pair of buttons here, the same
// spot the old "Voice FX..." button always was.
//
// Calls straight into the engine objects from button handlers - JUCE
// button callbacks already run on the message thread, which is the
// thread every engine method here expects.
class PlayerComponent : public juce::Component,
                         private juce::Timer
{
public:
    PlayerComponent(PlaylistEngine& playlistToUse,
                     MasterEngine& masterEngineToUse,
                     const TrackMetadataStore& trackMetadata,
                     std::function<void()> onTogglePlaylistToUse,
                     std::function<void()> onToggleLibraryToUse,
                     std::function<void()> onToggleVoiceFxToUse,
                     std::function<void()> onToggleSoundboardToUse,
                     std::function<void()> onSettingsClickedToUse);

    void resized() override;

    // Pushed from InkwyrdAudioApplication as DiscordConnector's status
    // callback fires (on the message thread, already marshaled there).
    void setDiscordStatus(const juce::String& text);

    // Prominent warning banner for things that silently produce "no
    // sound" and would otherwise have no visible indication: the audio
    // device failing to open, a playlist yielding zero playable files,
    // unreadable playlist files. Empty text keeps the banner hidden.
    void setWarningBanner(const juce::String& text);

    // Re-reads the engines' current shuffle/mute/monitor state into the
    // button labels, for when something outside this component changes
    // it (connecting to Discord turns local monitoring off).
    void refreshToggleStates();

    // Whether Discord credentials are configured, so the transport can
    // point out that Monitor being off means nothing is audible ANYWHERE
    // rather than just "not locally".
    void setDiscordConfigured(bool configured);

    // The master fader, 0..1. Set once from the saved value at startup;
    // the callback fires when the user moves it so it can be persisted.
    void setMasterVolume(float volume);
    void setMasterVolumeChangedCallback(std::function<void(float)> callback);

    // Crossfade on/off, its length, and the Fade out length. Set from
    // saved settings at startup; the callback fires when the user changes
    // any of them so they can be persisted.
    void setPlaybackSettings(bool crossfadeEnabled, double crossfadeSeconds, double fadeOutSeconds,
                              bool loopEnabled, double loopGapSeconds);
    void setPlaybackSettingsChangedCallback(std::function<void()> callback);
    double getFadeOutSeconds() const { return fadeOutSlider.getValue(); }

private:
    void timerCallback() override;
    void updateShuffleButtonText();
    void updateMuteButtonText();
    void updateMonitorButtonText();
    void updatePlayButtonText();
    void updateCrossfadeToggleText();
    void updateLoopToggleText();
    void updateMonitorHint();

    PlaylistEngine& playlist;
    MasterEngine& masterEngine;

    // The design's "digital screen": art slot, artist/title/time, the
    // spectrum, and a draggable seek bar. Replaced a single bold
    // "Now playing:" label.
    std::unique_ptr<NowPlayingDisplay> nowPlaying;
    juce::Label discordStatusLabel;
    juce::Label warningBannerLabel; // hidden (zero height) unless given non-empty text
    juce::Label monitorHintLabel;   // "Monitor is off" - only while that actually means silence

    bool discordConfigured = false;

    juce::TextButton playButton;
    juce::TextButton stopButton { "Stop" };
    juce::TextButton fadeOutButton { "Fade out" };
    juce::TextButton skipButton { "Skip" };
    juce::TextButton shuffleButton;
    juce::TextButton muteButton;
    juce::TextButton monitorButton;
    juce::Label crossfadeCaption { {}, "Crossfade" };
    juce::TextButton crossfadeToggle;
    juce::Slider crossfadeSlider;
    juce::Label loopCaption { {}, "Loop track" };
    juce::TextButton loopToggle;
    juce::Slider loopGapSlider;

    juce::Label fadeOutCaption { {}, "Fade out" };
    juce::Slider fadeOutSlider;
    std::function<void()> onPlaybackSettingsChanged;

    juce::Label masterVolumeCaption { {}, "Master" };
    juce::Slider masterVolumeSlider;
    std::function<void(float)> onMasterVolumeChanged;

    // Every satellite window can be shown or hidden from here. All four
    // are needed, not just the two that started out hideable: closing a
    // window with its X used to strand it, because only Voice FX and
    // Soundboard had a way back.
    juce::TextButton playlistButton { "Playlist" };
    juce::TextButton libraryButton { "Library" };
    juce::TextButton voiceFxButton { "Voice FX" };
    juce::TextButton soundboardButton { "Soundboard" };
    juce::TextButton settingsButton { "Settings" };

    std::function<void()> onTogglePlaylist;
    std::function<void()> onToggleLibrary;
    std::function<void()> onToggleVoiceFx;
    std::function<void()> onToggleSoundboard;
};
