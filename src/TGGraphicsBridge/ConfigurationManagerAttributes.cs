// SPDX-License-Identifier: MIT
namespace TGGraphicsBridge
{
    // Row hints for settings managers: FoA Mod Manager reads DisplayName, DisplaySection, SortOrder,
    // ChoiceLabels and Hidden; BepInEx ConfigurationManager reads DispName, Category and Browsable. FoA Mod
    // Manager sorts Order ascending and ConfigurationManager descending, so only SortOrder is set.
    public sealed class ConfigurationManagerAttributes
    {
        public string DisplayName, DispName, DisplaySection, Category, ChoiceLabels;
        public int? SortOrder;
        public bool? Hidden, Browsable;
        public bool Visible { set { Hidden = !value; Browsable = value; } }
    }
}
