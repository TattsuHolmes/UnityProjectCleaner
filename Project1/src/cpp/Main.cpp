#include "DxLib.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>

// ゲームシステム環境
#include "Main.h"

namespace fs = std::filesystem;

// =====================================================
//  画面の状態（ステート）を表す列挙型
// =====================================================
enum class AppState 
{
    INPUT,      // 1. ドラッグ＆ドロップ待ち画面
    SELECT,     // 2. リスト表示・チェック画面
    CONFIRM,    // 3. 最終確認ダイアログ画面
    DELETING,   // 4. 削除実行中画面
    DONE        // 5. 完了画面
};

// =====================================================
//  構造体とグローバル変数
// =====================================================
struct DeleteItem 
{
    std::string name;
    std::string path;
    bool isDelete = false;
};

std::vector<DeleteItem> deleteList;

int Window_H = 600;
int Window_W = 800;
char droppedPath[1024] = "";
int Font_UI = -1;

int oldMouseState = 0;
AppState currentState = AppState::INPUT; // 最初の画面はINPUT

std::vector<std::string> targetPaths; // 削除する全ファイルのフルパスを入れるリスト
int totalDeleteCount = 0; // 消すファイルの総数
int deletedCount = 0;     // 消し終わった数
int deleteStartTime = 0;  // 削除を開始した時間（ミリ秒）

// =====================================================
//  フォルダ解析処理
// =====================================================
void ScanUnityProject(const std::string& projectPath) 
{
    deleteList.clear();

    try {
            for (const auto& entry : fs::directory_iterator(projectPath)) 
        {
            std::string name = entry.path().filename().string();

            DeleteItem item;
            item.name = name;
            item.path = entry.path().string();

            if (name == "Library" || name == "Temp" || name == "Logs" || name == "obj" || name == ".vs") {
                item.isDelete = true;
            }
            else {
                item.isDelete = false;
            }

            deleteList.push_back(item);
        }
    }
    catch (...) 
    {

    }
}
// =====================================================
//  削除準備処理（キューの作成）
// =====================================================
void PrepareDelete() 
{
    targetPaths.clear();

    // 1. チェックが入っているフォルダの中身を全てリストアップする
    for (const auto& item : deleteList) 
    {
        if (item.isDelete) 
        {
            if (fs::is_directory(item.path)) 
            {
                std::error_code ec;
                // recursive_directory_iterator でフォルダの奥深くまで全検索
                for (const auto& entry : fs::recursive_directory_iterator(item.path, fs::directory_options::skip_permission_denied, ec)) 
                {
                    targetPaths.push_back(entry.path().string());
                }
            }
            targetPaths.push_back(item.path); // 親フォルダ自身も追加
        }
    }

    // 2. パスが「長い順」に並び替える（※超重要テクニック）
    // 深い階層のファイルから先に消さないと、親フォルダを消そうとした時にエラーになるため
    std::sort(targetPaths.begin(), targetPaths.end(), [](const std::string& a, const std::string& b) 
        {
        return a.length() > b.length();
        });

    // 3. 進捗用の変数をリセット
    totalDeleteCount = targetPaths.size();
    deletedCount = 0;
    deleteStartTime = GetNowCount(); // DXライブラリの機能で現在時刻（ミリ秒）を取得
}

// =====================================================
//  アプリのリセット処理
// =====================================================
void ResetApp() 
{
    deleteList.clear();
    targetPaths.clear();
    droppedPath[0] = '\0';
    totalDeleteCount = 0;
    deletedCount = 0;
    currentState = AppState::INPUT; // 最初の待機画面に戻す
}


// =====================================================
//  メインループ
// =====================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    ChangeWindowMode(TRUE);
    SetGraphMode(Window_W, Window_H, 32);

    SetWindowText("Unity Project Cleaner");
    SetDragFileValidFlag(TRUE);
    SetAlwaysRunFlag(TRUE);

    SetOutApplicationLogValidFlag(FALSE);

    if (DxLib_Init() == -1) { return -1; }
    SetDrawScreen(DX_SCREEN_BACK);

    MainInit();

    while (ProcessMessage() == 0 && CheckHitKey(KEY_INPUT_ESCAPE) == 0)
    {
        ClearDrawScreen();
        MainUpdate();
        MainDraw();
        ScreenFlip();
    }

    MainExit();
    DxLib_End();
    return 0;
}

// =====================================================
//  初期化
// =====================================================
void MainInit()
{
    Font_UI = CreateFontToHandle("メイリオ", 24, 2, DX_FONTTYPE_ANTIALIASING);
}

// =====================================================
//  更新処理（画面ごとに処理を分ける）
// =====================================================
void MainUpdate()
{
    int currentMouseState = GetMouseInput();
    bool isMouseClicked = ((currentMouseState & MOUSE_INPUT_LEFT) != 0 && (oldMouseState & MOUSE_INPUT_LEFT) == 0);

    switch (currentState) {
    case AppState::INPUT:
        if (GetDragFileNum() > 0) {
            GetDragFilePath(droppedPath);
            DragFileInfoClear();
            ScanUnityProject(droppedPath);
            currentState = AppState::SELECT;
        }
        break;

    case AppState::SELECT:
        if (isMouseClicked) {
            int mouseX, mouseY;
            GetMousePoint(&mouseX, &mouseY);

            // リストのON/OFF判定
            int startY = 130;
            for (size_t i = 0; i < deleteList.size(); ++i) {
                int itemY = startY + (i * 40);
                if (mouseX >= 20 && mouseX <= 400 && mouseY >= itemY && mouseY <= itemY + 30) {
                    deleteList[i].isDelete = !deleteList[i].isDelete;
                }
            }

            // ★修正：実行ボタンの当たり判定（DrawBoxの座標に合わせて上にずらしました）
            if (mouseX >= 600 && mouseX <= 750 && mouseY >= 480 && mouseY <= 530) {
                currentState = AppState::CONFIRM;
            }
        }
        break;

    case AppState::CONFIRM:
        if (isMouseClicked) {
            int mouseX, mouseY;
            GetMousePoint(&mouseX, &mouseY);

            // ★修正：「はい」ボタンの当たり判定
            if (mouseX >= 300 && mouseX <= 400 && mouseY >= 300 && mouseY <= 350) {
                PrepareDelete();
                currentState = AppState::DELETING;
            }
            // ★修正：「いいえ」ボタンの当たり判定
            else if (mouseX >= 450 && mouseX <= 550 && mouseY >= 300 && mouseY <= 350) {
                currentState = AppState::SELECT;
            }
        }
        break;

    case AppState::DELETING:
        for (int i = 0; i < 50; ++i) {
            if (deletedCount < totalDeleteCount) {
                std::error_code ec;
                fs::remove(targetPaths[deletedCount], ec);

                // =====================================================
                // 【第4章】エラーログ出力
                // =====================================================
                // ec（エラーコード）に何らかの値が入っていたら削除失敗
                if (ec) {
                    // std::ios::app は「追記モード（消さずにどんどん下に足していく）」
                    std::ofstream ofs("ErrorLog.txt", std::ios::app);
                    ofs << "削除失敗: " << targetPaths[deletedCount] << " (理由: " << ec.message() << ")\n";
                }

                deletedCount++;
            }
            else {
                currentState = AppState::DONE;
                break;
            }
        }
        break;

    case AppState::DONE:
        // =====================================================
        // 【第4章】リセット処理
        // =====================================================
        // Rキーが押されたら、変数を空にして最初の画面に戻る
        if (CheckHitKey(KEY_INPUT_R) == 1) {
            ResetApp();
        }
        break;
    }

    oldMouseState = currentMouseState;
}
// =====================================================
//  描画処理（画面ごとに処理を分ける）
// =====================================================
void MainDraw()
{
    // ① 画面ごとのメインUI描画
    switch (currentState) {
    case AppState::INPUT:
        DrawStringToHandle(20, 20, "ここにUnityのプロジェクトフォルダをドロップしてください", GetColor(200, 200, 200), Font_UI);
        break;

    case AppState::SELECT:
        DrawStringToHandle(20, 20, "読み込んだフォルダのパス:", GetColor(255, 255, 255), Font_UI);
        DrawStringToHandle(20, 50, droppedPath, GetColor(0, 255, 255), Font_UI);
        DrawStringToHandle(20, 90, "【削除候補リスト】クリックでON/OFF", GetColor(255, 255, 0), Font_UI);

        {
            int y = 130;
            for (size_t i = 0; i < deleteList.size(); ++i) {
                std::string checkStr = deleteList[i].isDelete ? "[ X ] " : "[   ] ";
                std::string dispStr = checkStr + deleteList[i].name;
                unsigned int color = deleteList[i].isDelete ? GetColor(255, 100, 100) : GetColor(255, 255, 255);
                DrawStringToHandle(20, y, dispStr.c_str(), color, Font_UI);
                y += 40;
            }
        }

        // 実行ボタンの描画（少し上にずらしました）
        DrawBox(600, 480, 750, 530, GetColor(255, 0, 0), TRUE);
        DrawStringToHandle(610, 490, "削除開始", GetColor(255, 255, 255), Font_UI);
        break;

    case AppState::CONFIRM:
        DrawStringToHandle(200, 200, "本当に削除しますか？(復元できません)", GetColor(255, 100, 100), Font_UI);
        DrawBox(300, 300, 400, 350, GetColor(255, 0, 0), TRUE);
        DrawStringToHandle(325, 310, "はい", GetColor(255, 255, 255), Font_UI);
        DrawBox(450, 300, 550, 350, GetColor(100, 100, 100), TRUE);
        DrawStringToHandle(465, 310, "いいえ", GetColor(255, 255, 255), Font_UI);
        break;

    case AppState::DELETING:
        DrawStringToHandle(20, 50, "不要なファイルを削除しています...", GetColor(255, 255, 255), Font_UI);
        break;

    case AppState::DONE: // 第4章の完了テキストも入れておきます
        DrawStringToHandle(20, 50, "★ 削除処理が完了しました ★", GetColor(0, 255, 0), Font_UI);

        {
            std::string resultText = "処理したファイル/フォルダ数: " + std::to_string(totalDeleteCount) + " 個";
            DrawStringToHandle(20, 100, resultText.c_str(), GetColor(255, 255, 255), Font_UI);
        }

        DrawStringToHandle(20, 200, "【 R 】キーを押して別のプロジェクトを掃除する", GetColor(255, 255, 0), Font_UI);
        DrawStringToHandle(20, 250, "【 ESC 】キーを押してツールを終了する", GetColor(255, 255, 0), Font_UI);
        break;
    }

    // =====================================================
    // ② 常時表示のステータスバー（ゲージ）
    // switch文の外に書くことで、常に画面の最下部に表示されます
    // =====================================================
    int barX1 = 20, barY1 = 560; // 左上座標
    int barX2 = 780, barY2 = 580; // 右下座標

    // 常時表示のグレー背景
    DrawBox(barX1, barY1, barX2, barY2, GetColor(80, 80, 80), TRUE);

    // 削除中・完了時のゲージ進行とテキスト表示
    if (currentState == AppState::DELETING || currentState == AppState::DONE) {
        float progress = 0.0f;
        if (totalDeleteCount > 0) {
            progress = (float)deletedCount / totalDeleteCount;
        }
        if (currentState == AppState::DONE) progress = 1.0f; // 完了時はMAXにする

        // 緑の進行ゲージを描画
        DrawBox(barX1, barY1, barX1 + (int)((barX2 - barX1) * progress), barY2, GetColor(0, 255, 0), TRUE);

        // 進捗テキストをゲージのすぐ上に描画
        std::string progText = "進捗: " + std::to_string(deletedCount) + " / " + std::to_string(totalDeleteCount);
        DrawStringToHandle(20, 520, progText.c_str(), GetColor(255, 255, 255), Font_UI);

        // 削除中のみ残り時間を計算して右端に描画
        if (currentState == AppState::DELETING && deletedCount > 0 && progress < 1.0f) {
            int elapsedTime = GetNowCount() - deleteStartTime;
            float timePerFile = (float)elapsedTime / deletedCount;
            int remainingSec = (int)(timePerFile * (totalDeleteCount - deletedCount)) / 1000;

            std::string timeText = "残り時間: 約 " + std::to_string(remainingSec) + " 秒";
            DrawStringToHandle(550, 520, timeText.c_str(), GetColor(0, 255, 255), Font_UI);
        }
    }
}

// =====================================================
//  終了
// =====================================================
void MainExit()
{
}