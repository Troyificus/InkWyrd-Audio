#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// AAC/M4A and WMA support via Windows Media Foundation's modern
// IMFSourceReader API - not JUCE's own bundled WindowsMediaAudioFormat,
// which uses the older, WMA-only Windows Media Format SDK (IWMSyncReader)
// and doesn't cover AAC at all. Per docs/design-brief.md section 8:
// decoding via the OS's own already-licensed decoder, rather than
// bundling a codec of our own, avoids the app taking on any licensing
// obligation for these formats.
class MediaFoundationAudioFormat : public juce::AudioFormat
{
public:
    MediaFoundationAudioFormat();
    ~MediaFoundationAudioFormat() override;

    juce::Array<int> getPossibleSampleRates() override;
    juce::Array<int> getPossibleBitDepths() override;
    bool canDoStereo() override;
    bool canDoMono() override;
    bool isCompressed() override;

    juce::AudioFormatReader* createReaderFor(juce::InputStream* sourceStream, bool deleteStreamIfOpeningFails) override;

    juce::AudioFormatWriter* createWriterFor(juce::OutputStream*, double, unsigned int, int,
                                              const juce::StringPairArray&, int) override;
    using juce::AudioFormat::createWriterFor;
};
