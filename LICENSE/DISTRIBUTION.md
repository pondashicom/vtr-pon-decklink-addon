# 配布条件・第三者通知 — 0.3.0

## 自作コード

Copyright (c) 2026 Tetsu Suzuki. 自作コードは[MIT License](MIT.txt)です。原文の著作権・許諾通知を保持してください。無保証・責任制限は原文によります。利用者の制作目的に非商用限定の条件を追加しません。

## インストール時の同意

パッケージのMIT原文を本体が同意対象として表示します。通知は別に閲覧できます。拒否・取り消しではそのパッケージを適用しません。更新版では文書と同意を改めて確認します。これによってMITやLGPLで認められた権利を減らすものではありません。

## 外部ソフト

[Blackmagic Design公式Desktop Video](https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software)のWindows x64版を、ドライバー・API Runtimeを含めて別途導入し、必要なWindows再起動を済ませてください。要求するAPIは16.0以上、使用版はDesktop Video 16.4.0.0です。Desktop Video Setupで機器・チャンネル・入出力プロファイル・端子を設定します。

ATEM Software、NDI Runtime／Tools、Desktop Video、利用者のVST3プラグインはこのパッケージに含まれません。外部ソフトを導入するときはその提供元の契約を確認・承諾してください。アドオンの同意操作は外部インストーラーの同意を代行しません。SDKは生成する開発者だけが別途取得して契約を確認します。

## 生成元と境界

使用するSDKは DeckLink SDK 16.0。配置は `decklink-addons/vtr-pon-decklink-addon.dll`、実行契約は `decklink-media-abi-v1`、Runtime Status v1です。DeckLinkの追加Output ABIはv1です。

SDK本体、生成ヘッダー、IDL、サンプル、テストDLL、デバッグ情報、ベンダーRuntime、他者のプラグインは配布しません。SDK由来の必要なコード・通知と、次項の同梱依存の条件を自作MITと区別します。

## DeckLink SDK 16.0

`Win/Include` のIDLから生成した機械実行コードを含みます。[Include通知原文](third-party/DeckLink-SDK-NOTICE.txt)の許諾・通知保持条項と機械実行コードの例外に従います。第三者として提供する対象にはこの通知の許諾条件を適用します。自作部分のMITとは適用範囲を分けます。

SDKの[EULA](https://downloads.blackmagicdesign.com/EULA/DeckLink/DeckLink-End-User-License-Agreement.pdf)第0.1項はInclude内のファイルについて第1・4.3・4.4・5・7・8項の適用を除外しています。この除外をSDK全体やDesktop Videoへ拡張しません。SDK取得は[公式開発者サイト](https://www.blackmagicdesign.com/desktopvideo_sdk)から行います。Desktop Videoの契約はその公式インストーラーで確認してください。DeckLink／Desktop VideoはBlackmagic Designの名称・商標です。
