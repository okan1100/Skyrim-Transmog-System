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

PrismaView g_transmogView = 0;
PRISMA_UI_API::IVPrismaUI1* PrismaUI = nullptr;
uint32_t g_hotkey = 0x3D; // Default: F3

void LoadConfig() {
    std::string path = "Data/SKSE/Plugins/SkyrimTransmog.ini";
    std::ifstream file(path);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("Hotkey=") != std::string::npos) {
                try { g_hotkey = std::stoul(line.substr(line.find("=") + 1), nullptr, 0); }
                catch (...) { SKSE::log::error("Invalid INI formatting! Using F3 as default."); }
            }
        }
        file.close();
    }
}

std::string CleanName(std::string_view input) {
    std::string out;
    for (char c : input) {
        if (c != '"' && c != '\\' && c != '\n' && c != '\r' && c != '\t') out += c;
    }
    return out;
}

struct TransmogData { RE::FormID source; RE::FormID target; bool isEquipped; };
std::unordered_map<RE::FormID, TransmogData> g_activeTransmogs;

void SaveCallback(SKSE::SerializationInterface* serde) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || !serde->OpenRecord('TRMG', 1)) return;
    
    std::vector<TransmogData> toSave;
    auto inv = player->GetInventory();
    
    // Only save active transmog items that are currently in the player's inventory to prevent save bloat
    for (const auto& [item, data] : inv) {
        if (item && item->IsArmor()) {
            auto formID = item->GetFormID();
            if (g_activeTransmogs.find(formID) != g_activeTransmogs.end()) {
                bool worn = false;
                if (data.second && data.second->extraLists) {
                    for (auto* xList : *data.second->extraLists) {
                        if (xList && xList->HasType(RE::ExtraDataType::kWorn)) { worn = true; break; }
                    }
                }
                auto record = g_activeTransmogs[formID]; record.isEquipped = worn; toSave.push_back(record);
            }
        }
    }
    
    serde->WriteRecordData(toSave.size());
    for (auto& record : toSave) {
        serde->WriteRecordData(record.source); 
        serde->WriteRecordData(record.target); 
        serde->WriteRecordData(record.isEquipped);
    }
}

void LoadCallback(SKSE::SerializationInterface* serde) {
    uint32_t type, version, length;
    auto player = RE::PlayerCharacter::GetSingleton(); if (!player) return;

    // Clean up temporary duplication forms
    auto currentInv = player->GetInventory();
    for (auto& [item, data] : currentInv) {
        if (item && item->IsArmor()) {
            const char* rawName = item->GetName();
            if (rawName) { 
                std::string name(rawName);
                if (name.find(" EDT") != std::string::npos || name.find(" [INV]") != std::string::npos) {
                    player->RemoveItem(item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            }
        }
    }
    g_activeTransmogs.clear();

    while (serde->GetNextRecordInfo(type, version, length)) {
        if (type == 'TRMG') {
            size_t count; serde->ReadRecordData(count);
            for (size_t i = 0; i < count; i++) {
                RE::FormID src, tgt; bool worn;
                serde->ReadRecordData(src); serde->ReadRecordData(tgt); serde->ReadRecordData(worn);

                RE::FormID newTgt; if (!serde->ResolveFormID(tgt, newTgt)) continue;
                auto tA = RE::TESForm::LookupByID<RE::TESObjectARMO>(newTgt); if (!tA) continue;

                auto nA = tA->CreateDuplicateForm(false, nullptr)->As<RE::TESObjectARMO>(); if (!nA) continue;

                RE::FormID finalSrc = 0;
                if (src == 0) {
                    // Rebuild 'Make Invisible' logic on load
                    nA->SetFullName((std::string(tA->GetName()) + " [INV]").c_str());
                    uint32_t sM = (uint32_t)tA->GetSlotMask();
                    nA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kHead);
                    nA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kHair);
                    nA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kBody);
                    nA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kHands);
                    nA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kFeet);

                    if (sM & 0x00000001 || sM & 0x00000002 || sM & 0x00001000) nA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 24));
                    else if (sM & 0x00000004) nA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 25));
                    else if (sM & 0x00000008) nA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 26));
                    else if (sM & 0x00000080) nA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 27));

                    nA->armorAddons.clear();
                    for (auto oldArma : tA->armorAddons) {
                        auto newArma = oldArma->CreateDuplicateForm(false, nullptr)->As<RE::TESObjectARMA>();
                        if (newArma) {
                            newArma->bipedModels[0].SetModel(""); newArma->bipedModels[1].SetModel("");
                            newArma->bipedModelData.bipedObjectSlots = static_cast<RE::BIPED_MODEL::BipedObjectSlot>(0);
                            nA->armorAddons.push_back(newArma); break;
                        }
                    }
                }
                else {
                    RE::FormID newSrc; if (serde->ResolveFormID(src, newSrc)) {
                        auto sA = RE::TESForm::LookupByID<RE::TESObjectARMO>(newSrc);
                        if (sA) {
                            finalSrc = newSrc; nA->SetFullName((std::string(tA->GetName()) + " EDT").c_str());
                            nA->armorAddons = sA->armorAddons;
                        }
                    }
                }
                player->AddObjectToContainer(nA, nullptr, 1, nullptr);
                if (worn) RE::ActorEquipManager::GetSingleton()->EquipObject(player, nA);
                g_activeTransmogs[nA->GetFormID()] = { finalSrc, newTgt, worn };
            }
        }
    }
}

std::string GetAllModArmorsJSON() {
    auto dataHandler = RE::TESDataHandler::GetSingleton(); if (!dataHandler) return "{}";
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> catalog;
    for (auto* armor : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
        if (!armor || !armor->GetName() || armor->GetName()[0] == '\0') continue;
        if (!armor->HasKeywordString("ArmorHeavy") && !armor->HasKeywordString("ArmorLight") && !armor->HasKeywordString("ArmorClothing")) continue;
        
        auto file = armor->GetFile(0);
        std::string mod = file ? CleanName(file->GetFilename()) : "Skyrim.esm";
        char buf[512]; snprintf(buf, sizeof(buf), "{\"id\":\"%08X\",\"name\":\"%s\"}", armor->GetFormID(), CleanName(armor->GetName()).c_str());
        uint32_t s = (uint32_t)armor->GetSlotMask(); std::string cat = "";
        
        if (s & 0x00000001 || s & 0x00000002 || s & 0x00001000) cat = "helmet";
        else if (s & 0x00000004) cat = "chest"; else if (s & 0x00000008) cat = "gloves"; else if (s & 0x00000080) cat = "boots";
        if (!cat.empty()) catalog[mod][cat].push_back(buf);
    }
    
    std::stringstream ss; ss << "{"; bool fP = true;
    for (const auto& [m, c] : catalog) {
        if (!fP) ss << ","; ss << "\"" << m << "\":{"; bool fC = true;
        for (const auto& [cn, itms] : c) {
            if (!fC) ss << ","; ss << "\"" << cn << "\":[";
            for (size_t i = 0; i < itms.size(); ++i) ss << itms[i] << (i == itms.size() - 1 ? "" : ",");
            ss << "]"; fC = false;
        }
        ss << "}"; fP = false;
    }
    ss << "}"; return ss.str();
}

std::string GetPlayerInventoryJSON() {
    auto player = RE::PlayerCharacter::GetSingleton(); if (!player) return "{}";
    std::stringstream h, c, g, b; bool hF = 1, cF = 1, gF = 1, bF = 1;
    for (auto& [item, data] : player->GetInventory()) {
        if (item && item->IsArmor()) {
            auto armor = item->As<RE::TESObjectARMO>(); uint32_t slot = (uint32_t)armor->GetSlotMask();
            auto it = g_activeTransmogs.find(armor->GetFormID());
            if (it != g_activeTransmogs.end()) {
                auto orig = RE::TESForm::LookupByID<RE::TESObjectARMO>(it->second.target);
                if (orig) slot = (uint32_t)orig->GetSlotMask();
            }
            char buf[512]; snprintf(buf, sizeof(buf), "{\"id\":\"%08X\",\"name\":\"%s\"}", armor->GetFormID(), CleanName(armor->GetName()).c_str());
            if (slot & 0x00000001 || slot & 0x00000002 || slot & 0x00001000) { if (hF) h << buf; else h << "," << buf; hF = 0; }
            else if (slot & 0x00000004) { if (cF) c << buf; else c << "," << buf; cF = 0; }
            else if (slot & 0x00000008) { if (gF) g << buf; else g << "," << buf; gF = 0; }
            else if (slot & 0x00000080) { if (bF) b << buf; else b << "," << buf; bF = 0; }
        }
    }
    return "{\"helmet\":[" + h.str() + "],\"chest\":[" + c.str() + "],\"gloves\":[" + g.str() + "],\"boots\":[" + b.str() + "]}";
}

class TransmogInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
public:
    static TransmogInputHandler* GetSingleton() { static TransmogInputHandler instance; return &instance; }
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override {
        if (!a_event || !*a_event || !PrismaUI) return RE::BSEventNotifyControl::kContinue;
        for (auto e = *a_event; e; e = e->next) {
            if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                auto b = e->AsButtonEvent();
                if (b && b->IsDown() && b->GetIDCode() == g_hotkey) {
                    if (PrismaUI->IsHidden(g_transmogView)) {
                        PrismaUI->Show(g_transmogView); PrismaUI->Focus(g_transmogView, false);
                        PrismaUI->Invoke(g_transmogView, ("loadUI(" + GetAllModArmorsJSON() + ", " + GetPlayerInventoryJSON() + ");").c_str());
                    }
                    else { PrismaUI->Unfocus(g_transmogView); PrismaUI->Hide(g_transmogView); }
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
        PrismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
        if (!PrismaUI) return;
        g_transmogView = PrismaUI->CreateView("SkyrimTransmogUI/index.html", [](PrismaView) {});
        PrismaUI->RegisterJSListener(g_transmogView, "TRANSMOGRIFY", [](const char* pData) {
            std::string rawData(pData);
            SKSE::GetTaskInterface()->AddTask([rawData]() {
                size_t sep = rawData.find(","); if (sep == std::string::npos) return;
                std::string sStr = rawData.substr(0, sep), tStr = rawData.substr(sep + 1);
                RE::FormID tID = std::stoul(tStr, nullptr, 16);
                
                auto* currentA = RE::TESForm::LookupByID<RE::TESObjectARMO>(tID); if (!currentA) return;
                RE::FormID rootID = tID; auto activeIt = g_activeTransmogs.find(tID);
                if (activeIt != g_activeTransmogs.end()) rootID = activeIt->second.target;
                auto* rootA = RE::TESForm::LookupByID<RE::TESObjectARMO>(rootID); if (!rootA) rootA = currentA;

                auto* player = RE::PlayerCharacter::GetSingleton(); bool wasWorn = false;
                auto inv = player->GetInventory(); auto it = inv.find(currentA);
                if (it != inv.end() && it->second.second) {
                    for (auto* xL : *it->second.second->extraLists) { if (xL && xL->HasType(RE::ExtraDataType::kWorn)) { wasWorn = true; break; } }
                }
                auto* newA = rootA->CreateDuplicateForm(false, nullptr)->As<RE::TESObjectARMO>(); if (!newA) return;

                RE::FormID finalSrcID = 0;
                
                // Invisible Mode Handler
                if (sStr == "HIDE") {
                    newA->SetFullName((std::string(rootA->GetName()) + " [INV]").c_str());
                    uint32_t sM = (uint32_t)rootA->GetSlotMask();
                    newA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kHead);
                    newA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kHair);
                    newA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kBody);
                    newA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kHands);
                    newA->RemoveSlotFromMask(RE::BIPED_MODEL::BipedObjectSlot::kFeet);
                    
                    if (sM & 0x00000001 || sM & 0x00000002 || sM & 0x00001000) newA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 24));
                    else if (sM & 0x00000004) newA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 25));
                    else if (sM & 0x00000008) newA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 26));
                    else if (sM & 0x00000080) newA->AddSlotToMask(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << 27));

                    newA->armorAddons.clear();
                    for (auto oldArma : rootA->armorAddons) {
                        auto newArma = oldArma->CreateDuplicateForm(false, nullptr)->As<RE::TESObjectARMA>();
                        if (newArma) {
                            newArma->bipedModels[0].SetModel(""); newArma->bipedModels[1].SetModel("");
                            newArma->bipedModelData.bipedObjectSlots = static_cast<RE::BIPED_MODEL::BipedObjectSlot>(0);
                            newA->armorAddons.push_back(newArma); break;
                        }
                    }
                }
                else {
                    RE::FormID sID = std::stoul(sStr, nullptr, 16);
                    auto* sA = RE::TESForm::LookupByID<RE::TESObjectARMO>(sID);
                    if (sA) { finalSrcID = sID; newA->SetFullName((std::string(rootA->GetName()) + " EDT").c_str()); newA->armorAddons = sA->armorAddons; }
                }
                
                player->RemoveItem(currentA, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                g_activeTransmogs.erase(currentA->GetFormID());
                player->AddObjectToContainer(newA, nullptr, 1, nullptr);
                if (wasWorn) RE::ActorEquipManager::GetSingleton()->EquipObject(player, newA);
                
                g_activeTransmogs[newA->GetFormID()] = { finalSrcID, rootID, wasWorn };
                PrismaUI->Invoke(g_transmogView, ("transmogSuccess(" + GetPlayerInventoryJSON() + ");").c_str());
                });
            });
            
        PrismaUI->RegisterJSListener(g_transmogView, "SET_TYPING", [](const char* p) {
            bool t = (std::string(p) == "true"); SKSE::GetTaskInterface()->AddTask([t]() { if (auto* cM = RE::ControlMap::GetSingleton()) cM->AllowTextInput(t); });
            });
            
        PrismaUI->RegisterJSListener(g_transmogView, "CLOSE_UI", [](const char*) { PrismaUI->Unfocus(g_transmogView); PrismaUI->Hide(g_transmogView); });
        PrismaUI->Hide(g_transmogView);
        RE::BSInputDeviceManager::GetSingleton()->AddEventSink(TransmogInputHandler::GetSingleton());
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse); LoadConfig();
    auto* serde = SKSE::GetSerializationInterface();
    serde->SetUniqueID('TRMG'); serde->SetSaveCallback(SaveCallback); serde->SetLoadCallback(LoadCallback);
    SKSE::GetMessagingInterface()->RegisterListener("SKSE", MessageHandler);
    return true;
}