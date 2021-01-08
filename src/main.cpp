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
		if (patchHorse && decapitateCheck) {
			_IsLimbGone = trampoline.write_call<5>(CanActorBeReanimated.address() + 0x72, IsLimbGone_DecapHorse);
		} else {
			_IsLimbGone = trampoline.write_call<5>(CanActorBeReanimated.address() + 0x72, patchHorse ? IsLimbGone_Horse : IsLimbGone);
		}
	}

private:
	static bool IsLimbGone(RE::Actor* a_actor, std::uint32_t a_limbEnum)
	{
		return false;
	}

	static bool IsLimbGone_Horse(RE::Actor* a_actor, std::uint32_t a_limbEnum)	//reimplements HasKeyword MagicNoReanimate condition check here.
	{
		if (a_actor->HasKeyword("MagicNoReanimate"sv) && !a_actor->HasKeyword("ActorTypeHorse"sv)) {
			return true;
		}
		return _IsLimbGone(a_actor, a_limbEnum);
	}

	static bool IsLimbGone_DecapHorse(RE::Actor* a_actor, std::uint32_t a_limbEnum)
	{
		if (a_actor->HasKeyword("MagicNoReanimate"sv) && !a_actor->HasKeyword("ActorTypeHorse"sv)) {
			return true;
		}
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
					auto distance = RE::NiPoint3::GetSquaredDistance(a_player->GetPosition(), actor->GetPosition());
					if (distance > (400.0f * 400.0f)) {
						actor->MoveTo(a_player);
					}
				}
			}
		}
	}
	static inline REL::Relocation<decltype(ClearPlayerCombatGroup)> _ClearPlayerCombatGroup;
};


class RideHorse
{
public:
	static void PatchActivate()	 // patch out TESNPC::Activate check that returns if the actor is reanimated
	{
		struct Patch : Xbyak::CodeGenerator
		{
			Patch()
			{
				// cmp(edx, 4)

				cmp(edx, 9);  // non existent life state
			}
		};

		Patch patch;
		patch.ready();

		REL::Relocation<std::uintptr_t> Activate{ REL::ID(24211) };
		REL::safe_write(Activate.address() + 0x362, stl::span{ patch.getCode(), patch.getSize() });
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


class PatchHorse
{
public:
	static void deleteReanimateNode(RE::TESConditionItem** a_head)
	{
		auto reanimateKYWD = RE::TESForm::LookupByID<RE::BGSKeyword>(0x0006F6FB);
		if (!reanimateKYWD) {
			return;
		}

		auto temp = *a_head;
		RE::TESConditionItem* prev = nullptr;

		if (temp && isReanimatedCondition(temp, reanimateKYWD)) {
			*a_head = temp->next;
			delete temp;
			return;
		}
		while (temp && !isReanimatedCondition(temp, reanimateKYWD)) {
			prev = temp;
			temp = temp->next;
		}
		if (!temp) {
			return;
		}

		prev->next = temp->next;
		delete temp;
	}

private:
	static bool isReanimatedCondition(RE::TESConditionItem* node, RE::BGSKeyword* reanimateKeyword)
	{
		using ID = RE::FUNCTION_DATA::FunctionID;

		return node->data.functionData.function == ID::kHasKeyword && node->data.functionData.params[0] == reanimateKeyword;
	}
};


void OnInit(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		if (rideHorse && patchHorse) {
			auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (dataHandler) {
				for (const auto& effect : dataHandler->GetFormArray<RE::EffectSetting>()) {
					if (effect && effect->GetArchetype() == RE::Archetype::kReanimate) {
						PatchHorse::deleteReanimateNode(&effect->conditions.head);	//delete condition so I can reimplement it myself
					}
				}
			}
		}
		break;
	}
}


bool ReadINI()
{
	try {
		static std::string pluginPath;
		if (pluginPath.empty()) {
			pluginPath = SKSE::GetPluginConfigPath("po3_EnhancedReanimation");
		}

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetMultiKey();

		SI_Error rc = ini.LoadFile(pluginPath.c_str());
		if (rc < 0) {
			logger::error("Can't load 'po3_EnhancedReanimation.ini'");
			return false;
		}

		decapitateCheck = ini.GetBoolValue("Settings", "Decapitated NPCs", true);

		fastTravel = ini.GetBoolValue("Settings", "Fast Travel", true);

		npcCombat = ini.GetBoolValue("Settings", "Necromancer Cast", true);

		rideHorse = ini.GetBoolValue("Riding", "Ride Horses", true);

		patchHorse = ini.GetBoolValue("Riding", "Patch Horses", true);

	} catch (...) {
		logger::error("Can't read 'po3_EnhancedReanimation.ini'");
		return false;
	}

	return true;
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
		SKSE::AllocTrampoline(1 << 6);

		ReadINI();

		auto messaging = SKSE::GetMessagingInterface();
		if (!messaging->RegisterListener("SKSE", OnInit)) {
			return false;
		}

		if (decapitateCheck || (rideHorse && patchHorse)) {
			logger::info("patching reanimated check");
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
