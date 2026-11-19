# OpenVR Overlay Template

Windows 向けの再利用可能な OpenVR オーバーレイ・ダッシュボードアプリ用テンプレートです。

## 含まれる機能

- **SteamVR / OpenVR 基盤 (`src/vr/`)**
  - ワールドオーバーレイ + SteamVR ダッシュボードオーバーレイ管理 (`OverlayManager`)
  - SteamVR ダッシュボード開閉に連動したワールドオーバーレイの自動退避・復帰
  - オーバーレイのドラッグ移動 (`OverlayDragger` / `OverlayDragMotion`)、スティックによる距離調整 (`OverlayDistanceMotion`)、HMD 相対位置の保存・復元 (`OverlayRelativePose` / `OverlayPlacement`)
  - 両手コントローラーからのレイ投影・ポインター操作 (`PointerInteractor` / `PointerProjection`)
  - SteamVR Input アクション制御 (`SteamVRInput` + `resources/openvr/actions.json` / `bindings_*.json`)
- **描画・操作補助 (`src/ui/`)**
  - Direct3D 11 + Dear ImGui オフスクリーン描画 & VR ポインター描画 (`OverlayRenderer`)
  - スティック入力対応のスムーズスクロール (`SmoothScroll`)
- **アプリ基盤 (`src/app/`, `src/config/`, `src/diagnostics/`)**
  - システムトレイ常駐（オーバーレイ位置リセット・終了）(`SystemTray`)
  - TOML 設定ファイル読み書き (`Settings` / `SettingsSession`)
  - ログ収集 (`Logger`)

## UI の実装場所

`src/app/ApplicationHost.cpp` の以下のメソッドに独自の ImGui UI を実装してください。

- `RenderWorldOverlayUi(float smoothScrollPixels)` — ワールド空間オーバーレイの UI
- `RenderDashboardUi(float smoothScrollPixels)` — SteamVR ダッシュボードタブの UI

## ビルド

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```
