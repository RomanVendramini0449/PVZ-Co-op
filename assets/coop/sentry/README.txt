Sentry Turret art goes here
===========================

  idle\        numbered PNG frames (PNG0000.png, PNG0001.png...): standing, looping
  fire\        numbered PNG frames: one shot, start to finish
  projectile.png   one picture of its shot, pointing RIGHT (it is turned to
                   face where it flies)

All frames the same canvas size, drawn in the same place on it (export them
the way the Legendary Demoman's were). The game works out the size and where
the turret stands from the frames: it is drawn 95 px tall, standing in the
middle of its square. Facing right, towards the zombies.

Then run:  python tools\zombie-art\pack_coop_assets.py   and BUILD.bat.
Anything missing is skipped: until then it looks like a Cattail.
