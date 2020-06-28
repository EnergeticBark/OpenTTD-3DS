# OpenTTD port for the 3DS
A port of OpenTTD v0.7.0 to the Nintendo 3DS
## Installation
* Download the latest version of openttd.zip from the releases page.
* Extract its contents to /3ds/ on your SD card.
## Building

I've only tried compiling this on Linux so I'm not sure if it will work on a non-Unix-like OS.
* Install the 3ds-sdl library using (dkp-)pacman
* From the root of the project directory, run:
```bash
./configure --os=N3DS --host $DEVKITARM/bin/arm-none-eabi --enable-static --prefix-dir=$DEVKITPRO --with-sdl --without-png --without-threads --disable-network --disable-unicode --without-libfontconfig --without-zlib --without-libfreetype --without-icu
```
* Type:
```bash
make
```
* Then, in ./bin/ run:
```bash
$DEVKITPRO/tools/bin/3dsxtool openttd openttd.3dsx
```
I don't know really what I'm doing when it come to writing configure files lol. If anyone has advice I'll gladly take it.
## Why 0.7.0 instead of a later version?
Memory restrictions. The later versions I tried would always crash, even with a small map, because they simply wouldn't fit into the application memory of the 3DS. (The OLD 3DS at least. I don't own a NEW model I could test with.)
One of the other reasons I picked 0.7.0 specifically, is it's the version that the DS port uses. This gives me something I can borrow some ideas from.
It also has plenty of the features you would expect from OpenTTD, like OpenGFX support.
## To do
- [x] Resize UI elements to better fit the 3DS's low screen resolution. 
- [ ] Put a viewport on the top screen, right now it's used as a console.
- [ ] Networking support.
- [ ] Fix music.
- [ ] (maybe) Move to a slightly newer version of OpenTTD??????
