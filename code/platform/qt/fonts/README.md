# Fonts embedded in the Qt backend

MeOS draws its user interface in Segoe UI, a Windows font that may not be passed on.
The Qt backend embeds Selawik instead, Microsoft's open replacement with the vertical
metrics of Segoe UI, and uses it when Segoe UI is not installed
(`code/platform/qt/win32_text.cpp`).

| File | Font |
|---|---|
| `selawk.ttf` | Selawik Regular |
| `selawkb.ttf` | Selawik Bold |
| `selawkl.ttf` | Selawik Light |

Source: https://github.com/microsoft/Selawik, release 1.01 (`Selawik_Release.zip`, SHA-256
`3f62c51e05e3b5a1e6241cf92a371f0be2ea1183aa87b30718bbd40832a8d423`), unchanged.
License: SIL Open Font License 1.1, see `Selawik-LICENSE.txt`.
