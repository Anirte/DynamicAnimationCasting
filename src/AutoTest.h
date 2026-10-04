#pragma once

// In-game self test for the HUD flash fix. Compiled only with -DDAC_AUTOTEST=ON; in normal builds
// everything below collapses to no-ops.
//
// The test build adds a console command, DynAnimTest, which teleports the player to QASmoke, spawns a few
// actors, runs three timed phases with built-in test triggers and reports how often the HUD would have
// flashed with the original logic versus how often it really flashed.

namespace Loki::AnimationCasting {
    struct CastTrigger;
}

namespace Loki::AutoTest {

#ifdef DAC_AUTOTEST
    // Registers the DynAnimTest console command.
    void Install();

    // True while a test run is in progress. Regular .toml triggers are skipped during a run.
    bool Active() noexcept;

    // The built-in trigger of the current phase for a_actor, or nullptr if this actor is not part of the phase.
    // It is evaluated for every animation event of that actor.
    AnimationCasting::CastTrigger* ActiveTrigger(const RE::Actor* a_actor) noexcept;

    // Called at the points where the ORIGINAL code flashed a HUD meter.
    void NoteLegacyFlash(const RE::Actor* a_actor) noexcept;

    // Called right before the game's FlashHUDMeter is really invoked.
    void NoteRealFlash() noexcept;

    // Called when the test trigger is evaluated, when a spell is really cast, and when magicka is deducted.
    void NoteInvocation() noexcept;
    void NoteCast() noexcept;
    void NoteMagickaSpent(float a_amount) noexcept;

    // Remembers which actor the current trigger evaluation is for.
    struct ActorScope {
        explicit ActorScope(const RE::Actor* a_actor) noexcept;
        ~ActorScope();
        const RE::Actor* previous;
    };
#else
    inline void Install() {}
    inline bool Active() noexcept { return false; }
    inline AnimationCasting::CastTrigger* ActiveTrigger(const RE::Actor*) noexcept { return nullptr; }
    inline void NoteLegacyFlash(const RE::Actor*) noexcept {}
    inline void NoteRealFlash() noexcept {}
    inline void NoteInvocation() noexcept {}
    inline void NoteCast() noexcept {}
    inline void NoteMagickaSpent(float) noexcept {}
    struct ActorScope {
        explicit ActorScope(const RE::Actor*) noexcept {}
    };
#endif

}  // namespace Loki::AutoTest
