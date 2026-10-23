// MessageRelay.cpp - Channel-based message forwarding implementation
#pragma once

#include "MessageRelay.h"
#include <spdlog/spdlog.h>

namespace TiltedPhoques {
    namespace RemoteMessaging {
        
        void InitializeChannel(uint32_t channelId, float rateLimitBytesPerSecond) noexcept {
            // Safe to call from addon initialization code
            if (channelId >= 256) return;
            
            auto& channel = TiltedPhoques::StringCacheService::GetActiveChannels()[channelId];
            channel.rateLimitBytesPerSecond = rateLimitBytesPerSecond;
        }

    } // namespace RemoteMessaging
    
} // namespace TiltedPhoques
