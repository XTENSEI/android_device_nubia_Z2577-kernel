# Kernel source for the Nubia V80 Max (Z2577)

ZTE's Android 15 kernel drop for `ums9230_6h10`, 5.15.178, published unchanged
except for the removals below. `prebuilts/` (5.6 GB of toolchains) and `gki2.0/`
(build output) are not published.

## Removed: 39 `Android.bp` and 4 `Android.mk`

The drop carries its own copies of AOSP's `kernel/configs` and `tools/mkbootimg`.
This repository is checked out at `kernel/nubia/Z2577` inside the ROM tree, where
Soong and kati scan every one of those files, and every module they define is
already defined by the ROM:

- `kernel/configs/build/Android.bp` exists three times: at the drop root, under
  `bsp/kernel5.15/`, and in the ROM itself, so `kernel-config-soong-rules` is
  defined three times and `mkbootimg`, `unpack_bootimg`, `repack_bootimg`,
  `bootimg_headers`, `kconfig_xml_fixup` and the versioned `kotlin_config_*`
  modules follow it.
- `drivers/staging/greybus/tools/Android.mk` exists twice, under `common/` and
  `kernel5.15/`, so `gb_loopback_test` is defined twice.
- `vendor/zte/charger/freetype/Android.mk` builds `libttf_chg` against
  `external/libpng`, and the charger is built by its own makefiles.

None of these files is read by `bsp/kernel5.15/build/build.sh`, which is what
builds this source. Removing them is the only deviation from the drop, and it is
required for the tree to coexist with the ROM at all.

## Build

```bash
cd bsp/kernel5.15/build
ZTE_BOARD_NAME=swgreene ./build.sh
```

`build.config/build.config.gki.aarch64.ums9230_user` selects
`ums9230_gki_defconfig`. The out of tree module trees under `../modules/` are not
in the drop, so `EXT_MODULES` cannot resolve: this source can produce `Image` and
the device tree, not the vendor modules.
