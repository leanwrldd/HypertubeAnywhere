# Creates the Enhanced Input assets this mod uses. Run headless from the SML project:
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript -script="<this file>"
# Re-running updates the existing assets in place.
import unreal

ROOT = "/HypertubeAnywhere/Input"
# Bound whenever the player controls their character, both on foot and while inside a hypertube.
PARENT_CONTEXT = "/Game/FactoryGame/Inputs/Player/MC_PlayerController"

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def get_or_create(name, cls):
    path = f"{ROOT}/{name}"
    if lib.does_asset_exist(path):
        asset = lib.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        asset = tools.create_asset(name, ROOT, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    return asset


def make_key(name):
    key = unreal.Key()
    key.set_editor_property("key_name", name)
    return key


def make_mapping(context, action, key_name, triggers, mappable_name, display_name):
    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property("action", action)
    mapping.set_editor_property("key", make_key(key_name))
    mapping.set_editor_property("triggers", list(triggers))
    settings = unreal.new_object(unreal.PlayerMappableKeySettings, outer=context)
    settings.set_editor_property("name", mappable_name)
    settings.set_editor_property("display_name", display_name)
    mapping.set_editor_property("setting_behavior", unreal.PlayerMappableKeySettingBehaviors.OVERRIDE_SETTINGS)
    mapping.set_editor_property("player_mappable_key_settings", settings)
    return mapping


toggle = get_or_create("IA_HTA_ToggleHypertube", unreal.InputAction)
toggle.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
toggle.set_editor_property("action_description", "Enter / Exit Hypertube")

# Rebindable key, shown under Options -> Controls.
keybinds = get_or_create("MC_HTA_Keybinds", unreal.FGChildInputMappingContext)
keybinds.set_editor_property("m_display_name", "Hypertube Anywhere")
keybinds.set_editor_property("m_parent_context", unreal.load_asset(PARENT_CONTEXT))
pressed = unreal.new_object(unreal.InputTriggerPressed, outer=keybinds)
keybinds.set_editor_property("mappings", [
    make_mapping(keybinds, toggle, "K", [pressed], "HTA.ToggleHypertube", "Enter / Exit Hypertube"),
])

for asset in (toggle, keybinds):
    if not lib.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {asset.get_path_name()}")
    unreal.log(f"HTA: saved {asset.get_path_name()}")
