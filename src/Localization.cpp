#include "Localization.h"
#include <fstream>
using std::unordered_map;
using std::vector;
using std::map;

extern char* _MESSAGE(const char* fmt, ...);

namespace Localization
{
	unordered_map<std::string, unordered_map<std::string, std::string>> localizedStrings;
	std::string currentLanguage = "en";
	ImGuiSettingsHandler ini_handler;
	bool langSettingFound = false;

	void* LocalizationSettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
	{
		return (void*) & currentLanguage;
	}

	void LocalizationSettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
	{
		char lang[32] = { 0 };
		if (sscanf_s(line, "Language=%31s", lang, (unsigned int)sizeof(lang)) == 1) {
			langSettingFound = true;
			currentLanguage = lang;
		}
	}

	void LocalizationHandler_ApplyAll(ImGuiContext* ctx, ImGuiSettingsHandler*)
	{
		if (langSettingFound == false) {
			currentLanguage = GetDefaultLanguage();
			langSettingFound = true;
		}
	}

	void LocalizationSettingsHandler_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
	{
		buf->reserve(buf->size() + 64);
		buf->appendf("[%s][%s]\n", handler->TypeName, "Localization");

		buf->appendf("%s=%s\n", "Language", currentLanguage.c_str());

		buf->append("\n");
	}

	void InitImGuiSettings()
	{
		ini_handler = ImGuiSettingsHandler();
		ini_handler.TypeName = "NanakoSurgeon";
		ini_handler.TypeHash = ImHashStr("NanakoSurgeon");
		ini_handler.ReadOpenFn = LocalizationSettingsHandler_ReadOpen;
		ini_handler.ReadLineFn = LocalizationSettingsHandler_ReadLine;
		ini_handler.ApplyAllFn = LocalizationHandler_ApplyAll;
		ini_handler.WriteAllFn = LocalizationSettingsHandler_WriteAll;
		ImGui::AddSettingsHandler(&ini_handler);
	}
	void LoadLocalizations()
	{
		namespace fs = std::filesystem;
		fs::path jsonPath = fs::current_path();
		jsonPath += "\\Data\\F4SE\\Plugins\\NanakoSurgeon\\Translations";
		std::stringstream stream;
		fs::directory_entry jsonEntry{ jsonPath };
		if (jsonEntry.exists()) {
			for (auto& it : fs::directory_iterator(jsonEntry)) {
				if (it.path().extension().compare(".json") == 0) {
					std::ifstream reader;
					reader.open(it.path());
					nlohmann::json transData;
					reader >> transData;
					for (auto langit = transData.begin(); langit != transData.end(); ++langit) {
						const std::string& lang = langit.key();
						if (localizedStrings.find(lang) == localizedStrings.end()) {
							localizedStrings.insert(std::pair<std::string, unordered_map<std::string, std::string>>(lang, unordered_map<std::string, std::string>()));
						}
						for (auto strit = langit.value().begin(); strit != langit.value().end(); ++strit) {
							if (strit.value().is_string()) {
								localizedStrings[lang].insert(std::pair<std::string, std::string>(strit.key(), strit.value().get<std::string>()));
							}
						}
					}
					reader.close();
				}
			}
		}
	}

	std::string GetDefaultLanguage()
	{
		LANGID curLangID = GetUserDefaultUILanguage();

		switch (curLangID) {
			case 0x412:
				return "ko";
			case 0x411:
				return "jp";
			case 0x804:
				return "zh_cn";
			case 0x404:
				return "zh_tw";
			case 0x409:
				return "en";
			case 0x809:
				return "en";
		}
		return "en";
	}

	std::string GetCurrentLanguage()
	{
		return currentLanguage;
	}

	void SetCurrentLanguage(std::string lang)
	{
		if (localizedStrings.find(lang) != localizedStrings.end()) {
			currentLanguage = lang;
			ImGui::SaveIniSettingsToDisk(ImGui::GetCurrentContext()->IO.IniFilename);
		}
	}

	std::vector<std::string> GetSupportedLanguages()
	{
		std::vector<std::string> languages;
		languages.reserve(localizedStrings.size());

		for (auto& kv : localizedStrings) {
			languages.push_back(kv.first);
		} 
		return languages;
	}

	std::string GetLocalizedString(std::string key)
	{
		if (localizedStrings.find(currentLanguage) == localizedStrings.end()) {
			localizedStrings.insert(std::pair<std::string, unordered_map<std::string, std::string>>(currentLanguage, unordered_map<std::string, std::string>()));
		}
		if (localizedStrings[currentLanguage].find(key) == localizedStrings[currentLanguage].end()) {
			localizedStrings[currentLanguage].insert(std::pair<std::string, std::string>(key, key));
		}
		return localizedStrings[currentLanguage][key];
	}

	const char* GetLocalizedPChar(std::string key)
	{
		if (localizedStrings.find(currentLanguage) == localizedStrings.end()) {
			localizedStrings.insert(std::pair<std::string, unordered_map<std::string, std::string>>(currentLanguage, unordered_map<std::string, std::string>()));
		}
		if (localizedStrings[currentLanguage].find(key) == localizedStrings[currentLanguage].end()) {
			localizedStrings[currentLanguage].insert(std::pair<std::string, std::string>(key, key));
		}
		return localizedStrings[currentLanguage][key].c_str();
	}
}
