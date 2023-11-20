#include "EditorUI.h"
#include "Localization.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <d3d11.h>
#include <fstream>
#include <unordered_set>
#include <wrl.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#include <windowsx.h>
#pragma execution_character_set("utf-8")
namespace WRL = Microsoft::WRL;
using std::unordered_map;
using std::vector;
using std::unordered_set;

extern RE::PlayerCharacter* p;
extern uint32_t editorKey;
extern char* _MESSAGE(const char* fmt, ...);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern unordered_map<uint32_t, vector<TranslationData>> customRaceFemaleTranslations;
extern unordered_map<uint32_t, vector<TranslationData>> customRaceMaleTranslations;
extern unordered_map<uint32_t, vector<TranslationData>> customNPCTranslations;
extern unordered_map<uint32_t, float> customRaceFemaleScales;
extern unordered_map<uint32_t, float> customRaceMaleScales;
extern unordered_map<uint32_t, float> customNPCScales;
extern void LoadConfigs();
extern void QueueSurgery(RE::Actor* a);
extern void DoGlobalSurgery();
extern void QueueScaling(RE::Actor* a);
extern void DoGlobalScaling();
extern void DoSurgeryPreview(RE::Actor* a, vector<TranslationData> transData);
extern void DoScalePreview(RE::Actor* a, float scale);
extern RE::TESForm* GetFormFromMod(std::string modname, uint32_t formid);
extern bool Visit(RE::NiAVObject* obj, const std::function<bool(RE::NiAVObject*)>& functor);

namespace EditorUI {
	bool Window::imguiInitialized = false;
	Window* Window::instance = nullptr;

	// Hook variables
	REL::Relocation<uintptr_t> ptr_D3D11CreateDeviceAndSwapChainCall{ REL::ID(224250), 0x419 };
	REL::Relocation<uintptr_t> ptr_D3D11CreateDeviceAndSwapChain{ REL::ID(254484)};
	typedef HRESULT (*FnD3D11CreateDeviceAndSwapChain)(IDXGIAdapter*,
		D3D_DRIVER_TYPE,
		HMODULE, UINT,
		const D3D_FEATURE_LEVEL*,
		UINT,
		UINT,
		const DXGI_SWAP_CHAIN_DESC*,
		IDXGISwapChain**,
		ID3D11Device**,
		D3D_FEATURE_LEVEL*,
		ID3D11DeviceContext**);
	FnD3D11CreateDeviceAndSwapChain D3D11CreateDeviceAndSwapChain_Orig;
	typedef HRESULT (*FnD3D11Present)(IDXGISwapChain*, UINT, UINT);
	FnD3D11Present D3D11Present_Orig;
	REL::Relocation<uintptr_t> ptr_ClipCursor{ REL::ID(641385)};
	typedef BOOL (*FnClipCursor)(const RECT*);
	FnClipCursor ClipCursor_Orig;
	WNDPROC WndProc_Orig;
	REL::Relocation<uintptr_t> ptr_RegisterClassA{ REL::ID(514993) };
	typedef ATOM (*FnRegisterClassA)(const WNDCLASSEXA* wnd);
	FnRegisterClassA RegisterClassA_Orig;

	// Stored variables for ImGui render
	RECT windowRect;
	ImGuiIO imguiIO;
	ImFont* notosansKR;
	ImFont* notosansJP;
	ImFont* notosansSC;
	ImFont* notosansTC;
	WRL::ComPtr<IDXGISwapChain> d3d11SwapChain;
	WRL::ComPtr<ID3D11Device> d3d11Device;
	WRL::ComPtr<ID3D11DeviceContext> d3d11Context;
	HWND window;

	// Variables for functionality
	static const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	vector<PresetFile> presetList{};
	int presetCurrentIdx = -1;
	std::string selectedFormID = "";
	FORM_TYPE selectedFormType = FORM_TYPE::kNone;
	RACE_GENDER selectedRaceGender = RACE_GENDER::kNone;
	std::string selectedPlugin = "";
	bool applyToTarget = false;
	bool jsonLoaded = false;
	bool rotateTarget = false;
	POINT lastMousePos = { 0, 0 };
	nlohmann::json jsonData;
	unordered_map<std::string, vector<std::string>> jsonRaceList;
	unordered_map<std::string, vector<std::string>> jsonNPCList;
	auto* jsonInsertTarget = &(jsonData);
	uint32_t previewTarget = 0x14;
	unordered_set<uint32_t> previewedList;

	// Strings for future localization or stuff
	std::string str_BoneExtraX = "X";
	std::string str_BoneExtraY = "Y";
	std::string str_BoneExtraZ = "Z";
	std::string str_BoneExtraScale = "Scale";
	std::string str_BoneExtraIgnoreXYZ = "IgnoreXYZ";
	std::string str_BoneExtraInsertion = "Insertion";
	std::string str_BoneExtraTpOnly = "ThirdPersonOnly";
	std::string str_SetScale = "SetScale";
	std::string str_Male = "Male";
	std::string str_Female = "Female";

	
	BOOL __stdcall HookedClipCursor(const RECT* lpRect) {
		if (Window::GetSingleton() && Window::GetSingleton()->GetShouldDraw())
			lpRect = &windowRect;
		return ClipCursor_Orig(lpRect);
	}

	LRESULT __stdcall WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
		switch (msg) {
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		{
			POINT mousePos = { (LONG)GET_X_LPARAM(lParam), (LONG)GET_Y_LPARAM(lParam) };
			POINT mouseDelta = { mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y };
			if (Window::GetSingleton()->GetShouldDraw()  && rotateTarget) {
				if (mouseDelta.x > 0 || mouseDelta.x < 0) {
					RE::Actor* a = RE::TESForm::GetFormByID(previewTarget)->As<RE::Actor>();
					if (a && a->Get3D()) {
						a->data.angle.z -= (float)mouseDelta.x / 100.f;
					}
				}
			}
			lastMousePos = mousePos;
			break;
		}
		case WM_RBUTTONDOWN:
			rotateTarget = true;
			break;
		case WM_RBUTTONUP:
			rotateTarget = false;
			break;
		case WM_SYSKEYUP:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
			break;
		case WM_KEYDOWN:
			bool isPressed = (lParam & 0x40000000) != 0x0;
			if (!isPressed && wParam == editorKey) {
				if (Window::GetSingleton())
					Window::GetSingleton()->ToggleShow();
			}
			break;
		}

		if (Window::GetSingleton() && Window::GetSingleton()->GetShouldDraw()) {
			ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
			return true;
		}

		return CallWindowProc(WndProc_Orig, hWnd, msg, wParam, lParam);
	}

	/* ATOM __stdcall HookedRegisterClassA(const WNDCLASSEXA* wnd)
	{
		WNDCLASSEXA tmp = *wnd;

		WndProc_Orig = wnd->lpfnWndProc;
		tmp.lpfnWndProc = WndProcHandler;

		return RegisterClassA_Orig(&tmp);
	}*/

	HRESULT __stdcall HookedPresent(IDXGISwapChain* SwapChain, UINT SyncInterval, UINT Flags) {
		if (Window::GetSingleton()) {
			if (!Window::imguiInitialized) {
				Window::ImGuiInit();
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();

			ImGui::NewFrame();

			Window::GetSingleton()->Draw();

			ImGui::Render();
			ImGui::EndFrame();

			auto* drawData = ImGui::GetDrawData();
			if (drawData) {
				ImGui_ImplDX11_RenderDrawData(drawData);
			}
		}

		return D3D11Present_Orig(SwapChain, SyncInterval, Flags);
	}

	HRESULT __stdcall HookedD3D11CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software, UINT Flags,
		const D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
		IDXGISwapChain** ppSwapChain,
		ID3D11Device** ppDevice,
		D3D_FEATURE_LEVEL* pFeatureLevel,
		ID3D11DeviceContext** ppImmediateContext)
	{
		HRESULT res = D3D11CreateDeviceAndSwapChain_Orig(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);

		if (res == S_OK) {
			REL::Relocation<uintptr_t> swapChain_vtbl(*(std::uintptr_t*)(*ppSwapChain));
			D3D11Present_Orig = (FnD3D11Present)swapChain_vtbl.write_vfunc(8, &HookedPresent);
			_MESSAGE("D3D11 Device created. SwapChain vtbl %llx", swapChain_vtbl.address());

			window = ::GetActiveWindow();

			::GetWindowRect(window, &windowRect);

			d3d11SwapChain = *ppSwapChain;
			d3d11Device = *ppDevice;
			d3d11Context = *ppImmediateContext;
		}

		return res;
	}

	void HookD3D11() {
		_MESSAGE("Hooking D3D11 calls");
		F4SE::Trampoline& trampoline = F4SE::GetTrampoline();
		D3D11CreateDeviceAndSwapChain_Orig = (FnD3D11CreateDeviceAndSwapChain)trampoline.write_call<5>(ptr_D3D11CreateDeviceAndSwapChainCall.address(), &HookedD3D11CreateDeviceAndSwapChain);
		//D3D11CreateDeviceAndSwapChain_Orig = *(FnD3D11CreateDeviceAndSwapChain*)ptr_D3D11CreateDeviceAndSwapChain.address();
		//ptr_D3D11CreateDeviceAndSwapChain.write_vfunc(0, &HookedD3D11CreateDeviceAndSwapChain);
		ClipCursor_Orig = *(FnClipCursor*)ptr_ClipCursor.address();
		ptr_ClipCursor.write_vfunc(0, &HookedClipCursor);
		//RegisterClassA_Orig = *(FnRegisterClassA*)ptr_RegisterClassA.address();
		//ptr_RegisterClassA.write_vfunc(0, &HookedRegisterClassA);
		_MESSAGE("CreateDevice %llx ClipCursor %llx", D3D11CreateDeviceAndSwapChain_Orig, ClipCursor_Orig);
	}

	void Window::BuildBoneData(auto json, vector<TranslationData>& map, float& s) {
		for (auto boneit = json.begin(); boneit != json.end(); ++boneit) {
			if (boneit.key() != str_SetScale) {
				float x = 0;
				float y = 0;
				float z = 0;
				float scale = 1;
				bool ignoreXYZ = false;
				bool insertion = false;
				bool tponly = false;
				for (auto valit = boneit.value().begin(); valit != boneit.value().end(); ++valit) {
					if (valit.key() == str_BoneExtraX) {
						x = valit.value().get<float>();
					} else if (valit.key() == str_BoneExtraY) {
						y = valit.value().get<float>();
					} else if (valit.key() == str_BoneExtraZ) {
						z = valit.value().get<float>();
					} else if (valit.key() == str_BoneExtraScale) {
						scale = valit.value().get<float>();
					} else if (valit.key() == str_BoneExtraIgnoreXYZ) {
						ignoreXYZ = valit.value().get<bool>();
					} else if (valit.key() == str_BoneExtraInsertion) {
						insertion = valit.value().get<bool>();
					} else if (valit.key() == str_BoneExtraTpOnly) {
						tponly = valit.value().get<bool>();
					}
				}
				std::string bonename = boneit.key();
				if (insertion) {
					bonename += "SurgeonInserted";
				}
				map.push_back(TranslationData(bonename, boneit.key(), RE::NiPoint3(x, y, z), scale, ignoreXYZ, insertion, tponly));
			} else {
				s = boneit.value().get<float>();
			}
		}
	}

	void Window::SetSelected(std::string plugin, std::string formID, FORM_TYPE formType, std::string genderStr) {
		selectedPlugin = plugin;
		selectedFormID = formID;
		selectedFormType = formType;
		if (genderStr == str_Female) {
			selectedRaceGender = RACE_GENDER::kFemale;
		} else {
			selectedRaceGender = RACE_GENDER::kMale;
		}
	}

	void Window::Reset3DByFormID(uint32_t formID) {
		RE::Actor* a = RE::TESForm::GetFormByID(formID)->As<RE::Actor>();
		if (a && a->Get3D()) {
			a->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kSkeleton);
			a->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kScale);
			a->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kFace);
			a->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kHead);
			RE::TaskQueueInterface::GetSingleton()->QueueUpdate3D(a);
		}
	}

	void Window::ApplySelectedToTarget() {
		RE::Actor* a = RE::TESForm::GetFormByID(previewTarget)->As<RE::Actor>();
		if (!a)
			return;
		if (selectedFormID != "") {
			vector<TranslationData> tempTransData;
			float tempScale = -1.f;
			if (selectedFormType == FORM_TYPE::kNPC) {
				BuildBoneData(jsonData[selectedPlugin][selectedFormID], tempTransData, tempScale);
			} else if (selectedFormType == FORM_TYPE::kRace) {
				if (selectedRaceGender == RACE_GENDER::kMale) {
					BuildBoneData(jsonData[selectedPlugin][selectedFormID][str_Male], tempTransData, tempScale);
				} else if (selectedRaceGender == RACE_GENDER::kFemale) {
					BuildBoneData(jsonData[selectedPlugin][selectedFormID][str_Female], tempTransData, tempScale);
				}
			}
			DoSurgeryPreview(a, tempTransData);
			if (tempScale > 0) {
				DoScalePreview(a, tempScale);
			}
			if (previewedList.find(previewTarget) == previewedList.end()) {
				previewedList.insert(previewTarget);
			}
		}
	}

	void Window::OnPresetChange() {
		selectedFormID = "";
		selectedFormType = FORM_TYPE::kNone;
		selectedRaceGender = RACE_GENDER::kNone;
		selectedPlugin = "";
		jsonLoaded = LoadPreset(presetList.at(presetCurrentIdx).path);
	}

	void Window::ChangeCurrentPreset(int idx) {
		if (presetCurrentIdx == idx)
			return;
		presetCurrentIdx = idx;
		OnPresetChange();
	}

	void Window::HelpMarker(const char* desc) {
		ImGui::TextDisabled("(?)");
		if (ImGui::BeginItemTooltip()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	void Window::ImGuiInit() {
		IMGUI_CHECKVERSION();

		DXGI_SWAP_CHAIN_DESC sd;
		d3d11SwapChain->GetDesc(&sd);

		WndProc_Orig = (WNDPROC) SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, (LONG_PTR)WndProcHandler);

		ImGui::CreateContext();
		imguiIO = ImGui::GetIO();

		ImVector<ImWchar> ranges;
		ImFontGlyphRangesBuilder builder;
		builder.AddRanges(imguiIO.Fonts->GetGlyphRangesKorean());
		builder.AddRanges(imguiIO.Fonts->GetGlyphRangesDefault());
		builder.AddRanges(imguiIO.Fonts->GetGlyphRangesGreek());
		builder.AddRanges(imguiIO.Fonts->GetGlyphRangesCyrillic());
		builder.BuildRanges(&ranges);

		namespace fs = std::filesystem;
		fs::path fontPath = fs::current_path();
		fontPath += "\\Data\\F4SE\\Plugins\\NanakoSurgeon\\Fonts\\";
		std::string notosansPathKR = fontPath.string() + "NotoSansKR-Light.ttf";
		std::string notosansPathJP = fontPath.string() + "NotoSansJP-Light.ttf";
		std::string notosansPathSC = fontPath.string() + "NotoSansSC-Light.ttf";
		std::string notosansPathTC = fontPath.string() + "NotoSansTC-Light.ttf";
		ImFontConfig config;
		config.MergeMode = true;
		notosansKR = imguiIO.Fonts->AddFontFromFileTTF(notosansPathKR.c_str(), 20.f, nullptr, ranges.Data);
		notosansJP = imguiIO.Fonts->AddFontFromFileTTF(notosansPathJP.c_str(), 20.f, &config, imguiIO.Fonts->GetGlyphRangesJapanese());
		notosansSC = imguiIO.Fonts->AddFontFromFileTTF(notosansPathSC.c_str(), 20.f, &config, imguiIO.Fonts->GetGlyphRangesChineseSimplifiedCommon());
		notosansTC = imguiIO.Fonts->AddFontFromFileTTF(notosansPathTC.c_str(), 20.f, &config, imguiIO.Fonts->GetGlyphRangesChineseFull());
		imguiIO.Fonts->Build();

		ImGui::StyleColorsDark();

		bool imguiWin32Init = ImGui_ImplWin32_Init(window);
		bool imguiDX11Init = ImGui_ImplDX11_Init(d3d11Device.Get(), d3d11Context.Get());

		if (imguiWin32Init && imguiDX11Init) {
			_MESSAGE("ImGui Init Success");
			Localization::InitImGuiSettings();
			Localization::LoadLocalizations();
		}
		
		Window::imguiInitialized = true;
	}

	float tempFloat = 0.f;
	void Window::Draw() {
		if (!this->shouldDraw)
			return;

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
		ImGuiWindowFlags window_flags = 0;
		window_flags |= ImGuiWindowFlags_MenuBar;
		ImGui::Begin(Localization::GetLocalizedPChar("str_EditorName"), NULL, window_flags);

		int requestPopup = 0;

		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu(Localization::GetLocalizedPChar("str_File"))) {
				if (ImGui::MenuItem(Localization::GetLocalizedPChar("str_New"))) {
					requestPopup = 1;
				}
				if (ImGui::MenuItem(Localization::GetLocalizedPChar("str_Save"))) {
					SaveCurrentPreset();
				}
				if (ImGui::MenuItem(Localization::GetLocalizedPChar("str_Delete"))) {
					requestPopup = 2;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(Localization::GetLocalizedPChar("str_Language"))) {
				for (auto& lang : Localization::GetSupportedLanguages()) {
					if (ImGui::MenuItem(lang.c_str())) {
						Localization::SetCurrentLanguage(lang);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		const char* previewValue = presetList.at(presetCurrentIdx).displayName.c_str();
		if (ImGui::BeginCombo(Localization::GetLocalizedPChar("str_Preset"), previewValue, 0)) {
			for (int n = 0; n < presetList.size(); n++) {
				const bool is_selected = (presetCurrentIdx == n);
				if (ImGui::Selectable(presetList.at(n).displayName.c_str(), is_selected)) {
					ChangeCurrentPreset(n);
				}

				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SeparatorText(Localization::GetLocalizedPChar("str_Setting"));
		ImVec2 sz = ImVec2(-FLT_MIN, 0.0f);
		if (ImGui::Button(Localization::GetLocalizedPChar("str_ResetTarget"), sz)) {
			Reset3DByFormID(previewTarget);
		}
		if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_ApplySelectedToTarget"), &applyToTarget) && applyToTarget) {
			ApplySelectedToTarget();
		}
		ImGui::SameLine();
		HelpMarker(Localization::GetLocalizedPChar("str_ApplySelectedToTarget_Help"));
		RE::TESObjectREFR* refr = RE::Console::GetCurrentPickREFR().get().get();
		RE::TESNPC* targetNPC = nullptr;
		if (refr && refr->As<RE::Actor>()) {
			previewTarget = refr->formID;
			targetNPC = static_cast<RE::Actor*>(refr)->GetNPC();
		} else {
			previewTarget = p->formID;
			targetNPC = p->GetNPC();
		}
		if (applyToTarget) {
			std::string str_PreviewTargetInfo = Localization::GetLocalizedString("str_PreviewTarget");
			if (targetNPC) {
				str_PreviewTargetInfo += targetNPC->GetFullName();
				str_PreviewTargetInfo += std::format(" ({:#x})", targetNPC->formID);
			}
			ImGui::TextColored(ImVec4(0.f, 0.95f, 0.f, 1.f), str_PreviewTargetInfo.c_str());
		}

		if (!jsonLoaded) {
			ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_NoPresetLoaded"));
		} else {
			ImGui::SeparatorText(Localization::GetLocalizedPChar("str_Race"));
			if (ImGui::BeginPopupContextItem("_Popup_AddRace")) {
				if (ImGui::Selectable(Localization::GetLocalizedPChar("str_AddRace"))) {
					requestPopup = 3;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			for (auto pluginit = jsonRaceList.begin(); pluginit != jsonRaceList.end(); ++pluginit) {
				std::string pluginNode_ID = "plugin_race_" + pluginit->first;
				bool isPluginNodeOpen = ImGui::TreeNodeEx(pluginNode_ID.c_str(), nodeFlags, "%s", pluginit->first.c_str());
				if (ImGui::BeginPopupContextItem((pluginNode_ID + "_Popup").c_str())) {
					if (ImGui::Selectable(Localization::GetLocalizedPChar("str_AddRace"))) {
						requestPopup = 3;
						ImGui::CloseCurrentPopup();
					}
					if (ImGui::Selectable(Localization::GetLocalizedPChar("str_DeletePlugin"))) {
						jsonData.erase(pluginit->first);
						jsonRaceList.erase(pluginit);
						isPluginNodeOpen = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
				if (isPluginNodeOpen) {
					for (auto raceit = pluginit->second.begin(); raceit != pluginit->second.end(); ++raceit) {
						RE::TESForm* raceForm = GetFormFromMod(pluginit->first, std::stoi(*raceit, 0, 16));
						if (raceForm) {
							std::string raceNode_ID = raceForm->GetFormEditorID();
							bool isRaceNodeOpen = ImGui::TreeNodeEx(raceNode_ID.c_str(), nodeFlags, "%s (%s)", raceNode_ID.c_str(), raceit->c_str());
							if (ImGui::BeginPopupContextItem((raceNode_ID + "_Popup").c_str())) {
								if (ImGui::Selectable(Localization::GetLocalizedPChar("str_AddGender"))) {
									jsonInsertTarget = &jsonData[pluginit->first][*raceit];
									requestPopup = 5;
									ImGui::CloseCurrentPopup();
								}
								if (ImGui::Selectable(Localization::GetLocalizedPChar("str_DeleteRace"))) {
									jsonData[pluginit->first].erase(*raceit);
									auto findForms = jsonRaceList.find(pluginit->first);
									if (findForms != jsonRaceList.end()) {
										vector<std::string> tempList = findForms->second;
										std::string tempFormID = *raceit;
										findForms->second.clear();
										for (std::string formID : tempList) {
											if (formID != tempFormID) {
												findForms->second.push_back(formID);
											}
										}
									}
									isRaceNodeOpen = false;
									ImGui::CloseCurrentPopup();
									ImGui::EndPopup();
									break;
								}
								ImGui::EndPopup();
							}
							if (isRaceNodeOpen) {
								auto& genders = jsonData[pluginit->first][*raceit];
								for (auto sexit = genders.begin(); sexit != genders.end(); ++sexit) {
									std::string genderNode_ID = raceNode_ID + "_" + sexit.key();
									bool isGenderNodeOpen = ImGui::TreeNodeEx(genderNode_ID.c_str(), nodeFlags, "%s", sexit.key().c_str());
									if (ImGui::BeginPopupContextItem((genderNode_ID + "_Popup").c_str())) {
										if (ImGui::Selectable(Localization::GetLocalizedPChar("str_AddBone"))) {
											jsonInsertTarget = &jsonData[pluginit->first][*raceit][sexit.key()];
											requestPopup = 6;
											ImGui::CloseCurrentPopup();
										}
										if (ImGui::Selectable(Localization::GetLocalizedPChar("str_DeleteGender"))) {
											jsonData[pluginit->first][*raceit].erase(sexit.key());
											isGenderNodeOpen = false;
											ImGui::CloseCurrentPopup();
										}
										ImGui::EndPopup();
									}
									if (isGenderNodeOpen) {
										std::string setScale_ID = genderNode_ID + "_SetScale";
										bool hasSetScale = jsonData[pluginit->first][*raceit][sexit.key()].contains(str_SetScale);
										if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_HasSetScale"), &hasSetScale)) {
											if (!hasSetScale) {
												if (jsonData[pluginit->first][*raceit][sexit.key()].contains(str_SetScale)) {
													jsonData[pluginit->first][*raceit][sexit.key()].erase(str_SetScale);
												}
											} else {
												if (!jsonData[pluginit->first][*raceit][sexit.key()].contains(str_SetScale)) {
													jsonData[pluginit->first][*raceit][sexit.key()][str_SetScale] = 1.f;
												}
											}
										}
										if (hasSetScale) {
											float tempScale = jsonData[pluginit->first][*raceit][sexit.key()][str_SetScale].get<float>();
											if (ImGui::DragFloat(Localization::GetLocalizedPChar("str_Scale"), &tempScale, 0.01f, 0.01f, 10.f, "%.2f")) {
												jsonData[pluginit->first][*raceit][sexit.key()][str_SetScale] = tempScale;
												if (applyToTarget) {
													SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
													ApplySelectedToTarget();
												}
											}
										}
										for (auto boneit = sexit.value().begin(); boneit != sexit.value().end(); ++boneit) {
											if (boneit.key() != str_SetScale) {
												std::string boneNode_ID = genderNode_ID + "_" + boneit.key();
												bool isBoneNodeOpen = ImGui::TreeNodeEx(boneNode_ID.c_str(), nodeFlags, "%s", boneit.key().c_str());
												if (ImGui::BeginPopupContextItem((boneNode_ID + "_Popup").c_str())) {
													if (ImGui::Selectable(Localization::GetLocalizedPChar("str_DeleteBone"))) {
														jsonData[pluginit->first][*raceit][sexit.key()].erase(boneit.key());
														isBoneNodeOpen = false;
														if (applyToTarget) {
															SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
															Reset3DByFormID(previewTarget);
															ApplySelectedToTarget();
														}
														ImGui::CloseCurrentPopup();
													}
													ImGui::EndPopup();
												}
												if (isBoneNodeOpen) {
													std::string extraNode_ID = boneNode_ID + "_Extra";
													bool isExtraNodeOpen = ImGui::TreeNodeEx(extraNode_ID.c_str(), nodeFlags, Localization::GetLocalizedPChar("str_ExtraData"));
													if (isExtraNodeOpen) {
														bool ignoreXYZ = false;
														if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
															ignoreXYZ = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraIgnoreXYZ];
														}
														if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_IgnoreXYZ"), &ignoreXYZ)) {
															if (!ignoreXYZ) {
																if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].erase(str_BoneExtraIgnoreXYZ);
																}
															} else {
																if (!jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraIgnoreXYZ] = true;
																}
															}
															if (applyToTarget) {
																SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
																ApplySelectedToTarget();
															}
														}
														bool insertion = false;
														if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraInsertion)) {
															insertion = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraInsertion];
														}
														if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_Insertion"), &insertion)) {
															if (!insertion) {
																if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraInsertion)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].erase(str_BoneExtraInsertion);
																}
															} else {
																if (!jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraInsertion)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraInsertion] = true;
																}
															}
															if (applyToTarget) {
																SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
																ApplySelectedToTarget();
															}
														}
														ImGui::SameLine();
														HelpMarker(Localization::GetLocalizedPChar("str_Insertion_Help"));
														bool tponly = false;
														if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraTpOnly)) {
															tponly = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraTpOnly];
														}
														if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_TpOnly"), &tponly)) {
															if (!tponly) {
																if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraTpOnly)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].erase(str_BoneExtraTpOnly);
																}
															} else {
																if (!jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraTpOnly)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraTpOnly] = true;
																}
															}
															if (applyToTarget) {
																SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
																ApplySelectedToTarget();
															}
														}
														ImGui::TreePop();
													}
													float transVec4f[4] = { 0.f, 0.f, 0.f, 0.f };
													if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraX)) {
														transVec4f[0] = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraX].get<float>();
													}
													if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraY)) {
														transVec4f[1] = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraY].get<float>();
													}
													if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraZ)) {
														transVec4f[2] = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraZ].get<float>();
													}
													if (ImGui::DragFloat3(Localization::GetLocalizedPChar("str_Translation"), transVec4f, 0.01f, -20.f, 20.f)) {
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraX] = transVec4f[0];
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraY] = transVec4f[1];
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraZ] = transVec4f[2];
														if (applyToTarget) {
															SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
															ApplySelectedToTarget();
														}
													}
													float boneScale = 1.f;
													if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraScale)) {
														boneScale = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraScale].get<float>();
													}
													if (ImGui::DragFloat(Localization::GetLocalizedPChar("str_Scale"), &boneScale, 0.01f, 0.01f, 10.f, "%.2f")) {
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraScale] = boneScale;
														if (applyToTarget) {
															SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
															ApplySelectedToTarget();
														}
													}
													ImGui::TreePop();
												}
											}
										}
										ImGui::TreePop();
									}
								}
								ImGui::TreePop();
							}
						}
					}
					ImGui::TreePop();
				}
			}
			ImGui::SeparatorText("NPC");
			if (ImGui::BeginPopupContextItem("_Popup_AddNPC")) {
				if (ImGui::Selectable(Localization::GetLocalizedPChar("str_AddNPC"))) {
					requestPopup = 4;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			for (auto pluginit = jsonNPCList.begin(); pluginit != jsonNPCList.end(); ++pluginit) {
				std::string pluginNode_ID = "plugin_npc_" + pluginit->first;
				bool isPluginNodeOpen = ImGui::TreeNodeEx(pluginNode_ID.c_str(), nodeFlags, "%s", pluginit->first.c_str());
				if (ImGui::BeginPopupContextItem((pluginNode_ID + "_Popup").c_str())) {
					if (ImGui::Selectable(Localization::GetLocalizedPChar("str_AddNPC"))) {
						requestPopup = 4;
						ImGui::CloseCurrentPopup();
					}
					if (ImGui::Selectable(Localization::GetLocalizedPChar("str_DeletePlugin"))) {
						jsonData.erase(pluginit->first);
						jsonNPCList.erase(pluginit);
						isPluginNodeOpen = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
				if (isPluginNodeOpen) {
					for (auto npcit = pluginit->second.begin(); npcit != pluginit->second.end(); ++npcit) {
						RE::TESNPC* npcForm = GetFormFromMod(pluginit->first, std::stoi(*npcit, 0, 16))->As<RE::TESNPC>();
						if (npcForm) {
							std::string npcNode_ID = pluginit->first + (*npcit);
							bool isNPCNodeOpen = ImGui::TreeNodeEx(npcNode_ID.c_str(), nodeFlags, "%s (%s)", npcForm->GetFullName(), npcit->c_str());
							if (ImGui::BeginPopupContextItem((npcNode_ID + "_Popup").c_str())) {
								if (ImGui::Selectable(Localization::GetLocalizedPChar("str_AddBone"))) {
									jsonInsertTarget = &jsonData[pluginit->first][*npcit];
									requestPopup = 6;
									ImGui::CloseCurrentPopup();
								}
								if (ImGui::Selectable(Localization::GetLocalizedPChar("str_DeleteNPC"))) {
									jsonData[pluginit->first].erase(*npcit);
									auto findForms = jsonNPCList.find(pluginit->first);
									if (findForms != jsonNPCList.end()) {
										vector<std::string> tempList = findForms->second;
										std::string tempFormID = *npcit;
										findForms->second.clear();
										for (std::string formID : tempList) {
											if (formID != tempFormID) {
												findForms->second.push_back(formID);
											}
										}
									}
									isNPCNodeOpen = false;
									ImGui::CloseCurrentPopup();
									ImGui::EndPopup();
									break;
								}
								ImGui::EndPopup();
							}
							if (isNPCNodeOpen) {
								std::string setScale_ID = npcNode_ID + "_SetScale";
								bool hasSetScale = jsonData[pluginit->first][*npcit].contains(str_SetScale);
								if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_HasSetScale"), &hasSetScale)) {
									if (!hasSetScale) {
										if (jsonData[pluginit->first][*npcit].contains(str_SetScale)) {
											jsonData[pluginit->first][*npcit].erase(str_SetScale);
										}
									} else {
										if (!jsonData[pluginit->first][*npcit].contains(str_SetScale)) {
											jsonData[pluginit->first][*npcit][str_SetScale] = 1.f;
										}
									}
								}
								if (hasSetScale) {
									float tempScale = jsonData[pluginit->first][*npcit][str_SetScale].get<float>();
									if (ImGui::DragFloat(Localization::GetLocalizedPChar("str_Scale"), &tempScale, 0.01f, 0.01f, 10.f, "%.2f")) {
										jsonData[pluginit->first][*npcit][str_SetScale] = tempScale;
										if (applyToTarget) {
											SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
											ApplySelectedToTarget();
										}
									}
								}
								auto& npcs = jsonData[pluginit->first][*npcit];
								for (auto boneit = npcs.begin(); boneit != npcs.end(); ++boneit) {
									if (boneit.key() != str_SetScale) {
										std::string boneNode_ID = npcNode_ID + "_" + boneit.key();
										bool isBoneNodeOpen = ImGui::TreeNodeEx(boneNode_ID.c_str(), nodeFlags, "%s", boneit.key().c_str());
										if (ImGui::BeginPopupContextItem((boneNode_ID + "_Popup").c_str())) {
											if (ImGui::Selectable(Localization::GetLocalizedPChar("str_DeleteBone"))) {
												jsonData[pluginit->first][*npcit].erase(boneit.key());
												isBoneNodeOpen = false;
												if (applyToTarget) {
													SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
													Reset3DByFormID(previewTarget);
													ApplySelectedToTarget();
												}
												ImGui::CloseCurrentPopup();
											}
											ImGui::EndPopup();
										}
										if (isBoneNodeOpen) {
											std::string extraNode_ID = boneNode_ID + "_Extra";
											bool isExtraNodeOpen = ImGui::TreeNodeEx(extraNode_ID.c_str(), nodeFlags, Localization::GetLocalizedPChar("str_ExtraData"));
											if (isExtraNodeOpen) {
												bool ignoreXYZ = false;
												if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
													ignoreXYZ = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraIgnoreXYZ];
												}
												if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_IgnoreXYZ"), &ignoreXYZ)) {
													if (!ignoreXYZ) {
														if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
															jsonData[pluginit->first][*npcit][boneit.key()].erase(str_BoneExtraIgnoreXYZ);
														}
													} else {
														if (!jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
															jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraIgnoreXYZ] = true;
														}
													}
													if (applyToTarget) {
														SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
														ApplySelectedToTarget();
													}
												}
												bool insertion = false;
												if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraInsertion)) {
													insertion = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraInsertion];
												}
												if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_Insertion"), &insertion)) {
													if (!insertion) {
														if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraInsertion)) {
															jsonData[pluginit->first][*npcit][boneit.key()].erase(str_BoneExtraInsertion);
														}
													} else {
														if (!jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraInsertion)) {
															jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraInsertion] = true;
														}
													}
													if (applyToTarget) {
														SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
														ApplySelectedToTarget();
													}
												}
												ImGui::SameLine();
												HelpMarker(Localization::GetLocalizedPChar("str_Insertion_Help"));
												bool tponly = false;
												if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraTpOnly)) {
													tponly = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraTpOnly];
												}
												if (ImGui::Checkbox(Localization::GetLocalizedPChar("str_TpOnly"), &tponly)) {
													if (!tponly) {
														if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraTpOnly)) {
															jsonData[pluginit->first][*npcit][boneit.key()].erase(str_BoneExtraTpOnly);
														}
													} else {
														if (!jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraTpOnly)) {
															jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraTpOnly] = true;
														}
													}
													if (applyToTarget) {
														SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
														ApplySelectedToTarget();
													}
												}
												ImGui::TreePop();
											}
											float transVec4f[4] = { 0.f, 0.f, 0.f, 0.f };
											if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraX)) {
												transVec4f[0] = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraX].get<float>();
											}
											if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraY)) {
												transVec4f[1] = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraY].get<float>();
											}
											if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraZ)) {
												transVec4f[2] = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraZ].get<float>();
											}
											if (ImGui::DragFloat3(Localization::GetLocalizedPChar("str_Translation"), transVec4f, 0.01f, -20.f, 20.f)) {
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraX] = transVec4f[0];
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraY] = transVec4f[1];
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraZ] = transVec4f[2];
												if (applyToTarget) {
													SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
													ApplySelectedToTarget();
												}
											}
											float boneScale = 1.f;
											if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraScale)) {
												boneScale = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraScale].get<float>();
											}
											if (ImGui::DragFloat(Localization::GetLocalizedPChar("str_Scale"), &boneScale, 0.01f, 0.01f, 10.f, "%.2f")) {
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraScale] = boneScale;
												if (applyToTarget) {
													SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
													ApplySelectedToTarget();
												}
											}
											ImGui::TreePop();
										}
									}
								}
								ImGui::TreePop();
							}
						}
					}
					ImGui::TreePop();
				}
			}
		}

		if (requestPopup > 0) {
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			switch (requestPopup) {
			case 1:
				ImGui::OpenPopup(Localization::GetLocalizedPChar("str_Modal_NewFile"));
				break;
			case 2:
				ImGui::OpenPopup(Localization::GetLocalizedPChar("str_Modal_AskAgain"));
				break;
			case 3:
				ImGui::OpenPopup(Localization::GetLocalizedPChar("str_Modal_AddRace"));
				break;
			case 4:
				ImGui::OpenPopup(Localization::GetLocalizedPChar("str_Modal_AddNPC"));
				break;
			case 5:
				ImGui::OpenPopup(Localization::GetLocalizedPChar("str_Modal_AddGender"));
				break;
			case 6:
				ImGui::OpenPopup(Localization::GetLocalizedPChar("str_Modal_AddBone"));
				break;
			}
		}

		if (ImGui::BeginPopupModal(Localization::GetLocalizedPChar("str_Modal_NewFile"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text(Localization::GetLocalizedPChar("str_EnterFileName"));
			ImGui::Separator();

			static char filename[64] = "";
			static bool errorMsg = false;
			ImGui::InputText("", filename, 64);
			ImGui::SameLine();
			ImGui::Text(".json");
			if (ImGui::Button(Localization::GetLocalizedPChar("str_Create"), ImVec2(120, 0))) {
				std::string nameStr = filename;
				if (nameStr.length() == 0) {
					if (!errorMsg) {
						errorMsg = true;
					}
				} else {
					nameStr += ".json";
					this->NewPreset(nameStr);
					errorMsg = false;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(Localization::GetLocalizedPChar("str_Cancel"), ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			if (errorMsg) {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_EnterText"));
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(Localization::GetLocalizedPChar("str_Modal_AskAgain"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (presetCurrentIdx == 0) {
				ImGui::Text(Localization::GetLocalizedPChar("str_CannotDeleteDefault"));
				ImGui::Separator();

				if (ImGui::Button(Localization::GetLocalizedPChar("str_Understand"), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			} else {
				ImGui::Text(Localization::GetLocalizedPChar("str_CannotRevert"));
				ImGui::Separator();

				if (ImGui::Button(Localization::GetLocalizedPChar("str_Positive"), ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
					this->DeletePreset(presetList.at(presetCurrentIdx).path);
				}
				ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Negative"), ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(Localization::GetLocalizedPChar("str_Modal_AddRace"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::vector<FormRecord> raceRecords;
			static int raceCurrentIdx = 0;

			for (auto& race : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESRace>()) {
				if (race) {
					RE::TESFile* file = race->sourceFiles.array->front();
					std::string plugin = file->filename;
					uint32_t rawFormID = race->formID - (file->compileIndex << 24) - (file->smallFileCompileIndex << 12);
					std::string formID = std::format("{:#x}", rawFormID);
					if (!jsonData.contains(plugin) || !jsonData[plugin].contains(formID)) {
						raceRecords.push_back(FormRecord(plugin, formID, race->GetFormEditorID()));
					}
				}
			}
			if (raceRecords.size() > 0) {
				if (raceCurrentIdx >= raceRecords.size())
					raceCurrentIdx = (int)(raceRecords.size() - 1);
				struct Funcs {
					static bool SortByDisplayName(FormRecord& r1, FormRecord& r2) {
						return r1.displayName.compare(r2.displayName) < 0;
					}
				};
				std::sort(raceRecords.begin(), raceRecords.end(), Funcs::SortByDisplayName);
				const char* racePreviewValue = raceRecords.at(raceCurrentIdx).displayName.c_str();
				if (ImGui::BeginCombo(Localization::GetLocalizedPChar("str_Race"), racePreviewValue, 0)) {
					for (int n = 0; n < raceRecords.size(); n++) {
						const bool is_selected = (raceCurrentIdx == n);
						if (ImGui::Selectable(raceRecords.at(n).displayName.c_str(), is_selected)) {
							raceCurrentIdx = n;
						}

						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Add"), ImVec2(120, 0))) {
					FormRecord& formRecord = raceRecords.at(raceCurrentIdx);
					auto existit = jsonRaceList.find(formRecord.plugin);
					if (existit == jsonRaceList.end()) {
						jsonRaceList.insert(std::pair<std::string, vector<std::string>>(formRecord.plugin, vector<std::string>{ formRecord.formID }));
					} else {
						existit->second.push_back(formRecord.formID);
					}
					jsonData[formRecord.plugin][formRecord.formID] = nlohmann::json({});
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Cancel"), ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
			} else {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_RaceEmpty"));
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Positive"), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(Localization::GetLocalizedPChar("str_Modal_AddNPC"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::vector<FormRecord> npcRecords;
			static int npcCurrentIdx = 0;

			for (auto& npc : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESNPC>()) {
				if (npc) {
					RE::TESFile* file = npc->sourceFiles.array->front();
					std::string plugin = file->filename;
					uint32_t rawFormID = npc->formID - (file->compileIndex << 24) - (file->smallFileCompileIndex << 12);
					std::string formID = std::format("{:#x}", rawFormID);
					std::string npcFullName = std::format("{} ({})", npc->GetFullName(), formID.c_str());
					if (!jsonData.contains(plugin) || !jsonData[plugin].contains(formID)) {
						npcRecords.push_back(FormRecord(plugin, formID, npcFullName));
					}
				}
			}
			if (npcRecords.size() > 0) {
				if (npcCurrentIdx >= npcRecords.size())
					npcCurrentIdx = (int)(npcRecords.size() - 1);
				struct Funcs
				{
					static bool SortByDisplayName(FormRecord& r1, FormRecord& r2)
					{
						return r1.displayName.compare(r2.displayName) < 0;
					}
				};
				std::sort(npcRecords.begin(), npcRecords.end(), Funcs::SortByDisplayName);
				const char* npcPreviewValue = npcRecords.at(npcCurrentIdx).displayName.c_str();
				if (ImGui::BeginCombo(Localization::GetLocalizedPChar("str_Race"), npcPreviewValue, 0)) {
					for (int n = 0; n < npcRecords.size(); n++) {
						const bool is_selected = (npcCurrentIdx == n);
						if (ImGui::Selectable(npcRecords.at(n).displayName.c_str(), is_selected)) {
							npcCurrentIdx = n;
						}

						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Add"), ImVec2(120, 0))) {
					FormRecord& formRecord = npcRecords.at(npcCurrentIdx);
					auto existit = jsonNPCList.find(formRecord.plugin);
					if (existit == jsonNPCList.end()) {
						jsonNPCList.insert(std::pair<std::string, vector<std::string>>(formRecord.plugin, vector<std::string>{ formRecord.formID }));
					} else {
						existit->second.push_back(formRecord.formID);
					}
					jsonData[formRecord.plugin][formRecord.formID] = nlohmann::json({});
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Cancel"), ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
			} else {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_RaceEmpty"));
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Positive"), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(Localization::GetLocalizedPChar("str_Modal_AddGender"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::vector<std::string> genderList;
			static int genderCurrentIdx = 0;
			if (!(*jsonInsertTarget).contains(str_Female)) {
				genderList.push_back(str_Female);
			}
			if (!(*jsonInsertTarget).contains(str_Male)) {
				genderList.push_back(str_Male);
			}
			if (genderList.size() > 0) {
				if (genderCurrentIdx >= genderList.size())
					genderCurrentIdx = (int)(genderList.size() - 1);
				const char* genderPreviewValue = genderList.at(genderCurrentIdx).c_str();
				if (ImGui::BeginCombo(Localization::GetLocalizedPChar("str_Gender"), genderPreviewValue, 0)) {
					for (int n = 0; n < genderList.size(); n++) {
						const bool is_selected = (genderCurrentIdx == n);
						if (ImGui::Selectable(genderList.at(n).c_str(), is_selected)) {
							genderCurrentIdx = n;
						}

						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Add"), ImVec2(120, 0))) {
					(*jsonInsertTarget)[genderList.at(genderCurrentIdx)] = nlohmann::json({});
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Cancel"), ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
			} else {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_GenderEmpty"));
				if (ImGui::Button(Localization::GetLocalizedPChar("str_Positive"), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(Localization::GetLocalizedPChar("str_Modal_AddBone"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::vector<std::string> boneList;
			std::unordered_map<std::string, bool> boneListDuplicateCheck;
			static int boneCurrentIdx = 0;
			RE::Actor* a = RE::TESForm::GetFormByID(previewTarget)->As<RE::Actor>();

			if (a && a->Get3D()) {
				RE::NiAVObject* root = a->Get3D()->GetObjectByName("Root");
				Visit(root, [&boneList, &boneListDuplicateCheck](RE::NiAVObject* obj) {
					if (!obj->IsNode())
						return false;

					std::string name = obj->name.c_str();
					if (!jsonInsertTarget->contains(name) && boneListDuplicateCheck.find(name) == boneListDuplicateCheck.end()) {
						boneList.push_back(name);
						boneListDuplicateCheck.insert(std::pair<std::string, bool>(name, true));
					}
					return false;
				});
			} else {
				ImGui::Text(Localization::GetLocalizedPChar("str_TargetBoneNotFound"));
			}
			std::sort(boneList.begin(), boneList.end());
			boneList.push_back(Localization::GetLocalizedPChar("str_ManualInput"));

			if (boneCurrentIdx >= boneList.size())
				boneCurrentIdx = (int)(boneList.size() - 1);

			const char* bonePreviewValue = boneList.at(boneCurrentIdx).c_str();
			if (ImGui::BeginCombo(Localization::GetLocalizedPChar("str_BoneName"), bonePreviewValue, 0)) {
				for (int n = 0; n < boneList.size(); n++) {
					const bool is_selected = (boneCurrentIdx == n);
					if (ImGui::Selectable(boneList.at(n).c_str(), is_selected)) {
						boneCurrentIdx = n;
					}

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			static char boneName[64] = "";
			static bool errorMsg = false;
			static int errorCode = 0;
			bool userInputText = false;
			if (boneCurrentIdx == boneList.size() - 1) {
				ImGui::InputText("", boneName, 64);
				userInputText = true;
			}
			if (errorMsg) {
				switch (errorCode){
				case 0:
					ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_EnterText"));
					break;
				case 1:
					ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_AlreadyExists"));
					break;
				case 2:
					ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), Localization::GetLocalizedPChar("str_CannotUseName"));
					break;
				}
			}
			if (ImGui::Button(Localization::GetLocalizedPChar("str_Add"), ImVec2(120, 0))) {
				std::string nameStr = boneList.at(boneCurrentIdx);
				errorMsg = false;
				if (userInputText) {
					nameStr = boneName;
				}
				if (nameStr.length() == 0) {
					errorMsg = true;
					errorCode = 0;
				}
				if (jsonInsertTarget->contains(nameStr)) {
					errorMsg = true;
					errorCode = 1;
				}
				if (nameStr == str_SetScale) {
					errorMsg = true;
					errorCode = 2;
				}
				if (!errorMsg) {
					(*jsonInsertTarget)[nameStr] = nlohmann::json({});
					errorMsg = false;
					errorCode = 0;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(Localization::GetLocalizedPChar("str_Cancel"), ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::End();
	}

	void Window::ToggleShow() {
		if (presetCurrentIdx == -1)
			return;

		this->shouldDraw = !this->shouldDraw;
		::ShowCursor(this->shouldDraw);
		RE::ControlMap::GetSingleton()->ignoreKeyboardMouse = this->shouldDraw;
		if (this->shouldDraw)
			RefreshFiles();
		else {
			for (uint32_t formID : previewedList) {
				Reset3DByFormID(formID);
			}
			previewedList.clear();
			LoadConfigs();
			DoGlobalSurgery();
			DoGlobalScaling();
			QueueSurgery(p);
			QueueScaling(p);
		}
	}

	void Window::Reset() {
		this->shouldDraw = false;
		::ShowCursor(this->shouldDraw);
		RE::ControlMap::GetSingleton()->ignoreKeyboardMouse = this->shouldDraw;

		selectedFormID = "";
		selectedFormType = FORM_TYPE::kNone;
		selectedRaceGender = RACE_GENDER::kNone;
		selectedPlugin = "";
	}

	void Window::RefreshFiles(bool resetSelected) {
		presetList.clear();

		namespace fs = std::filesystem;
		presetList.push_back(PresetFile((fs::current_path() += "\\Data\\F4SE\\Plugins\\NanakoSurgeon.json").string(), "NanakoSurgeon.json"));

		fs::path jsonPath = fs::current_path();
		jsonPath += "\\Data\\F4SE\\Plugins\\NanakoSurgeon";
		fs::directory_entry jsonEntry{ jsonPath };
		if (jsonEntry.exists()) {
			for (auto& it : fs::directory_iterator(jsonEntry)) {
				if (it.path().extension().compare(".json") == 0) {
					presetList.push_back(PresetFile(it.path().string(), it.path().filename().string()));
				}
			}
		}

		if (resetSelected) {
			ChangeCurrentPreset(0);
		}
	}

	void Window::NewPreset(std::string filename) {
		namespace fs = std::filesystem;
		fs::path jsonPath = fs::current_path();
		jsonPath += "\\Data\\F4SE\\Plugins\\NanakoSurgeon";
		fs::directory_entry jsonEntry{ jsonPath };
		if (jsonEntry.exists()) {
			fs::path filePath = jsonPath;
			filePath += "\\" + filename;
			std::ofstream writer(filePath, std::ios::trunc);
			writer << "{"
				   << "\n"
				   << "}" << std::endl;
			writer.close();
			_MESSAGE("Created new preset %s", filename.c_str());
			this->RefreshFiles();
		}
	}

	void Window::SaveCurrentPreset() {
		if (!jsonLoaded)
			_MESSAGE("No preset loaded! Cannot save!");

		namespace fs = std::filesystem;
		fs::path filePath = presetList.at(presetCurrentIdx).path;
		std::ofstream writer(filePath, std::ios::trunc);
		writer << jsonData.dump(4);
		writer.close();
		_MESSAGE("Saved preset %s", presetList.at(presetCurrentIdx).displayName.c_str());
	}

	void Window::DeletePreset(const std::filesystem::path path) {
		_MESSAGE("Deleted preset %s", path.filename().string().c_str());
		std::filesystem::remove(path);
		this->RefreshFiles(true);
	}

	void Window::LoadDefaultPreset() {
		if (presetCurrentIdx == -1) {
			RefreshFiles(true);
		}
	}

	bool Window::LoadPreset(const std::filesystem::path path) {
		try {
			std::ifstream reader;
			reader.open(path);
			jsonData.clear();
			reader >> jsonData;
			reader.close();

			jsonNPCList.clear();
			jsonRaceList.clear();

			for (auto pluginit = jsonData.begin(); pluginit != jsonData.end(); ++pluginit) {
				for (auto formit = pluginit.value().begin(); formit != pluginit.value().end(); ++formit) {
					RE::TESForm* form = GetFormFromMod(pluginit.key(), std::stoi(formit.key(), 0, 16));
					if (form) {
						if (form->GetFormType() == RE::ENUM_FORM_ID::kNPC_) {
							auto existit = jsonNPCList.find(pluginit.key());
							if (existit == jsonNPCList.end()) {
								jsonNPCList.insert(std::pair<std::string, vector<std::string>>(pluginit.key(), vector<std::string>{ formit.key() }));
							} else {
								existit->second.push_back(formit.key());
							}
						} else if (form->GetFormType() == RE::ENUM_FORM_ID::kRACE) {
							auto existit = jsonRaceList.find(pluginit.key());
							if (existit == jsonRaceList.end()) {
								jsonRaceList.insert(std::pair<std::string, vector<std::string>>(pluginit.key(), vector<std::string>{ formit.key() }));
							} else {
								existit->second.push_back(formit.key());
							}
						}
					}
				}
			}
		}
		catch (...) {
			_MESSAGE("Failed to load preset %s", path.filename().string().c_str());
			return false;
		}
		return true;
	}
}
