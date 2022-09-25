#pragma once

namespace EnhancedReanimation
{
	namespace NPCCombatCast
	{
		void Install();
	};

	namespace DecapitateCheck
	{
		void Install();
	}

	namespace FastTravel
	{
		using EventResult = RE::BSEventNotifyControl;

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
			EventResult ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		private:
			LocationChangeHandler() = default;
			LocationChangeHandler(const LocationChangeHandler&) = delete;
			LocationChangeHandler(LocationChangeHandler&&) = delete;

			~LocationChangeHandler() override = default;

			LocationChangeHandler& operator=(const LocationChangeHandler&) = delete;
			LocationChangeHandler& operator=(LocationChangeHandler&&) = delete;

			static inline float followDistSquared = 160000.0f;
		};

		void Register();
	}

	namespace Riding
	{
		namespace Name
		{
			void Install();
		}

		namespace StolenTag
		{
			void Install();
		}

		namespace RaceReanimateCheck
		{
			void Install();
		}

		void Install();
	}

	void InstallOnPostLoad();

	void InstallOnDataLoad();
}
