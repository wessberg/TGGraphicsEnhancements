// SPDX-License-Identifier: MIT
// TG Path Tracing's cloth rules (src/TGPathTracing/ClothRules.cs), checked against the census of the harness's scenes
// (docs/agents/cloth.md: every material whose shader or name says cloth, in the state audits of every run up to
// 2026-10-07). The owner (PT-49): "we really need to make sure we detect cloth correctly. That also includes clothes hanging
// on a drier and stuff". Thin cloth is a hanging sheet the light passes through; cloth lying on a floor, thick, stuffed or
// wrapped round something (one face drawn) is a solid surface; characters' clothes are left as they are.
using System;
using TGPathTracing;

internal static class Program
{
    private static int failures;
    private static void Expect(bool condition, string what)
    {
        if (!condition) { Console.WriteLine("FAIL: " + what); failures++; }
    }
    private static void Cloth(string shader, string name, bool doubleSided, bool thin) =>
        Expect(ClothRules.IsThinCloth(shader, name, doubleSided) == thin, name + " (" + shader + (doubleSided ? ", both sides" : ", one side") + ") " +
            (thin ? "is thin cloth" : "is no thin cloth"));
    private static int Main()
    {
        const string Lit = "HDRP/Lit", Wind = "TG/Clothes/Cloth_Wind";
        // The game's cloth in the wind: thin cloth by its shader family, whatever the material is called.
        foreach (var name in new[] { "Cloth_Wind", "Mat_BannerWind", "Mat_BannerWind_HoS_HeavyWind", "Mat_Banner_RedPriest_01_Wind",
                     "Mat_Prop_Cloth_PlaneBig_04_HoS_moving", "Mat_Prop_Drakkar_Ruin_02_Cloth", "Mat_Prop_Laundry_01_Moving", "Mat_Prop_Laundry_02_Moving",
                     "Mat_Sagremore_Banner_01_Cloth", "Mat_Ulfr_Banner_01" })
            Cloth(Wind, name, true, true);
        Cloth(Wind, "Cloth_Wind", false, true); // the plain one, drawn from one side: still the canopy shader
        Cloth("TG/Clothes/Cloth_Wind_HeavyWind", "Mat_Something_Moving_07", true, true); // the family, not one exact name
        Cloth(Wind, "", false, true);
        // Hanging sheets of HDRP/Lit: tents, awnings, wagon and boat covers, stall cloths, curtains, arrases, laundry on a line,
        // towels and sheets on a drier, rags hung up.
        foreach (var name in new[] { "Mat_BigTent_Plague_01", "Mat_DestroyedTent_01_Cloth", "Mat_Prop_Tent_01 1", "Mat_Tent_03DalRiata", "Mat_tent_03",
                     "Mat_BoatCloth_A", "Mat_Cloth_Wagon_01", "Mat_Wagoncloth_02", "Mat_Vech_Cage_Wagon_Cloth", "Mat_MerchantStall_Cloth_02_A_HoS",
                     "Mat_Prop_Cloth_Curtain_02", "Mat_Prop_Curtain_Medic_01", "Mat_Prop_Cloth_Line_01", "Mat_Prop_Cloth_Plane_06",
                     "Mat_Prop_Cloth_PlaneBig_KAM_01", "Mat_Prop_Cloth_Towel_01", "Mat_Prop_Towel_01", "Mat_Hos_sheet_01", "Mat_Prop_Sheet",
                     "Mat_Prop_Sheet_2_Red", "Mat_Prop_Rags_02", "Mat_Prop_Plane_Rag_03", "Mat_Tavern_Cloth_Tilt_01", "Mat_Prop_Cloth_Arras_01",
                     "Mat_Prop_Tablecloth_01", "Mat_Cradle_Sheets" })
            Cloth(Lit, name, true, true);
        // Hanging words make thin cloth even drawn from one side (a tent modelled with both faces), curtains too.
        Cloth(Lit, "Mat_Prop_TentBig_02", false, true);
        Cloth(Lit, "Mat_Prop_Curtain_red", true, true);
        // Cloth that lies, is thick or stuffed, or is no cloth at all: solid.
        foreach (var name in new[] { "Mat_Fabric_Floor_01", "Mat_Prop_FloorRag_02", "Mat_Prop_AnimalRug_01", "Mat_Marine_Fishing_Net_01",
                     "Mat_Plain_FishingNet_01", "Mat_Prop_Sack_02", "Mat_Prop_CoinsBag_01", "Mat_Prop_Sleeping_Bag_01", "Mat_Prop_Pillow_01",
                     "Mat_Prop_Wyrddear_Fur", "Mat_Prop_PrisonBed_01", "MI_Window_Glass_02", "Mat_Biome_Ivy_NoWind", "Mat_Prop_Bandage_01" })
            Cloth(Lit, name, true, false);
        // Cloth drawn from one side is wrapped round something (a body in its shroud, a shield's cover, bed sheets on a
        // mattress); stone drapery is stone.
        Cloth(Lit, "Mat_grave_body_cloth", false, false);
        Cloth(Lit, "Mat_Prop_Shootshield_03_Cloth", false, false);
        Cloth(Lit, "Mat_Prop_Bed_Sheets_01", false, false);
        Cloth(Lit, "Mat_Statue_HarpLady_Cloth_01", false, false);
        Cloth(Lit, "Mat_Statue_HarpLady_Cloth_01", true, false);
        // Words that only end like cloth words are none ("content", "intent").
        Cloth(Lit, "Mat_Content_01", true, false);
        Cloth(Lit, "Mat_Intent_02", true, false);
        // Characters' clothes stay as they are, and the interface is no surface.
        Cloth("TG/Character/Kandra_Lit", "Mat_Cape_Cloth_01", true, false);
        Cloth("TG/Character/Kandra_Lit", "Ribbon", true, false);
        Cloth("TG/UI/VFX_UI_SceneTransition_FBM", "TransitionScreen_Canvas", false, false);
        // The word parts of a name.
        Expect(string.Join(",", ClothRules.Words("Mat_BigTent_Plague_01")) == "mat,big,tent,plague", "a name's word parts split at case and underscores");
        Expect(string.Join(",", ClothRules.Words("Mat_Wagoncloth_02")) == "mat,wagoncloth", "a compound stays one word");
        Expect(ClothRules.Words(null).Count == 0, "no name, no words");
        Console.WriteLine(failures == 0 ? "cloth rules: all checks passed" : "cloth rules: " + failures + " checks failed");
        return failures == 0 ? 0 : 1;
    }
}
