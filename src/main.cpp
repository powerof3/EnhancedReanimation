#include "Settings.h"

namespace NPCCombatCast
{
	void Patch()
	{
		//CombatMagicCasterReanimate::CheckShouldEquip
		REL::Relocation<std::uintptr_t> target{ REL::ID(44036) };

		constexpr uintptr_t START = 0x9F;
		constexpr uintptr_t END = 0xB1;

		//.text: 000000014078601F mov eax, [rsi + 0C0h]
		//.text: 0000000140786025 and eax, 1E00000h
		//.text: 000000014078602A cmp eax, 800000h
		//.text: 000000014078602F jz short loc_1407860A7

		for (uintptr_t i = START; i < END; ++i) {
			REL::safe_write(target.address() + i, REL::NOP);
		}
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
	class LocationChangeHandler final :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		[[nodiscard]] static LocationChangeHandler* GetSingleton()
		{
			static LocationChangeHandler singleton;
			return std::addressof(singleton);
		}

	protected:
		using EventResult = RE::BSEventNotifyControl;

		EventResult ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			auto intfcStr = RE::InterfaceStrings::GetSingleton();
			if (!intfcStr || !a_event || a_event->menuName != intfcStr->loadingMenu || a_event->opening) {
				return EventResult::kContinue;
			}

			if (const auto player = RE::PlayerCharacter::GetSingleton(); player) {
				const auto process = player->currentProcess;
				const auto middleHigh = process ? process->middleHigh : nullptr;

				if (middleHigh) {
					for (auto& commandedActorData : middleHigh->commandedActors) {
						const auto zombie = commandedActorData.commandedActor.get();
						if (zombie && zombie->GetLifeState() == RE::ACTOR_LIFE_STATE::kReanimate && !zombie->IsAMount()) {
							const auto distance = player->GetPosition().GetSquaredDistance(zombie->GetPosition());
							if (distance > followDistSquared) {
								zombie->MoveTo(player);
							}
						}
					}
				}
			}

			return EventResult::kContinue;
		}

	private:
		LocationChangeHandler() = default;
		LocationChangeHandler(const LocationChangeHandler&) = delete;
		LocationChangeHandler(LocationChangeHandler&&) = delete;

		~LocationChangeHandler() override = default;

		LocationChangeHandler& operator=(const LocationChangeHandler&) = delete;
		LocationChangeHandler& operator=(LocationChangeHandler&&) = delete;

		static inline float followDistSquared = 160000.0f;
	};

	void Patch()
	{
		auto menuSrc = RE::UI::GetSingleton();
		if (menuSrc) {
			menuSrc->AddEventSink(LocationChangeHandler::GetSingleton());
		}
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
		const auto settings = Settings::GetSingleton();
		if (settings->patchHorse) {
			Riding::RaceReanimateCheck::Patch();
		}
		if (settings->fastTravel) {
			FastTravel::Patch();
		}
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
	SKSE::AllocTrampoline(28);

	const auto settings = Settings::GetSingleton();
	settings->Load();

	if (settings->decapitateCheck) {
		DecapitateCheck::Patch();
	}
	if (settings->npcCombat) {
		NPCCombatCast::Patch();
	}
	if (settings->rideHorse) {
		Riding::Patch();
		Riding::Name::Patch();
		Riding::StolenTag::Patch();
	}

	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", OnInit);

	return true;
}
