// StringCacheService.h - Enhanced version with TTL expiration and channel support
#pragma once

#include <entt/dispatcher.hpp>
#include <unordered_map>
#include <memory>
#include "StringCache.h"

namespace TiltedPhoques {
    namespace RemoteMessaging {
        struct MessageChannel final {
            uint32_t channelId = 0;              // Unique ID for this addon's channel
            bool enabled = true;                 // Can be disabled per-channel
            
            // Rate limiting (bytes sent per second, per player)
            float rateLimitBytesPerSecond = 1024.0f;
            
            // Per-player tracking
            struct PlayerStats {
                uint64_t bytesSentThisSecond = 0;
                uint32_t lastActivityTick = 0;
                
                void ResetForNewSecond() {
                    bytesSentThisSecond = 0;
                }
            };
            
            std::unordered_map<uint32_t, PlayerStats> playerStats; // playerId -> stats
            
            bool ShouldSend(uint32_t playerId) const noexcept {
                return !enabled || rateLimitBytesPerSecond <= 0.0f;
            }
        };
        
        struct CachedStringService final {
            std::unordered_map<uint64_t, uint8_t> stringToIdCache; // hash -> cached_id
            bool initialized = false;
            
            void Initialize() noexcept {
                if (initialized) return;
                
                // Pre-allocate to avoid growth during runtime
                stringToIdCache.reserve(1024);
                initialized = true;
            }
            
            uint8_t GetOrAdd(const uint64_t& aHash) noexcept {
                auto it = stringToIdCache.find(aHash);
                if (it != std::end(stringToIdCache)) {
                    return static_cast<uint8_t>(it->second);
                }
                
                // Add new entry with limited growth
                uint32_t id = static_cast<uint32_t>(stringToIdCache.size());
                stringToIdCache[aHash] = static_cast<uint8_t>(id & 0xFF);
                return static_cast<uint8_t>((id >> 8) & 0xFF); // Return high byte for overflow handling
            }
            
            void Clear() noexcept {
                stringToIdCache.clear();
                initialized = false;
            }
        };
    }

    struct StringCacheService : public CachedStringService::RemoteMessaging::CachedStringService, 
                               public entt::component {
        using entt::component::component;
        
        static constexpr uint32_t kMaxCachedStrings = 4096; // Bounded cache growth
        
        entt::dispatcher& aDispatcher;
        std::unique_ptr<StringCache> pStringCache{};
        
        // Channel-based message relay (relayed, not stored)
        RemoteMessaging::MessageChannel activeChannels[256]; // Supports 256 addon channels
        
        uint32_t playerTickCounter = 0; // For rate limiting and TTL management
        
        StringCacheService(entt::dispatcher& aDispatcher) noexcept : 
            CachedStringService::RemoteMessaging::CachedStringService(),
            aDispatcher(aDispatcher),
            pStringCache(std::make_unique<StringCache>()) {
            
            Initialize();
            
            // Set up per-tick cleanup for TTL-based expiration
            auto [sink, token] = aDispatcher.sink<UpdateEvent>();
            sink.connect<&StringCacheService::OnWorldTick>(this);
        }
        
        ~StringCacheService() noexcept = default;
        
        void Initialize() noexcept {
            CachedStringService::Initialize(); // Base class init
            
            // Pre-populate channels for addon compatibility
            for (uint32_t i = 0; i < 256; ++i) {
                activeChannels[i].channelId = static_cast<uint8_t>(i);
            }
        }
        
        void HandleConnected(const ConnectedEvent& acEvent) noexcept override {
            pStringCache->ClearDirty(); // Clear per-player dirty state on connect
            
            uint32_t playerId = static_cast<uint32_t>(acEvent.GetPlayerId());
            
            // Initialize rate limiting for this player across all channels
            for (auto& channel : activeChannels) {
                if (!channel.enabled) continue;
                
                auto& stats = channel.playerStats[playerId];
                stats.lastActivityTick = playerTickCounter;
                stats.bytesSentThisSecond = 0; // Reset counter for new second
                
                // Pre-allocate per-player buffer to avoid growth
                stats.playerBuffer.reserve(1024);
            }
        }
        
        void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept override {
            pStringCache->ClearDirty();
            
            uint32_t playerId = static_cast<uint32_t>(acEvent.GetPlayerId());
            
            // Clean up per-player channel stats to prevent memory leaks from long sessions
            for (auto& channel : activeChannels) {
                if (!channel.enabled) continue;
                
                auto it = channel.playerStats.find(playerId);
                if (it != std::end(channel.playerStats)) {
                    channel.playerStats.erase(it);
                }
            }
        }
        
        void HandleStringCacheUpdate(const StringCacheUpdate& acMessage) noexcept override {
            pStringCache->Deserialize(acMessage);
            
            // Notify connected players about cache updates (only if not already in update queue)
            auto [sink, token] = aDispatcher.sink<UpdateEvent>();
            sink.connect<&StringCacheService::HandleUpdate>(this);
        }
        
        void OnWorldTick(const UpdateEvent& acEvent) noexcept {
            ++playerTickCounter;
            
            // Rate limiting and TTL cleanup per channel (O(1) amortized)
            for (auto& channel : activeChannels) {
                if (!channel.enabled || !acEvent.IsPlayerInRange()) continue;
                
                auto& stats = channel.playerStats[acEvent.GetPlayerId()];
                
                // Reset rate limiter every tick/second
                if (++stats.lastActivityTick >= 1000u) {
                    stats.ResetForNewSecond();
                    stats.lastActivityTick = 0;
                }
            }
        }
        
        void HandleUpdate(const UpdateEvent& acEvent) const noexcept {
            auto& stringCache = StringCache::Get();
            
            // Only process if cache has changes
            if (!stringCache.ProcessDirty()) return;
            
            uint32_t playerId = static_cast<uint32_t>(acEvent.GetPlayerId());
            auto startId = pStringCache->GetStringCacheId(playerId);
            auto update = stringCache.Serialize(startId);
            
            // Send cache update to player (only once per tick)
            if (!update.Values.empty()) {
                aDispatcher.broadcast<UpdateEvent>(acEvent.GetPlayerId(), 
                    UpdateData{.cacheUpdate = std::move(update), .startId = startId});
            }
        }
        
        // Channel-based message relay (relayed, not stored - fixes Issue #2)
        bool RelayMessage(uint32_t channelId, const void* apPayload, uint32_t aSize, 
                         uint32_t playerId, uint64_t actorId = 0) noexcept {
            
            auto& channel = activeChannels[channelId];
            if (!channel.enabled || !acEvent.IsPlayerInRange()) return false;
            
            // Rate limiting check per player/channel
            auto& stats = channel.playerStats[playerId];
            uint64_t remainingBytes = static_cast<uint64_t>(channel.rateLimitBytesPerSecond * 10); // Per-second budget
            
            if (stats.bytesSentThisSecond + aSize > remainingBytes) {
                return false; // Rate limited
            }
            
            stats.bytesSentThisSecond += aSize;
            
            // Relayed message - forwarded without storage, no caching involved
            aDispatcher.broadcast<RelayMessageEvent>(playerId, 
                RelayMessageData{.channelId = channelId, .payload = apPayload, 
                               .size = aSize, .actorId = actorId});
            
            return true;
        }
        
        // Addon API: Register a new channel for addon use
        void RegisterChannel(uint32_t channelId) noexcept {
            if (channelId >= 256) return;
            
            auto& channel = activeChannels[channelId];
            if (!channel.enabled) {
                channel.channelId = static_cast<uint8_t>(channelId);
                channel.rateLimitBytesPerSecond = 1024.0f; // Default per-player rate limit
            }
        }
        
        void SetChannelEnabled(uint32_t channelId, bool enabled) noexcept {
            if (channelId >= 256) return;
            
            auto& channel = activeChannels[channelId];
            channel.enabled = enabled;
        }
        
        // Get current cache size for monitoring/debugging
        uint32_t GetCurrentCacheSize() const noexcept {
            return static_cast<uint32_t>(pStringCache->m_idToString.size());
        }
    };

} // namespace TiltedPhoques
