# Skyrim Transmog System (SKSE)

This repository contains the C++ source code for the **Skyrim Armor and Cloth Transmog** and **Skyrim Weapon Transmog** mods. 

These SKSE plugins instantly swap the visual models of items while retaining original stats, smithing upgrades, and enchantments without using Papyrus scripts. The user interface is powered by **PrismaUI**.

## Nexus Mods Pages
* **[Skyrim Armor and Cloth Transmog](https://www.nexusmods.com/skyrimspecialedition/mods/174356)**
* **[Skyrim Weapon Transmog](https://www.nexusmods.com/skyrimspecialedition/mods/174586)**

## How It Works
1. **Dynamic Duplication:** Uses `CreateDuplicateForm` to generate a temporary copy of the Target item.
2. **Visual Swap:** Extracts 3D model data (`TESModel::SetModel` and `firstPersonModelObject`) from the Source item and applies it to the duplicate.
3. **Stat Restoration:** Restores original stats, keywords, and enchantments.
4. **Instant Equip:** Re-equips the new item using `ActorEquipManager`. 
5. **Save Bloat Protection:** Custom serialization callbacks (`SaveCallback` / `LoadCallback`) ensure only active transmogged items are saved.
6. **C++ to Web Bridge:** Communicates with the **PrismaUI** frontend via custom JS listeners.

## Repository Structure
* `ArmorTransmog.cpp` : Armor/Clothing transmog module (Includes "Make Invisible" logic).
* `ArmorUI.html` : PrismaUI frontend for the Armor mod.
* `WeaponTransmog.cpp` : Weapon transmog module (Includes strict weapon-type checks).
* `WeaponUI.html` : PrismaUI frontend for the Weapon mod.

## Requirements & Building
This project is built using CommonLibSSE-NG. To compile these plugins, you will need:
* **PrismaUI API**
* C++20 compatible compiler
* CMake
* **[CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)** by CharmedBaryon
* Generated from **[CommonLibSSE-NG-Template-Plugin](https://github.com/Monitor221hz/CommonLibSSE-NG-Template-Plugin)** by Monitor221hz

## License
MIT License
