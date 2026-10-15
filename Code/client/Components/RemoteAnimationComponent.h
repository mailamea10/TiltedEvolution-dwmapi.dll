#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

struct RemoteAnimationComponent
{
    List<ActionEvent> TimePoints;
    ActionEvent LastRanAction;
    ActionEvent LastProcessedAction;
    uint32_t ReplayCount;
    bool ResetAnimationGraphForReplay{false};

    // A magic hand action the graph refused is retried for a short while instead of dropped
    // (see AnimationSystem::Update). Start of the current retry window, 0 when not retrying.
    uint64_t MagicRetryStartMs{0};
    uint32_t MagicRetryAttempts{0};
};
