#include "Settings.h"

namespace NPCCombatCast
{
	void Patch()
	{
		//CombatMagicCasterReanimate::CheckShouldEquip
		REL::Relocation<std::uintptr_t> target{ REL::ID(45432) };

		constexpr std::array<std::uint8_t, 2> bytes{ REL::NOP, REL::NOP };
		REL::safe_write(target.address() + 0x8B, std::span{ bytes.data(), bytes.size() });
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
		REL::Relocation<std::uintptr_t> target{ REL::ID(34617) };
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
			REL::Relocation<std::uintptr_t> target{ REL::ID(24716) };
			stl::write_thunk_call<IsHorse>(target.address() + 0x8A);
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
			REL::Relocation<std::uintptr_t> target{ REL::ID(34750) };
			stl::write_thunk_call<CommandedEffect__Start>(target.address() + 0x24E);
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

				for (auto& race : dataHandler->GetFormArray<RE::TESRace>()) {
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
			REL::Relocation<std::uintptr_t> target{ REL::ID(24715) };

			constexpr uintptr_t START = 0x36E;
			constexpr uintptr_t END = 0x37A;

			//.text: 0000000140377D6E cmp edx, 800000h
			//.text: 0000000140377D74 jz loc_140377B3F

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
		FastTravel::ZombieEventHandler::GetSingleton()->Install();
	}
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(Version::MAJOR);
	v.PluginName("Enhanced Reanimation");
	v.AuthorName("powerofthree");
	v.UsesAddressLibrary(true);
	v.CompatibleVersions({ SKSE::RUNTIME_LATEST });

	return v;
}();

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

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
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();

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
