#include "MediaFoundationAudioFormat.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mutex>
#include <vector>

namespace
{
    // MFStartup/MFShutdown are process-wide, not per-instance - reference
    // count them so the platform stays up as long as any reader needs it.
    std::mutex mfLifecycleMutex;
    int mfRefCount = 0;

    bool ensureMfStarted()
    {
        const std::lock_guard<std::mutex> lock(mfLifecycleMutex);
        if (mfRefCount == 0 && FAILED(MFStartup(MF_VERSION)))
            return false;
        ++mfRefCount;
        return true;
    }

    void releaseMf()
    {
        const std::lock_guard<std::mutex> lock(mfLifecycleMutex);
        if (--mfRefCount == 0)
            MFShutdown();
    }

    // COM apartment state is per-thread. readSamples() runs on whatever
    // thread JUCE's read-ahead buffering happens to use (its own
    // TimeSliceThread, not necessarily the thread that constructed this
    // reader), so this has to be checked on every thread that calls in.
    void ensureComInitialisedOnThisThread()
    {
        thread_local bool initialised = false;
        if (!initialised)
        {
            // S_FALSE ("already initialised on this thread") isn't an
            // error. Deliberately never paired with CoUninitialize(): we
            // don't own this thread's lifetime, so leaving the apartment
            // initialised for the process's lifetime is the safe choice.
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            initialised = true;
        }
    }

    template <typename T>
    void safeRelease(T*& p)
    {
        if (p != nullptr)
        {
            p->Release();
            p = nullptr;
        }
    }

    class MediaFoundationAudioFormatReader : public juce::AudioFormatReader
    {
    public:
        MediaFoundationAudioFormatReader(juce::InputStream* sourceStream, const juce::String& filePath)
            : juce::AudioFormatReader(sourceStream, "Media Foundation")
        {
            if (!ensureMfStarted())
                return;
            mfStarted = true;

            ensureComInitialisedOnThisThread();
            initialised = open(filePath);
        }

        ~MediaFoundationAudioFormatReader() override
        {
            safeRelease(sourceReader);
            if (mfStarted)
                releaseMf();
        }

        bool isValid() const { return initialised; }

        bool readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer,
                          juce::int64 startSampleInFile, int numSamples) override
        {
            if (!initialised)
                return false;

            ensureComInitialisedOnThisThread();

            if (startSampleInFile != nextReadPosition && !seekTo(startSampleInFile))
                return false;

            int framesWritten = 0;

            while (framesWritten < numSamples)
            {
                if (pendingFrames.empty())
                {
                    if (!decodeNextSample())
                        break; // genuine end of stream
                    if (pendingFrames.empty())
                        continue; // gap in the stream, not EOF - try the next sample
                }

                auto framesAvailable = (int) (pendingFrames.size() / numChannels) - pendingFrameOffset;
                auto framesToCopy = juce::jmin(framesAvailable, numSamples - framesWritten);

                for (int ch = 0; ch < numDestChannels; ++ch)
                {
                    if (destSamples[ch] == nullptr)
                        continue;

                    auto* destFloat = reinterpret_cast<float*>(destSamples[ch]) + startOffsetInDestBuffer + framesWritten;
                    auto sourceChannel = (size_t) juce::jmin(ch, (int) numChannels - 1);

                    for (int i = 0; i < framesToCopy; ++i)
                        destFloat[i] = pendingFrames[(size_t) (pendingFrameOffset + i) * numChannels + sourceChannel];
                }

                pendingFrameOffset += framesToCopy;
                framesWritten += framesToCopy;
                nextReadPosition += framesToCopy;

                if (pendingFrameOffset >= (int) (pendingFrames.size() / numChannels))
                {
                    pendingFrames.clear();
                    pendingFrameOffset = 0;
                }
            }

            for (int ch = 0; ch < numDestChannels; ++ch)
                if (destSamples[ch] != nullptr)
                    for (int i = framesWritten; i < numSamples; ++i)
                        reinterpret_cast<float*>(destSamples[ch])[startOffsetInDestBuffer + i] = 0.0f;

            return true;
        }

    private:
        bool open(const juce::String& filePath)
        {
            if (FAILED(MFCreateSourceReaderFromURL(filePath.toWideCharPointer(), nullptr, &sourceReader))
                || sourceReader == nullptr)
                return false;

            sourceReader->SetStreamSelection((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
            sourceReader->SetStreamSelection((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

            IMFMediaType* partialType = nullptr;
            HRESULT hr = MFCreateMediaType(&partialType);
            if (SUCCEEDED(hr)) hr = partialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            // Float PCM output, not the int16 PCM the Microsoft tutorial
            // this is based on writes to WAV - reuses the exact
            // usesFloatingPointData + raw-memcpy convention already
            // verified against JUCE's own OggVorbisAudioFormat, rather
            // than introduce a second, untested int-scaling conversion.
            if (SUCCEEDED(hr)) hr = partialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            if (SUCCEEDED(hr))
                hr = sourceReader->SetCurrentMediaType((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, partialType);
            safeRelease(partialType);
            if (FAILED(hr))
                return false;

            IMFMediaType* actualType = nullptr;
            hr = sourceReader->GetCurrentMediaType((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actualType);
            if (FAILED(hr) || actualType == nullptr)
                return false;

            UINT32 rate = 0, channels = 0;
            actualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            actualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            safeRelease(actualType);

            if (rate == 0 || channels == 0)
                return false;

            sampleRate = rate;
            numChannels = channels;
            bitsPerSample = 32;
            usesFloatingPointData = true;

            // Duration -> sample count is Media Foundation's own estimate
            // from container metadata, not an exact decoded frame count -
            // there's no way to know the exact PCM length of a compressed
            // source without decoding the whole thing. Same tradeoff any
            // compressed-format duration estimate makes.
            PROPVARIANT durationProp;
            PropVariantInit(&durationProp);
            if (SUCCEEDED(sourceReader->GetPresentationAttribute((DWORD) MF_SOURCE_READER_MEDIASOURCE,
                                                                  MF_PD_DURATION, &durationProp)))
            {
                // MF_PD_DURATION is documented as UINT64 (VT_UI8), but
                // accept VT_I8 too rather than silently leave the
                // duration unknown on a variant-type mismatch - both
                // share the same 8-byte layout via LARGE_INTEGER/
                // ULARGE_INTEGER, so QuadPart is safe to read from either.
                if (durationProp.vt == VT_UI8 || durationProp.vt == VT_I8)
                {
                    auto duration100ns = (double) durationProp.uhVal.QuadPart;
                    lengthInSamples = (juce::int64) (duration100ns * sampleRate / 10000000.0);
                }
            }
            PropVariantClear(&durationProp);

            return true;
        }

        bool seekTo(juce::int64 sampleIndex)
        {
            PROPVARIANT pos;
            PropVariantInit(&pos);
            pos.vt = VT_I8;
            pos.hVal.QuadPart = (LONGLONG) (sampleIndex * 10000000.0 / sampleRate);

            auto hr = sourceReader->SetCurrentPosition(GUID_NULL, pos);
            PropVariantClear(&pos);

            pendingFrames.clear();
            pendingFrameOffset = 0;

            if (FAILED(hr))
                return false;

            nextReadPosition = sampleIndex;
            return true;
        }

        // Returns false only on genuine end-of-stream. A "gap" (sample
        // == nullptr, not EOF) returns true with pendingFrames left
        // empty - the caller's loop tries again rather than stopping.
        bool decodeNextSample()
        {
            DWORD flags = 0;
            IMFSample* sample = nullptr;

            if (FAILED(sourceReader->ReadSample((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                                 0, nullptr, &flags, nullptr, &sample)))
                return false;

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            {
                safeRelease(sample);
                return false;
            }

            if (sample == nullptr)
                return true;

            IMFMediaBuffer* buffer = nullptr;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || buffer == nullptr)
            {
                safeRelease(sample);
                return false;
            }

            BYTE* data = nullptr;
            DWORD dataLen = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &dataLen)))
            {
                auto numFloats = dataLen / sizeof(float);
                pendingFrames.assign(reinterpret_cast<float*>(data), reinterpret_cast<float*>(data) + numFloats);
                buffer->Unlock();
            }

            safeRelease(buffer);
            safeRelease(sample);
            return true;
        }

        IMFSourceReader* sourceReader = nullptr;
        bool mfStarted = false;
        bool initialised = false;
        juce::int64 nextReadPosition = 0;
        std::vector<float> pendingFrames; // interleaved
        int pendingFrameOffset = 0;
    };
}

namespace
{
    juce::StringArray mediaFoundationExtensions()
    {
        juce::StringArray extensions;
        extensions.add(".m4a");
        extensions.add(".aac");
        extensions.add(".wma");
        return extensions;
    }
}

MediaFoundationAudioFormat::MediaFoundationAudioFormat()
    : juce::AudioFormat("Media Foundation audio", mediaFoundationExtensions()) {}

MediaFoundationAudioFormat::~MediaFoundationAudioFormat() = default;

juce::Array<int> MediaFoundationAudioFormat::getPossibleSampleRates()
{
    return { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
}

juce::Array<int> MediaFoundationAudioFormat::getPossibleBitDepths() { return { 32 }; }
bool MediaFoundationAudioFormat::canDoStereo() { return true; }
bool MediaFoundationAudioFormat::canDoMono() { return true; }
bool MediaFoundationAudioFormat::isCompressed() { return true; }

juce::AudioFormatReader* MediaFoundationAudioFormat::createReaderFor(juce::InputStream* sourceStream,
                                                                       bool deleteStreamIfOpeningFails)
{
    // Media Foundation opens the file itself via MFCreateSourceReaderFromURL
    // rather than reading through the juce::InputStream (which would need
    // a full IMFByteStream COM wrapper) - so this only works for real
    // files on disk, not arbitrary streams. That covers every real use
    // in this app (playlist/soundboard always load from a juce::File).
    auto* fileStream = dynamic_cast<juce::FileInputStream*>(sourceStream);
    if (fileStream == nullptr)
    {
        if (deleteStreamIfOpeningFails)
            delete sourceStream;
        return nullptr;
    }

    auto path = fileStream->getFile().getFullPathName();
    auto reader = std::make_unique<MediaFoundationAudioFormatReader>(sourceStream, path);
    if (reader->isValid())
        return reader.release();

    if (!deleteStreamIfOpeningFails)
        reader->input = nullptr;
    return nullptr;
}

juce::AudioFormatWriter* MediaFoundationAudioFormat::createWriterFor(juce::OutputStream*, double, unsigned int, int,
                                                                      const juce::StringPairArray&, int)
{
    return nullptr; // decode-only, matching the design brief's scope
}
