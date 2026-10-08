# MW Japanese Bridge

日本語版『Need for Speed: Most Wanted』（2005）の正規インストール資産を使い、
NFSPatcher生成の英語1.3系`speed.exe`で日本語UI・日本語警察無線・日本語ムービーを
読み込めるようにするASI MODです。

English summary: this ASI bridge lets the supported NFSPatcher English 1.3
executable use resources from a legitimate Japanese retail installation. The
repository and release do not include game executables, language data, audio,
movies, NFSPatcher, an ASI loader, or Widescreen Fix.

## 対応実行ファイル

ファイル名やサイズだけでなく、SHA-256が一致する場合だけインストールします。

| 種類 | サイズ | SHA-256 | LAA |
|---|---:|---|---|
| NFSPatcher English 1.3 | 6,029,312 | `80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C` | 無効 |
| 上記へ4GB Patchを適用 | 6,029,312 | `B248271BF8EAC8C9B283B8C95E3ADD672B713BF529B05F1780E58268493B9D06` | 有効 |

日本語版オリジナル`nfsMW.exe`や未知の`speed.exe`には導入しません。

## 前提

- 正規の日本語版クリーンインストール
- NFSPatcherでMain PatchとNo-CD Patchを適用した上表の`speed.exe`
- Ultimate ASI Loader（`dinput8.dll`）
- ThirteenAG NFSMostWanted Widescreen Fix

これらの第三者ファイルは本リポジトリとReleaseには含まれません。

## NFSPatcher適用時の重要事項

NFSPatcherを当てる場合は「nfsMW.exe」を「speed.exe」に改名してから適用してください。

1. 日本語版をクリーンインストールします。
2. 元のゲームフォルダーをバックアップします。
3. 上記のとおり`nfsMW.exe`を`speed.exe`へ改名します。
4. NFSPatcherでMain Patchを適用し、続けてNo-CD Patchを適用します。
5. 必要な場合だけ4GB Patchを適用します。
6. 上表のサイズとSHA-256のどちらかに一致することを確認します。

本MODはNFSPatcher、No-CDパッチ、4GB Patch、ゲーム実行ファイルを配布しません。

## インストール

1. ゲームを終了します。
2. GitHub ReleaseのZIPを展開します。
3. `Install.cmd`を右クリックし、「管理者として実行」を選びます。
4. 既定のインストール先でなければ、コマンドプロンプトからゲームフォルダーを指定します。

```bat
Install.cmd "D:\Games\Need for Speed Most Wanted"
```

インストーラーは次を実施します。

- 対象`speed.exe`の完全SHA-256検証
- 日本語版リソース40件のサイズ・SHA-256検証
- `version.dll`、`scripts\NFSMWJapaneseBridge.asi`、設定INIだけを配置
- WideFixの`ImproveGamepadSupport`を日本語UI互換のため`0`へ変更
- 上書き対象と元のWideFix INIを`_NFSMWJapaneseBridge_Backup`へ退避

レジストリとセーブデータは変更しません。不一致がある場合はfail-closedで停止します。

## アンインストール

ゲームを終了し、`Uninstall.cmd`を管理者として実行します。インストール時の状態記録と
SHA-256を検査し、本MODが配置したファイルだけを削除してバックアップを復元します。

## HD Font / HD Content Support

日本語フォントは欧米版とグリフ構成が異なります。旧`NFSMWHDFontSupport.asi`は使用せず、
`NFSMWHDContentSupport.asi`を使用する場合は`scripts\NFSMWHDContentSupport.ini`を
次のように設定してください。

```ini
[GENERAL]
HDFontSupport = 0
HDCursorSupport = 1
HDFMVSupport = 1
```

設定例は`compatibility/NFSMWHDContentSupport.ini`にあります。

## 検証済み範囲

2026-09-15に日本語版クリーンインストールからNFSPatcherのMain Patch／No-CD Patchを
適用して生成したnoLAA版とLAA版の両方を隔離環境で確認しました。

- Release/Win32のソースビルド成功
- ネイティブ単体テスト14件PASS
- 両EXEで起動12秒後も応答あり
- 日本語テキスト、LanguageTextures、フロントエンド、INGAMECのファイルオープン成功
- PSA／Attractムービーの日本語NTSC版へのリダイレクト成功
- 両EXEで静的フック面37 guards＋5 vtables PASS

画面を操作してのフリーローム、日本語警察無線の聴取、通常終了はこのReleaseの
最終自動試験には含めていません。

## ソースからビルド

Visual StudioのC++ x86ツールチェーンを導入し、PowerShellで実行します。

```powershell
.\tools\Build-PublicRelease.ps1
```

詳細は[BUILDING.md](BUILDING.md)を参照してください。ビルドスクリプトは単体テストを
実行し、ゲーム資産や第三者MODがZIPへ混入していないことも検証します。

## ライセンスと権利

本プロジェクトの独自コードと文書は[MIT License](LICENSE)で公開します。
MinHookには同梱のBSD 2-Clause Licenseが適用されます。MIT Licenseはゲーム資産、
NFSPatcher、ASI Loader、Widescreen Fix、その他の第三者MODに関する権利を付与しません。
詳細は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。

Original project code and documentation are licensed under the [MIT License](LICENSE).
MinHook remains under its bundled BSD 2-Clause License. The MIT License grants no
rights to game assets, NFSPatcher, an ASI loader, Widescreen Fix, or other third-party mods.
