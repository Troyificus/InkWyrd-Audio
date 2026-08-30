#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <crtdbg.h>
#endif

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "PlaylistEngine.h"
#include "SoundboardEngine.h"
#include "Mp3AudioFormat.h"
#include "MediaFoundationAudioFormat.h"

namespace
{
    juce::StringArray registerSoundboardFolder(SoundboardEngine& soundboard,
                                                juce::AudioFormatManager& formatManager,
                                                const juce::File& folder)
    {
        juce::StringArray names;
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
        {
            auto file = entry.getFile();
            if (formatManager.findFormatForFileExtension(file.getFileExtension()) == nullptr)
                continue;
            auto name = file.getFileNameWithoutExtension();
            soundboard.registerSound(name, file);
            names.add(name);
        }
        return names;
    }
}

int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);

#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    auto playlistFolder = juce::SystemStats::getEnvironmentVariable("PLAYLIST_FOLDER", "");
    auto soundboardFolder = juce::SystemStats::getEnvironmentVariable("SOUNDBOARD_FOLDER", "");

    if (playlistFolder.isEmpty())
    {
        std::cout << "Set PLAYLIST_FOLDER (and optionally SOUNDBOARD_FOLDER) env vars first." << std::endl;
        return 1;
    }

    // PlaylistEngine's crossfade timing runs on a juce::Timer, which is
    // only ever dispatched by an active JUCE message loop pumped from
    // whichever thread JUCE considers "the" message thread - normally
    // the thread that first initialises MessageManager. That has to be
    // this (main) thread, so interactive stdin reading runs on a
    // background thread instead, with playlist/soundboard calls
    // marshalled back via callAsync to avoid racing the Timer callback.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats(); // WAV/AIFF/FLAC/Ogg Vorbis
    formatManager.registerFormat(new Mp3AudioFormat(), false);
    formatManager.registerFormat(new MediaFoundationAudioFormat(), false); // AAC/M4A + WMA

    PlaylistEngine playlist(formatManager);
    SoundboardEngine soundboard(formatManager);

    juce::StringArray soundNames;
    if (soundboardFolder.isNotEmpty())
        soundNames = registerSoundboardFolder(soundboard, formatManager, juce::File(soundboardFolder));

    juce::MixerAudioSource masterMixer;
    masterMixer.addInputSource(&playlist, false);
    masterMixer.addInputSource(&soundboard, false);

    juce::AudioSourcePlayer sourcePlayer;
    sourcePlayer.setSource(&masterMixer);

    juce::AudioDeviceManager deviceManager;
    auto openError = deviceManager.initialiseWithDefaultDevices(0, 2); // no inputs, stereo out
    if (openError.isNotEmpty())
    {
        std::cout << "Failed to open audio device: " << openError << std::endl;
        return 1;
    }
    deviceManager.addAudioCallback(&sourcePlayer);

    playlist.loadFolder(juce::File(playlistFolder));
    playlist.start();

    std::cout << "Playing from: " << playlistFolder << std::endl;
    std::cout << "Commands: s = skip/crossfade to next track, h = toggle shuffle, t = now playing";
    if (!soundNames.isEmpty())
    {
        std::cout << ", soundboard:";
        for (int i = 0; i < soundNames.size(); ++i)
            std::cout << " " << i << "=" << soundNames[i];
    }
    std::cout << ", q = quit" << std::endl;

    bool shuffle = true; // only ever touched from callAsync lambdas below, all on the message thread

    std::thread inputThread([&]
    {
        std::string line;
        while (true)
        {
            std::cout << "> ";
            if (!std::getline(std::cin, line) || line == "q")
            {
                // Ends runDispatchLoop() below - safe to call from any
                // thread, that's the documented purpose of this method.
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            }

            if (line.empty())
                continue;

            if (line == "s")
            {
                juce::MessageManager::callAsync([&playlist]
                {
                    playlist.skipToNext();
                    std::cout << "Crossfading to next track..." << std::endl;
                });
            }
            else if (line == "h")
            {
                juce::MessageManager::callAsync([&playlist, &shuffle]
                {
                    shuffle = !shuffle;
                    playlist.setShuffle(shuffle);
                    std::cout << "Shuffle: " << (shuffle ? "on" : "off")
                               << " (takes effect next time the order wraps)" << std::endl;
                });
            }
            else if (line == "t")
            {
                juce::MessageManager::callAsync([&playlist]
                {
                    std::cout << "Now playing: " << playlist.getCurrentTrackName()
                               << (playlist.isCrossfading() ? " (crossfading)" : "") << std::endl;
                });
            }
            else
            {
                auto index = std::atoi(line.c_str());
                if (index >= 0 && index < soundNames.size())
                {
                    auto name = soundNames[index];
                    juce::MessageManager::callAsync([&soundboard, name]
                    {
                        soundboard.trigger(name);
                        std::cout << "Triggered: " << name << std::endl;
                    });
                }
                else
                {
                    std::cout << "Unknown command." << std::endl;
                }
            }
        }
    });

    juce::MessageManager::getInstance()->runDispatchLoop(); // blocks until stopDispatchLoop() above

    inputThread.join();

    deviceManager.removeAudioCallback(&sourcePlayer);
    sourcePlayer.setSource(nullptr);
    playlist.stop();

    return 0;
}
