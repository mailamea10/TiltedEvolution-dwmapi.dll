
#include <base/threading/ThreadUtils.h>

#include <Games/GamePatch.h>

namespace
{
struct BSTaskletManager
{
    char pad0[0x30];
    void* threadHandles[6];
};

static void (*Construct_TaskletManager)(BSTaskletManager*);
// 1.5.97's constructor also takes the thread count (6) in edx.
static void (*Construct_TaskletManagerLegacy)(BSTaskletManager*, uint32_t);

static void NameTaskletThreads(BSTaskletManager* apSelf)
{
    for (int i = 0; i < 6; i++)
    {
        if (!apSelf->threadHandles[i])
            continue;

        auto name = fmt::format("TaskletThread{}", i);
        Base::SetThreadName(apSelf->threadHandles[i], name.c_str());
    }
}

static void Hook_Construct_TaskletManagerLegacy(BSTaskletManager* apSelf, uint32_t aThreadCount)
{
    Construct_TaskletManagerLegacy(apSelf, aThreadCount);
    NameTaskletThreads(apSelf);
}

static void Hook_Construct_TaskletManager(BSTaskletManager* apSelf)
{
    Construct_TaskletManager(apSelf);
    NameTaskletThreads(apSelf);
}
} // namespace

static TiltedPhoques::Initializer s_BSThreadInit(
    []()
    {
        // tasklet naming
        if (VersionDb::Get().IsLegacyFormat())
        {
            // No 1.5.x entry for the instance getter (AE 69554). On 1.5.97 the
            // constructor (RVA 0xC3AFB0) is called once, from RVA 0xC399A6, with the
            // thread handles at +0x30 as on 1.6.
            GamePatch::SwapCall(GamePatch::AtLegacyRva(0xC399A6, {0xE8, 0x05, 0x16, 0x00, 0x00}, "1.5.97", "tasklet thread names"),
                                Construct_TaskletManagerLegacy, &Hook_Construct_TaskletManagerLegacy,
                                "tasklet thread names");
        }
        else if (auto* pGetTaskletManagerInstance = GamePatch::Anchor(69554, "tasklet thread names"))
        {
            GamePatch::SwapCall(GamePatch::At(pGetTaskletManagerInstance, {0x63}, "tasklet thread names"),
                                Construct_TaskletManager, &Hook_Construct_TaskletManager,
                                "tasklet thread names");
        }
    });
