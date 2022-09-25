#pragma once

class Settings
{
public:
	[[nodiscard]] static Settings* GetSingleton()
	{
		static Settings singleton;
		return std::addressof(singleton);
	}

	void Load()
	{				
		constexpr auto path = L"Data/SKSE/Plugins/po3_EnhancedReanimation.ini";

		CSimpleIniA ini;
		ini.SetUnicode();

		ini.LoadFile(path);

		detail::get_value(ini, fastTravel, "Settings", "Fast Travel", ";fast travel fix");
		detail::get_value(ini, npcCombat, "Settings", "Necromancer Cast", ";reanimated necromancers can cast reanimate spells");
		detail::get_value(ini, decapitateCheck, "Settings", "Decapitated NPCs", ";reanimate decapitated enemies");
		detail::get_value(ini, rideHorse, "Riding", "Ride Horses", ";ride reanimated horses/mounts (not dragons)");
		detail::get_value(ini, patchHorse, "Riding", "Patch Horses", ";patch all reanimate spells so mounts can be reanimated");

		ini.SaveFile(path);
	}

	bool fastTravel{ true };
	bool npcCombat{ true };
	bool decapitateCheck{ true };
	bool rideHorse{ true };
	bool patchHorse{ true };

private:
	struct detail
	{
		static void get_value(CSimpleIniA& a_ini, bool& a_value, const char* a_section, const char* a_key, const char* a_comment)
		{
			a_value = a_ini.GetBoolValue(a_section, a_key, a_value);
			a_ini.SetBoolValue(a_section, a_key, a_value, a_comment);
		};
	};
};
