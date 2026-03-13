# ✨ Skyrim Transmog System (SKSE) ✨
*The Ultimate, Lag-Free Outfit & Weapon System for Modern Skyrim.*

This repository contains the pure C++ source code for the highly popular **Skyrim Armor and Cloth Transmog** and **Skyrim Weapon Transmog** mods. 

Instead of relying on slow and heavy Papyrus scripts, these plugins utilize the power of SKSE to instantly swap the visual models of items while retaining all original stats, smithing upgrades, and enchantments in a fraction of a millisecond. 

The gorgeous, real-time, multilingual drag-and-drop user interface is proudly powered by **PrismaUI** (A modern Chromium-embedded framework for Skyrim).

## 🔗 Nexus Mods Pages (Downloads)
If you are a player looking to download and install the mods, please visit the official Nexus Mods pages:
* 🛡️ **[Skyrim Armor and Cloth Transmog (Nexus Mods)](https://www.nexusmods.com/skyrimspecialedition/mods/174356)**
* ⚔️ **[Skyrim Weapon Transmog (Nexus Mods)](https://www.nexusmods.com/skyrimspecialedition/mods/174586)**

---

## 🛠️ How It Works (Under the Hood)
For fellow mod authors and developers curious about the "magic" behind the instant swap:

1. **Dynamic Duplication:** The plugin uses `CreateDuplicateForm` to generate a temporary copy of the Target item (the one with the good stats) in the background.
2. **Visual Swap:** It extracts the 3D model data (`TESModel::SetModel` and `firstPersonModelObject`) from the Source item and applies it to the duplicate.
3. **Stat Restoration:** All critical data (Damage, Armor Rating, Weight, Value, Keywords, and Enchantments) are meticulously restored to ensure nothing is lost.
4. **Instant Equip:** The plugin removes the original item, adds the new transmogged item, and forces a seamless re-equip using the engine's `ActorEquipManager`. 
5. **Save Bloat Protection:** Custom serialization callbacks (`SaveCallback` / `LoadCallback`) ensure that only actively held transmogged items are saved, preventing save file bloat and clearing out discarded temporary forms.
6. **C++ to Web Bridge:** The plugin communicates seamlessly with the **PrismaUI** frontend via custom JS listeners, passing JSON payload data back and forth instantly.

## 📂 Repository Structure
* `ArmorTransmog.cpp` : The C++ source code for the Armor and Clothing transmog module (Features the "Make Invisible" logic).
* `ArmorUI.html` : The PrismaUI frontend for the Armor mod.
* `WeaponTransmog.cpp` : The C++ source code for the Weapon transmog module (Includes strict weapon-type checks).
* `WeaponUI.html` : The PrismaUI frontend for the Weapon mod.

## 📝 Requirements & Dependencies
To compile and run these plugins, you will need:
* **PrismaUI API** - The required framework for rendering the HTML/CSS/JS frontend interfaces.
* C++20 compatible compiler (e.g., MSVC)
* CMake
* [CommonLibSSE](https://github.com/Ryan-rsm-McKenzie/CommonLibSSE) (or CommonLibVR depending on your target)

## 📄 License
This project is licensed under the **MIT License**. You are free to read, learn from, and use this code in your own projects, provided that you include the original license and copyright notice.
