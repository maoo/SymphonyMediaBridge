#pragma once
#include "codec/OpusEncoder.h"
#include "memory/PacketPoolAllocator.h"
#include <cstdint>

namespace emulator
{
enum Audio
{
    None = 0,
    Opus,
    Fake,
    Muted
};
class AudioSource
{
public:
    enum IsPttState
    {
        NotSpecified = 0,
        Unset,
        Set
    };

    AudioSource(memory::PacketPoolAllocator& allocator, uint32_t ssrc, Audio fakeAudio, uint32_t ptime = 20);
    ~AudioSource(){};
    memory::UniquePacket getPacket(uint64_t timestamp);
    int64_t timeToRelease(uint64_t timestamp) const;

    memory::PacketPoolAllocator& getAllocator() { return _allocator; }

    uint32_t getSsrc() const { return _ssrc; }

    void setVolume(double normalized) { _amplitude = 15000 * normalized; }
    void setFrequency(double frequency) { _frequency = frequency; }
    void setIsPtt(const IsPttState isPtt);
    void setUseAudioLevel(const bool useAudioLevel);

private:
    static const uint32_t maxSentBufferSize = 12 * 1024;
    uint32_t _ssrc;
    codec::OpusEncoder _encoder;
    uint64_t _nextRelease;
    memory::PacketPoolAllocator& _allocator;
    double _phase;
    uint32_t _rtpTimestamp;
    uint16_t _sequenceCounter;
    uint16_t _amplitude;
    double _frequency;
    uint32_t _ptime;
    IsPttState _isPtt;
    bool _useAudioLevel;
    Audio _fakeAudio;
};

} // namespace emulator
