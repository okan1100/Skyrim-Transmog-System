#include "log.h"
#include "PrismaUI_API.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include <sstream>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <string>
#include <string_view>
#include <windows.h> 

PrismaView g_transmogView = 0;
PRISMA_UI_API::IVPrismaUI1* PrismaUI = nullptr;

uint32_t g_hotkey = 0x26; // Default: L key

void LoadConfig() {
    std::string path = "Data/SKSE/Plugins/SkyrimWeaponTransmog.ini";
    std::ifstream file(path);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("Hotkey=") != std::string::npos) {
                try { g_hotkey = static_cast<uint32_t>(std::stoul(line.substr(line.find("=") + 1), nullptr, 0)); }
                catch (...) { SKSE::log::error("Invalid INI formatting! Using L as default."); }
            }
        }
        file.close();
    }
    else {
        std::ofstream out(path);
        if (out.is_open()) { out << "[Settings]\nHotkey=0x26\n"; out.close(); }
    }
}

std::string CleanName(std::string_view input) {
    std::string out;
    for (char c : input) {
        if (c != '"' && c != '\\' && c != '\n' && c != '\r' && c != '\t') {
            out += c;
        }
    }
    return out;
}

// --- HELPER: RESTORE ORIGINAL WEAPON STATS ---
// Copies all core stats from the source weapon to the new transmogged duplicate
void RestoreWeaponStats(RE::TESObjectWEAP* dest, RE::TESObjectWEAP* src) {
    if (!dest || !src) return;

    dest->weaponData = src->weaponData;               
    dest->criticalData = src->criticalData;           
    dest->attackDamage = src->attackDamage;           
    dest->weight = src->weight;                       
    dest->value = src->value;                         
    dest->formEnchanting = src->formEnchanting;       
    dest->amountofEnchantment = src->amountofEnchantment; 
    dest->equipSlot = src->equipSlot;                 
    dest->keywords = src->keywords;                   
    dest->numKeywords = src->numKeywords;
}
// ---------------------------------------------

struct WeaponTransmogData { RE::FormID source; RE::FormID target; bool isEquipped; };
std::unordered_map<RE::FormID, WeaponTransmogData> g_activeWeaponTransmogs;

void SaveCallback(SKSE::SerializationInterface* serde) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || !serde->OpenRecord('TRMW', 1)) return;

    std::vector<WeaponTransmogData> toSave;
    auto inv = player->GetInventory();
    
    // Prevent save bloat: Only save active transmogs currently in the player's inventory
    for (const auto& [item, data] : inv) {
        if (item && item->IsWeapon()) {
            auto formID = item->GetFormID();
            if (g_activeWeaponTransmogs.find(formID) != g_activeWeaponTransmogs.end()) {
                bool worn = false;
                if (data.second && data.second->extraLists) {
                    for (auto* xList : *data.second->extraLists) {
                        if (xList && (xList->HasType(RE::ExtraDataType::kWorn) || xList->HasType(RE::ExtraDataType::kWornLeft))) {
                            worn = true; break;
                        }
                    }
                }
                auto record = g_activeWeaponTransmogs[formID];
                record.isEquipped = worn;
                toSave.push_back(record);
            }
        }
    }

    uint32_t count = static_cast<uint32_t>(toSave.size());
    serde->WriteRecordData(count);
    for (auto& record : toSave) {
        serde->WriteRecordData(record.source);
        serde->WriteRecordData(record.target);
        serde->WriteRecordData(record.isEquipped ? 1 : 0);
    }
}

void LoadCallback(SKSE::SerializationInterface* serde) {
    uint32_t type, version, length;
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // Clean up temporary duplication forms (Quick-load duplication glitch protection)
    auto currentInv = player->GetInventory();
    for (auto& [item, data] : currentInv) {
        if (item && item->IsWeapon()) {
            const char* rawName = item->GetName();
            if (rawName) { 
                std::string name(rawName);
                if (name.find(" EDT") != std::string::npos) {
                    player->RemoveItem(item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            }
        }
    }
    g_activeWeaponTransmogs.clear();

    while (serde->GetNextRecordInfo(type, version, length)) {
        if (type == 'TRMW') {
            uint32_t count = 0;
            if (!serde->ReadRecordData(count)) continue;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t srcID = 0, tgtID = 0, isEq = 0;
                serde->ReadRecordData(srcID); serde->ReadRecordData(tgtID); serde->ReadRecordData(isEq);
                bool worn = (isEq != 0); RE::FormID nS = srcID, nT = tgtID; RE::FormID newSrc = 0, newTgt = 0;

                if (!serde->ResolveFormID(nS, newSrc) || !serde->ResolveFormID(nT, newTgt)) continue;
                auto sW = RE::TESForm::LookupByID<RE::TESObjectWEAP>(newSrc);
                auto tW = RE::TESForm::LookupByID<RE::TESObjectWEAP>(newTgt);

                if (sW && tW) {
                    auto nW = tW->CreateDuplicateForm(false, nullptr)->As<RE::TESObjectWEAP>();
                    if (nW) {

                        RestoreWeaponStats(nW, tW); 

                        std::string bN(tW->GetName());
                        if (bN.find(" EDT") == std::string::npos) bN += " EDT";
                        nW->SetFullName(bN.c_str());

                        // Transfer visual model data
                        auto sM = sW->As<RE::TESModel>(), tM = nW->As<RE::TESModel>();
                        if (sM && tM) tM->SetModel(sM->GetModel());
                        nW->firstPersonModelObject = sW->firstPersonModelObject;

                        player->AddObjectToContainer(nW, nullptr, 1, nullptr);
                        if (worn) RE::ActorEquipManager::GetSingleton()->EquipObject(player, nW);
                        g_activeWeaponTransmogs[nW->GetFormID()] = { newSrc, newTgt, worn };
                    }
                }
            }
        }
    }
}

std::string GetWeaponCategory(RE::TESObjectWEAP* weap) {
    if (!weap) return "";
    uint32_t type = static_cast<uint32_t>(weap->GetWeaponType());
    if (type == 1) return "swords"; if (type == 2) return "daggers"; if (type == 3) return "waraxes";
    if (type == 4) return "maces"; if (type == 5) return "greatswords";
    if (type == 6) { if (weap->HasKeywordString("WeapTypeWarhammer")) return "warhammers"; return "battleaxes"; }
    if (type == 7) return "bows"; if (type == 9) return "crossbows";
    return "";
}

std::string GetPlayerInventoryJSON() {
    auto player = RE::PlayerCharacter::GetSingleton(); if (!player) return "{}";
    auto inv = player->GetInventory(); std::unordered_map<std::string, std::vector<std::string>> invCats;

    for (auto const& pair : inv) {
        auto item = pair.first;
        if (item && item->IsWeapon()) {
            auto w = item->As<RE::TESObjectWEAP>(); if (!w) continue;

            std::string cat = GetWeaponCategory(w);
            auto it = g_activeWeaponTransmogs.find(w->GetFormID());
            if (it != g_activeWeaponTransmogs.end()) {
                auto origW = RE::TESForm::LookupByID<RE::TESObjectWEAP>(it->second.target);
                if (origW) cat = GetWeaponCategory(origW);
            }
            if (cat.empty()) continue;

            const char* rawName = w->GetName(); if (!rawName || rawName[0] == '\0') continue;
            std::string cleanName = CleanName(std::string(rawName));

            char buf[512]; snprintf(buf, sizeof(buf), "{\"id\":\"%08X\",\"name\":\"%s\"}", w->GetFormID(), cleanName.c_str());
            invCats[cat].push_back(buf);
        }
    }
    std::stringstream ss; ss << "{"; bool first = true;
    for (auto const& catPair : invCats) {
        if (!first) ss << ","; ss << "\"" << catPair.first << "\":[";
        for (size_t i = 0; i < catPair.second.size(); ++i) { ss << catPair.second[i] << (i != catPair.second.size() - 1 ? "," : ""); }
        ss << "]"; first = false;
    }
    ss << "}"; return ss.str();
}

class WeaponInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
public:
    static WeaponInputHandler* GetSingleton() { static WeaponInputHandler instance; return &instance; }

    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override {
        if (!a_event || !*a_event || !PrismaUI) return RE::BSEventNotifyControl::kContinue;
        auto event = *a_event;
        while (event) {
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                auto btn = event->AsButtonEvent();
                if (btn && btn->IsDown() && btn->GetIDCode() == g_hotkey) {
                    bool isShiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShiftDown) {
                        if (PrismaUI->IsHidden(g_transmogView)) {
                            PrismaUI->Show(g_transmogView);
                            PrismaUI->Focus(g_transmogView, false);

                            SKSE::GetTaskInterface()->AddTask([]() {
                                std::string cmd = "loadUI(" + GetPlayerInventoryJSON() + ");";
                                if (PrismaUI) PrismaUI->Invoke(g_transmogView, cmd.c_str());
                                });
                        }
                        else {
                            if (auto cm = RE::ControlMap::GetSingleton()) cm->AllowTextInput(false);
                            PrismaUI->Unfocus(g_transmogView);
                            PrismaUI->Hide(g_transmogView);
                        }
                        return RE::BSEventNotifyControl::kStop;
                    }
                }
            }
            event = event->next;
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
        PrismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
        if (!PrismaUI) return;

        g_transmogView = PrismaUI->CreateView("SkyrimWeaponTransmogUI/index.html", [](PrismaView) {});

        PrismaUI->RegisterJSListener(g_transmogView, "TRANSMOGRIFY", [](const char* pData) {
            if (!pData) return;
            std::string rawData(pData);

            SKSE::GetTaskInterface()->AddTask([rawData]() {
                auto updateUI = []() {
                    std::string invJSON = GetPlayerInventoryJSON();
                    std::string cmd = "transmogSuccess(" + invJSON + ");";
                    if (PrismaUI) PrismaUI->Invoke(g_transmogView, cmd.c_str());
                    };

                size_t sep = rawData.find(",");
                if (sep == std::string::npos) { updateUI(); return; }

                RE::FormID sID = 0, tID = 0;
                try {
                    sID = static_cast<RE::FormID>(std::stoul(rawData.substr(0, sep), nullptr, 16));
                    tID = static_cast<RE::FormID>(std::stoul(rawData.substr(sep + 1), nullptr, 16));
                }
                catch (...) { updateUI(); return; }

                auto currentWeapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(tID);
                if (!currentWeapon) { updateUI(); return; }

                RE::FormID rootTargetID = tID;
                auto activeIt = g_activeWeaponTransmogs.find(tID);
                if (activeIt != g_activeWeaponTransmogs.end()) {
                    rootTargetID = activeIt->second.target;
                }
                auto rootWeapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(rootTargetID);
                if (!rootWeapon) rootWeapon = currentWeapon;

                auto player = RE::PlayerCharacter::GetSingleton();
                if (!player) { updateUI(); return; }

                bool wasEquipped = false;
                auto inventory = player->GetInventory();
                auto it = inventory.find(currentWeapon);

                if (it != inventory.end() && it->second.second) {
                    auto entry = it->second.second.get();
                    if (entry && entry->extraLists) {
                        for (auto xList : *entry->extraLists) {
                            if (xList && (xList->HasType(RE::ExtraDataType::kWorn) || xList->HasType(RE::ExtraDataType::kWornLeft))) {
                                wasEquipped = true; break;
                            }
                        }
                    }
                }

                auto sourceWeapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(sID);
                if (!sourceWeapon) { updateUI(); return; }

                auto newWeapon = rootWeapon->CreateDuplicateForm(false, nullptr)->As<RE::TESObjectWEAP>();
                if (!newWeapon) { updateUI(); return; }

                RestoreWeaponStats(newWeapon, rootWeapon); 

                std::string baseN(rootWeapon->GetName());
                if (baseN.find(" EDT") == std::string::npos) baseN += " EDT";
                newWeapon->SetFullName(baseN.c_str());

                auto sM = sourceWeapon->As<RE::TESModel>(); auto tM = newWeapon->As<RE::TESModel>();
                if (sM && tM) tM->SetModel(sM->GetModel());
                newWeapon->firstPersonModelObject = sourceWeapon->firstPersonModelObject;

                player->RemoveItem(currentWeapon, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                g_activeWeaponTransmogs.erase(currentWeapon->GetFormID());

                player->AddObjectToContainer(newWeapon, nullptr, 1, nullptr);
                if (wasEquipped) RE::ActorEquipManager::GetSingleton()->EquipObject(player, newWeapon);

                g_activeWeaponTransmogs[newWeapon->GetFormID()] = { sID, rootTargetID, wasEquipped };

                updateUI();
                });
            });

        PrismaUI->RegisterJSListener(g_transmogView, "SET_TYPING_ON", [](const char*) {
            SKSE::GetTaskInterface()->AddTask([]() { if (auto cm = RE::ControlMap::GetSingleton()) cm->AllowTextInput(true); });
            });

        PrismaUI->RegisterJSListener(g_transmogView, "SET_TYPING_OFF", [](const char*) {
            SKSE::GetTaskInterface()->AddTask([]() { if (auto cm = RE::ControlMap::GetSingleton()) cm->AllowTextInput(false); });
            });

        PrismaUI->RegisterJSListener(g_transmogView, "CLOSE_UI", [](const char*) {
            if (auto cm = RE::ControlMap::GetSingleton()) cm->AllowTextInput(false);
            if (PrismaUI) { PrismaUI->Unfocus(g_transmogView); PrismaUI->Hide(g_transmogView); }
            });

        PrismaUI->Hide(g_transmogView);
        RE::BSInputDeviceManager::GetSingleton()->AddEventSink(WeaponInputHandler::GetSingleton());
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse); LoadConfig();
    auto serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID('TRMW');
    serialization->SetSaveCallback(SaveCallback);
    serialization->SetLoadCallback(LoadCallback);
    SKSE::GetMessagingInterface()->RegisterListener("SKSE", MessageHandler);
    return true;
}