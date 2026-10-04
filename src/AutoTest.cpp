#include "PCH.h"

#ifdef DAC_AUTOTEST

#include "DynamicAnimationCasting.h"
#include "AutoTest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace Loki::AutoTest {
    namespace {
        using namespace std::chrono_literals;
        using Trigger = AnimationCasting::CastTrigger;

        struct Counters {
            std::atomic<int> legacyNpc{0};     // original code would have flashed because of an NPC
            std::atomic<int> legacyPlayer{0};  // original code would have flashed because of the player
            std::atomic<int> realNpc{0};       // this build really flashed because of an NPC
            std::atomic<int> realPlayer{0};    // this build really flashed because of the player
            std::atomic<int> invocations{0};   // the test trigger was evaluated
            std::atomic<int> casts{0};         // a spell was really cast
            std::atomic<float> spent{0.f};     // magicka deducted by the plugin

            void Reset() noexcept {
                legacyNpc = 0;
                legacyPlayer = 0;
                realNpc = 0;
                realPlayer = 0;
                invocations = 0;
                casts = 0;
                spent = 0.f;
            }
        };

        Counters g_counters;
        std::atomic<Trigger*> g_trigger{nullptr};
        std::atomic<bool> g_running{false};
        std::atomic<bool> g_npcOnly{false};  // phase 1: the player's own animation events are ignored
        thread_local const RE::Actor* t_actor = nullptr;

        Trigger* g_anyActorTrigger = nullptr;  // any actor, impossible magicka cost
        Trigger* g_bowOnlyTrigger = nullptr;   // player only, requires a bow, impossible magicka cost
        Trigger* g_swordTrigger = nullptr;     // realistic: player, one-handed sword, "weaponSwing", casts Firebolt

        constexpr RE::FormID kPlayerRef = 0x14;
        constexpr RE::FormID kFlames = 0x12FCD;
        constexpr RE::FormID kFirebolt = 0x12FD0;
        constexpr RE::FormID kWolf = 0x23ABE;
        constexpr RE::FormID kBanditFaction = 0x1BCC0;

        // Runs a_func on the game's main thread and waits for the result.
        template <class F>
        auto OnMain(F a_func) -> decltype(a_func()) {
            std::promise<decltype(a_func())> promise;
            auto future = promise.get_future();
            SKSE::GetTaskInterface()->AddTask([&]() { promise.set_value(a_func()); });
            return future.get();
        }

        void RunConsole(std::string_view a_command) {
            const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
            if (const auto script = factory ? factory->Create() : nullptr) {
                script->SetCommand(a_command);
                script->CompileAndRun(nullptr);
                delete script;
            }
        }

        // Log + console + HUD notification. Main thread only.
        void SayOnMain(const std::string& a_text, bool a_hud) {
            logger::info("[DynAnimTest] {}", a_text);
            if (auto console = RE::ConsoleLog::GetSingleton()) {
                console->Print("[DynAnimTest] %s", a_text.c_str());
            }
            if (a_hud) {
                RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
            }
        }

        void Say(const std::string& a_text, bool a_hud = true) {
            OnMain([&]() {
                SayOnMain(a_text, a_hud);
                return true;
            });
        }

        bool GameBusy() {
            auto ui = RE::UI::GetSingleton();
            return !ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
        }

        // Waits until the game is running (no loading screen, not paused). False on timeout.
        bool WaitUntilRunning(std::chrono::seconds a_timeout) {
            const auto deadline = std::chrono::steady_clock::now() + a_timeout;
            while (GameBusy()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return false;
                }
                std::this_thread::sleep_for(250ms);
            }
            return true;
        }

        // Sleeps for a_seconds of unpaused game time, calling a_tick roughly every 1.5 s.
        template <class F>
        void SleepActive(double a_seconds, F a_tick) {
            double elapsed = 0.0;
            double sinceTick = 1.5;
            while (elapsed < a_seconds) {
                std::this_thread::sleep_for(100ms);
                if (GameBusy()) {
                    continue;
                }
                elapsed += 0.1;
                sinceTick += 0.1;
                if (sinceTick >= 1.5) {
                    sinceTick = 0.0;
                    a_tick();
                }
            }
        }

        void SleepActive(double a_seconds) {
            SleepActive(a_seconds, []() {});
        }

        RE::TESNPC* PickEnemyBase() {
            if (auto form = RE::TESForm::LookupByID(kWolf)) {
                if (auto npc = form->As<RE::TESNPC>()) {
                    return npc;
                }
            }
            auto bandits = RE::TESForm::LookupByID<RE::TESFaction>(kBanditFaction);
            if (auto data = RE::TESDataHandler::GetSingleton(); data && bandits) {
                for (auto npc : data->GetFormArray<RE::TESNPC>()) {
                    if (npc && !npc->IsUnique() && npc->IsInFaction(bandits)) {
                        return npc;
                    }
                }
            }
            return nullptr;
        }

        void PlayerAttack() {
            OnMain([]() {
                if (auto player = RE::PlayerCharacter::GetSingleton()) {
                    player->NotifyAnimationGraph("attackStart");
                }
                return true;
            });
        }

        // "12, 340, 8" - distance of every spawned actor from the player, or "gone"
        std::string Distances(const std::vector<RE::ActorHandle>& a_spawned) {
            return OnMain([&]() {
                std::string text;
                auto player = RE::PlayerCharacter::GetSingleton();
                for (auto& handle : a_spawned) {
                    if (!text.empty()) {
                        text += ", ";
                    }
                    auto actor = handle.get();
                    if (actor && player) {
                        text += fmt::format("{:.0f}{}", actor->GetPosition().GetDistance(player->GetPosition()),
                                            actor->IsDead() ? " (dead)" : "");
                    } else {
                        text += "gone";
                    }
                }
                return text.empty() ? std::string("none") : text;
            });
        }

        float PlayerMagicka() {
            return OnMain([]() {
                auto player = RE::PlayerCharacter::GetSingleton();
                return player ? player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka) : 0.f;
            });
        }

        void Console(std::initializer_list<const char*> a_commands) {
            OnMain([&]() {
                for (auto command : a_commands) {
                    RunConsole(command);
                }
                return true;
            });
        }

        struct Snapshot {
            int legacyNpc, legacyPlayer, realNpc, realPlayer;
        };

        Snapshot Take() {
            return {g_counters.legacyNpc.load(), g_counters.legacyPlayer.load(), g_counters.realNpc.load(),
                    g_counters.realPlayer.load()};
        }

        std::string Describe(const Snapshot& s) {
            return fmt::format("original would flash: NPC {} / player {}; really flashed: NPC {} / player {}", s.legacyNpc,
                               s.legacyPlayer, s.realNpc, s.realPlayer);
        }

        void Run() {
            Say("Starting. Teleporting to QASmoke - CLOSE THE CONSOLE now.", false);
            const RE::TESObjectCELL* oldCell = OnMain([]() -> const RE::TESObjectCELL* {
                auto player = RE::PlayerCharacter::GetSingleton();
                return player ? player->GetParentCell() : nullptr;
            });
            OnMain([]() {
                RunConsole("coc QASmoke");
                return true;
            });

            // wait until the player really is in the new cell (give up waiting for a change after ~15 s,
            // in case the test was started inside QASmoke)
            std::this_thread::sleep_for(2s);
            for (int i = 0; i < 480; i++) {
                const bool arrived = !GameBusy() && OnMain([&]() {
                    auto player = RE::PlayerCharacter::GetSingleton();
                    auto cell = player ? player->GetParentCell() : nullptr;
                    return cell && (cell != oldCell || i >= 60) && player->Is3DLoaded();
                });
                if (arrived) {
                    break;
                }
                std::this_thread::sleep_for(250ms);
            }
            if (!WaitUntilRunning(180s)) {
                Say("ABORTED: the game stayed paused or loading for 3 minutes.");
                g_running = false;
                return;
            }
            SleepActive(3.0);

            // move the player to the test spot inside QASmoke, then let the game settle
            OnMain([]() {
                RunConsole("player.setpos x -1699.43");
                RunConsole("player.setpos y 1529.77");
                RunConsole("player.setpos z 6976.43");
                return true;
            });
            SleepActive(0.2);

            // ---- setup ----
            std::vector<RE::ActorHandle> spawned;
            bool wasGodMode = false;
            const std::string enemyName = OnMain([&]() -> std::string {
                auto player = RE::PlayerCharacter::GetSingleton();
                // god mode makes magicka infinite, which would spoil phases 4 and 5; the spawned actors are caged
                wasGodMode = RE::PlayerCharacter::IsGodMode();
                if (wasGodMode) {
                    RunConsole("tgm");
                }
                RunConsole("player.additem 12EB7 1");
                RunConsole("player.equipitem 12EB7");

                auto base = PickEnemyBase();
                if (!base || !player) {
                    return {};
                }
                // spawn the actors, then move each to the chosen spot (spread out along Y so they do not overlap)
                for (int i = 0; i < 3; i++) {
                    if (auto ref = player->PlaceObjectAtMe(base, false)) {
                        if (auto actor = ref->As<RE::Actor>()) {
                            spawned.push_back(actor->GetHandle());
                            const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
                            for (auto& command : { fmt::format("setpos x {:.2f}", -2527.80f),
                                                   fmt::format("setpos y {:.2f}", 1542.93f + 70.0f * (i - 1)),
                                                   fmt::format("setpos z {:.2f}", 6976.45f) }) {
                                if (const auto script = factory ? factory->Create() : nullptr) {
                                    script->SetCommand(command);
                                    script->CompileAndRun(actor);
                                    delete script;
                                }
                            }
                        }
                    }
                }
                const char* name = base->GetName();
                auto cell = player->GetParentCell();
                const char* cellName = cell ? cell->GetName() : nullptr;
                return fmt::format("{} x{} (base {:08X}) at the test spot, in cell {:08X} '{}'", name && *name ? name : "unnamed",
                                   spawned.size(), base->GetFormID(), cell ? cell->GetFormID() : 0,
                                   cellName ? cellName : "");
            });
            Say(enemyName.empty() ? "No test actor could be spawned - phase 1 will be inconclusive."
                                  : "Spawned: " + enemyName,
                false);

            std::vector<std::string> report;

            // ---- phase 1: NPCs act, player stands still ----
            g_counters.Reset();
            g_npcOnly = true;
            g_trigger = g_anyActorTrigger;
            Say("Phase 1/5: stand still, do NOT move or attack (12 s). The magicka bar must NOT flash.");
            SleepActive(12.0);
            const auto p1 = Take();
            const char* v1 = p1.legacyNpc == 0 ? "INCONCLUSIVE (the NPCs produced no animation events)"
                             : p1.realNpc == 0 ? "PASS"
                                               : "FAIL";
            report.push_back(fmt::format("Phase 1 (NPC lacks magicka): {} - {}", v1, Describe(p1)));
            Say(report.back(), false);
            Say("  spawned actors, distance from player: " + Distances(spawned), false);

            // ---- phase 2: player acts; the bar is supposed to flash ----
            g_npcOnly = false;
            g_counters.Reset();
            OnMain([]() {
                if (auto player = RE::PlayerCharacter::GetSingleton()) {
                    player->DrawWeaponMagicHands(true);
                }
                return true;
            });
            Say("Phase 2/5: the test attacks for you (10 s). The magicka bar SHOULD flash.");
            SleepActive(10.0, PlayerAttack);
            const auto p2 = Take();
            const char* v2 = p2.legacyPlayer == 0 ? "INCONCLUSIVE (the player produced no animation events)"
                             : p2.realPlayer > 0 ? "PASS"
                                                 : "FAIL";
            report.push_back(fmt::format("Phase 2 (player lacks magicka): {} - {}", v2, Describe(p2)));
            Say(report.back(), false);
            Say("  spawned actors, distance from player: " + Distances(spawned), false);

            // The spawned actors stay: phase 3 uses a player-only trigger, so they cannot affect it.
            // (Disabling them here from a task crashed the game inside their movement controller.)
            g_trigger = nullptr;
            SleepActive(1.0);

            // ---- phase 3: player-only trigger that needs a bow; player holds a sword ----
            g_counters.Reset();
            g_trigger = g_bowOnlyTrigger;
            Say("Phase 3/5: the test attacks for you (10 s). The magicka bar must NOT flash.");
            SleepActive(10.0, PlayerAttack);
            const auto p3 = Take();
            const char* v3 = p3.legacyPlayer == 0 ? "INCONCLUSIVE (the player produced no animation events)"
                             : (p3.realPlayer + p3.realNpc) == 0 ? "PASS"
                                                                 : "FAIL";
            report.push_back(fmt::format("Phase 3 (trigger for another weapon): {} - {}", v3, Describe(p3)));
            Say(report.back(), false);
            Say("  spawned actors, distance from player: " + Distances(spawned), false);

            // ---- phase 4: realistic trigger, enough magicka -> the spell must be cast, no flash ----
            // Like a real .toml entry: AnimationEvent "weaponSwing", player only, one-handed sword,
            // MagickaCost 10, SpellFormIDs [Firebolt]. Matched by event tag, like a regular trigger.
            g_trigger = nullptr;
            SleepActive(1.0);
            Console({ "player.modav magicka 1000", "player.restoreav magicka 100000" });
            g_counters.Reset();
            const float magickaBefore = PlayerMagicka();
            g_trigger = g_swordTrigger;
            Say("Phase 4/5: the test swings the sword (10 s). Firebolts should fly, the magicka bar must NOT flash.");
            SleepActive(10.0, PlayerAttack);
            g_trigger = nullptr;
            const auto p4 = Take();
            const int invocations4 = g_counters.invocations.load();
            const int casts4 = g_counters.casts.load();
            const float spent4 = g_counters.spent.load();
            const float magickaAfter = PlayerMagicka();
            const char* v4 = invocations4 == 0 ? "INCONCLUSIVE (no weaponSwing event from the player)"
                             : (casts4 > 0 && spent4 > 0.f && p4.realPlayer == 0) ? "PASS"
                                                                                   : "FAIL";
            report.push_back(fmt::format(
                "Phase 4 (real trigger, enough magicka): {} - weaponSwing events {}, spells cast {}, magicka deducted {:.0f} "
                "(player magicka {:.0f} -> {:.0f}), flashes {}",
                v4, invocations4, casts4, spent4, magickaBefore, magickaAfter, p4.realPlayer + p4.realNpc));
            Say(report.back(), false);

            // ---- phase 5: same trigger, magicka drained -> no cast, the bar must flash ----
            SleepActive(1.0);
            g_counters.Reset();
            g_trigger = g_swordTrigger;
            Say("Phase 5/5: the test swings the sword (10 s). No firebolts, the magicka bar SHOULD flash.");
            SleepActive(10.0, []() {
                Console({ "player.damageav magicka 100000" });  // keep it empty despite regeneration
                PlayerAttack();
            });
            g_trigger = nullptr;
            const auto p5 = Take();
            const int invocations5 = g_counters.invocations.load();
            const int casts5 = g_counters.casts.load();
            const char* v5 = invocations5 == 0 ? "INCONCLUSIVE (no weaponSwing event from the player)"
                             : (casts5 == 0 && p5.realPlayer > 0) ? "PASS"
                                                                  : "FAIL";
            report.push_back(fmt::format("Phase 5 (real trigger, no magicka): {} - weaponSwing events {}, spells cast {}, flashes {}",
                                         v5, invocations5, casts5, p5.realPlayer + p5.realNpc));
            Say(report.back(), false);
            Console({ "player.restoreav magicka 100000", "player.modav magicka -1000" });

            // ---- teardown ----
            g_trigger = nullptr;
            OnMain([&]() {
                if (wasGodMode && !RE::PlayerCharacter::IsGodMode()) {
                    RunConsole("tgm");
                }
                // remove the spawned actors the same way the console would
                for (auto& handle : spawned) {
                    if (auto actor = handle.get()) {
                        const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
                        if (const auto script = factory ? factory->Create() : nullptr) {
                            script->SetCommand("kill");
                            script->CompileAndRun(actor.get());
                            delete script;
                        }
                    }
                }
                SayOnMain("===== RESULT =====", false);
                for (auto& line : report) {
                    SayOnMain(line, false);
                }
                SayOnMain("Finished - see the console or DynamicAnimationCasting.log. Do not save this game.", true);
                return true;
            });
            g_running = false;
        }

        bool Execute(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData*, RE::TESObjectREFR*, RE::TESObjectREFR*,
                     RE::Script*, RE::ScriptLocals*, double&, std::uint32_t&) {
            if (g_running.exchange(true)) {
                SayOnMain("A test run is already in progress.", false);
                return true;
            }
            std::thread(Run).detach();
            return true;
        }

        Trigger* MakeTrigger(RE::SpellItem* a_spell) {
            auto trigger = new Trigger();
            trigger->magickaCost = 100000.f;
            if (a_spell) {
                trigger->spells.push_back(a_spell);
            }
            return trigger;
        }
    }  // namespace

    void Install() {
        auto flames = RE::TESForm::LookupByID<RE::SpellItem>(kFlames);

        g_anyActorTrigger = MakeTrigger(flames);

        g_bowOnlyTrigger = MakeTrigger(flames);
        g_bowOnlyTrigger->caster = kPlayerRef;
        for (auto& weapon : g_bowOnlyTrigger->weapons) {
            weapon.type = static_cast<std::int8_t>(std::to_underlying(RE::WEAPON_TYPE::kBow));
            weapon.cast = true;
        }

        // realistic trigger, equivalent to this .toml entry:
        //   AnimationEvent = "weaponSwing", HasActorFormID = player, HasWeaponType = "OneHandSword",
        //   MagickaCost = 10.0, SpellFormIDs = [Firebolt]
        g_swordTrigger = new Trigger();
        g_swordTrigger->tag = "weaponSwing";
        g_swordTrigger->caster = kPlayerRef;
        g_swordTrigger->magickaCost = 10.f;
        if (auto firebolt = RE::TESForm::LookupByID<RE::SpellItem>(kFirebolt)) {
            g_swordTrigger->spells.push_back(firebolt);
        }
        for (auto& weapon : g_swordTrigger->weapons) {
            weapon.type = static_cast<std::int8_t>(std::to_underlying(RE::WEAPON_TYPE::kOneHandSword));
            weapon.cast = true;
        }

        for (auto name : {"DumpNiUpdates"sv, "TestSeenData"sv}) {
            auto command = RE::SCRIPT_FUNCTION::LocateConsoleCommand(name);
            if (!command) {
                continue;
            }
            const RE::SCRIPT_FUNCTION original = *command;
            RE::SCRIPT_FUNCTION replacement = original;
            replacement.functionName = "DynAnimTest";
            replacement.shortName = "dynat";
            replacement.helpString = "Runs the Dynamic Animation Casting HUD flash self test";
            replacement.referenceFunction = false;
            replacement.numParams = 0;
            replacement.params = nullptr;
            replacement.executeFunction = &Execute;
            replacement.conditionFunction = nullptr;
            replacement.editorFilter = false;
            replacement.invalidatesCellList = false;
            if (REL::safe_write(reinterpret_cast<std::uintptr_t>(command), &replacement, sizeof(replacement), &original,
                                sizeof(original))) {
                logger::info("[DynAnimTest] TEST BUILD: console command DynAnimTest registered (replaces {}).", name);
                return;
            }
        }
        logger::error("[DynAnimTest] could not register the console command.");
    }

    bool Active() noexcept { return g_running.load(std::memory_order_relaxed); }

    AnimationCasting::CastTrigger* ActiveTrigger(const RE::Actor* a_actor) noexcept {
        if (g_npcOnly.load(std::memory_order_relaxed) && a_actor && a_actor->IsPlayerRef()) {
            return nullptr;
        }
        return g_trigger.load(std::memory_order_relaxed);
    }

    void NoteLegacyFlash(const RE::Actor* a_actor) noexcept {
        if (!Active()) {
            return;
        }
        if (a_actor && a_actor->IsPlayerRef()) {
            g_counters.legacyPlayer++;
        } else {
            g_counters.legacyNpc++;
        }
    }

    void NoteRealFlash() noexcept {
        if (!Active()) {
            return;
        }
        if (t_actor && t_actor->IsPlayerRef()) {
            g_counters.realPlayer++;
        } else {
            g_counters.realNpc++;
        }
    }

    void NoteInvocation() noexcept {
        if (Active()) {
            g_counters.invocations++;
        }
    }

    void NoteCast() noexcept {
        if (Active()) {
            g_counters.casts++;
        }
    }

    void NoteMagickaSpent(float a_amount) noexcept {
        if (Active()) {
            g_counters.spent.store(g_counters.spent.load() + a_amount);
        }
    }

    ActorScope::ActorScope(const RE::Actor* a_actor) noexcept : previous(t_actor) { t_actor = a_actor; }

    ActorScope::~ActorScope() { t_actor = previous; }

}  // namespace Loki::AutoTest

#endif  // DAC_AUTOTEST
