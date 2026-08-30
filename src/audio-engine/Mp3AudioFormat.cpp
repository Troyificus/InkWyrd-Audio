#include "Mp3AudioFormat.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../../third_party/dr_mp3.h"

namespace
{
    size_t mp3ReadCallback(void* pUserData, void* pBufferOut, size_t bytesToRead)
    {
        auto* stream = static_cast<juce::InputStream*>(pUserData);
        return (size_t) stream->read(pBufferOut, (int) bytesToRead);
    }

    drmp3_bool32 mp3SeekCallback(void* pUserData, int offset, drmp3_seek_origin origin)
    {
        auto* stream = static_cast<juce::InputStream*>(pUserData);
        juce::int64 newPos = offset;
        if (origin == DRMP3_SEEK_CUR)
            newPos += stream->getPosition();
        else if (origin == DRMP3_SEEK_END)
            newPos += stream->getTotalLength();
        return stream->setPosition(newPos) ? DRMP3_TRUE : DRMP3_FALSE;
    }

    drmp3_bool32 mp3TellCallback(void* pUserData, drmp3_int64* pCursor)
    {
        auto* stream = static_cast<juce::InputStream*>(pUserData);
        *pCursor = stream->getPosition();
        return DRMP3_TRUE;
    }

    class Mp3AudioFormatReader : public juce::AudioFormatReader
    {
    public:
        explicit Mp3AudioFormatReader(juce::InputStream* sourceStream)
            : juce::AudioFormatReader(sourceStream, "MP3")
        {
            initialised = drmp3_init(&mp3, mp3ReadCallback, mp3SeekCallback, mp3TellCallback,
                                      nullptr, input, nullptr) == DRMP3_TRUE;
            if (!initialised)
                return;

            sampleRate = mp3.sampleRate;
            numChannels = mp3.channels;
            bitsPerSample = 32; // we hand JUCE float data - see usesFloatingPointData below
            usesFloatingPointData = true;
            lengthInSamples = (juce::int64) drmp3_get_pcm_frame_count(&mp3);
        }

        ~Mp3AudioFormatReader() override
        {
            if (initialised)
                drmp3_uninit(&mp3);
        }

        bool isValid() const { return initialised; }

        bool readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer,
                          juce::int64 startSampleInFile, int numSamples) override
        {
            if (!initialised)
                return false;

            if (startSampleInFile != nextReadPosition)
            {
                if (!drmp3_seek_to_pcm_frame(&mp3, (drmp3_uint64) startSampleInFile))
                    return false;
                nextReadPosition = startSampleInFile;
            }

            scratch.resize((size_t) numSamples * numChannels);
            auto framesRead = drmp3_read_pcm_frames_f32(&mp3, (drmp3_uint64) numSamples, scratch.data());
            nextReadPosition += (juce::int64) framesRead;

            // De-interleave into JUCE's per-channel buffers. destSamples are
            // declared int* but - per JUCE's own convention for
            // floating-point-source readers (see OggVorbisAudioFormat) -
            // hold raw float bit patterns once usesFloatingPointData is set.
            for (int ch = 0; ch < numDestChannels; ++ch)
            {
                if (destSamples[ch] == nullptr)
                    continue;

                auto* destFloat = reinterpret_cast<float*>(destSamples[ch]) + startOffsetInDestBuffer;
                auto sourceChannel = (juce::uint32) juce::jmin(ch, (int) numChannels - 1);

                for (drmp3_uint64 i = 0; i < framesRead; ++i)
                    destFloat[i] = scratch[(size_t) i * numChannels + sourceChannel];

                for (auto i = (juce::int64) framesRead; i < numSamples; ++i)
                    destFloat[i] = 0.0f;
            }

            return true;
        }

    private:
        drmp3 mp3 {};
        bool initialised = false;
        juce::int64 nextReadPosition = 0;
        std::vector<float> scratch;
    };
}

Mp3AudioFormat::Mp3AudioFormat() : juce::AudioFormat("MP3 file", juce::StringArray(".mp3")) {}
Mp3AudioFormat::~Mp3AudioFormat() = default;

juce::Array<int> Mp3AudioFormat::getPossibleSampleRates()
{
    return { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
}

juce::Array<int> Mp3AudioFormat::getPossibleBitDepths() { return { 32 }; }
bool Mp3AudioFormat::canDoStereo() { return true; }
bool Mp3AudioFormat::canDoMono() { return true; }
bool Mp3AudioFormat::isCompressed() { return true; }

juce::AudioFormatReader* Mp3AudioFormat::createReaderFor(juce::InputStream* sourceStream, bool deleteStreamIfOpeningFails)
{
    auto reader = std::make_unique<Mp3AudioFormatReader>(sourceStream);
    if (reader->isValid())
        return reader.release();

    if (!deleteStreamIfOpeningFails)
        reader->input = nullptr; // AudioFormatReader's destructor owns/deletes `input` unconditionally otherwise
    return nullptr;
}

juce::AudioFormatWriter* Mp3AudioFormat::createWriterFor(juce::OutputStream*, double, unsigned int, int,
                                                          const juce::StringPairArray&, int)
{
    return nullptr; // decode-only, matching the design brief's scope
}
