# Motorola Razr V3 — source for the phone item

From `~/Downloads/motorola-razr-v3` (OBJ authored in cm, no .mtl shipped). Imported to
`/Game/Justin/Items/{Meshes,Textures,Materials}`.

| OBJ material slot | Parts | Texture |
|---|---|---|
| initialShadingGroup | body, keypad, cover | T_PhoneRazr_Body_BC |
| lambert3SG / lambert4SG / lambert11SG | back, single side button, logo | T_PhoneRazr_Back_BC |
| lambert6SG | side button pair | T_PhoneRazr_SideButtons_BC |
| lambert7SG / lambert8SG | hinge, speaker grille | T_PhoneRazr_Decals_BC |
| lambert9SG | front screen | none (dark body-atlas area) |

The mapping was matched from each slot's UV bounds, since the .mtl was missing.
