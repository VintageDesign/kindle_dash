# Kindle Dash

A GTK2 dashboard for a  Kindle Paperwhite 4. It cycles through a configurable set of pages. 

## Configuration (`dash.ini`)
`[app] pages=` lists the pages in order. Each entry names a `[page:*]` section, and the section's `type=` picks the code that draws it. See `kual/kindledash/dash.ini.example`.

Supported Pages:
- World Clock: Tells the time. 
- Weather: Pulls data from Open-Meteo for a given location.
- Image: Renders a given SVG to the screen.

## Building:
### For Hardware:
The Kindle build needs a koxtoolchain cross compiler **plus a sysroot that contains the Kindle's GTK2, pango, cairo and glib headers and libraries**. koxtoolchain alone does not include these; one way to get them is KindleModding's kindle-sdk.

```sh
make                       # cross build -> build/kindle/kindledash  (CROSS_TC=arm-kindlepw2-linux-gnueabi)
make deploy KINDLE=/media/$USER/Kindle   # copy to the Kindle's extensions/ (keeps an existing dash.ini)
```

Use `CROSS_TC=arm-kindlehf-linux-gnueabihf` for firmware 5.16.3 or newer.

### For Desktop
```sh
make HOST=1
./build/host/kindledash kual/kindledash/dash.ini.example
# Render every page to PNG at an exact size, without a window manager:
KINDLEDASH_RENDER=/tmp/shot KINDLEDASH_SIZE=1448x1072 ./build/host/kindledash dash.ini
```
A page's `url=` can point the weather page at a mock server.

