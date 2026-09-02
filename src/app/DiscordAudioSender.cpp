#include "DiscordAudioSender.h"

#include <opus.h>

DiscordAudioSender::DiscordAudioSender(VoiceGatewayClient& voiceGatewayToUse, VoiceUdpSocket& udpToUse)
    : voiceGateway(voiceGatewayToUse), udp(udpToUse)
{
    int error = 0;
    opusEncoder = opus_encoder_create(kDiscordSampleRate, 2, OPUS_APPLICATION_AUDIO, &error);
    if (error == OPUS_OK && opusEncoder != nullptr)
        opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(128000)); // higher than the mono spike's 64k - this carries music, not just speech
}

DiscordAudioSender::~DiscordAudioSender()
{
    stop();
    if (opusEncoder != nullptr)
        opus_encoder_destroy(opusEncoder);
}

void DiscordAudioSender::start()
{
    if (running.exchange(true))
        return;
    senderThread = std::make_unique<std::thread>([this] { senderThreadLoop(); });
}

void DiscordAudioSender::stop()
{
    if (!running.exchange(false))
        return;
    if (senderThread != nullptr && senderThread->joinable())
        senderThread->join();
    senderThread.reset();
}

void DiscordAudioSender::pushSamples(const juce::AudioBuffer<float>& buffer, double sourceSampleRate)
{
    auto numInputSamples = buffer.getNumSamples();
    if (numInputSamples <= 0)
        return;

    const juce::AudioBuffer<float>* toWrite = &buffer;
    int numToWrite = numInputSamples;

    // Most WASAPI shared-mode devices on Windows already run at 48kHz,
    // in which case this is skipped entirely.
    if (!juce::approximatelyEqual(sourceSampleRate, (double) kDiscordSampleRate))
    {
        auto ratio = sourceSampleRate / (double) kDiscordSampleRate;
        auto estimatedOut = juce::jmin((int) (numInputSamples / ratio), kResampleScratchCapacity);
        if (estimatedOut <= 0)
            return;

        resampleScratch.setSize(2, estimatedOut, false, false, true); // avoidReallocating - no audio-thread allocation
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* src = buffer.getReadPointer(juce::jmin(ch, buffer.getNumChannels() - 1));
            resamplers[ch].process(ratio, src, resampleScratch.getWritePointer(ch), estimatedOut,
                                    numInputSamples, 0);
        }
        toWrite = &resampleScratch;
        numToWrite = estimatedOut;
    }

    int start1, size1, start2, size2;
    fifo.prepareToWrite(numToWrite, start1, size1, start2, size2);

    auto copyIn = [&](int fifoStart, int count)
    {
        if (count <= 0)
            return;
        for (int ch = 0; ch < 2; ++ch)
            fifoBuffer.copyFrom(ch, fifoStart, *toWrite, juce::jmin(ch, toWrite->getNumChannels() - 1), 0, count);
    };
    copyIn(start1, size1);
    // second block, if the write wrapped the ring buffer, starts after size1 samples of source data
    if (size2 > 0)
        for (int ch = 0; ch < 2; ++ch)
            fifoBuffer.copyFrom(ch, start2, *toWrite, juce::jmin(ch, toWrite->getNumChannels() - 1), size1, size2);

    fifo.finishedWrite(size1 + size2);
}

void DiscordAudioSender::senderThreadLoop()
{
    std::vector<float> interleaved((size_t) kFrameSamples * 2);
    std::vector<uint8_t> encodedBuf(4000);

    // Frames are paced against an absolute deadline that advances by
    // exactly one frame each time, NOT by sleeping 20ms per iteration.
    // A per-iteration sleep makes each pass cost 20ms *plus* the Opus
    // encode, DAVE encrypt and UDP send, so the loop drains slower than
    // the audio callback fills - the FIFO then grows until it hits its
    // ~2s capacity and parks there. That was a real beta report of
    // "severely delayed" mic audio. Windows makes it worse: the default
    // timer granularity is ~15.6ms, so sleep_for(20ms) commonly sleeps
    // ~31ms. An accumulating deadline self-corrects, because an overrun
    // on one frame simply shortens (or skips) the next sleep.
    auto nextFrameTime = std::chrono::steady_clock::now();

    while (running.load())
    {
        if (fifo.getNumReady() < kFrameSamples)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            // Nothing to send, so don't accrue a backlog of "missed"
            // frame deadlines to burst through once audio resumes.
            nextFrameTime = std::chrono::steady_clock::now();
            continue;
        }

        // Bound end-to-end latency. Device-rate -> 48kHz resampling is
        // not sample-exact, so a long session can still drift into a
        // growing backlog; discarding the excess costs one audible seam
        // instead of permanently delaying everything behind it.
        if (auto backlog = fifo.getNumReady(); backlog > kMaxBacklogSamples)
        {
            fifo.finishedRead(backlog - kTargetBacklogSamples);
            nextFrameTime = std::chrono::steady_clock::now();
        }

        int start1, size1, start2, size2;
        fifo.prepareToRead(kFrameSamples, start1, size1, start2, size2);

        int written = 0;
        auto copyOut = [&](int fifoStart, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                interleaved[(size_t) (written + i) * 2 + 0] = fifoBuffer.getSample(0, fifoStart + i);
                interleaved[(size_t) (written + i) * 2 + 1] = fifoBuffer.getSample(1, fifoStart + i);
            }
            written += count;
        };
        copyOut(start1, size1);
        copyOut(start2, size2);

        fifo.finishedRead(size1 + size2);

        auto encodedLen = opus_encode_float(opusEncoder, interleaved.data(), kFrameSamples,
                                             encodedBuf.data(), (opus_int32) encodedBuf.size());
        if (encodedLen > 0)
        {
            auto encrypted = voiceGateway.encryptOpusFrame(encodedBuf.data(), (size_t) encodedLen);
            if (!encrypted.empty())
                udp.sendOpusFrame(encrypted.data(), (int) encrypted.size());
        }

        nextFrameTime += std::chrono::milliseconds(kFrameMs);
        std::this_thread::sleep_until(nextFrameTime);
    }
}
