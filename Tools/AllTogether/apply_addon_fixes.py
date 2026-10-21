#!/usr/bin/env python3
"""Apply small, bundle-specific fixes after the pinned add-on sources are installed."""

from pathlib import Path
import sys


def fix_racemenu_reconnect_resend(source: Path) -> None:
    path = source / "Code/client/Services/Generic/RaceMenuTogetherService.cpp"
    text = path.read_text(encoding="utf-8")
    old = '''void RaceMenuTogetherService::WatchForNewPlayers(double aDelta) noexcept
{
    bool newPlayer = false;
    auto view = m_world.view<PlayerComponent>();
    for (auto entity : view)
    {
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        if (cPlayerId == m_transport.GetLocalPlayerId())
            continue;
        if (m_knownPlayers.insert(cPlayerId).second)
            newPlayer = true;
    }
'''
    new = '''void RaceMenuTogetherService::WatchForNewPlayers(double aDelta) noexcept
{
    bool newPlayer = false;
    Set<uint32_t> currentPlayers;
    auto view = m_world.view<PlayerComponent>();
    for (auto entity : view)
    {
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        if (cPlayerId == m_transport.GetLocalPlayerId())
            continue;
        currentPlayers.insert(cPlayerId);
        if (!m_knownPlayers.contains(cPlayerId))
            newPlayer = true;
    }

    // A reconnect can reuse the same player id. Keep only players present in this snapshot so
    // the returning client triggers the same appearance resend as a first-time join.
    m_knownPlayers = std::move(currentPlayers);
'''
    if new in text:
        return
    if old not in text:
        raise RuntimeError(f"Could not find RaceMenu Together player-presence block in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_addon_fixes.py <STR source tree>")
    fix_racemenu_reconnect_resend(Path(sys.argv[1]).resolve())


if __name__ == "__main__":
    main()
