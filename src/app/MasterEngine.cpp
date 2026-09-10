#include "MasterEngine.h"

MasterEngine::MasterEngine(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse, PluginChain& voiceChainToUse)
    : playlist(playlistToUse), soundboard(soundboardToUse), voiceChain(voiceChainToUse)
{
    musicMixer.addInputSource(&playlist, false);
    musicMixer.addInputSource(&soundboard, false);
}

void MasterEngine::setDiscordSender(DiscordAudioSender* sender)
{
    discordSender.store(sender);
}

void MasterEngine::setMicMuted(bool muted)
{
    // exchange, so the callback fires on a real transition rather than
    // on every click of a button that was already in that state.
    if (micMuted.exchange(muted) != muted && onMicMuteChanged != nullptr)
        onMicMuteChanged(muted);
}

void MasterEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    auto blockSize = device->getCurrentBufferSizeSamples();

    musicMixer.prepareToPlay(blockSize, currentSampleRate);
    noiseSuppressor.prepare(currentSampleRate, blockSize);
    voiceChain.prepareToPlay(currentSampleRate, blockSize);

    micBuffer.setSize(2, blockSize);
    masterBuffer.setSize(2, blockSize);
}

void MasterEngine::audioDeviceStopped()
{
    musicMixer.releaseResources();
    voiceChain.releaseResources();
}

void MasterEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                     int numInputChannels,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    // 1. Mic input, run through the VST3 chain.
    micBuffer.setSize(2, numSamples, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* src = numInputChannels > 0 ? inputChannelData[juce::jmin(ch, numInputChannels - 1)] : nullptr;
        if (src != nullptr)
            micBuffer.copyFrom(ch, 0, src, numSamples);
        else
            micBuffer.clear(ch, 0, numSamples);
    }
    if (micMuted.load())
        micBuffer.clear();

    // Suppression runs BEFORE the VST chain, not after: the plugins are
    // there to shape the voice, and shaping a signal that still has the
    // room in it means the FX process the room too. A no-op when the
    // user hasn't turned it on.
    noiseSuppressor.process(micBuffer, numSamples);

    scratchMidi.clear();
    voiceChain.processBlock(micBuffer, scratchMidi);

    // 2. Playlist + soundboard mix, straight into the master buffer.
    masterBuffer.setSize(2, numSamples, false, false, true);
    juce::AudioSourceChannelInfo info(&masterBuffer, 0, numSamples);
    musicMixer.getNextAudioBlock(info);

    // 3. Sum processed voice into the same buffer - this is now the
    // final master mix, used for both local output and the Discord send.
    masterBuffer.addFrom(0, 0, micBuffer, 0, 0, numSamples);
    masterBuffer.addFrom(1, 0, micBuffer, 1, 0, numSamples);

    // 3b. Master fader. Ramped from wherever the last block ended rather
    // than applied flat, so dragging the slider doesn't step the gain
    // between blocks and click.
    auto targetGain = masterGain.load();
    if (targetGain != lastMasterGain)
    {
        masterBuffer.applyGainRamp(0, numSamples, lastMasterGain, targetGain);
        lastMasterGain = targetGain;
    }
    else if (targetGain != 1.0f)
    {
        masterBuffer.applyGain(targetGain);
    }

    // 4. Local speakers. Silenced while streaming to Discord - the host
    // is in the call too and hears the bot's stream there, so playing it
    // locally as well doubles everything with a slight offset.
    auto monitorLocally = localMonitoring.load();
    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;
        if (ch < 2 && monitorLocally)
            juce::FloatVectorOperations::copy(outputChannelData[ch], masterBuffer.getReadPointer(ch), numSamples);
        else
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
    }

    // Tapped here, after the fader and before the send, so the display
    // reflects the mix as it actually leaves.
    spectrumTap.pushBlock(masterBuffer, numSamples);

    // Unaffected by local monitoring - what Discord receives is the same
    // master mix either way.
    if (auto* sender = discordSender.load())
        sender->pushSamples(masterBuffer, currentSampleRate);
}
