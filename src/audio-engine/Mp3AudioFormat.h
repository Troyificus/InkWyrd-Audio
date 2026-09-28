#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// MP3 support via dr_mp3 (public domain / MIT-0), not JUCE's own bundled
// MP3AudioFormat - that one requires JUCE_USE_MP3AUDIOFORMAT and ships
// with an explicit "not guaranteed to be free from infringements of
// 3rd-party intellectual property, at your own risk" disclaimer from
// Raw Material Software themselves. dr_mp3 has no such ambiguity: MP3's
// patents expired worldwide in 2017, and the decoder itself is public
// domain. See docs/design-brief.md section 8.
class Mp3AudioFormat : public juce::AudioFormat
{
public:
    Mp3AudioFormat();
    ~Mp3AudioFormat() override;

    juce::Array<int> getPossibleSampleRates() override;
    juce::Array<int> getPossibleBitDepths() override;
    bool canDoStereo() override;
    bool canDoMono() override;
    bool isCompressed() override;

    juce::AudioFormatReader* createReaderFor(juce::InputStream* sourceStream, bool deleteStreamIfOpeningFails) override;

    // Decode-only. JUCE 8.0.15 made the AudioFormatWriterOptions overload
    // the pure virtual and demoted the old six-argument one to a
    // non-virtual deprecated helper, so overriding the old signature no
    // longer makes these classes concrete - it left them abstract, which
    // is what the upgrade broke.
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor(std::unique_ptr<juce::OutputStream>& streamToWriteTo,
                                                              const juce::AudioFormatWriterOptions& options) override;
};
