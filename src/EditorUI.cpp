#include "EditorUI.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <d3d11.h>
#include <fstream>
#include <wrl.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#pragma execution_character_set("utf-8")
namespace WRL = Microsoft::WRL;
using std::unordered_map;
using std::vector;

extern RE::PlayerCharacter* p;
extern uint32_t editorKey;
extern char* _MESSAGE(const char* fmt, ...);
extern char NotoSans_compressed_data_base85[4676395 + 1];
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

	// Stored variables for ImGui render
	RECT windowRect;
	ImGuiIO imguiIO;
	ImFont* notosans;
	WRL::ComPtr<IDXGISwapChain> d3d11SwapChain;
	WRL::ComPtr<ID3D11Device> d3d11Device;
	WRL::ComPtr<ID3D11DeviceContext> d3d11Context;

	// Variables for functionality
	static const ImWchar ranges[] = {
		0x0020,
		0xFFFF,  //almost language of utf8 range
		0,
	};
	static const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	vector<PresetFile> presetList{};
	int presetCurrentIdx = -1;
	std::string selectedFormID = "";
	FORM_TYPE selectedFormType = FORM_TYPE::kNone;
	RACE_GENDER selectedRaceGender = RACE_GENDER::kNone;
	std::string selectedPlugin = "";
	bool applyToPlayer = false;
	bool jsonLoaded = false;
	nlohmann::json jsonData;
	unordered_map<std::string, vector<std::string>> jsonRaceList;
	unordered_map<std::string, vector<std::string>> jsonNPCList;
	unordered_map<std::string, bool> hasSetScale;
	auto* jsonInsertTarget = &(jsonData);

	// Strings for future localization or stuff
	std::string str_EditorName = "외과의사 나나코 편집기";
	std::string str_File = "파일";
	std::string str_New = "새로 만들기";
	std::string str_Save = "저장";
	std::string str_Delete = "삭제";
	std::string str_Preset = "프리셋";
	std::string str_Setting = "설정";
	std::string str_ResetPlayer = "플레이어 모델 초기화";
	std::string str_ApplySelectedToPlayer = "선택된 항목을 플레이어에게 적용하기";
	std::string str_ApplySelectedToPlayer_Help =
		"현재 선택된 종족/NPC 항목을 플레이어에 적용시켜 미리볼 수 있습니다.\n"
		"뼈대가 일치하지 않는 경우 최종 결과물에 차이가 있을 수 있다는 점 유의해주세요.";
	std::string str_NoPresetLoaded = "프리셋 데이터가 없습니다.";
	std::string str_Race = "종족";
	std::string str_AddRace = "종족 추가";
	std::string str_AddGender = "성별 추가";
	std::string str_AddBone = "뼈 추가";
	std::string str_AddNPC = "NPC 추가";
	std::string str_DeletePlugin = "플러그인 삭제";
	std::string str_DeleteRace = "종족 삭제";
	std::string str_DeleteNPC = "NPC 삭제";
	std::string str_DeleteGender = "성별 삭제";
	std::string str_DeleteBone = "뼈 삭제";
	std::string str_HasSetScale = "스케일 변경 사용";
	std::string str_Scale = "스케일";
	std::string str_ExtraData = "세부 설정";
	std::string str_IgnoreXYZ = "XYZ 값 무시";
	std::string str_Insertion = "추가뼈 삽입";
	std::string str_Insertion_Help =
		"Head와 같은 뼈는 애니메이션이 위치와 스케일을 조정하기 때문에 외과의사 나나코가 수정할 수 없습니다..\n"
		"이러한 뼈를 수정하고 싶으시다면 해당 기능을 활성화해주세요.";
	std::string str_TpOnly = "3인칭에만 적용";
	std::string str_Translation = "위치 XYZ";
	std::string str_Modal_NewFile = "새 파일 만들기";
	std::string str_Modal_AskAgain = "정말요?";
	std::string str_Modal_AddRace = "종족 추가";
	std::string str_Modal_AddNPC = "NPC 추가";
	std::string str_Modal_AddGender = "성별 추가";
	std::string str_Modal_AddBone = "뼈 추가";
	std::string str_EnterFileName = "파일 이름을 입력하세요.";
	std::string str_Create = "만들기";
	std::string str_EnterText = "내용을 입력하세요!";
	std::string str_CannotDeleteDefault = "기본 프리셋은 삭제할 수 없습니다!";
	std::string str_Understand = "그렇군요";
	std::string str_CannotRevert = "해당 작업은 되돌릴 수 없습니다. 계속하시겠습니까?";
	std::string str_Positive = "예";
	std::string str_Negative = "아니오";
	std::string str_Add = "추가하기";
	std::string str_RaceEmpty = "추가 가능한 종족이 없습니다!";
	std::string str_Gender = "성별";
	std::string str_GenderEmpty = "추가 가능한 성별이 없습니다!";
	std::string str_PlayerBoneNotFound = "플레이어 뼈대를 찾을 수 없습니다. 직접 입력만 가능합니다.";
	std::string str_ManualInput = "직접 입력";
	std::string str_BoneName = "뼈 이름";
	std::string str_AlreadyExists = "이미 목록에 있습니다!";
	std::string str_CannotUseName = "사용할 수 없는 이름입니다!";

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

			auto window = ::GetActiveWindow();

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
		ClipCursor_Orig = *(FnClipCursor*)ptr_ClipCursor.address();
		ptr_ClipCursor.write_vfunc(0, &HookedClipCursor);
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

	void Window::ApplySelectedToPlayer() {
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
			DoSurgeryPreview(p, tempTransData);
			if (tempScale > 0) {
				DoScalePreview(p, tempScale);
			}
		}
	}

	void Window::OnPresetChange() {
		selectedFormID = "";
		selectedFormType = FORM_TYPE::kNone;
		selectedRaceGender = RACE_GENDER::kNone;
		selectedPlugin = "";
		jsonLoaded = LoadPreset(presetList.at(presetCurrentIdx).path);
		hasSetScale.clear();
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

		notosans = imguiIO.Fonts->AddFontFromMemoryCompressedBase85TTF(NotoSans_compressed_data_base85, 20.f, nullptr, &ranges[0]);
		imguiIO.Fonts->Build();

		ImGui::StyleColorsDark();


		bool imguiWin32Init = ImGui_ImplWin32_Init(sd.OutputWindow);
		bool imguiDX11Init = ImGui_ImplDX11_Init(d3d11Device.Get(), d3d11Context.Get());

		if (imguiWin32Init && imguiDX11Init) {
			_MESSAGE("ImGui Init Success");
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
		ImGui::Begin(str_EditorName.c_str(), NULL, window_flags);

		int requestPopup = 0;

		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu(str_File.c_str())) {
				if (ImGui::MenuItem(str_New.c_str())) {
					requestPopup = 1;
				}
				if (ImGui::MenuItem(str_Save.c_str())) {
					SaveCurrentPreset();
				}
				if (ImGui::MenuItem(str_Delete.c_str())) {
					requestPopup = 2;
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		const char* previewValue = presetList.at(presetCurrentIdx).displayName.c_str();
		if (ImGui::BeginCombo(str_Preset.c_str(), previewValue, 0)) {
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

		ImGui::SeparatorText(str_Setting.c_str());
		ImVec2 sz = ImVec2(-FLT_MIN, 0.0f);
		if (ImGui::Button(str_ResetPlayer.c_str(), sz)) {
			if (p->Get3D()) {
				p->Load3D(true);
				p->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kSkeleton);
				p->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kScale);
				p->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kSkin);
				p->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kFace);
				p->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kHead);
				p->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kModel);
			}
		}
		if (ImGui::Checkbox(str_ApplySelectedToPlayer.c_str(), &applyToPlayer) && applyToPlayer) {
			ApplySelectedToPlayer();
		}
		ImGui::SameLine();
		HelpMarker(str_ApplySelectedToPlayer_Help.c_str());

		if (!jsonLoaded) {
			ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_NoPresetLoaded.c_str());
		} else {
			ImGui::SeparatorText(str_Race.c_str());
			if (ImGui::BeginPopupContextItem("Add Race")) {
				if (ImGui::Selectable(str_AddRace.c_str())) {
					requestPopup = 3;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			for (auto pluginit = jsonRaceList.begin(); pluginit != jsonRaceList.end(); ++pluginit) {
				std::string pluginNode_ID = "plugin_race_" + pluginit->first;
				bool isPluginNodeOpen = ImGui::TreeNodeEx(pluginNode_ID.c_str(), nodeFlags, "%s", pluginit->first.c_str());
				if (ImGui::BeginPopupContextItem((pluginNode_ID + "_Popup").c_str())) {
					if (ImGui::Selectable(str_DeletePlugin.c_str())) {
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
								if (ImGui::Selectable(str_AddGender.c_str())) {
									jsonInsertTarget = &jsonData[pluginit->first][*raceit];
									requestPopup = 5;
									ImGui::CloseCurrentPopup();
								}
								if (ImGui::Selectable(str_DeleteRace.c_str())) {
									jsonData[pluginit->first].erase(*raceit);
									isRaceNodeOpen = false;
									ImGui::CloseCurrentPopup();
								}
								ImGui::EndPopup();
							}
							if (isRaceNodeOpen) {
								auto& genders = jsonData[pluginit->first][*raceit];
								for (auto sexit = genders.begin(); sexit != genders.end(); ++sexit) {
									std::string genderNode_ID = raceNode_ID + "_" + sexit.key();
									bool isGenderNodeOpen = ImGui::TreeNodeEx(genderNode_ID.c_str(), nodeFlags, "%s", sexit.key().c_str());
									if (ImGui::BeginPopupContextItem((genderNode_ID + "_Popup").c_str())) {
										if (ImGui::Selectable(str_AddBone.c_str())) {
											jsonInsertTarget = &jsonData[pluginit->first][*raceit][sexit.key()];
											requestPopup = 6;
											ImGui::CloseCurrentPopup();
										}
										if (ImGui::Selectable(str_DeleteGender.c_str())) {
											jsonData[pluginit->first][*raceit].erase(sexit.key());
											isGenderNodeOpen = false;
											ImGui::CloseCurrentPopup();
										}
										ImGui::EndPopup();
									}
									if (isGenderNodeOpen) {
										std::string setScale_ID = genderNode_ID + "_SetScale";
										if (hasSetScale.find(setScale_ID) == hasSetScale.end()) {
											hasSetScale.insert(std::pair<std::string, bool>(setScale_ID, jsonData[pluginit->first][*raceit][sexit.key()].contains(str_SetScale)));
										}
										if (ImGui::Checkbox(str_HasSetScale.c_str(), &hasSetScale[setScale_ID])) {
											if (!hasSetScale[setScale_ID]) {
												if (jsonData[pluginit->first][*raceit][sexit.key()].contains(str_SetScale)) {
													jsonData[pluginit->first][*raceit][sexit.key()].erase(str_SetScale);
												}
											} else {
												if (!jsonData[pluginit->first][*raceit][sexit.key()].contains(str_SetScale)) {
													jsonData[pluginit->first][*raceit][sexit.key()][str_SetScale] = 1.f;
												}
											}
										}
										if (hasSetScale[setScale_ID]) {
											float tempScale = jsonData[pluginit->first][*raceit][sexit.key()][str_SetScale].get<float>();
											if (ImGui::DragFloat(str_Scale.c_str(), &tempScale, 0.01f, 0.01f, 10.f, "%.2f")) {
												jsonData[pluginit->first][*raceit][sexit.key()][str_SetScale] = tempScale;
												if (applyToPlayer) {
													SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
													ApplySelectedToPlayer();
												}
											}
										}
										for (auto boneit = sexit.value().begin(); boneit != sexit.value().end(); ++boneit) {
											if (boneit.key() != str_SetScale) {
												std::string boneNode_ID = genderNode_ID + "_" + boneit.key();
												bool isBoneNodeOpen = ImGui::TreeNodeEx(boneNode_ID.c_str(), nodeFlags, "%s", boneit.key().c_str());
												if (ImGui::BeginPopupContextItem((boneNode_ID + "_Popup").c_str())) {
													if (ImGui::Selectable(str_DeleteBone.c_str())) {
														jsonData[pluginit->first][*raceit][sexit.key()].erase(boneit.key());
														isBoneNodeOpen = false;
														ImGui::CloseCurrentPopup();
													}
													ImGui::EndPopup();
												}
												if (isBoneNodeOpen) {
													std::string extraNode_ID = boneNode_ID + "_Extra";
													bool isExtraNodeOpen = ImGui::TreeNodeEx(extraNode_ID.c_str(), nodeFlags, str_ExtraData.c_str());
													if (isExtraNodeOpen) {
														bool ignoreXYZ = false;
														if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
															ignoreXYZ = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraIgnoreXYZ];
														}
														if (ImGui::Checkbox(str_IgnoreXYZ.c_str(), &ignoreXYZ)) {
															if (!ignoreXYZ) {
																if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].erase(str_BoneExtraIgnoreXYZ);
																}
															} else {
																if (!jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraIgnoreXYZ] = true;
																}
															}
															if (applyToPlayer) {
																SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
																ApplySelectedToPlayer();
															}
														}
														bool insertion = false;
														if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraInsertion)) {
															insertion = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraInsertion];
														}
														if (ImGui::Checkbox(str_Insertion.c_str(), &insertion)) {
															if (!insertion) {
																if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraInsertion)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].erase(str_BoneExtraInsertion);
																}
															} else {
																if (!jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraInsertion)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraInsertion] = true;
																}
															}
															if (applyToPlayer) {
																SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
																ApplySelectedToPlayer();
															}
														}
														ImGui::SameLine();
														HelpMarker(str_Insertion_Help.c_str());
														bool tponly = false;
														if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraTpOnly)) {
															tponly = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraTpOnly];
														}
														if (ImGui::Checkbox(str_TpOnly.c_str(), &tponly)) {
															if (!tponly) {
																if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraTpOnly)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].erase(str_BoneExtraTpOnly);
																}
															} else {
																if (!jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraTpOnly)) {
																	jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraTpOnly] = true;
																}
															}
															if (applyToPlayer) {
																SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
																ApplySelectedToPlayer();
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
													if (ImGui::DragFloat3(str_Translation.c_str(), transVec4f, 0.01f, -20.f, 20.f)) {
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraX] = transVec4f[0];
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraY] = transVec4f[1];
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraZ] = transVec4f[2];
														if (applyToPlayer) {
															SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
															ApplySelectedToPlayer();
														}
													}
													float boneScale = 1.f;
													if (jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()].contains(str_BoneExtraScale)) {
														boneScale = jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraScale].get<float>();
													}
													if (ImGui::DragFloat(str_Scale.c_str(), &boneScale, 0.01f, 0.01f, 10.f, "%.2f")) {
														jsonData[pluginit->first][*raceit][sexit.key()][boneit.key()][str_BoneExtraScale] = boneScale;
														if (applyToPlayer) {
															SetSelected(pluginit->first, *raceit, FORM_TYPE::kRace, sexit.key());
															ApplySelectedToPlayer();
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
			if (ImGui::BeginPopupContextItem("Add NPC")) {
				if (ImGui::Selectable(str_AddNPC.c_str())) {
					requestPopup = 4;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			for (auto pluginit = jsonNPCList.begin(); pluginit != jsonNPCList.end(); ++pluginit) {
				std::string pluginNode_ID = "plugin_npc_" + pluginit->first;
				bool isPluginNodeOpen = ImGui::TreeNodeEx(pluginNode_ID.c_str(), nodeFlags, "%s", pluginit->first.c_str());
				if (ImGui::BeginPopupContextItem((pluginNode_ID + "_Popup").c_str())) {
					if (ImGui::Selectable(str_DeletePlugin.c_str())) {
						jsonData.erase(pluginit->first);
						jsonRaceList.erase(pluginit);
						isPluginNodeOpen = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
				if (isPluginNodeOpen) {
					for (auto npcit = pluginit->second.begin(); npcit != pluginit->second.end(); ++npcit) {
						RE::TESNPC* npcForm = GetFormFromMod(pluginit->first, std::stoi(*npcit, 0, 16))->As<RE::TESNPC>();
						if (npcForm) {
							std::string npcNode_ID = pluginit->first + *npcit;
							bool isNPCNodeOpen = ImGui::TreeNodeEx(npcNode_ID.c_str(), nodeFlags, "%s (%s)", npcForm->GetFullName(), npcit->c_str());
							if (ImGui::BeginPopupContextItem((npcNode_ID + "_Popup").c_str())) {
								if (ImGui::Selectable(str_AddBone.c_str())) {
									jsonInsertTarget = &jsonData[pluginit->first][*npcit];
									requestPopup = 6;
									ImGui::CloseCurrentPopup();
								}
								if (ImGui::Selectable(str_DeleteNPC.c_str())) {
									jsonData[pluginit->first].erase(*npcit);
									isNPCNodeOpen = false;
									ImGui::CloseCurrentPopup();
								}
								ImGui::EndPopup();
							}
							if (isNPCNodeOpen) {
								std::string setScale_ID = npcNode_ID + "_SetScale";
								if (hasSetScale.find(setScale_ID) == hasSetScale.end()) {
									hasSetScale.insert(std::pair<std::string, bool>(setScale_ID, jsonData[pluginit->first][*npcit].contains(str_SetScale)));
								}
								if (ImGui::Checkbox(str_HasSetScale.c_str(), &hasSetScale[setScale_ID])) {
									if (!hasSetScale[setScale_ID]) {
										if (jsonData[pluginit->first][*npcit].contains(str_SetScale)) {
											jsonData[pluginit->first][*npcit].erase(str_SetScale);
										}
									} else {
										if (!jsonData[pluginit->first][*npcit].contains(str_SetScale)) {
											jsonData[pluginit->first][*npcit][str_SetScale] = 1.f;
										}
									}
								}
								if (hasSetScale[setScale_ID]) {
									float tempScale = jsonData[pluginit->first][*npcit][str_SetScale].get<float>();
									if (ImGui::DragFloat(str_Scale.c_str(), &tempScale, 0.01f, 0.01f, 10.f, "%.2f")) {
										jsonData[pluginit->first][*npcit][str_SetScale] = tempScale;
										if (applyToPlayer) {
											SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
											ApplySelectedToPlayer();
										}
									}
								}
								auto& npcs = jsonData[pluginit->first][*npcit];
								for (auto boneit = npcs.begin(); boneit != npcs.end(); ++boneit) {
									if (boneit.key() != str_SetScale) {
										std::string boneNode_ID = npcNode_ID + "_" + boneit.key();
										bool isBoneNodeOpen = ImGui::TreeNodeEx(boneNode_ID.c_str(), nodeFlags, "%s", boneit.key().c_str());
										if (ImGui::BeginPopupContextItem((boneNode_ID + "_Popup").c_str())) {
											if (ImGui::Selectable(str_DeleteBone.c_str())) {
												jsonData[pluginit->first][*npcit].erase(boneit.key());
												isBoneNodeOpen = false;
												ImGui::CloseCurrentPopup();
											}
											ImGui::EndPopup();
										}
										if (isBoneNodeOpen) {
											std::string extraNode_ID = boneNode_ID + "_Extra";
											bool isExtraNodeOpen = ImGui::TreeNodeEx(extraNode_ID.c_str(), nodeFlags, str_ExtraData.c_str());
											if (isExtraNodeOpen) {
												bool ignoreXYZ = false;
												if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
													ignoreXYZ = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraIgnoreXYZ];
												}
												if (ImGui::Checkbox(str_IgnoreXYZ.c_str(), &ignoreXYZ)) {
													if (!ignoreXYZ) {
														if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
															jsonData[pluginit->first][*npcit][boneit.key()].erase(str_BoneExtraIgnoreXYZ);
														}
													} else {
														if (!jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraIgnoreXYZ)) {
															jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraIgnoreXYZ] = true;
														}
													}
													if (applyToPlayer) {
														SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
														ApplySelectedToPlayer();
													}
												}
												bool insertion = false;
												if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraInsertion)) {
													insertion = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraInsertion];
												}
												if (ImGui::Checkbox(str_Insertion.c_str(), &insertion)) {
													if (!insertion) {
														if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraInsertion)) {
															jsonData[pluginit->first][*npcit][boneit.key()].erase(str_BoneExtraInsertion);
														}
													} else {
														if (!jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraInsertion)) {
															jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraInsertion] = true;
														}
													}
													if (applyToPlayer) {
														SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
														ApplySelectedToPlayer();
													}
												}
												ImGui::SameLine();
												HelpMarker(str_Insertion_Help.c_str());
												bool tponly = false;
												if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraTpOnly)) {
													tponly = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraTpOnly];
												}
												if (ImGui::Checkbox(str_TpOnly.c_str(), &tponly)) {
													if (!tponly) {
														if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraTpOnly)) {
															jsonData[pluginit->first][*npcit][boneit.key()].erase(str_BoneExtraTpOnly);
														}
													} else {
														if (!jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraTpOnly)) {
															jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraTpOnly] = true;
														}
													}
													if (applyToPlayer) {
														SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
														ApplySelectedToPlayer();
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
											if (ImGui::DragFloat3(str_Translation.c_str(), transVec4f, 0.01f, -20.f, 20.f)) {
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraX] = transVec4f[0];
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraY] = transVec4f[1];
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraZ] = transVec4f[2];
												if (applyToPlayer) {
													SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
													ApplySelectedToPlayer();
												}
											}
											float boneScale = 1.f;
											if (jsonData[pluginit->first][*npcit][boneit.key()].contains(str_BoneExtraScale)) {
												boneScale = jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraScale].get<float>();
											}
											if (ImGui::DragFloat(str_Scale.c_str(), &boneScale, 0.01f, 0.01f, 10.f, "%.2f")) {
												jsonData[pluginit->first][*npcit][boneit.key()][str_BoneExtraScale] = boneScale;
												if (applyToPlayer) {
													SetSelected(pluginit->first, *npcit, FORM_TYPE::kNPC, "");
													ApplySelectedToPlayer();
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
				ImGui::OpenPopup(str_Modal_NewFile.c_str());
				break;
			case 2:
				ImGui::OpenPopup(str_Modal_AskAgain.c_str());
				break;
			case 3:
				ImGui::OpenPopup(str_Modal_AddRace.c_str());
				break;
			case 4:
				ImGui::OpenPopup(str_Modal_AddNPC.c_str());
				break;
			case 5:
				ImGui::OpenPopup(str_Modal_AddGender.c_str());
				break;
			case 6:
				ImGui::OpenPopup(str_Modal_AddBone.c_str());
				break;
			}
		}

		if (ImGui::BeginPopupModal(str_Modal_NewFile.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text(str_EnterFileName.c_str());
			ImGui::Separator();

			static char filename[64] = "";
			static bool errorMsg = false;
			ImGui::InputText("", filename, 64);
			ImGui::SameLine();
			ImGui::Text(".json");
			if (ImGui::Button(str_Create.c_str(), ImVec2(200, 0))) {
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
			if (errorMsg) {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_EnterText.c_str());
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(str_Modal_AskAgain.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (presetCurrentIdx == 0) {
				ImGui::Text(str_CannotDeleteDefault.c_str());
				ImGui::Separator();

				if (ImGui::Button(str_Understand.c_str(), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			} else {
				ImGui::Text(str_CannotRevert.c_str());
				ImGui::Separator();

				if (ImGui::Button(str_Positive.c_str(), ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
					this->DeletePreset(presetList.at(presetCurrentIdx).path);
				}
				ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
				if (ImGui::Button(str_Negative.c_str(), ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(str_Modal_AddRace.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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
				if (ImGui::BeginCombo(str_Race.c_str(), racePreviewValue, 0)) {
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
				if (ImGui::Button(str_Add.c_str(), ImVec2(200, 0))) {
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
			} else {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_RaceEmpty.c_str());
				if (ImGui::Button(str_Positive.c_str(), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(str_Modal_AddNPC.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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
				if (ImGui::BeginCombo(str_Race.c_str(), npcPreviewValue, 0)) {
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
				if (ImGui::Button(str_Add.c_str(), ImVec2(200, 0))) {
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
			} else {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_RaceEmpty.c_str());
				if (ImGui::Button(str_Positive.c_str(), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(str_Modal_AddGender.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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
				if (ImGui::BeginCombo(str_Gender.c_str(), genderPreviewValue, 0)) {
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
				if (ImGui::Button(str_Add.c_str(), ImVec2(200, 0))) {
					(*jsonInsertTarget)[genderList.at(genderCurrentIdx)] = nlohmann::json({});
					ImGui::CloseCurrentPopup();
				}
			} else {
				ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_GenderEmpty.c_str());
				if (ImGui::Button(str_Positive.c_str(), ImVec2(200, 0))) {
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal(str_Modal_AddBone.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::vector<std::string> boneList;
			static int boneCurrentIdx = 0;

			if (p->Get3D()) {
				RE::NiAVObject* root = p->Get3D()->GetObjectByName("Root");
				Visit(root, [&boneList](RE::NiAVObject* obj) {
					if (!obj->IsNode())
						return false;

					std::string name = obj->name.c_str();
					if (!jsonInsertTarget->contains(name)) {
						boneList.push_back(name);
					}
					return false;
				});
			} else {
				ImGui::Text(str_PlayerBoneNotFound.c_str());
			}
			std::sort(boneList.begin(), boneList.end());
			boneList.push_back(str_ManualInput.c_str());

			if (boneCurrentIdx >= boneList.size())
				boneCurrentIdx = (int)(boneList.size() - 1);

			const char* bonePreviewValue = boneList.at(boneCurrentIdx).c_str();
			if (ImGui::BeginCombo(str_BoneName.c_str(), bonePreviewValue, 0)) {
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
					ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_EnterText.c_str());
					break;
				case 1:
					ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_AlreadyExists.c_str());
					break;
				case 2:
					ImGui::TextColored(ImVec4(0.95f, 0.05f, 0.05f, 1.f), str_CannotUseName.c_str());
					break;
				}
			}
			if (ImGui::Button(str_Add.c_str(), ImVec2(200, 0))) {
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