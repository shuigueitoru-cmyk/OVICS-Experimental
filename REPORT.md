# OVICS 0.2.2 Experimental — Horizon Native Integration

Horizon0.0.1のHTML/レイアウトコアをOVICS0.2.1から派生した新しいカーネルへ組み込みました。
**実Wikipediaの英語記事Operating systemの概要を、PC側HTTPS中継→OVICS自身のHTTP/TCP→GUIで表示しました。**
これはOVICS自身のHTTPS/TLSではなく、記事全文やWikipediaサイト全体の再現でもありません。

## 実装・構成

- Horizonを4番目のGUIアプリ、メニュー、ダブルクリックアイコン、taskbarへ追加。
  4アプリ同時の場合はtaskbarボタンをコンパクトにし、入力の貫通を防止。
- 添付コアのhtml.c/layout.cはバイト同一。上限を設定できるヘッダーguardとnative profileを追加。
  入力8192byte、arena16KiB、ノード512、深さ24、リンク32、72列×256行。
  document33424bytes＋cells147456bytes、合計約177KiBを固定BSSへ置き、kernel stackへ置きません。
- 既存WMのclient描画へHzPaintを接続。1回だけVRAMへ重ね書きせず、ドラッグ/重なり/再描画で維持。
  ASCII以外は?として描画。heading/link/選択色、折り返し、Up/Down、リンク先表示。
- home:と実RAM FS ram:filename、URL欄247文字、Go/Enter、Ctrl+U、Homeを追加。
- HTTPは共有処理から本文/長さ/状態/エラーを直接返すnative API。CUI表示を解析しません。
  元のCUI HTTP出力/制限は保持。host frontend/libcurl/Linuxカーネルはリンクしません。
- GUI取得の待機中は通常コンテキストでキー/マウス/WMを協調処理。再帰的HTTPは開始しません。
  Esc/Xで中止し、owned TCPと出力sink/serviceを解放。IRQ内に重いGUI/HTTP処理を追加しません。
- PC中継はPython標準HTTPS検証、固定en.wikipedia.org上流、127.0.0.1:8088のみ待受け。
  Wikipediaの実JSON概要を出典・原文リンク付きHTMLへ変換。失敗は502と明示し、成功を装いません。

起動手順はREADME.md、再ビルド/検査はBUILD.md、制限はLIMITATIONS.md。
旧OVICS0.2.1と添付Horizonのバックアップは変更していません。

## Wikipedia実通信と画面

8/32/128/512MiB各構成で、実PS/2のアイコン/メニュー、Wikiボタン、URL欄、スクロール、
ドラッグ、終了/再開を操作。各回、PCが実WikipediaへHTTPS接続し、固定のテスト文を代入せず実概要を取得しました。
source title/extract/URL/hashはresults/wikipedia-fetches.json、各horizon-*/upstream.jsonに保存。

OVICSのhttp_bodyはPCが生成した出典付きHTMLと完全一致し、実extractの文章は表示中のセル列にも存在。
各native PCAPを独立再構成し、GET /wiki/Operating_system、Host、200、Content-Length、
IPv4/TCPチェックサム、シーケンス、双方のFIN/ACK、本文との一致を確認しました。
このPCAPは**OVICSとPC間のHTTP**の証拠で、ゲストTLSの証拠ではありません。

各RAM6場面、計24画面で、全画面スクリーンショットと物理VRAMを照合。
本文576×240領域は読んだセル列/属性/BIOSフォントから独立に描き、カーソルを含めて画素照合。
画面chrome全域を独立したモデルで再描画したと誇張していません。
実RAM FS内容、HTTPS直接接続の安全な拒否、Home、scroll、drag、guard4KiB不変、PM不変も確認。
PNG/本文HTML/セル列/上流記録/PCAPをresults/horizon-*に保存。

PC中継の別テストは**LOCAL FIXTURE**と明示した2件と不正route4件で、実Wikipedia検証と区別。
任意URL/ファイルを代理取得しないこと、Content-Length、HTML escapingを検査しました。

## 操作中のHTTPキャンセル

故意に応答を遅らせた実TCP/HTTP相手へ接続し、実PS/2で読み込み中にドラッグ、Escを注入。
CUI復帰、TCP_CLOSED、sink/serviceポインター解放、後続echo、PFなしを確認しました。

| RAM | EscからCUI復帰 | 結果 |
|---|---:|---|
| 8 MiB | 0.038s | PASS |
| 32 MiB | 0.037s | PASS |
| 128 MiB | 0.037s | PASS |
| 512 MiB | 0.037s | PASS |

4アプリ同時起動、4個のtaskbar、全アプリ切替、Terminal実echo、ブラウザへの誤入力防止、全窓の終了も全構成で合格。

## コア・メモリ・既存回帰

コアは元のhost上限とnative上限の両profileで各18グループ・決定的5000入力に合格。
同じnative profileでASan/UBSan検査も合格。LeakSanitizer成功は主張しません。
profile向けテストは固定160列/3277行の期待を構成値へ変更し、元の試験ソースも別保存。

既存GUIは単独QEMUで4RAM×28状態=112画面が独立期待画像/VRAM/画面と一致。
CUI/Explorer実RAM FS/Terminal実コマンド/WM/PS2/memtest/vmemtestを再検証。
旧WM68画面、API fixture28画面、マウス、基本描画、DNS/TCP/HTTP行列も記録を同梱。
既存障害36件（PF20＋gfx/cursor/WM/desktop各4）は専用ELFで実#PF/CR2/安全停止を確認。

| 回帰 | 最終結果 |
|---|---|
| verify_qemu_v010.py  | PASS |
| verify_network_v010.py none | PASS |
| verify_network_v010.py other | PASS |
| verify_ethernet_v010.py  | PASS |
| verify_error_v010.py  | PASS |
| verify_protocol_v010.py  | PASS |
| verify_udp_dns_wire_v010.py  | PASS |
| verify_udp_dns_environment_v010.py  | PASS |
| verify_udp_editor_v010.py  | PASS |
| verify_tcp_wire_v010.py  | PASS |
| verify_tcp_flags_v010.py  | PASS |
| verify_tcp_extra_v010.py  | PASS |
| verify_tcp_user_linux_v010.py  | PASS |
| verify_http_user_linux_v010.py  | PASS |
| verify_http_wire_linux_v010.py  | PASS |
| verify_http_edge_linux_v010.py  | PASS |
| verify_http_none_v010.py  | PASS |
| check_udp_dns_environment_pcap_v010.py  | PASS |
| check_tcp_pcap_v010.py  | PASS |
| check_http_pcap_v010.py  | PASS |

HTTP32ケースはPCAPから実要求/応答・チェックサム・Content-Length・終了を再照合して合格。
独立DNS→TCP→HTTPを全4RAMで各2件確認しています。

## 修正した問題と未確認の区別

- 最初のPC上流取得は既存HTTP idle3秒を超えました。GUI専用をidle20秒/全体30秒にし、CUIの3秒/15秒は保持。
- QMPでunderscoreをそのままqcodeにした試験失敗、invalid pageでも古いセルを描く期待画像の失敗、
  ビルド中に一時的に消えるELFを読み込んだ試験実行の失敗を修正。初回ログをattemptsに保存しました。
- バージョン文字列置換が試験のUDP転送先IPにも及んでいました。正しい10.0.2.15へ戻し、
  受信待受け確認後に注入して実通信/PCAPを再検証。再起動試験も固定0.7秒待ちから実起動/空RAM確認へ変更。
  パケットや保存内容の合格条件は緩めていません。
- 並行VM負荷では0.2.1由来のタイマー分解能/遅れの制限が操作へ影響。時間依存GUI試験は単独QEMUで再実行。
  新版で1ms境界や高負荷時実時間保証を新たに主張しません。0.2.1の精度12件を本版で再測定したとは扱いません。

## 成果物・制限・次期候補

NASM/GCC/GNU ldのクリーン再ビルドは、最終検証ELFとbyte-identical。
ELF SHA256: 5080d161eeb3ed5a0e02aba4c12f192d4264daa37302ea37f1cb63429a26f73a
旧ZIPの既知ハッシュ、旧フォルダー全マニフェストを最終確認し、backup-preservation.jsonを同梱。
Windows補助ランチャー自体、人間の手動操作、実機、GRUB/UEFIは未確認。
T100TA試験と外部公開は実施していません。

直接HTTPS/TLS、記事全文、CSS/画像/JS、リンク自動遷移、日本語グリフは未実装。
次期候補はリンク遷移/履歴、逐次文書レイアウト、非同期network API、日本語フォント。
詳細はLIMITATIONS.md。今回のWikipedia確認経路はPC補助を必要とする概要表示です。
