#pragma once
#include "transport/Endpoint.h"
#include "transport/RtcSocket.h"
#include "transport/RtcePoll.h"

namespace transport
{
class UdpEndpoint : public Endpoint, public RtcePoll::IEventListener
{
public:
    virtual ~UdpEndpoint() {}

    // auxilary
    virtual bool openPort(uint16_t port) = 0;
    virtual bool isGood() const = 0;
    virtual EndpointMetrics getMetrics(uint64_t timestamp) const = 0;
};
} // namespace transport