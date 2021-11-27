#include "Settings.h"

namespace NPCCombatCast
{
	void Patch()
	{
		//CombatMagicCasterReanimate::CheckShouldEquip
		REL::Relocation<std::uintptr_t> target{ REL::ID(44036) };

		constexpr std::array<std::uint8_t, 2> bytes{ REL::NOP, REL::NOP };
		REL::safe_write(target.address() + 0xAF, std::span{ bytes.data(), bytes.size() });
	}
};

namespace DecapitateCheck
{
	struct IsDismembered
	{
		static bool thunk(RE::Actor*, std::uint32_t)
		{
			return false;
		}
		static inline REL::Relocation<decltype(&thunk)> func;
	};

	void Patch()
	{
		//Actor::CanBeReanimated
		REL::Relocation<std::uintptr_t> target{ REL::ID(33825) };
		stl::write_thunk_call<IsDismembered>(target.address() + 0x72);
	}
}

namespace FastTravel
{
	struct ClearPlayerCombatGroup
	{
		static void thunk(RE::PlayerCharacter* a_player)
		{
			func(a_player);

			const auto extraFollowers = a_player->extraList.GetByType<RE::ExtraFollower>();
			if (extraFollowers) {
				for (const auto& followers : extraFollowers->actorFollowers) {
					auto actor = followers.actor.get();
					if (actor && actor->GetLifeState() == RE::ACTOR_LIFE_STATE::kReanimate && !actor->IsAMount()) {
						const auto distance = a_player->GetPosition().GetSquaredDistance(actor->GetPosition());
						if (distance > followDistSquared) {
							actor->MoveTo(a_player);
						}
					}
				}
			}
		}
		static inline REL::Relocation<decltype(&thunk)> func;

		static inline float followDistSquared = 160000.0f;
	};

	void Patch()
	{
		//FastTravel
		REL::Relocation<std::uintptr_t> target{ REL::ID(39373) };
		stl::write_thunk_call<ClearPlayerCombatGroup>(target.address() + 0xA4C);
	}
}

namespace Riding
{
	namespace Name
	{
		struct IsHorse
		{
			static bool thunk(RE::Actor*)
			{
				return false;
			}
			static inline REL::Relocation<decltype(&thunk)> func;
		};

		void Patch()
		{
			//TESNPC::GetActivateText
			REL::Relocation<std::uintptr_t> target{ REL::ID(24212) };
			stl::write_thunk_call<IsHorse>(target.address() + 0x88);
		}
	}

	namespace StolenTag
	{
		struct CommandedEffect__Start
		{
			static void thunk(RE::CommandEffect* a_effect)
			{
				const auto zombie = a_effect->commandedActor.get();
				const auto caster = a_effect->caster.get();

				if (zombie && caster && zombie != caster) {
					auto ownerData = zombie->extraList.GetByType<RE::ExtraOwnership>();
					if (ownerData) {
						ownerData->owner = caster->GetActorBase();
					}
				}

				func(a_effect);
			}
			static inline REL::Relocation<decltype(&thunk)> func;
		};

		void Patch()
		{
			//Reanimate::Start
			REL::Relocation<std::uintptr_t> target{ REL::ID(33956) };
			stl::write_thunk_call<CommandedEffect__Start>(target.address() + 0x22B);
		}
	}

	namespace RaceReanimateCheck
	{
		void Patch()
		{
			if (auto dataHandler = RE::TESDataHandler::GetSingleton(); dataHandler) {
				const auto reanimateKYWD = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicNoReanimate"sv);
				if (!reanimateKYWD) {
					return;
				}

				const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSKeyword>();
				const auto dummyKYWD = factory ? factory->Create() : nullptr;
				if (!dummyKYWD) {
					return;
				}
				dummyKYWD->SetFormEditorID("DummyHorseReanimate");
				dataHandler->GetFormArray<RE::BGSKeyword>().push_back(dummyKYWD);

				for (const auto& race : dataHandler->GetFormArray<RE::TESRace>()) {
					if (race) {
						std::string name{ race->GetName() };
						auto index = race->GetKeywordIndex(reanimateKYWD);
						if (index && string::icontains(name, "horse"sv)) {
							race->keywords[*index] = dummyKYWD;
						}
					}
				}
			}
		}
	}

	namespace
	{
		void Patch()
		{
			//TESNPC::Activate
			REL::Relocation<std::uintptr_t> target{ REL::ID(24211) };

			constexpr uintptr_t START = 0x362;
			constexpr uintptr_t END = 0x36B;

			//.text : 0000000140360F72 cmp edx, 4
			//.text : 0000000140360F75 jz loc_14036161D

			for (uintptr_t i = START; i < END; ++i) {
				REL::safe_write(target.address() + i, REL::NOP);
			}
		}
	}
}

void OnInit(SKSE::MessagingInterface::Message* a_msg)
{
	if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
		Riding::RaceReanimateCheck::Patch();
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%H:%M:%S] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "Enhanced Reanimation";
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	logger::info("loaded plugin");

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(43);

	const auto settings = Settings::GetSingleton();
	settings->Load();

	if (settings->decapitateCheck) {
		DecapitateCheck::Patch();
	}
	if (settings->fastTravel) {
		FastTravel::Patch();
	}
	if (settings->npcCombat) {
		NPCCombatCast::Patch();
	}
	if (settings->rideHorse) {
		Riding::Patch();
		Riding::Name::Patch();
		Riding::StolenTag::Patch();

		if (settings->patchHorse) {
			auto messaging = SKSE::GetMessagingInterface();
			messaging->RegisterListener("SKSE", OnInit);
		}
	}

	return true;
}
