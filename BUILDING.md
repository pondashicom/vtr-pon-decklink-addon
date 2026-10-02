# 0.3.0の生成

Windows 11 x64、MSVC 2022 x64開発者環境、Windows SDK、CMake 3.25以上、Ninja、Python 3を用意してください。外部SDKは DeckLink SDK 16.0。SDKをこのリポジトリへコピーしません。ATEM・DeckLinkではWindows SDKのMIDLも使用します。

`v0.3.0` を取得し、変更のないソースから生成します。VST SDKは公式 `v3.8.1_build_84` と、その `base`・`cmake`・`pluginterfaces`・`public.sdk` サブモジュールを使用します。VSTGUI・SDKの例は生成しません。

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DVTRPON_DECKLINK_ENABLE_SDK=ON -DBUILD_TESTING=OFF `
  "-DVTRPON_DECKLINK_SDK_DIR=C:/SDK/DeckLink"
cmake --build build-release --target vtr-pon-decklink-addon
```

生成先は `build-release/decklink-addons/` です。本体からは独立して生成します。公開パッケージは[パッケージ生成手順](scripts/README.md)を使います。公開版は0.3.0、IDは `vtr-pon-decklink-addon`、契約は `decklink-media-abi-v1` です。ビルドディレクトリ全体を梱包せず、必要なRelease依存と完成文書だけを収集します。

公開後に同じID・版の内容を置き換えません。修正は新しい版のソース・タグ・パッケージとして提供します。生成物とソースの対応はパッケージ内の `licenses/RELEASE-SOURCE.md` とRelease本文へ記録します。
