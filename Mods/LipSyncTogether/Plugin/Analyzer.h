#pragma once

#include "Common.h"

#include <cmath>
#include <vector>

namespace LipSync
{
// Second-order IIR section (RBJ cookbook), transposed direct form II.
struct Biquad
{
    float b0{}, b1{}, b2{}, a1{}, a2{};
    float z1{}, z2{};
    bool Enabled{false};

    float Run(float aIn) noexcept
    {
        const float cOut = b0 * aIn + z1;
        z1 = b1 * aIn - a1 * cOut + z2;
        z2 = b2 * aIn - a2 * cOut;
        return cOut;
    }

    static Biquad BandPass(float aRate, float aCenter, float aQ) noexcept
    {
        Biquad f;
        if (aCenter >= aRate * 0.45f)
            return f;
        const float cW = 2.f * 3.14159265f * aCenter / aRate;
        const float cAlpha = std::sin(cW) / (2.f * aQ);
        const float cA0 = 1.f + cAlpha;
        f.b0 = cAlpha / cA0;
        f.b1 = 0.f;
        f.b2 = -cAlpha / cA0;
        f.a1 = -2.f * std::cos(cW) / cA0;
        f.a2 = (1.f - cAlpha) / cA0;
        f.Enabled = true;
        return f;
    }

    static Biquad LowPass(float aRate, float aCutoff) noexcept
    {
        Biquad f;
        const float cW = 2.f * 3.14159265f * aCutoff / aRate;
        const float cAlpha = std::sin(cW) / (2.f * 0.7071f);
        const float cCos = std::cos(cW);
        const float cA0 = 1.f + cAlpha;
        f.b0 = (1.f - cCos) / 2.f / cA0;
        f.b1 = (1.f - cCos) / cA0;
        f.b2 = (1.f - cCos) / 2.f / cA0;
        f.a1 = -2.f * cCos / cA0;
        f.a2 = (1.f - cAlpha) / cA0;
        f.Enabled = true;
        return f;
    }

    static Biquad HighPass(float aRate, float aCutoff) noexcept
    {
        Biquad f;
        const float cW = 2.f * 3.14159265f * aCutoff / aRate;
        const float cAlpha = std::sin(cW) / (2.f * 0.7071f);
        const float cCos = std::cos(cW);
        const float cA0 = 1.f + cAlpha;
        f.b0 = (1.f + cCos) / 2.f / cA0;
        f.b1 = -(1.f + cCos) / cA0;
        f.b2 = (1.f + cCos) / 2.f / cA0;
        f.a1 = -2.f * cCos / cA0;
        f.a2 = (1.f - cAlpha) / cA0;
        f.Enabled = true;
        return f;
    }
};

// Turns mono samples into mouth shapes, one block (16 ms) at a time.
//
// Speech is told apart from other sounds by its pitch: a voiced sound repeats every 2.5 to 12.5 ms (80 to 400 Hz),
// which keyboard clicks, fans and most room noise do not. With NoiseFilter on, the gate only opens after voicing
// above the noise in two of the last three blocks, and stays open through the unvoiced consonants that follow
// for HoldMs. The jaw follows loudness against the speaker's own recent peak, so it opens and closes with the
// syllables whatever the microphone's gain, and the lip shapes follow how each vowel's spectrum differs from the
// speaker's own average, so an ordinary voice reads as neutral speech rather than as one fixed vowel.
//
// The local microphone's analyzer also handles push-to-talk, the mute key, the digital silence warning and level
// logging; the ones run on other players' received voices (aLocal false) do not.
class Analyzer
{
public:
    static constexpr float kBlockSeconds = 0.016f;
    static constexpr float kPitchSeconds = 0.032f;

    Analyzer(float aRate, bool aLocal)
        : m_rate(aRate)
        , m_local(aLocal)
        , m_blockSamples(std::max<uint32_t>(64, static_cast<uint32_t>(aRate * kBlockSeconds)))
    {
        m_highPass = Biquad::HighPass(aRate, 80.f);
        // First formant / back-vowel second formant, mid, front-vowel second formant, and fricative hiss.
        m_bands[0] = Biquad::BandPass(aRate, 550.f, 0.8f);
        m_bands[1] = Biquad::BandPass(aRate, 1350.f, 1.6f);
        m_bands[2] = Biquad::BandPass(aRate, 2500.f, 1.6f);
        m_bands[3] = Biquad::BandPass(aRate, 5500.f, 1.0f);

        // Pitch is looked for in the voice brought down to about 8 kHz.
        m_decimation = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(aRate / 8000.f)));
        m_pitchRate = aRate / m_decimation;
        m_antiAlias = Biquad::LowPass(aRate, 3400.f);
        m_minLag = std::max<uint32_t>(2, static_cast<uint32_t>(m_pitchRate / 400.f));
        m_maxLag = static_cast<uint32_t>(m_pitchRate / 80.f);
        m_window = static_cast<uint32_t>(m_pitchRate * kPitchSeconds);
        m_history.assign(m_window + m_maxLag, 0.f);
        m_linear.assign(m_history.size(), 0.f);
    }

    // Returns true when the sample completed a block, so Current() has a new mouth.
    bool Push(float aSample) noexcept
    {
        const float cHigh = m_highPass.Run(aSample);
        m_sumSquares += cHigh * cHigh;
        for (int b = 0; b < 4; ++b)
        {
            if (m_bands[b].Enabled)
            {
                const float cBand = m_bands[b].Run(cHigh);
                m_bandEnergy[b] += cBand * cBand;
            }
        }
        const bool cPositive = cHigh >= 0.f;
        if (cPositive != m_lastPositive && std::fabs(cHigh) > 1e-4f)
            ++m_crossings;
        m_lastPositive = cPositive;

        m_decimationSum += m_antiAlias.Run(cHigh);
        if (++m_decimationCount >= m_decimation)
        {
            m_history[m_historyPos] = m_decimationSum / m_decimation;
            m_historyPos = (m_historyPos + 1) % m_history.size();
            m_decimationSum = 0.f;
            m_decimationCount = 0;
        }

        if (++m_count < m_blockSamples)
            return false;
        FinishBlock();
        return true;
    }

    // No audio arrived for a while (device paused or gone, or a voice stopped): let the mouth close.
    void Silence(float aSeconds) noexcept
    {
        m_gateOpen = false;
        m_hold = 0.f;
        m_voicedHistory = 0;
        Smooth(STRLipSync::Frame{}, aSeconds);
        m_current.Speaking = 0;
        ++m_current.Sequence;
    }

    const STRLipSync::Frame& Current() const noexcept { return m_current; }

    // How far the voice carries right now, as a multiple of the normal range: above 1 while yelling, below 1 while
    // whispering (always 1 with AutoRange off).
    float RangeScale() const noexcept { return g_config.AutoRange ? m_range : 1.f; }

    // How much of the speaker's normal voice has been learned (1 = done), for the settings window.
    float Calibration() const noexcept { return Clamp01(m_calibration / kCalibrationSeconds); }
    static constexpr float kCalibrationSeconds = 10.f;

    // The last block's readings, for the settings window's meter.
    STRLipSync::Meter Meter() const noexcept
    {
        return STRLipSync::Meter{m_lastLevel, m_floor, m_lastThreshold, m_lastVoicing, m_current.Open, RangeScale(), m_current.Speaking,
                                 m_current.Speaking && m_whispering ? 1u : 0u, m_current.Sequence, Calibration()};
    }

private:
    static uint32_t CountBits(uint32_t aBits) noexcept
    {
        uint32_t count = 0;
        for (; aBits; aBits &= aBits - 1)
            ++count;
        return count;
    }

    // How far the voice carries: louder than the speaker's normal voice reaches further, quieter or whispered less
    // far. Loudness is the running average of syllable peaks (the loudest block of each quarter second of voiced
    // speech): quiet dips that fall under the gate would bias an average, peaks move exactly with the voice. "Normal" is
    // the long-term average of that, learned over the first 10 seconds of talking and slowly after; once learned,
    // only speech near normal updates it, so yelling or whispering for a while does not drag it along. Nothing
    // counts as a yell or a whisper until it is learned (breathy whispers excepted).
    void UpdateRange(bool aSpeaking, bool aVoiced, float aLevel, float aDt) noexcept
    {
        bool windowDone = false;
        if (aSpeaking && aVoiced)
        {
            m_windowPeak = std::max(m_windowPeak, aLevel);
            m_windowVoiced = true;
        }
        if (++m_windowBlocks >= 15) // 240 ms
        {
            if (m_windowVoiced)
            {
                if (m_loudHold < -99.f)
                    m_loudHold = m_windowPeak;
                m_loudHold += (m_windowPeak - m_loudHold) * 0.5f;
                windowDone = true;
            }
            m_windowBlocks = 0;
            m_windowPeak = -100.f;
            m_windowVoiced = false;
        }
        if (windowDone)
        {
            const float cWindowDt = 15 * kBlockSeconds; // the baseline learns once per window

            if (!m_haveBaseline)
            {
                m_baseline = m_loudHold;
                m_haveBaseline = true;
            }
            const bool cLearning = m_calibration < kCalibrationSeconds;
            const float cRelative = m_loudHold - m_baseline;
            if (cLearning || std::fabs(cRelative) < 0.5f * std::min(g_config.YellDb, g_config.WhisperDb))
            {
                const float cTau = cLearning ? std::max(1.f, m_calibration) : 60.f;
                m_baseline += cRelative * std::min(1.f, cWindowDt / cTau);
            }
            m_calibration += cWindowDt;
        }

        float target = m_range > 1.f ? 1.f : m_range; // between words: let a yell settle, keep a whisper
        if (aSpeaking && m_whispering)
            target = g_config.WhisperRange;
        else if (aSpeaking && m_calibration >= kCalibrationSeconds)
        {
            const float cRelative = m_loudHold - m_baseline;
            if (cRelative > 0.f)
                target = 1.f + (g_config.YellRange - 1.f) * Ramp(cRelative, 0.5f * g_config.YellDb, g_config.YellDb);
            else
                target = 1.f + (g_config.WhisperRange - 1.f) * Ramp(-cRelative, 0.5f * g_config.WhisperDb, g_config.WhisperDb);
        }
        else if (!aSpeaking)
            target = 1.f;

        // Widen at once (a shout carries from its first word), narrow into a whisper quickly, and let a yell
        // settle over half a second or so, so it does not collapse between shouted words.
        const float cTau = target > m_range ? 0.1f : (m_range > 1.f ? 0.6f : 0.3f);
        m_range += (target - m_range) * std::min(1.f, aDt / cTau);

        if (m_local && g_config.LogLevels && g_config.AutoRange)
        {
            const int cMode = m_range > 1.5f ? 2 : (m_range < 0.5f ? 0 : 1);
            if (cMode != m_loggedMode)
            {
                m_loggedMode = cMode;
                static const char* const kModes[] = {"whispering", "normal", "yelling"};
                Log("Voice range: %s (x%.2f; %.1f dB against your normal voice)", kModes[cMode], m_range, m_loudHold - m_baseline);
            }
        }
    }

    // How strongly the last 32 ms repeat at some pitch between 80 and 400 Hz: about 0.8 or more for a held vowel,
    // well under 0.4 for clicks, hiss and fan noise.
    float Voicing() noexcept
    {
        const size_t cSize = m_history.size();
        for (size_t i = 0; i < cSize; ++i)
            m_linear[i] = m_history[(m_historyPos + i) % cSize];

        const float* pNow = m_linear.data() + m_maxLag;
        float energyNow = 0.f;
        for (uint32_t n = 0; n < m_window; ++n)
            energyNow += pNow[n] * pNow[n];
        if (energyNow < 1e-9f)
            return 0.f;

        float best = 0.f;
        for (uint32_t lag = m_minLag; lag <= m_maxLag; ++lag)
        {
            const float* pThen = pNow - lag;
            float cross = 0.f;
            float energyThen = 0.f;
            for (uint32_t n = 0; n < m_window; ++n)
            {
                cross += pNow[n] * pThen[n];
                energyThen += pThen[n] * pThen[n];
            }
            best = std::max(best, cross / std::sqrt(energyNow * energyThen + 1e-12f));
        }
        return Clamp01(best);
    }

    void FinishBlock() noexcept
    {
        const float cCount = static_cast<float>(m_count);
        const bool cDigitalSilence = m_sumSquares == 0.f;
        const float cLevel = 10.f * std::log10(m_sumSquares / cCount + 1e-12f);
        const float cZeroCrossingRate = m_crossings / cCount * m_rate;
        const float cVoicing = Voicing();

        float energy[4];
        float total = 1e-12f;
        for (int b = 0; b < 4; ++b)
        {
            energy[b] = m_bandEnergy[b];
            total += energy[b];
        }

        m_count = 0;
        m_crossings = 0;
        m_sumSquares = 0.f;
        std::fill(std::begin(m_bandEnergy), std::end(m_bandEnergy), 0.f);

        const float cDt = kBlockSeconds;

        // Exact zeros are not a quiet room: the device is muted, or a virtual one with nothing routed into it.
        if (m_local && !m_reportedSilence)
        {
            m_digitalSilence = cDigitalSilence ? m_digitalSilence + cDt : 0.f;
            if (m_digitalSilence >= 5.f)
            {
                m_reportedSilence = true;
                Log("This microphone has sent nothing but digital silence for 5 seconds: it is muted, or it is a virtual "
                    "audio device with nothing routed into it. Set Device= in "
                    "LipSyncTogether.ini to part of another name from the list above, then restart the game.");
            }
        }

        // Background noise: follows a quieter room quickly, a louder one slowly (barely at all while speaking),
        // so a long sentence does not teach the gate that speech is noise.
        if (!m_floorReady)
        {
            m_floor = cLevel;
            m_floorReady = true;
        }
        else if (cLevel < m_floor)
            m_floor += (cLevel - m_floor) * std::min(1.f, cDt / 0.3f);
        else
            m_floor += (m_gateOpen ? 0.2f : 1.5f) * cDt;
        m_floor = std::clamp(m_floor, -100.f, -25.f);

        const float cThreshold = std::max(m_floor + g_config.GateDb, g_config.MinLevelDb);
        const bool cLoudEnough = cLevel > cThreshold;
        m_lastLevel = cLevel;
        m_lastThreshold = cThreshold;
        m_lastVoicing = cVoicing;
        const bool cVoiced = cLoudEnough && cVoicing >= g_config.VoiceThreshold;

        if (g_config.NoiseFilter)
        {
            m_voicedHistory = ((m_voicedHistory << 1) | (cVoiced ? 1u : 0u)) & 0x7u;
            const uint32_t cVoicedBlocks = (m_voicedHistory & 1u) + ((m_voicedHistory >> 1) & 1u) + ((m_voicedHistory >> 2) & 1u);
            if (cVoicedBlocks >= 2)
            {
                m_gateOpen = true;
                m_hold = g_config.HoldSeconds;
            }
            else if (m_gateOpen)
            {
                m_hold -= cDt;
                if (m_hold <= 0.f)
                    m_gateOpen = false;
            }
        }
        else if (cLoudEnough)
        {
            m_gateOpen = true;
            m_hold = g_config.HoldSeconds;
        }
        else if (cLevel < cThreshold - 3.f)
        {
            m_hold -= cDt;
            if (m_hold <= 0.f)
                m_gateOpen = false;
        }

        // Breathy whispers have no pitch, so the voice filter above never opens for them. A whisper is steady breathy
        // sound over the noise for most of a third of a second; clicks are too short, and fans too steady (the noise
        // floor follows them), to look like one.
        const bool cBreathy = cLevel > cThreshold - 3.f && cVoicing < g_config.VoiceThreshold && (energy[1] + energy[2] + energy[3]) / total >= 0.6f;
        m_breathyHistory = ((m_breathyHistory << 1) | (cBreathy ? 1u : 0u)) & 0xFFFFFu;
        const bool cWhisper = g_config.UnvoicedWhisper && g_config.NoiseFilter && !cVoiced && CountBits(m_breathyHistory) >= 14;
        if (cWhisper)
        {
            m_gateOpen = true;
            m_hold = g_config.HoldSeconds;
        }
        if (cVoiced)
            m_whispering = false;
        else if (cWhisper)
            m_whispering = true;
        else if (!m_gateOpen)
            m_whispering = false;

        bool talkKey = true;
        if (m_local)
        {
            if (g_config.MuteToggleKey)
            {
                const bool cDown = KeyDown(g_config.MuteToggleKey);
                if (cDown && !m_muteKeyDown)
                {
                    m_muted = !m_muted;
                    Log("Microphone %s", m_muted ? "muted (the mouth stays still, nothing is sent)" : "unmuted");
                }
                m_muteKeyDown = cDown;
            }
            talkKey = !g_config.PushToTalkKey || KeyDown(g_config.PushToTalkKey);
        }
        const bool cSpeaking = m_gateOpen && talkKey && !m_muted;

        // The speaker's recent peak: rises at once, sinks 4 dB a second, and stays at least 12 dB over the gate.
        const float cBase = cThreshold - 3.f;
        m_peak = std::max(m_peak - 4.f * cDt, cBase + 12.f);
        if (cSpeaking && cVoiced)
            m_peak = std::max(m_peak, cLevel);
        UpdateRange(cSpeaking, cVoiced, cLevel, cDt);

        STRLipSync::Frame target{};
        if (cSpeaking)
        {
            const float cLoud = Ramp(cLevel, cBase, m_peak);
            float open = Clamp01(std::pow(cLoud, 1.4f) * g_config.Sensitivity);
            // Consonants without voice ("s", "f", "t") barely move the jaw.
            if (!cVoiced)
                open *= 0.6f;

            if (g_config.ShapesEnabled)
            {
                const float cVowelEnergy = energy[0] + energy[1] + energy[2];
                if (cVoiced && cVowelEnergy > 1e-12f)
                {
                    // Where the vowel's energy sits (log2 Hz), against the speaker's own average.
                    const float cCentre = std::log2((energy[0] * 550.f + energy[1] * 1350.f + energy[2] * 2500.f) / cVowelEnergy);
                    if (!m_haveCentre)
                    {
                        m_centre = cCentre;
                        m_haveCentre = true;
                    }
                    else
                        m_centre += (cCentre - m_centre) * std::min(1.f, cDt / 3.f);

                    const float cDeviation = cCentre - m_centre;
                    target.Wide = Ramp(cDeviation, 0.12f, 0.45f);   // brighter than usual: "ee", "i"
                    target.Round = Ramp(-cDeviation, 0.12f, 0.45f); // darker than usual: "oo", "o"
                }
                else if (!cVoiced)
                {
                    // Hiss with many zero crossings: "s", "f", "sh" - teeth together.
                    target.Teeth = Ramp(energy[3] / total, 0.3f, 0.6f) * Ramp(cZeroCrossingRate, 2000.f, 4000.f);
                }

                // Dips between syllables close the lips a little, as speech does.
                target.Lips = 0.5f * Ramp(0.15f - cLoud, 0.f, 0.15f);
                open *= (1.f - 0.6f * target.Lips) * (1.f - 0.5f * target.Teeth);
            }
            target.Open = open;
        }

        Smooth(target, cDt);
        m_current.Speaking = cSpeaking ? 1 : 0;
        ++m_current.Sequence;

        if (m_local && g_config.LogLevels)
        {
            m_logTimer -= cDt;
            if (m_logTimer <= 0.f)
            {
                m_logTimer = 1.f;
                Log("level %.1f dB, noise %.1f dB, gate %.1f dB, voicing %.2f, %s | open %.2f wide %.2f round %.2f lips %.2f teeth %.2f | range x%.2f",
                    cLevel, m_floor, cThreshold, cVoicing,
                    cSpeaking ? (m_whispering ? "whispering" : "speaking")
                              : (m_muted ? "muted" : (m_gateOpen ? "waiting for push-to-talk" : (cLoudEnough ? "noise (not a voice)" : "quiet"))),
                    m_current.Open, m_current.Wide, m_current.Round, m_current.Lips, m_current.Teeth, RangeScale());
            }
        }
    }

    // Opens quickly, closes a little slower, so syllables read without jitter.
    void Smooth(const STRLipSync::Frame& acTarget, float aDt) noexcept
    {
        const float cAttack = 1.f - std::exp(-aDt / 0.025f);
        const float cRelease = 1.f - std::exp(-aDt / 0.06f);
        auto step = [&](float& aValue, float aTarget) {
            aValue += (aTarget - aValue) * (aTarget > aValue ? cAttack : cRelease);
            if (aValue < 0.005f)
                aValue = 0.f;
        };
        step(m_current.Open, acTarget.Open);
        step(m_current.Wide, acTarget.Wide);
        step(m_current.Round, acTarget.Round);
        step(m_current.Lips, acTarget.Lips);
        step(m_current.Teeth, acTarget.Teeth);
    }

    float m_rate;
    bool m_local;
    uint32_t m_blockSamples;
    Biquad m_highPass;
    Biquad m_bands[4];

    uint32_t m_count{};
    uint32_t m_crossings{};
    bool m_lastPositive{};
    float m_sumSquares{};
    float m_bandEnergy[4]{};

    // Pitch detection.
    Biquad m_antiAlias;
    uint32_t m_decimation{1};
    float m_pitchRate{};
    uint32_t m_minLag{};
    uint32_t m_maxLag{};
    uint32_t m_window{};
    std::vector<float> m_history;
    std::vector<float> m_linear;
    size_t m_historyPos{};
    float m_decimationSum{};
    uint32_t m_decimationCount{};
    uint32_t m_voicedHistory{};

    float m_digitalSilence{};
    bool m_reportedSilence{};

    float m_lastLevel{-100.f};
    float m_lastThreshold{-55.f};
    float m_lastVoicing{};

    // Yell and whisper range.
    uint32_t m_breathyHistory{};
    bool m_whispering{};
    float m_loudHold{-100.f};
    float m_windowPeak{-100.f};
    uint32_t m_windowBlocks{};
    bool m_windowVoiced{};
    bool m_haveBaseline{};
    float m_baseline{};
    float m_calibration{};
    float m_range{1.f};
    int m_loggedMode{1};

    bool m_floorReady{};
    float m_floor{-60.f};
    float m_peak{-40.f};
    bool m_haveCentre{};
    float m_centre{};
    bool m_gateOpen{};
    float m_hold{};
    bool m_muted{};
    bool m_muteKeyDown{};
    float m_logTimer{};

    STRLipSync::Frame m_current{};
};
} // namespace LipSync
