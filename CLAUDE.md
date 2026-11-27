# CLAUDE.md

Claude Code 在本 repo 工作時的操作設定。請用繁體中文與使用者溝通。

**程式慣例、硬性規範、HAL 決策、模擬器獨立性規則一律見 `docs/conventions.md`。**
專案現況與時程見 `docs/schedule.md`，分層架構見 `docs/architecture.md`。

## 操作規則

1. 一次只做一個步驟，完成後跑 `make test` 與 `make layers`，全部通過才算完成
2. **不要自行 commit 或 push**，也不要直接寫進使用者的工作目錄。
   產出打包成 zip，附上可直接貼的 commit 指令，由使用者自行放置與提交
3. 停下來時給一段簡短摘要：做了什麼、動到哪些檔案、哪些行為是在模擬器上自行決定
   而需要真機驗證的（列成「待真機驗證」）
4. 不要修改 `docs/schedule.md`、`docs/lab-log/`、`docs/protocol-notes.md`、
   `docs/LeRobot-notes.md`、`docs/architecture.md`——這些是使用者的紀錄。
   發現有誤時告知，不要自己改

## 要先問使用者的情況

純技術選擇（命名、內部資料結構、錯誤碼數值）自行決定，並在註解寫明理由。以下要先問：

- 影響隊友的事：上層 UDP 介面格式、傳輸方式
- 影響範圍或時程：要做的事超出目前步驟，或某步驟明顯做不完
- 協定手冊與 `docs/` 筆記互相矛盾，或手冊沒寫清楚、會影響驅動行為的地方
- 需要修改 `docs/conventions.md` 的硬性規範或 HAL 決策
