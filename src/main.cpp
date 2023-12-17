#include "EditorUI.h"
#include "include/detours.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <shared_mutex>
#include <wtypes.h>
using namespace RE;
using std::ifstream;
using std::pair;
using std::queue;
using std::string;
using std::unordered_map;
using std::vector;

REL::Relocation<uint64_t*> ptr_engineTime{ REL::ID(1280610) };
REL::Relocation<ProcessLists*> ptr_processLists{ REL::ID(474742) };
REL::Relocation<uintptr_t> ptr_RunActorUpdates{ REL::ID(556439), 0x17 };
REL::Relocation<uintptr_t> ptr_ScaleSkinBones{ REL::ID(574183) };
uintptr_t RunActorUpdatesOrig;
uintptr_t PCLoad3DOrig;
uintptr_t ActorLoad3DOrig;
uintptr_t ScaleSkinBonesOrig;
PlayerCharacter* p;
PlayerCamera* pcam;
unordered_map<uint32_t, vector<TranslationData>> customRaceFemaleTranslations;
unordered_map<uint32_t, vector<TranslationData>> customRaceMaleTranslations;
unordered_map<uint32_t, vector<TranslationData>> customNPCTranslations;
unordered_map<uint32_t, float> customRaceFemaleScales;
unordered_map<uint32_t, float> customRaceMaleScales;
unordered_map<uint32_t, float> customNPCScales;

using SharedLock = std::shared_mutex;
using ReadLocker = std::shared_lock<SharedLock>;
using WriteLocker = std::unique_lock<SharedLock>;
unordered_map<uint32_t, float> actorScaleQueue;
SharedLock scaleQueueLock;
queue<uint32_t> actorSurgeryQueue;
SharedLock surgeryQueueLock;

const F4SE::TaskInterface* taskInterface;

bool isLoading = false;
uint32_t editorKey = 0xDD;

char tempbuf[1024] = { 0 };
char* _MESSAGE(const char* fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(tempbuf, sizeof(tempbuf), fmt, args);
	va_end(args);
	spdlog::log(spdlog::level::warn, tempbuf);

	return tempbuf;
}

void Dump(void* mem, unsigned int size) {
	char* c = static_cast<char*>(mem);
	unsigned char* up = (unsigned char*)c;
	std::stringstream stream;
	int row = 0;
	for (unsigned int i = 0; i < size; i++) {
		stream << std::setfill('0') << std::setw(2) << std::hex << (int)up[i] << " ";
		if (i % 8 == 7) {
			stream << "\t0x"
				<< std::setw(2) << std::hex << (int)up[i]
				<< std::setw(2) << (int)up[i - 1]
				<< std::setw(2) << (int)up[i - 2]
				<< std::setw(2) << (int)up[i - 3]
				<< std::setw(2) << (int)up[i - 4]
				<< std::setw(2) << (int)up[i - 5]
				<< std::setw(2) << (int)up[i - 6]
				<< std::setw(2) << (int)up[i - 7] << std::setfill('0');
			stream << "\t0x" << std::setw(2) << std::hex << row * 8 << std::setfill('0');
			_MESSAGE("%s", stream.str().c_str());
			stream.str(std::string());
			row++;
		}
	}
}

TESForm* GetFormFromMod(std::string modname, uint32_t formid)
{
	if (!modname.length() || !formid)
		return nullptr;
	TESDataHandler* dh = TESDataHandler::GetSingleton();
	return dh->LookupForm(formid, modname);
}

template <class Ty>
Ty SafeWrite64Function(uintptr_t addr, Ty data) {
	DWORD oldProtect;
	void* _d[2];
	memcpy(_d, &data, sizeof(data));
	size_t len = sizeof(_d[0]);

	VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
	Ty olddata;
	memset(&olddata, 0, sizeof(Ty));
	memcpy(&olddata, (void*)addr, len);
	memcpy((void*)addr, &_d[0], len);
	VirtualProtect((void*)addr, len, oldProtect, &oldProtect);
	return olddata;
}

NiNode* CreateBone(const char* name) {
	NiNode* newbone = new NiNode(0);
	newbone->name = name;
	//_MESSAGE("%s created.", name);
	return newbone;
}

NiNode* InsertBone(NiAVObject* root, NiNode* node, const char* name) {
	NiNode* parent = node->parent;
	NiNode* inserted = (NiNode*)root->GetObjectByName(name);
	if (!inserted) {
		inserted = CreateBone(name);
		inserted->local.translate = NiPoint3();
		inserted->local.rotate.MakeIdentity();
		inserted->AttachChild(node, true);
		//_MESSAGE("%s (%llx) created.", name, inserted);
		if (parent) {
			parent->DetachChild(node);
			parent->AttachChild(inserted, true);
			inserted->parent = parent;
		}
		else {
			parent = node;
		}
		//_MESSAGE("%s (%llx) inserted to %s (%llx).", name, inserted, parent->name.c_str(), parent);
		return inserted;
	}
	return nullptr;
}

bool Visit(NiAVObject* obj, const std::function<bool(NiAVObject*)>& functor) {
	if (functor(obj))
		return true;

	NiPointer<NiNode> node(obj->IsNode());
	if (node) {
		for (auto it = node->children.begin(); it != node->children.end(); ++it) {
			NiPointer<NiAVObject> object(*it);
			if (object) {
				if (Visit(object.get(), functor))
					return true;
			}
		}
	}

	return false;
}

void SetScale(TESObjectREFR* ref, float scale) {
	using func_t = decltype(&SetScale);
	REL::Relocation<func_t> func{ REL::ID(817930) };
	return func(ref, scale);
}

float GetActorScale(Actor* a) {
	typedef float (*_GetPlayerScale)(Actor*);
	REL::Relocation<_GetPlayerScale> func{ REL::ID(911188) };
	return func(a);
}

TESNPC* GetBaseNPC(Actor* a) {
	BSExtraData* extraLvlCreature = a->extraList->GetByType(EXTRA_DATA_TYPE::kLeveledCreature);
	if (extraLvlCreature) {
		return *(TESNPC**)((uintptr_t)extraLvlCreature + 0x18);
	}
	return a->GetNPC();
}

void ApplyBoneData(NiAVObject* node, TranslationData& data) {
	NiNode* bone = (NiNode*)node->GetObjectByName(data.bonename);
	if (bone) {
		if (!data.insertion) {
			if (!data.ignoreXYZ) {
				bone->local.translate = data.translation;
			}
			bone->local.scale = data.scale;
		}
		else {
			if (!bone->GetObjectByName(data.bonenameorig)) {
				//_MESSAGE("%s structure mismatch. Reinserting...", data.bonename.c_str());
				bone->parent->DetachChild(bone);
				NiNode* boneorig = (NiNode*)node->GetObjectByName(data.bonenameorig);
				if (boneorig) {
					bone = InsertBone(node, boneorig, data.bonename.c_str());
					if (bone) {
						if (!data.ignoreXYZ) {
							bone->local.translate = data.translation;
						}
						bone->local.scale = data.scale;
					}
				}
			}
			else {
				if (!data.ignoreXYZ) {
					bone->local.translate = data.translation;
				}
				bone->local.scale = data.scale;
			}
		}
		//_MESSAGE("Bone %s x %f y %f z %f scale %f", data.bonename.c_str(), bone->local.translate.x, bone->local.translate.y, bone->local.translate.z, bone->local.scale);
	}
	else {
		if (data.insertion) {
			NiNode* boneorig = (NiNode*)node->GetObjectByName(data.bonenameorig);
			if (boneorig) {
				bone = InsertBone(node, boneorig, data.bonename.c_str());
				if (bone) {
					if (!data.ignoreXYZ) {
						bone->local.translate = data.translation;
					}
					bone->local.scale = data.scale;
				}
			}
		}
	}
}

void QueueSurgery(Actor* a)
{
	if (EditorUI::Window::GetSingleton()->GetShouldDraw() && EditorUI::Window::GetSingleton()->GetPreviewTargetFormID() == a->formID)
		return;
	WriteLocker lock(surgeryQueueLock);
	actorSurgeryQueue.push(a->formID);
	//_MESSAGE("Adding Actor %llx to queue", a->formID);
}

void DoSurgery() {
	WriteLocker lock(surgeryQueueLock);
	while (!actorSurgeryQueue.empty()) {
		uint32_t formID = actorSurgeryQueue.front();
		actorSurgeryQueue.pop();
		auto form = TESForm::GetFormByID(formID);
		//_MESSAGE("Surgery FormID %llx", formID);
		if (!form) {
			_MESSAGE("FormID %llx wasn't loaded", formID);
			continue;
		}

		auto actor = form->As<Actor>();
		if (!actor) {
			_MESSAGE("FormID %llx is not an actor", formID);
			continue;
		}

		int found = 0;
		NiAVObject* node = actor->Get3D(false);
		if (!node) {
			_MESSAGE("Actor %llx - Couldn't find skeleton", actor->formID);
			continue;
		}
		TESNPC* npc = actor->GetNPC();
		if (!npc) {
			_MESSAGE("Actor %llx - Couldn't find NPC data", actor->formID);
			continue;
		}
		//_MESSAGE("NPC %s (%llx, Actor %llx) Flag %04x moreFlag %04x", npc->GetFullName(), npc->formID, actor->formID, actor->flags, actor->moreFlags);
		unordered_map<uint32_t, vector<TranslationData>>::iterator raceit;
		if (npc->GetSex() == 0) {
			raceit = customRaceMaleTranslations.find(actor->race->formID);
			if (raceit != customRaceMaleTranslations.end()) {
				found = 1;
			}
		}
		else {
			raceit = customRaceFemaleTranslations.find(actor->race->formID);
			if (raceit != customRaceFemaleTranslations.end()) {
				found = 1;
			}
		}
		auto npcit = customNPCTranslations.find(GetBaseNPC(actor)->formID);
		if (npcit != customNPCTranslations.end()) {
			found = 2;
		}
		if (found > 0) {
			NiAVObject* fpnode = nullptr;
			if (actor == p)
				fpnode = actor->Get3D(true);
			if (found == 2) {
				for (auto it = npcit->second.begin(); it != npcit->second.end(); ++it) {
					ApplyBoneData(node, *it);
					if (actor == p && !it->tponly) {
						ApplyBoneData(fpnode, *it);
					}
				}
			}
			else if (found == 1) {
				for (auto it = raceit->second.begin(); it != raceit->second.end(); ++it) {
					ApplyBoneData(node, *it);
					if (actor == p && !it->tponly) {
						ApplyBoneData(fpnode, *it);
					}
				}
			}
		}
	}
}

void DoSurgeryPreview(Actor* a, vector<TranslationData> transData) {
	if (!a || !a->As<Actor>())
		return;

	NiAVObject* node = a->Get3D(false);
	if (!node)
		return;

	NiAVObject* fpnode = nullptr;
	if (a == p)
		fpnode = a->Get3D(true);
	for (auto it = transData.begin(); it != transData.end(); ++it) {
		ApplyBoneData(node, *it);
		if (a == p && !it->tponly) {
			ApplyBoneData(fpnode, *it);
		}
	}
}

void DoGlobalSurgery() {
	BSTArray<ActorHandle>* highActorHandles = (BSTArray<ActorHandle>*)(ptr_processLists.address() + 0x40);
	if (highActorHandles->size() > 0) {
		for (auto it = highActorHandles->begin(); it != highActorHandles->end(); ++it) {
			Actor* a = it->get().get();
			if (a && a->Get3D())
				QueueSurgery(a);
		}
	}
}

void QueueScaling(Actor* a)
{
	TESNPC* npc = a->GetNPC();
	if (npc) {
		float scale = -1.f;
		if (a->race) {
			if (npc->GetSex() == 0) {
				auto raceit = customRaceMaleScales.find(a->race->formID);
				if (raceit != customRaceMaleScales.end()) {
					scale = raceit->second;
					//_MESSAGE("NPC %s (%llx, Actor %llx) Male Scale %f", npc->GetFullName(), npc->formID, a->formID, scale);
				}
			} else {
				auto raceit = customRaceFemaleScales.find(a->race->formID);
				if (raceit != customRaceFemaleScales.end()) {
					scale = raceit->second;
					//_MESSAGE("NPC %s (%llx, Actor %llx) Female Scale %f", npc->GetFullName(), npc->formID, a->formID, scale);
				}
			}
		}
		auto npcit = customNPCScales.find(GetBaseNPC(a)->formID);
		if (npcit != customNPCScales.end()) {
			scale = npcit->second;
			//_MESSAGE("NPC %s (%llx, Actor %llx) NPC Scale %f", npc->GetFullName(), npc->formID, a->formID, scale);
		}
		WriteLocker lock(scaleQueueLock);
		if (scale >= 0.f && actorScaleQueue.find(a->formID) == actorScaleQueue.end()) {
			actorScaleQueue.insert(pair<uint32_t, float>(a->formID, scale));
		}
	}
}

void RemoveFromScaleQueue(const uint32_t formID) {
	WriteLocker lock(scaleQueueLock);
	actorScaleQueue.erase(formID);
}

void DoScaling() {
	ReadLocker lock(scaleQueueLock);
	unordered_map<uint32_t, float> tempScaleQueue = actorScaleQueue;
	lock.unlock();
	for (auto& [formID, scale] : tempScaleQueue) {
		auto form = TESForm::GetFormByID(formID);
		//_MESSAGE("Scaling FormID %llx", formID);
		if (!form) {
			//_MESSAGE("FormID %llx wasn't loaded", formID);
			RemoveFromScaleQueue(formID);
			continue;
		}

		auto actor = form->As<Actor>();

		if (!actor) {
			//_MESSAGE("FormID %llx is not an actor", formID);
			RemoveFromScaleQueue(formID);
			continue;
		}

		if (actor->Get3D()) {
			if (GetActorScale(actor) != scale) {
				SetScale(actor, scale);
			}
			RemoveFromScaleQueue(formID);
		}
	}
}

void DoScalePreview(Actor* a, float scale) {
	if (!a || !a->As<Actor>())
		return;

	NiAVObject* node = a->Get3D(false);
	if (!node)
		return;

	WriteLocker lock(scaleQueueLock);
	actorScaleQueue.insert(pair<uint32_t, float>(a->formID, scale));
}

void DoGlobalScaling() {
	BSTArray<ActorHandle>* highActorHandles = (BSTArray<ActorHandle>*)(ptr_processLists.address() + 0x40);
	if (highActorHandles->size() > 0) {
		for (auto it = highActorHandles->begin(); it != highActorHandles->end(); ++it) {
			Actor* a = it->get().get();
			if (a && a->Get3D() && actorScaleQueue.find(a->formID) == actorScaleQueue.end()) {
				QueueScaling(a);
			}
		}
	}
}

void HookedUpdate(ProcessLists* list, float deltaTime, bool instant) {
	if (!isLoading) {
		if (actorScaleQueue.size() > 0) {
			DoScaling();
		}
		if (!actorSurgeryQueue.empty()) {
			DoSurgery();
		}
	}

	typedef void (*FnUpdate)(ProcessLists*, float, bool);
	FnUpdate fn = (FnUpdate)RunActorUpdatesOrig;
	if (fn)
		(*fn)(list, deltaTime, instant);
}

void HookedScaleSkinBones(Actor* a, NiAVObject* bone, void* boneScaleMap) {
	typedef void (*FnScaleSkinBones)(Actor*, NiAVObject*, void*);
	FnScaleSkinBones fn = (FnScaleSkinBones)ScaleSkinBonesOrig;
	if (fn)
		(*fn)(a, bone, boneScaleMap);
	//_MESSAGE("HookedScaleSkinBones %llx", a->formID);
	if (a->formType == ENUM_FORM_ID::kACHR && a->Get3D()) {
		QueueSurgery(a);
	}
}

NiAVObject* HookedActorLoad3D(Actor* a, bool b) {
	NiAVObject* ret = nullptr;
	typedef NiAVObject* (*FnLoad3D)(Actor*, bool);
	FnLoad3D fn = (FnLoad3D)ActorLoad3DOrig;
	if (fn)
		ret = (*fn)(a, b);

	//_MESSAGE("HookedActorLoad3D %llx", a->formID);
	QueueScaling(a);
	return ret;
}

NiAVObject* HookedPCLoad3D(Actor* a, bool b) {
	NiAVObject* ret = nullptr;
	typedef NiAVObject* (*FnLoad3D)(Actor*, bool);
	FnLoad3D fn = (FnLoad3D)PCLoad3DOrig;
	if (fn)
		ret = (*fn)(a, b);

	//_MESSAGE("HookedPCLoad3D %llx", a->formID);
	QueueScaling(a);
	return ret;
}

class MenuWatcher : public BSTEventSink<MenuOpenCloseEvent> {
public:
	virtual BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent& evn, BSTEventSource<MenuOpenCloseEvent>* src) override {
		if (evn.menuName == BSFixedString("LoadingMenu")) {
			if (evn.opening) {
				isLoading = true;
			}
			else {
				isLoading = false;
				DoGlobalSurgery();
				DoGlobalScaling();
				QueueSurgery(p);
				QueueScaling(p);
			}
		}
		return BSEventNotifyControl::kContinue;
	}
	F4_HEAP_REDEFINE_NEW(MenuWatcher);
};

void RegisterBoneData(auto iter, TESForm* form, unordered_map<uint32_t, vector<TranslationData>>& map, unordered_map<uint32_t, float>& scaleMap) {
	auto conflictit = map.find(form->formID);
	if (conflictit != map.end()) {
		map.erase(conflictit);
	}

	for (auto boneit = iter.value().begin(); boneit != iter.value().end(); ++boneit) {
		if (boneit.key() != "SetScale") {
			float x = 0;
			float y = 0;
			float z = 0;
			float scale = 1;
			bool ignoreXYZ = false;
			bool insertion = false;
			bool tponly = false;
			for (auto valit = boneit.value().begin(); valit != boneit.value().end(); ++valit) {
				if (valit.key() == "X") {
					x = valit.value().get<float>();
				}
				else if (valit.key() == "Y") {
					y = valit.value().get<float>();
				}
				else if (valit.key() == "Z") {
					z = valit.value().get<float>();
				}
				else if (valit.key() == "Scale") {
					scale = valit.value().get<float>();
				}
				else if (valit.key() == "IgnoreXYZ") {
					ignoreXYZ = valit.value().get<bool>();
				}
				else if (valit.key() == "Insertion") {
					insertion = valit.value().get<bool>();
				}
				else if (valit.key() == "ThirdPersonOnly") {
					tponly = valit.value().get<bool>();
				}
			}
			string bonename = boneit.key();
			if (insertion) {
				bonename += "SurgeonInserted";
			}
			auto existit = map.find(form->formID);
			if (existit == map.end()) {
				map.insert(pair<uint32_t, vector<TranslationData>>(form->formID, vector<TranslationData>{
																					 TranslationData(bonename, boneit.key(), NiPoint3(x, y, z), scale, ignoreXYZ, insertion, tponly) }));
			}
			else {
				existit->second.push_back(TranslationData(bonename, boneit.key(), NiPoint3(x, y, z), scale, ignoreXYZ, insertion, tponly));
			}
			//_MESSAGE("Registered Bone %s X %f Y %f Z %f Scale %f IgnoredXYZ %d Insertion %d ThirdPersonOnly %d", boneit.key().c_str(), x, y, z, scale, ignoreXYZ, insertion, tponly);
		}
		else {
			scaleMap.insert(pair<uint32_t, float>(form->formID, boneit.value().get<float>()));
			//_MESSAGE("SetScale %f", boneit.value().get<float>());
		}
	}
}

void ParseJSON(const std::filesystem::path path) {
	ifstream reader;
	reader.open(path);
	nlohmann::json customData;
	reader >> customData;
	for (auto pluginit = customData.begin(); pluginit != customData.end(); ++pluginit) {
		for (auto formit = pluginit.value().begin(); formit != pluginit.value().end(); ++formit) {
			TESForm* form = GetFormFromMod(pluginit.key(), std::stoi(formit.key(), 0, 16));
			if (form) {
				if (form->GetFormType() == ENUM_FORM_ID::kNPC_) {
					if (form->formID == 0x7) {
						_MESSAGE("Player");
					}
					else {
						_MESSAGE("NPC %s", ((TESNPC*)form)->GetFullName());
					}
					RegisterBoneData(formit, form, customNPCTranslations, customNPCScales);
				}
				else if (form->GetFormType() == ENUM_FORM_ID::kRACE) {
					for (auto sexit = formit.value().begin(); sexit != formit.value().end(); ++sexit) {
						if (sexit.key() == "Female") {
							_MESSAGE("Race %s Female", form->GetFormEditorID());
							RegisterBoneData(sexit, form, customRaceFemaleTranslations, customRaceFemaleScales);
						}
						else if (sexit.key() == "Male") {
							_MESSAGE("Race %s Male", form->GetFormEditorID());
							RegisterBoneData(sexit, form, customRaceMaleTranslations, customRaceMaleScales);
						}
					}
				}
			}
		}
	}
	reader.close();
}

void LoadConfigs() {
	customRaceFemaleTranslations.clear();
	customRaceMaleTranslations.clear();
	customNPCTranslations.clear();
	customRaceFemaleScales.clear();
	customRaceMaleScales.clear();
	customNPCScales.clear();
	namespace fs = std::filesystem;
	fs::path jsonPath = fs::current_path();
	jsonPath += "\\Data\\F4SE\\Plugins\\NanakoSurgeon";
	std::stringstream stream;
	fs::directory_entry jsonEntry{ jsonPath };
	if (jsonEntry.exists()) {
		for (auto& it : fs::directory_iterator(jsonEntry)) {
			if (it.path().extension().compare(".json") == 0) {
				stream << it.path().filename();
				_MESSAGE("Loading bone data %s", stream.str().c_str());
				stream.str(std::string());
				ParseJSON(it.path());
			}
		}
	}
	ParseJSON("Data\\F4SE\\Plugins\\NanakoSurgeon.json");
}

void InitializePlugin() {
	p = PlayerCharacter::GetSingleton();
	pcam = PlayerCamera::GetSingleton();

	REL::Relocation<uintptr_t> ActorVtbl{ RE::VTABLE::Actor[0] };
	ActorLoad3DOrig = ActorVtbl.write_vfunc(0x86, HookedActorLoad3D);
	REL::Relocation<uintptr_t> PCVtbl{ RE::VTABLE::PlayerCharacter[0] };
	PCLoad3DOrig = PCVtbl.write_vfunc(0x86, HookedPCLoad3D);

	MenuWatcher* mw = new MenuWatcher();
	UI::GetSingleton()->GetEventSource<MenuOpenCloseEvent>()->RegisterSink(mw);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface * a_f4se, F4SE::PluginInfo * a_info) {
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
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_f4se->IsEditor()) {
		logger::critical(FMT_STRING("loaded in editor"));
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_162) {
		logger::critical(FMT_STRING("unsupported runtime v{}"), ver.string());
		return false;
	}

	F4SE::AllocTrampoline(8 * 8);

	EditorUI::HookD3D11();

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface * a_f4se) {
	F4SE::Init(a_f4se);

	F4SE::Trampoline& trampoline = F4SE::GetTrampoline();
	RunActorUpdatesOrig = trampoline.write_call<5>(ptr_RunActorUpdates.address(), &HookedUpdate);

	ScaleSkinBonesOrig = ptr_ScaleSkinBones.address();
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)ScaleSkinBonesOrig, HookedScaleSkinBones);
	DetourTransactionCommit();

	taskInterface = F4SE::GetTaskInterface();

	const F4SE::MessagingInterface* message = F4SE::GetMessagingInterface();
	message->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
			InitializePlugin();
			LoadConfigs();
			EditorUI::Window::GetSingleton()->LoadDefaultPreset();
		}
		else if (msg->type == F4SE::MessagingInterface::kPostLoadGame) {
			LoadConfigs();
			EditorUI::Window::GetSingleton()->Reset();
		}
		else if (msg->type == F4SE::MessagingInterface::kNewGame) {
			LoadConfigs();
			EditorUI::Window::GetSingleton()->Reset();
		}
	});

	return true;
}
