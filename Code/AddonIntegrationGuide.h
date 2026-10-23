// AddonIntegrationGuide.h - How to use the new channel-based messaging (Issue #2 fix)
#pragma once

#include "MessageRelay.h"
#include <spdlog/spdlog.h>

namespace TiltedPhoques {
    namespace RemoteMessaging {
        
        /**
         * @brief Initialize your addon's message channels during startup.
         * 
         * Call this in your addon initialization function, before any messages are sent.
         */
        void AddonMessageSystem::Initialize() noexcept {
            // Register the main sync channel (ragdoll/pose updates)
            StringCacheService::RegisterChannel(1, 2048.0f); // 2KB/s per player
            
            // Register lip-sync channel (small, frequent messages)
            StringCacheService::RegisterChannel(2, 512.0f);   // 512B/s per player
            
            // Register race menu channel (reliable updates only)
            StringCacheService::RegisterChannel(3, 1024.0f);  // 1KB/s per player
        }

    } // namespace RemoteMessaging
    
} // namespace TiltedPhoques
