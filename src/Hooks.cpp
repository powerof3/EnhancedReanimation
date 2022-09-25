#include "Hooks.h"
#include "Settings.h"

namespace EnhancedReanimation
{
	namespace NPCCombatCast
	{
		void Install()
		{
			//CombatMagicCasterReanimate::CheckShouldEquip
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(44036, 45432) };

			constexpr uintptr_t START = OFFSET(0x9F, 0x7B);
			constexpr uintptr_t END = OFFSET(0xB1, 0x91);

			//.text: 000000014078601F mov eax, [rsi + 0C0h]
			//.text: 0000000140786025 and eax, 1E00000h
			//.text: 000000014078602A cmp eax, 800000h
			//.text: 000000014078602F jz short loc_1407860A7

			for (uintptr_t i = START; i < END; ++i) {
				REL::safe_write(target.address() + i, REL::NOP);
			}
		}
	}

	namespace DecapitateCheck
	{
		struct IsDismembered
		{
			static bool thunk(RE::Actor*, std::uint32_t)
			{
				return false;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void Install()
		{
			//Actor::CanBeReanimated
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(33825, 34617), 0x72 };
			stl::write_thunk_call<IsDismembered>(target.address());
		}
	}

	namespace FastTravel
	{
		EventResult LocationChangeHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
		{
			if (!a_event || a_event->menuName != RE::LoadingMenu::MENU_NAME || a_event->opening) {
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

		void Register()
		{
			if (auto UI = RE::UI::GetSingleton()) {
				UI->AddEventSink(LocationChangeHandler::GetSingleton());
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
				static inline REL::Relocation<decltype(thunk)> func;
			};

			void Install()
			{
				//TESNPC::GetActivateText
				REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(24212, 24716), OFFSET(0x88, 0x8A) };
				stl::write_thunk_call<IsHorse>(target.address());
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
						if (auto ownerData = zombie->extraList.GetByType<RE::ExtraOwnership>()) {
							ownerData->owner = caster->GetActorBase();
						}
					}

					func(a_effect);
				}
				static inline REL::Relocation<decltype(thunk)> func;
			};

			void Install()
			{
				//Reanimate::Start
				REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(33956, 34750), OFFSET(0x22B, 0x24E) };
				stl::write_thunk_call<CommandedEffect__Start>(target.address());
			}
		}

		namespace RaceReanimateCheck
		{
			void Install()
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
						if (race && string::icontains(race->GetName(), "Horse")) {
							if (auto index = race->GetKeywordIndex(reanimateKYWD)) {
								race->keywords[*index] = dummyKYWD;
							}
						}
					}
				}
			}
		}

		void Install()
		{
			//TESNPC::Activate
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(24211, 24715) };

			constexpr uintptr_t START = OFFSET(0x362, 0x36E);
			constexpr uintptr_t END = OFFSET(0x36B, 0x37A);

			//.text : 0000000140360F72 cmp edx, 4
			//.text : 0000000140360F75 jz loc_14036161D

			for (uintptr_t i = START; i < END; ++i) {
				REL::safe_write(target.address() + i, REL::NOP);
			}
		}
	}

	void InstallOnPostLoad()
	{
		const auto settings = Settings::GetSingleton();
		settings->Load();

		if (settings->decapitateCheck) {
			DecapitateCheck::Install();
		}
		if (settings->npcCombat) {
			NPCCombatCast::Install();
		}
		if (settings->rideHorse) {
			Riding::Install();
			Riding::Name::Install();
			Riding::StolenTag::Install();
		}
	}

	void InstallOnDataLoad()
	{
		const auto settings = Settings::GetSingleton();
		if (settings->patchHorse) {
			Riding::RaceReanimateCheck::Install();
		}
		if (settings->fastTravel) {
			FastTravel::Register();
		}
	}
}
