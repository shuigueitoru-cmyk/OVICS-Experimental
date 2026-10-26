# 0.2.2 Experimental — Horizon Native Integration

- OVICS0.2.1を別版として保持し、Horizon0.0.1のHTML/レイアウトコアをnativeリンク。
- Horizonを4番目のGUIアプリ/メニュー/アイコン/taskbarへ追加。
- URL入力、HTTP実取得、実RAM FS、Home、スクロール、リンク情報、移動/閉じる/再開。
- 既存HTTPに本文/状態/エラーを直接返すnative API。コンソール文字列の解析はしません。
- ネットワーク待機に協調UIサービスと中止を追加し、CUIキーボード処理を再利用可能な関数へ分離。
- PC側HTTPS中継を追加。実Wikipediaの概要と出典をOVICSのHTTP経路へ渡し、直接TLSと区別。
- 8MiB用native制限、コア/不正入力/サニタイザー、実PS2/PCAP/VRAM、旧機能回帰を検証。
