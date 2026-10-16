Zombie headwear, as the game draws it
=====================================

Your repainted head pieces: every PNG directly in this folder replaces the
game's piece of the same name in Adventure Plus (CMakeLists.txt reads them,
with assets/coop/zombie-parts). The game's own are kept in save/, which the
game does not read -- copy one back here to undo a change.

  zombie_cone1/2/3              Conehead: whole, dented, nearly gone (yours: raised 7 px, @0,-7)
  zombie_bucket1/2/3            Buckethead: the same three stages (yours: raised 7 px, @0,-7)
  zombie_football_helmet/2/3    Football Zombie's helmet
  zombie_digger_hardhat/2/3     Digger (miner) Zombie's hard hat
  _all-headwear-sheet.png       all of them on one page, to look at

The 1/2/3 stages are how damaged it is: the game swaps to 2, then 3, as the
zombie takes hits, then knocks it off. Repaint all three if you want the
damage to show on your version (or use the same picture for all three).

To use your version (Adventure Plus only)
  1. Paint over the original. Easiest is to keep the same canvas size, with
     your hat where the original sits: then it lines up exactly.
  2. Save it as a PNG with transparency, with the EXACT same file name,
     e.g. zombie_cone1.png.
  3. Put it in  assets\coop\zombie-parts\   (NOT this folder).
  4. Run BUILD.bat.

  Only Adventure Plus uses it; the normal campaign keeps the cones and
  buckets.

Bigger or a different shape than the original?
  A picture of another size is squeezed into the original's box, which can
  look stretched. To draw it at its own size instead, add where its top-left
  goes, relative to the original's top-left, to the name:
      zombie_cone1@-10,-25.png    (10 px further left, 25 px higher)
  That is how the Crazy Dave parts are done (assets\coop\dave-parts).
