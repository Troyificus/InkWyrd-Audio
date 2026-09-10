// Spike: how much noise does RNNoise actually remove from a mic signal,
// and how much of the voice survives?
//
// Deliberately standalone and dependency-light (no JUCE) so it measures
// the library rather than the plumbing around it.
//
// It takes a CLEAN speech recording, mixes in synthetic mic noise at a
// known SNR, runs the result through RNNoise, and compares three
// signals: clean, noisy, denoised. Because the clean reference is known
// sample for sample, this reports real numbers rather than impressions:
//
//   * noise-floor attenuation, measured only on the frames where the
//     clean reference is silent (the pauses between words);
//   * segmental SNR before and after, measured only on speech frames -
//     this is the metric that actually matters, because it accounts for
//     damage done to the VOICE as well as noise removed. A suppressor
//     that quietens everything equally scores zero here, correctly;
//   * RNNoise's own VAD output on speech vs silence frames, which says
//     whether the model is discriminating at all or just applying a
//     flat gain.
//
// WHY A REAL RECORDING AND NOT A SYNTHESISED TONE STACK. The first
// version of this spike synthesised a "voice" from harmonics at a
// speech-like pitch. It produced a uniform -3.2dB on every section,
// noise and voice alike - i.e. the model was applying a flat gain and
// discriminating nothing. That is exactly what you'd expect from a
// trained model shown a signal unlike anything in its training set, and
// it says nothing useful about how RNNoise behaves on a real mic. Feed
// it something with real formants, real consonants and real pauses.
//
// Pass the path to a 16-bit mono 48kHz WAV as argv[1]. (Windows' own
// SAPI voices can produce one, which is how this was first run - see
// CLAUDE.md.)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <rnnoise.h>

namespace
{
    constexpr int kSampleRate = 48000; // RNNoise is 48kHz only
    constexpr double kPi = 3.14159265358979323846;

    // Anything quieter than this in the CLEAN reference counts as a
    // pause rather than speech.
    constexpr double kSilenceThresholdDb = -55.0;

    // Frames next to a speech/silence transition are excluded from BOTH
    // measurements. The suppressor needs a moment to open and close, and
    // counting its attack as steady-state performance would flatter the
    // noise figure and punish the speech figure for the same reason.
    constexpr int kTransitionGuardFrames = 3;

    double toDb(double linear) { return linear > 0.0 ? 20.0 * std::log10(linear) : -200.0; }

    double rms(const std::vector<float>& samples, size_t first, size_t last)
    {
        if (last <= first) return 0.0;
        double sum = 0.0;
        for (auto i = first; i < last; ++i)
            sum += (double) samples[i] * (double) samples[i];
        return std::sqrt(sum / (double) (last - first));
    }

    bool readMonoWav16(const std::string& path, std::vector<float>& out, int& sampleRate)
    {
        std::ifstream in(path, std::ios::binary);
        if (! in) return false;

        char riff[4], wave[4];
        uint32_t riffSize = 0;
        in.read(riff, 4);
        in.read(reinterpret_cast<char*>(&riffSize), 4);
        in.read(wave, 4);
        if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
            return false;

        uint16_t channels = 0, bits = 0, format = 0;

        // Walk the chunks rather than assuming fmt-then-data: real
        // writers (SAPI included) put LIST/fact chunks in between.
        while (in)
        {
            char id[4];
            uint32_t size = 0;
            in.read(id, 4);
            in.read(reinterpret_cast<char*>(&size), 4);
            if (! in) break;

            if (std::memcmp(id, "fmt ", 4) == 0)
            {
                uint32_t rate = 0, byteRate = 0;
                uint16_t blockAlign = 0;
                in.read(reinterpret_cast<char*>(&format), 2);
                in.read(reinterpret_cast<char*>(&channels), 2);
                in.read(reinterpret_cast<char*>(&rate), 4);
                in.read(reinterpret_cast<char*>(&byteRate), 4);
                in.read(reinterpret_cast<char*>(&blockAlign), 2);
                in.read(reinterpret_cast<char*>(&bits), 2);
                sampleRate = (int) rate;
                if (size > 16) in.seekg(size - 16, std::ios::cur);
            }
            else if (std::memcmp(id, "data", 4) == 0)
            {
                if (format != 1 || bits != 16 || channels != 1)
                {
                    std::printf("wav must be 16-bit mono PCM (got format=%u bits=%u channels=%u)\n",
                                 format, bits, channels);
                    return false;
                }

                out.resize(size / 2);
                for (auto& sample : out)
                {
                    int16_t value = 0;
                    in.read(reinterpret_cast<char*>(&value), 2);
                    sample = (float) value / 32768.0f;
                }
                return true;
            }
            else
            {
                in.seekg(size + (size & 1), std::ios::cur);
            }
        }

        return false;
    }

    void writeWav(const std::string& path, const std::vector<float>& samples)
    {
        std::ofstream out(path, std::ios::binary);
        if (! out) return;

        auto dataBytes = (uint32_t) (samples.size() * 2);
        uint32_t riffSize = 36 + dataBytes;
        uint16_t channels = 1, bitsPerSample = 16, blockAlign = 2, audioFormat = 1;
        uint32_t sampleRate = kSampleRate, byteRate = kSampleRate * 2, fmtSize = 16;

        auto u32 = [&out](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&out](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

        out.write("RIFF", 4); u32(riffSize); out.write("WAVE", 4);
        out.write("fmt ", 4); u32(fmtSize); u16(audioFormat); u16(channels);
        u32(sampleRate); u32(byteRate); u16(blockAlign); u16(bitsPerSample);
        out.write("data", 4); u32(dataBytes);

        for (auto sample : samples)
        {
            auto clamped = std::max(-1.0f, std::min(1.0f, sample));
            auto value = (int16_t) std::lround(clamped * 32767.0f);
            out.write(reinterpret_cast<const char*>(&value), 2);
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: MicSpike <clean-speech.wav> [target-snr-db]\n");
        return 2;
    }

    const double targetSnrDb = argc > 2 ? std::atof(argv[2]) : 10.0;

    std::vector<float> clean;
    int fileRate = 0;
    if (! readMonoWav16(argv[1], clean, fileRate))
    {
        std::printf("RESULT: FAILED - could not read %s\n", argv[1]);
        return 1;
    }

    if (fileRate != kSampleRate)
    {
        std::printf("RESULT: FAILED - RNNoise is 48kHz only, file is %d Hz\n", fileRate);
        return 1;
    }

    const auto frameSize = (size_t) rnnoise_get_frame_size();
    const auto numFrames = clean.size() / frameSize;
    const auto totalSamples = numFrames * frameSize;
    clean.resize(totalSamples);

    std::printf("input: %s\n", argv[1]);
    std::printf("%.1f s at %d Hz, RNNoise frame size %zu samples (%.1f ms)\n\n",
                 (double) totalSamples / kSampleRate, kSampleRate, frameSize,
                 1000.0 * frameSize / kSampleRate);

    // ---- classify each frame from the CLEAN reference ----
    std::vector<bool> isSpeech(numFrames, false);
    for (size_t f = 0; f < numFrames; ++f)
        isSpeech[f] = toDb(rms(clean, f * frameSize, (f + 1) * frameSize)) > kSilenceThresholdDb;

    std::vector<bool> measurable(numFrames, true);
    for (size_t f = 0; f < numFrames; ++f)
    {
        auto lo = f > (size_t) kTransitionGuardFrames ? f - kTransitionGuardFrames : 0;
        auto hi = std::min(numFrames - 1, f + (size_t) kTransitionGuardFrames);
        for (auto n = lo; n <= hi; ++n)
            if (isSpeech[n] != isSpeech[f]) { measurable[f] = false; break; }
    }

    // ---- build the noise, scaled to hit the requested SNR ----
    double speechEnergy = 0.0; size_t speechSamples = 0;
    for (size_t f = 0; f < numFrames; ++f)
        if (isSpeech[f])
            for (size_t i = f * frameSize; i < (f + 1) * frameSize; ++i)
                { speechEnergy += (double) clean[i] * clean[i]; ++speechSamples; }

    if (speechSamples == 0)
    {
        std::printf("RESULT: FAILED - no speech found above %.0f dBFS\n", kSilenceThresholdDb);
        return 1;
    }

    const auto speechRms = std::sqrt(speechEnergy / (double) speechSamples);
    const auto wantedNoiseRms = speechRms / std::pow(10.0, targetSnrDb / 20.0);

    std::mt19937 rng(20260910);
    std::normal_distribution<float> gaussian(0.0f, 1.0f);

    // Broadband hiss plus a little mains hum, which is roughly what a
    // cheap mic in a room actually sounds like. Hum is a fixed fraction
    // of the hiss so the total lands on the requested SNR.
    std::vector<float> noise(totalSamples, 0.0f);
    for (size_t i = 0; i < totalSamples; ++i)
    {
        auto t = (double) i / kSampleRate;
        noise[i] = gaussian(rng) + 0.2f * (float) std::sin(2.0 * kPi * 50.0 * t);
    }

    auto rawNoiseRms = rms(noise, 0, totalSamples);
    auto noiseGain = (float) (wantedNoiseRms / rawNoiseRms);

    std::vector<float> noisy(totalSamples, 0.0f);
    for (size_t i = 0; i < totalSamples; ++i)
    {
        noise[i] *= noiseGain;
        noisy[i] = clean[i] + noise[i];
    }

    // ---- run it through RNNoise ----
    auto* state = rnnoise_create(nullptr);
    if (state == nullptr)
    {
        std::printf("RESULT: FAILED - rnnoise_create returned null\n");
        return 1;
    }

    std::vector<float> denoised(totalSamples, 0.0f);
    std::vector<float> vad(numFrames, 0.0f);
    std::vector<float> inFrame(frameSize), outFrame(frameSize);

    // Timed, because this build has no SIMD path (see
    // third_party/rnnoise/CMakeLists.txt) and upstream warns at compile
    // time that the scalar fallback "will be very slow". Whether that
    // matters is a measurement, not an opinion: what counts is the cost
    // of one 10ms frame against a 10ms budget.
    auto processingStart = std::chrono::steady_clock::now();

    for (size_t f = 0; f < numFrames; ++f)
    {
        // THE gotcha worth recording: RNNoise works in int16 RANGE as
        // floats (-32768..32767), not -1..1. Feed it normalised audio and
        // it does almost nothing, because everything looks like silence.
        for (size_t i = 0; i < frameSize; ++i)
            inFrame[i] = noisy[f * frameSize + i] * 32768.0f;

        vad[f] = rnnoise_process_frame(state, outFrame.data(), inFrame.data());

        for (size_t i = 0; i < frameSize; ++i)
            denoised[f * frameSize + i] = outFrame[i] / 32768.0f;
    }

    auto processingMs = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - processingStart).count();

    rnnoise_destroy(state);

    writeWav("mic-spike-clean.wav", clean);
    writeWav("mic-spike-noisy.wav", noisy);
    writeWav("mic-spike-denoised.wav", denoised);

    // ---- RNNoise delays the signal; find by how much before comparing ----
    // Measuring distortion against a misaligned reference would report
    // the delay as damage.
    size_t bestDelay = 0;
    double bestCorrelation = -1.0e30;
    for (size_t delay = 0; delay <= 2 * frameSize; ++delay)
    {
        double correlation = 0.0;
        for (size_t i = 0; i + delay < totalSamples; i += 7) // stride: this is a search, not a measurement
            correlation += (double) clean[i] * denoised[i + delay];
        if (correlation > bestCorrelation) { bestCorrelation = correlation; bestDelay = delay; }
    }

    // ---- measure ----
    double noisyErr = 0.0, cleanEnergyMeasured = 0.0, denoisedErr = 0.0;
    double silenceNoisy = 0.0, silenceDenoised = 0.0; size_t silenceCount = 0;
    double vadSpeech = 0.0, vadSilence = 0.0; size_t vadSpeechCount = 0, vadSilenceCount = 0;

    for (size_t f = 0; f < numFrames; ++f)
    {
        if (! measurable[f]) continue;

        for (size_t i = f * frameSize; i < (f + 1) * frameSize; ++i)
        {
            auto aligned = i + bestDelay < totalSamples ? denoised[i + bestDelay] : 0.0f;

            if (isSpeech[f])
            {
                cleanEnergyMeasured += (double) clean[i] * clean[i];
                auto e1 = (double) noisy[i] - clean[i];      noisyErr += e1 * e1;
                auto e2 = (double) aligned - clean[i];       denoisedErr += e2 * e2;
            }
            else
            {
                silenceNoisy += (double) noisy[i] * noisy[i];
                silenceDenoised += (double) aligned * aligned;
                ++silenceCount;
            }
        }

        if (isSpeech[f]) { vadSpeech += vad[f]; ++vadSpeechCount; }
        else             { vadSilence += vad[f]; ++vadSilenceCount; }
    }

    auto snrBefore = 10.0 * std::log10(cleanEnergyMeasured / noisyErr);
    auto snrAfter = 10.0 * std::log10(cleanEnergyMeasured / denoisedErr);
    auto silenceBeforeDb = toDb(std::sqrt(silenceNoisy / (double) silenceCount));
    auto silenceAfterDb = toDb(std::sqrt(silenceDenoised / (double) silenceCount));

    std::printf("frames: %zu speech, %zu silence (%zu excluded as transitions)\n",
                 vadSpeechCount, vadSilenceCount, numFrames - vadSpeechCount - vadSilenceCount);
    std::printf("alignment: RNNoise output delayed by %zu samples (%.1f ms)\n\n",
                 bestDelay, 1000.0 * (double) bestDelay / kSampleRate);

    std::printf("background level in the PAUSES\n");
    std::printf("  noisy    %8.1f dBFS\n", silenceBeforeDb);
    std::printf("  denoised %8.1f dBFS   -> %+.1f dB of noise removed\n\n",
                 silenceAfterDb, silenceAfterDb - silenceBeforeDb);

    std::printf("SNR during SPEECH (against the clean reference)\n");
    std::printf("  noisy    %8.1f dB\n", snrBefore);
    std::printf("  denoised %8.1f dB   -> %+.1f dB improvement\n\n", snrAfter, snrAfter - snrBefore);

    std::printf("RNNoise's own VAD\n");
    std::printf("  speech frames  %.3f\n", vadSpeech / (double) vadSpeechCount);
    std::printf("  silence frames %.3f\n", vadSilence / (double) vadSilenceCount);

    // A mean near 0.5 can hide either "confidently wrong half the time"
    // or "never commits to anything" - and those mean very different
    // things about whether the model is working at all.
    int speechHist[10] = {}, silenceHist[10] = {};
    for (size_t f = 0; f < numFrames; ++f)
    {
        if (! measurable[f]) continue;
        auto bucket = std::min(9, std::max(0, (int) (vad[f] * 10.0f)));
        (isSpeech[f] ? speechHist : silenceHist)[bucket]++;
    }
    std::printf("  vad     0.0  0.1  0.2  0.3  0.4  0.5  0.6  0.7  0.8  0.9\n");
    std::printf("  speech ");
    for (int b = 0; b < 10; ++b) std::printf("%4.0f%%", 100.0 * speechHist[b] / (double) vadSpeechCount);
    std::printf("\n  silent ");
    for (int b = 0; b < 10; ++b) std::printf("%4.0f%%", 100.0 * silenceHist[b] / (double) vadSilenceCount);
    std::printf("\n\n");

    auto perFrameMs = processingMs / (double) numFrames;
    std::printf("cost\n");
    std::printf("  %.3f ms per 10.0 ms frame (%.0fx realtime, %.1f%% of one core)\n\n",
                 perFrameMs, 10.0 / perFrameMs, 100.0 * perFrameMs / 10.0);

    std::printf("wrote mic-spike-clean.wav, mic-spike-noisy.wav, mic-spike-denoised.wav\n");
    return 0;
}
