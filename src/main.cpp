#include "version.h"

//reanimate state = (actorState1_C0 & 0x1E00000) != 0x800000

static bool fastTravel = true;
static bool npcCombat = true;
static bool decapitateCheck = true;
static bool rideHorse = true;
static bool patchHorse = true;


class NPCCombatCast	 //nop out reanimated lifestate check
{
public:
	static void Patch()
	{
		REL::Relocation<std::uintptr_t> CombatMagicCastReanimate{ REL::ID(44036) };

		constexpr std::array<std::uint8_t, 2> bytes{
			0x90,
			0x90
		};
		REL::safe_write(CombatMagicCastReanimate.address() + 0xAF, stl ::span{ bytes.data(), bytes.size() });
	}
};


class ReanimateCheck  //using IsLimbGone (aka decapitate)
{
public:
	static void Patch()
	{
		REL::Relocation<std::uintptr_t> CanActorBeReanimated{ REL::ID(33825) };

		auto& trampoline = SKSE::GetTrampoline();
		_IsLimbGone = trampoline.write_call<5>(CanActorBeReanimated.address() + 0x72, IsLimbGone);
	}

private:
	static bool IsLimbGone(RE::Actor* a_actor, std::uint32_t a_limbEnum)
	{
		return false;
	}
	static inline REL::Relocation<decltype(IsLimbGone)> _IsLimbGone;
};


class FastTravel  //moveto right after the game does it, but before the event sink is dispatched
{
public:
	static void Patch()
	{
		REL::Relocation<std::uintptr_t> HandleFastTravel{ REL::ID(39373) };

		auto& trampoline = SKSE::GetTrampoline();
		_ClearPlayerCombatGroup = trampoline.write_call<5>(HandleFastTravel.address() + 0xA4C, ClearPlayerCombatGroup);	 //arbitary function, called after package processing
	}

private:
	static void ClearPlayerCombatGroup(RE::PlayerCharacter* a_player)
	{
		_ClearPlayerCombatGroup(a_player);

		auto extraFollowers = a_player->extraList.GetByType<RE::ExtraFollower>();
		if (extraFollowers) {
			for (auto& followers : extraFollowers->actorFollowers) {
				auto actorPtr = followers.actor.get();
				auto actor = actorPtr.get();
				if (actor && actor->GetLifeState() == RE::ACTOR_LIFE_STATE::kReanimate && !actor->IsAMount()) {
					auto distance = a_player->GetPosition().GetSquaredDistance(actor->GetPosition());
					if (distance > followDistSquared) {
						actor->MoveTo(a_player);
					}
				}
			}
		}
	}
	static inline REL::Relocation<decltype(ClearPlayerCombatGroup)> _ClearPlayerCombatGroup;

	static inline float followDistSquared = 160000.0f;
};


class RideHorse
{
public:
	static void PatchActivate()	 // patch out TESNPC::Activate return if the actor is reanimated
	{
		REL::Relocation<std::uintptr_t> Activate{ REL::ID(24211) };

		constexpr uintptr_t START = 0x362;
		constexpr uintptr_t END = 0x36B;
		constexpr std::uint8_t NOP = 0x90;

		//.text : 0000000140360F72 cmp edx, 4
		//.text : 0000000140360F75 jz loc_14036161D

		for (uintptr_t i = START; i < END; ++i) {
			REL::safe_write(Activate.address() + i, NOP);
		}
	}


	static void PatchStolenTag()
	{
		REL::Relocation<std::uintptr_t> ReanimateStart{ REL::ID(33956) };

		auto& trampoline = SKSE::GetTrampoline();
		_SetupCommandedEffect = trampoline.write_call<5>(ReanimateStart.address() + 0x22B, SetupCommandedEffect);
	}


	static void PatchNaming()
	{
		REL::Relocation<std::uintptr_t> GetActivateText{ REL::ID(24212) };

		auto& trampoline = SKSE::GetTrampoline();
		_IsHorse = trampoline.write_call<5>(GetActivateText.address() + 0x88, IsHorse);
	}

private:
	static void SetupCommandedEffect(RE::CommandEffect* a_effect)
	{
		auto zombiePtr = a_effect->commandedActor.get();
		auto zombie = zombiePtr.get();

		auto casterPtr = a_effect->caster.get();
		auto caster = casterPtr.get();

		if (zombie && caster && zombie != caster) {
			auto ownerData = zombie->extraList.GetByType<RE::ExtraOwnership>();
			if (ownerData) {
				ownerData->owner = caster->GetActorBase();
			}
		}

		_SetupCommandedEffect(a_effect);
	}
	static inline REL::Relocation<decltype(SetupCommandedEffect)> _SetupCommandedEffect;


	static bool IsHorse(RE::Actor* a_actor)
	{
		return false;
	}
	static inline REL::Relocation<decltype(IsHorse)> _IsHorse;
};


//new method for patching horses, by swapping unused keyword in place of MagicNoReanimate. RemoveKeyword caused crashes (numKeywords wasn't decremented?)
void OnInit(SKSE::MessagingInterface::Message* a_msg)
{
	if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
		if (rideHorse && patchHorse) {
			const auto reanimateKYWD = RE::TESForm::LookupByID<RE::BGSKeyword>(0x0006F6FB);
			const auto traitGreedy = RE::TESForm::LookupByID<RE::BGSKeyword>(0x000335FA);  //unused keyword in vanilla game.
			if (!reanimateKYWD || !traitGreedy) {
				return;
			}

			if (auto dataHandler = RE::TESDataHandler::GetSingleton(); dataHandler) {
				for (const auto& race : dataHandler->GetFormArray<RE::TESRace>()) {
					if (race && race->HasKeywordString("MagicNoReanimate"sv)) {
						std::string name{ race->GetName() };
						if (SKSE::STRING::insenstiveStringFind(name, "horse"sv)) {
							race->SwapKeyword(reanimateKYWD, traitGreedy);
						}
					}
				}
			}
		}
	}
}


void LoadSettings()
{
	constexpr auto path = L"Data/SKSE/Plugins/po3_EnhancedReanimation.ini";

	CSimpleIniA ini;
	ini.SetUnicode();
	ini.SetMultiKey();

	ini.LoadFile(path);

	fastTravel = ini.GetBoolValue("Settings", "Fast Travel", true);
	ini.SetBoolValue("Settings", "Fast Travel", fastTravel, ";fast travel fix", true);

	npcCombat = ini.GetBoolValue("Settings", "Necromancer Cast", true);
	ini.SetBoolValue("Settings", "Necromancer Cast", npcCombat, ";reanimated necromancers can cast reanimate spells", true);

	decapitateCheck = ini.GetBoolValue("Settings", "Decapitated NPCs", true);
	ini.SetBoolValue("Settings", "Decapitated NPCs", decapitateCheck, ";reanimate decapitated enemies", true);

	rideHorse = ini.GetBoolValue("Riding", "Ride Horses", true);
	ini.SetBoolValue("Riding", "Ride Horses", rideHorse, ";ride reanimated horses/mounts (not dragons)", true);

	patchHorse = ini.GetBoolValue("Riding", "Patch Horses", true);
	ini.SetBoolValue("Riding", "Patch Horses", patchHorse, ";patch all reanimate spells so mounts can be reanimated", true);

	ini.SaveFile(path);
}


extern "C" DLLEXPORT bool APIENTRY SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	try {
		auto path = logger::log_directory().value() / "po3_EnhancedReanimation.log";
		auto log = spdlog::basic_logger_mt("global log", path.string(), true);
		log->flush_on(spdlog::level::info);

#ifndef NDEBUG
		log->set_level(spdlog::level::debug);
		log->sinks().push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#else
		log->set_level(spdlog::level::info);

#endif
		spdlog::set_default_logger(log);
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");

		logger::info("Enhanced Reanimation {}", SOS_VERSION_VERSTRING);

		a_info->infoVersion = SKSE::PluginInfo::kVersion;
		a_info->name = "Enhanced Reanimation";
		a_info->version = SOS_VERSION_MAJOR;

		if (a_skse->IsEditor()) {
			logger::critical("Loaded in editor, marking as incompatible");
			return false;
		}

		const auto ver = a_skse->RuntimeVersion();
		if (ver < SKSE::RUNTIME_1_5_39) {
			logger::critical("Unsupported runtime version {}", ver.string());
			return false;
		}
	} catch (const std::exception& e) {
		logger::critical(e.what());
		return false;
	} catch (...) {
		logger::critical("caught unknown exception");
		return false;
	}

	return true;
}


extern "C" DLLEXPORT bool APIENTRY SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	try {
		logger::info("Enhanced Reanimation loaded");

		SKSE::Init(a_skse);
		SKSE::AllocTrampoline(43);

		LoadSettings();

		auto messaging = SKSE::GetMessagingInterface();
		if (!messaging->RegisterListener("SKSE", OnInit)) {
			return false;
		}

		if (decapitateCheck) {
			logger::info("patching decapitate check");
			ReanimateCheck::Patch();
		}
		if (npcCombat) {
			NPCCombatCast::Patch();
			logger::info("patching NPC combat casting");
		}
		if (fastTravel) {
			FastTravel::Patch();
			logger::info("fixing fast travel bugs");
		}
		if (rideHorse) {
			RideHorse::PatchActivate();
			RideHorse::PatchStolenTag();
			RideHorse::PatchNaming();
			logger::info("patching zombie horse riding");
		}

	} catch (const std::exception& e) {
		logger::critical(e.what());
		return false;
	} catch (...) {
		logger::critical("caught unknown exception");
		return false;
	}

	return true;
}
