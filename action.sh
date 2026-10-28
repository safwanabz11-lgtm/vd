#!/system/bin/sh
# One-click RANDOM: new seed -> each target app gets a stable random full device profile.
MODDIR=${0%/*}
SEED=$(od -An -N4 -tu4 /dev/urandom | tr -d ' ')
sed -i "s/^seed=.*/seed=$SEED/; s/^mode=.*/mode=random/" "$MODDIR/config.txt"
echo "New seed: $SEED (mode=random)"
grep -vE '^\s*(#|$)' "$MODDIR/apps.txt" | cut -d= -f1 | while read -r p; do
  am force-stop "$p" 2>/dev/null && echo "restarted: $p"
done
echo "Open the game again to apply."
