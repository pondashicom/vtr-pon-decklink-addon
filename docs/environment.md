# 対応環境 — 0.3.0

利用環境はWindows 11 x64とVTR-PON 3.0.0-beta.1。Desktop Videoの公式Windows x64版を別途導入し、要求された再起動を済ませます。API 16.0以上を要求し、使用版はDesktop Video 16.4.0.0です。機器・チャンネル・端子・入出力プロファイルはDesktop Video Setupで確認します。

生成にはDeckLink SDK 16.0、MSVC 2022 x64、Windows SDK/MIDL、CMake 3.25以上、Ninjaを使用します。SDKは開発者が外部に用意し、リポジトリには入れません。[生成手順](../BUILDING.md)・[利用条件](../LICENSE/DISTRIBUTION.md)を参照してください。

SDKによる生成・ABI・模擬入出力と、Desktop Videoが使用可能である状態・機器未接続での検出を確認しています。実機・実信号・物理音声・同期・長時間運用は未確認です。機器の種類、映像形式、端子、全二重動作、同期再生は機器とプロファイルの能力に従います。[実機確認項目](hardware-checklist.md)は利用環境に合わせて確認するための一覧であり、すべて合格した記録ではありません。
