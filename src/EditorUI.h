struct TranslationData {
	std::string bonename;
	std::string bonenameorig;
	RE::NiPoint3 translation;
	float scale;
	bool ignoreXYZ;
	bool insertion;
	bool tponly;
	TranslationData(std::string _b, std::string _balt, RE::NiPoint3 _t, float _s, bool _ig, bool _i, bool _tp) {
		bonename = _b;
		bonenameorig = _balt;
		translation = _t;
		scale = _s;
		ignoreXYZ = _ig;
		insertion = _i;
		tponly = _tp;
	}
};

namespace EditorUI {
	void HookD3D11();

	enum class FORM_TYPE
	{
		kNone,
		kNPC,
		kRace
	};

	enum class RACE_GENDER
	{
		kNone,
		kMale,
		kFemale
	};

	struct PresetFile {
		std::string path;
		std::string displayName;
		PresetFile(std::string _path, std::string _displayName) {
			path = _path;
			displayName = _displayName;
		}
	};

	struct FormRecord {
		std::string plugin;
		std::string formID;
		std::string displayName;
		FormRecord(std::string _plugin, std::string _formID, std::string _displayName) {
			plugin = _plugin;
			formID = _formID;
			displayName = _displayName;
		}
	};

	class Window {
	protected:
		static Window* instance;
		uint32_t previewTarget = 0x14;
		bool previewQueued = false;
		float previewQueueTimer = 0.f;
		bool globalQueued = false;
		float globalQueueTimer = 0.f;
		bool shouldDraw = false;
		void BuildBoneData(auto json, std::vector<TranslationData>& map, float& s);
		void SetSelected(std::string plugin, std::string formID, FORM_TYPE formType, std::string genderStr);
		void Reset3DByFormID(uint32_t formID);
		void ApplySelectedToTarget();
		void OnPresetChange();
		void ChangeCurrentPreset(int idx);
		void HelpMarker(const char* desc);

	public:
		static bool imguiInitialized;
		static void ImGuiInit();
		Window() = default;
		Window(Window&) = delete;
		void operator=(const Window&) = delete;
		static Window* GetSingleton()
		{
			if (!instance)
				instance = new Window();
			return instance;
		}
		inline uint32_t GetPreviewTargetFormID() {
			return previewTarget;
		}
		void Draw();
		void ToggleShow();
		void Reset();
		void RefreshFiles(bool resetSelected = false);
		void NewPreset(std::string filename);
		void SaveCurrentPreset();
		void DeletePreset(const std::filesystem::path path);
		void LoadDefaultPreset();
		bool LoadPreset(const std::filesystem::path path);
		inline bool GetShouldDraw() {
			return shouldDraw;
		}
	};
}
