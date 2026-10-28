SKIPUNZIP=0
ui_print "- Badhon Game Profile (Zygisk)"
ui_print "- Enable Zygisk, then reboot."
ui_print "- Edit apps.txt (target games) and config.txt in the module folder."
OLD=/data/adb/modules/badhon_game_profile
if [ -f "$OLD/apps.txt" ]; then
  cp "$OLD/apps.txt" "$MODPATH/apps.txt"
  cp "$OLD/config.txt" "$MODPATH/config.txt"
  ui_print "- Kept your old apps.txt / config.txt"
fi
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/action.sh" 0 0 0755
