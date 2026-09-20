#include "MasterEngine.h"

MasterEngine::MasterEngine(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse,
                            PluginChain& voiceChainToUse, juce::AudioFormatManager& formatManagerToUse)
    : playlist(playlistToUse), soundboard(soundboardToUse), voiceChain(voiceChainToUse),
      formatManager(formatManagerToUse)
{
    musicMixer.addInputSource(&playlist, false);
    musicMixer.addInputSource(&soundboard, false);
}

bool MasterEngine::startPreview(const juce::File& file, float linearGain)
{
    stopPreview();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return false;

    previewReader = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    previewTransport.setSource(previewReader.get(), 0, nullptr,
                                previewReader->getAudioFormatReader()->sampleRate);
    previewTransport.setGain(linearGain);
    previewFile = file;
    previewActive.store(true);
    previewTransport.start();

    return true;
}

void MasterEngine::stopPreview()
{
    previewTransport.stop();

    // Ordered: the transport lets go of the source before the source is
    // destroyed, and destroying it closes the file - which is what lets
    // the tag editor write to a track that was just previewed.
    previewTransport.setSource(nullptr);
    previewReader.reset();

    previewActive.store(false);
    previewFile = juce::File();
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
    duck.prepare(device->getCurrentSampleRate());

    currentSampleRate = device->getCurrentSampleRate();
    auto blockSize = device->getCurrentBufferSizeSamples();

    musicMixer.prepareToPlay(blockSize, currentSampleRate);
    previewTransport.prepareToPlay(blockSize, currentSampleRate);
    noiseSuppressor.prepare(currentSampleRate, blockSize);
    voiceChain.prepareToPlay(currentSampleRate, blockSize);

    micBuffer.setSize(2, blockSize);
    masterBuffer.setSize(2, blockSize);
}

void MasterEngine::audioDeviceStopped()
{
    previewTransport.releaseResources();

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

    // 2b. Ducking: pull the MUSIC down while the mic is live, before
    // the voice is summed in below - ducking the finished mix would
    // duck the voice along with it, which is the opposite of the point.
    //
    // The level is read after the gate/suppressor and the VST chain, so
    // whatever the user already uses to decide what counts as their
    // voice decides this too. A muted mic was cleared in step 1, so it
    // can never hold the music down.
    {
        auto micPeak = juce::jmax(micBuffer.getMagnitude(0, 0, numSamples),
                                   micBuffer.getMagnitude(1, 0, numSamples));
        auto duckGain = duck.processBlock(micPeak, numSamples);

        if (! juce::approximatelyEqual(duckGain, lastDuckGain))
            masterBuffer.applyGainRamp(0, numSamples, lastDuckGain, duckGain);
        else if (duckGain < 1.0f)
            masterBuffer.applyGain(duckGain);

        lastDuckGain = duckGain;
    }

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

    // 4b. The preview, local output ONLY: after the monitor copy above so
    // it is heard even with Monitor off, and before the Discord send
    // below so it never leaves this machine.
    if (previewActive.load())
    {
        previewBuffer.setSize(2, numSamples, false, false, true);
        juce::AudioSourceChannelInfo previewInfo(&previewBuffer, 0, numSamples);
        previewTransport.getNextAudioBlock(previewInfo);

        for (int ch = 0; ch < juce::jmin(2, numOutputChannels); ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::add(outputChannelData[ch],
                                                  previewBuffer.getReadPointer(ch), numSamples);
    }

    // Tapped here, after the fader and before the send, so the display
    // reflects the mix as it actually leaves.
    spectrumTap.pushBlock(masterBuffer, numSamples);

    // Unaffected by local monitoring - what Discord receives is the same
    // master mix either way.
    if (auto* sender = discordSender.load())
        sender->pushSamples(masterBuffer, currentSampleRate);
}
