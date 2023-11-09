#include "nlohmann/json.hpp"
#include "imgui/imgui_internal.h"
#include <string>
namespace Localization {
	void InitImGuiSettings();
	void LoadLocalizations();
	std::string GetDefaultLanguage();
	std::string GetCurrentLanguage();
	void SetCurrentLanguage(std::string lang);
	std::vector<std::string> GetSupportedLanguages();
	std::string GetLocalizedString(std::string key);
	const char* GetLocalizedPChar(std::string key);
}
