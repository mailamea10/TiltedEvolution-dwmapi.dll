#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"
#include <Misc/GameVM.h>
#include <World.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

struct Main;

TP_THIS_FUNCTION(TVMUpdate, int, GameVM, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

// Counts consecutive vm ticks that reported the vm as inactive. See HookVMUpdate
// for why a long run of them is treated as a broken offset rather than an idle
// game.
static int s_inactiveFrames = 0;

// Tick instrumentation.
//
// Everything else in the client eventually lands in World::Update(), so when a
// session stalls the question is always the same one: did this tick reach us at
// all, and did it decide to run the update. Those two have very different fixes
// and no other evidence tells them apart, because nothing downstream prints
// either way.
static constexpr uint64_t kTickHeartbeatEvery = 600; // roughly ten seconds at 60fps

static std::atomic<uint64_t> s_ticks{0};
static std::atomic<uint64_t> s_updates{0};
static std::atomic<uint64_t> s_skipped{0};
static std::atomic<uint64_t> s_mainLoopTicks{0};

// Where World::Update() runs.
//
// Upstream runs it from the SkyrimVM::Update hook, which on 1.6 is called on the
// game's main thread. On 1.5.97 the vm tick runs on another thread, while the
// client (TransportService above all) treats the thread that built World - the
// main thread - as the only one allowed to dispatch server messages. So a vm tick
// that arrives off the main thread only asks for an update, and the next
// Main::Update (main thread, once per frame) runs it. The window timer in
// InputService stays as a fallback for when neither hook drives the update, and
// all paths share one lock so two updates never overlap.
static std::mutex s_worldUpdateLock;
static std::atomic<int64_t> s_lastDrivenUpdateMs{0};
static std::atomic<bool> s_updateRequested{false};
static std::atomic<std::thread::id> s_mainLoopThread{};
static constexpr int64_t kHookDrivingWindowMs = 250;

static int64_t NowMs() noexcept
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void RunDrivenUpdate()
{
    std::scoped_lock lock(s_worldUpdateLock);
    s_lastDrivenUpdateMs = NowMs();
    g_appInstance->Update();
}

static void RunUpdateFromVm()
{
    if (std::this_thread::get_id() == s_mainLoopThread.load())
        RunDrivenUpdate();
    else
        s_updateRequested = true;
}

// Called by the window-timer fallback. Returns false when a game hook drove an
// update recently or one is running right now.
bool TryRunWorldUpdateFromTimer()
{
    if (NowMs() - s_lastDrivenUpdateMs.load() < kHookDrivingWindowMs)
        return false;

    std::unique_lock lock(s_worldUpdateLock, std::try_to_lock);
    if (!lock.owns_lock())
        return false;

    World::Get().Update();
    return true;
}

struct RawOffset
{
    bool readable{false};
    int32_t value{};
};

// The two candidate offsets for SkyrimVM::inactive are read even when they are
// not the one this build uses, because the value sitting next door is exactly
// what decides whether the assumed offset is right. Reading past the object has
// to fail quietly rather than take the game down with it.
static RawOffset ReadInt32At(const void* acpBase, size_t aOffset) noexcept
{
    RawOffset probe{};

    __try
    {
        const uint8_t* pAddress = static_cast<const uint8_t*>(acpBase) + aOffset;
        int32_t value = 0;
        memcpy(&value, pAddress, sizeof(value));
        probe.readable = true;
        probe.value = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return probe;
}

static std::string DescribeOffset(const RawOffset& acProbe) noexcept
{
    return acProbe.readable ? std::to_string(acProbe.value) : std::string("<unreadable>");
}

int TP_MAKE_THISCALL(HookVMUpdate, GameVM, float a2)
{
    // inactive is how the game parks the vm (main menu, loading, pause). The
    // runner lives on this tick, so honouring the flag exactly is what makes the
    // client idle outside the game.
    //
    // The cost of being wrong about where the flag sits is not a bad frame, it
    // is a silent, total stop: World::Update() never runs, the task queue is
    // never drained, and every attempt to connect is queued and forgotten while
    // the overlay waits forever on "connecting". That exact failure shipped once
    // because 1.5.97 keeps inactive at 0x680 while the shared struct said 0x690.
    //
    // The flag is therefore treated as untrusted. A run of consecutive frames
    // that all report "inactive" is far past any real menu or load screen, so at
    // that point the tick is let through and the disagreement is logged instead
    // of being allowed to hang the session. A genuine idle state reports the same
    // flag on every frame anyway, so the recovery costs one extra Update() per
    // kInactiveFrameLimit frames and changes nothing else.
    static constexpr int kInactiveFrameLimit = 600;

    const uint64_t cTick = s_ticks.fetch_add(1) + 1;

    // Announce the very first tick and afterwards one in every
    // kTickHeartbeatEvery. Seeing the first and never another means the tick
    // stopped; seeing none means it never started and the hook is elsewhere.
    const bool cAnnounce = cTick == 1 || cTick % kTickHeartbeatEvery == 0;

    const RawOffset cAtLegacy = cAnnounce ? ReadInt32At(apThis, 0x680) : RawOffset{};
    const RawOffset cAtModern = cAnnounce ? ReadInt32At(apThis, 0x690) : RawOffset{};

    if (apThis->inactive == 0)
    {
        s_inactiveFrames = 0;
        ++s_updates;
        RunUpdateFromVm();
    }
    else if (++s_inactiveFrames >= kInactiveFrameLimit)
    {
        s_inactiveFrames = 0;

        static bool s_reported = false;
        if (!s_reported)
        {
            s_reported = true;
            spdlog::warn("SkyrimVM::inactive has read non-zero for {} consecutive vm ticks, which no menu or load "
                         "screen lasts; running the client update anyway. The offset this build assumes is {} and the "
                         "real one on this runtime differs, so the field is being read from the wrong place",
                         kInactiveFrameLimit, static_cast<int>(SkyrimVM::kInactiveOffset));
        }

        ++s_updates;
        RunUpdateFromVm();
    }
    else
    {
        ++s_skipped;
    }

    if (cAnnounce)
    {
        spdlog::debug("vm tick heartbeat: tick {}, client update ran {} times and was gated {} times, this build reads "
                     "inactive at {:#x}, where the value is {}; offset {:#x} holds {} and offset {:#x} holds {}",
                     cTick, s_updates.load(), s_skipped.load(), static_cast<int>(SkyrimVM::kInactiveOffset),
                     apThis->inactive, 0x680, DescribeOffset(cAtLegacy), 0x690, DescribeOffset(cAtModern));
    }

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
    // Runs on the game's main thread once per frame. Carries out the update a vm
    // tick asked for from another thread (see RunUpdateFromVm).
    s_mainLoopThread = std::this_thread::get_id();
    if (s_updateRequested.exchange(false))
        RunDrivenUpdate();

    const uint64_t cTick = s_mainLoopTicks.fetch_add(1) + 1;
    if (cTick == 1 || cTick % kTickHeartbeatEvery == 0)
    {
        spdlog::debug("main loop heartbeat: tick {}, vm ticks {} so far, client update ran {} times", cTick,
                     s_ticks.load(), s_updates.load());
    }

    return TiltedPhoques::ThisCall(MainLoop, apThis);
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(VMDestructor, apThis);
}

static TiltedPhoques::Initializer s_mainHooks(
    []()
    {
        POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564);
        POINTER_SKYRIMSE(TVMUpdate, cVMUpdate, 53926);
        POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412);

        VMUpdate = cVMUpdate.Get();
        MainLoop = cMainLoop.Get();
        VMDestructor = cVMDestructor.Get();

        TP_HOOK(&VMUpdate, HookVMUpdate);
        TP_HOOK(&MainLoop, HookMainLoop);
        TP_HOOK(&VMDestructor, HookVMDestructor);
    });

