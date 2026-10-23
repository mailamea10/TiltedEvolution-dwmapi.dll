// MessageRelay.h - Channel-based forwarded messaging (Issue #2 fix)
#pragma once

#include <cstdint>
#include "Structs/ActorComponent.h"

namespace TiltedPhoques {
    namespace RemoteMessaging {
        
        // Forwarded message event for channel-based relay (no storage, just forward-and-forget)
        struct RelayMessageEvent final : public entt::event {
            uint32_t channelId = 0;
            const void* payload = nullptr;
            uint32_t size = 0;
            uint64_t actorId = 0; // For targeting specific actors
            
            bool operator==(const RelayMessageEvent& rhs) const noexcept {
                return channelId == rhs.channelId && 
                       size == rhs.size &&
                       actorId == rhs.actorId;
            }
        };
        
        struct UpdateData final : public entt::event {
            uint32_t startId = 0;
            StringCacheUpdate cacheUpdate{};
            
            bool operator==(const UpdateData& rhs) const noexcept {
                return startId == rhs.startId && 
                       cacheUpdate == rhs.cacheUpdate;
            }
        };

    } // namespace RemoteMessaging
    
    using RemoteMessaging::RelayMessageEvent;
    using RemoteMessaging::UpdateData;

} // namespace TiltedPhoques
