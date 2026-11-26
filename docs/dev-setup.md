# 開發環境設定（Windows）

**適用範圍**：C 線（`src/`、`sim/`、`tests/`）的編譯與測試。
Python 線（`pyctrl/`、`tools/`）用 Miniforge 的 `lerobot` 環境，建置過程見 `lab-log/week06.md` 第二、四節。

**用途**：重灌或換電腦時照著做一遍。每一步都寫了理由，因為這裡的每個「不要」都是實際踩過的坑（11/04）。

---

## 1. 安裝 MSYS2 與編譯器

```powershell
winget install MSYS2.MSYS2        # 或至 msys2.org 下載，安裝路徑用預設的 C:\msys64
```

從開始選單開 **MSYS2 UCRT64**（不是 MSYS，也不是 MINGW64），然後：

```bash
pacman -Syu                       # 若要求關閉視窗，關掉後重開 UCRT64 再跑一次
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
```

**為什麼是 MSYS2 而不是 WSL**（C-12）：MSYS2 編出 Windows 原生執行檔，下學期的 `hal_win32.c` 可以在同一個環境裡直接呼叫 Win32 序列埠 API。WSL 要碰 USB 序列埠還得多一層 `usbipd`。

**為什麼不是只在 PowerShell 裝一個 make**：Makefile 用到 `mkdir -p`、`for` 迴圈、`grep`，需要 bash。

**路徑寫法**：`C:\GithubRepo` 在 UCRT64 裡是 `/c/GithubRepo`。

## 2. 讓 UCRT64 用 Windows 上原本的 git

**不要在 MSYS2 裡另外裝 git**（`pacman -S git`）。MSYS2 的 git 與 Git for Windows 的換行預設不同，兩個 git 同時操作同一個 repo，會出現整檔都被改過的假 diff。

先在 PowerShell 查 git 的位置：

```powershell
where.exe git                     # 通常是 C:\Program Files\Git\cmd\git.exe
```

再在 UCRT64 把**那個 `cmd` 資料夾**加進 PATH（只做一次）：

```bash
echo 'export PATH="$PATH:/c/Program Files/Git/cmd"' >> ~/.bashrc
```

- 只加 `cmd`，**不要加 `bin` 或 `usr\bin`**。`cmd` 裡只有 git 本身；另外兩個會帶進一整套 bash 工具，與 MSYS2 的同名程式互相干擾
- 用單引號：`$PATH` 要原封不動寫進檔案，每次開終端機才展開
- 用 `>>`（附加），不是 `>`（會覆蓋整個 `.bashrc`）

## 3. 讓 git 認得你是誰

**症狀**：在 UCRT64 裡 commit，出現 `Author identity unknown` / `unable to auto-detect email address`。PowerShell 裡卻正常。

**原因**：MSYS2 有自己的家目錄（`C:\msys64\home\<使用者>`）。在 UCRT64 裡跑 git，它去那裡找 `~/.gitconfig`，而你的名字與 email 存在 Windows 使用者資料夾的那份。

**解法**：讓 UCRT64 的 git 直接讀 Windows 那份設定（只做一次）：

```bash
echo 'export GIT_CONFIG_GLOBAL="$(cygpath -u "$USERPROFILE")/.gitconfig"' >> ~/.bashrc
```

**不要照錯誤訊息的建議在 UCRT64 裡跑 `git config --global user.name ...`**。那會在 MSYS2 家目錄另建第二份設定，之後兩邊各改各的，不知道哪份才算數。

## 4. 驗證

```bash
source ~/.bashrc
tail -2 ~/.bashrc                 # 應看到上面加的兩行，各一次
git --version
git config user.name              # 應印出你的名字；空白代表第 3 步沒生效
gcc --version
make --version
cd /c/GithubRepo/sts3215-driver
make test                         # 應看到 == all tests passed
make test                         # 第二次不應再出現 gcc 那行（Makefile 已處理 .exe 副檔名）
make layers
```

## 5. 已知限制

| 項目 | 狀況 | 怎麼辦 |
|---|---|---|
| `make SAN=1`（ASan + UBSan） | UCRT64 的 gcc 沒有 `libasan` / `libubsan`，連結失敗 | 在 Linux 上補跑（W9 的 review 就是這樣做的） |
| gcc 與 clang 的 `-Wconversion` | 範圍不同，`uint16_t v = lo \| (hi << 8);` clang 報錯、gcc 不報（F-21） | 偶爾用 clang 編一次：`pacman -S mingw-w64-ucrt-x86_64-clang` 後 `make CC=clang test`（此套件未在本機裝過） |
| Python | 不在 UCRT64 的 PATH 上 | 用 Miniforge Prompt（`conda activate lerobot`） |
| `LF will be replaced by CRLF` 警告 | 換行轉換的提示 | 不影響內容。repo 以 `.gitattributes` 規定 `eol=lf` |

## 6. 不要做的事（總整理）

- 不要在 MSYS2 裡裝 git
- 不要把 `C:\msys64\usr\bin` 或 Git 的 `bin` / `usr\bin` 加進 PATH（Windows 或 MSYS2 皆然）
- 不要在 UCRT64 裡跑 `git config --global`
- `.bashrc` 那兩行各只加一次
