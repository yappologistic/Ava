# Notch wallpaper collection

This directory contains the 12 source-of-truth wallpaper masters used by Ava.

## Delivery format

- Resolution: 3840 x 2160
- Aspect ratio: 16:9
- Master format: PNG
- Runtime format: high-quality 4K JPEG (`runtime/`)

The PNG masters are intentionally kept untouched. The runtime JPEGs are compressed
directly from those masters without resizing and are the files embedded into the app
for responsive thumbnail decoding and a smaller executable.

## Display policy

Always preserve the image's aspect ratio. Scale to fill the monitor and crop from the
center (`PreserveAspectCrop` / `cover`); never stretch. This keeps normal 16:9 and
16:10 displays balanced.

## Wallpaper order

1. `01-alpine-first-light_upscayl_1x_high-fidelity-4x.png`
2. `02-volcanic-coast-after-rain_upscayl_1x_high-fidelity-4x.png`
3. `03-redwood-creek-mist_upscayl_1x_high-fidelity-4x.png`
4. `04-highland-river-clearing_upscayl_1x_high-fidelity-4x.png`
5. `05-glacial-lagoon-blue-hour_upscayl_1x_high-fidelity-4x.png`
6. `06-sandstone-stormlight_upscayl_1x_high-fidelity-4x.png`
7. `07-autumn-larch-valley_upscayl_1x_high-fidelity-4x.png`
8. `08-moonlit-winter-pond_upscayl_1x_high-fidelity-4x.png`
9. `09-volcanic-braided-dusk_upscayl_1x_high-fidelity-4x.png`
10. `10-coastal-dunes-sunrise_upscayl_1x_high-fidelity-4x.png`
11. `11-rainforest-basalt-falls_upscayl_1x_high-fidelity-4x.png`
12. `12-salt-flat-storm-sunset_upscayl_1x_high-fidelity-4x.png`
