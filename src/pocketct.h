#ifndef __H_POCKETCT__
#define __H_POCKETCT__

#include <stdint.h>

#define VERSION "0.3.0 (2026/05/29)"

// コースデータファイル名
#define COURSE_PHYS_DATA_FILE  "COURSE1.DAT"
#define COURSE_DISP_DATA_FILE  "COURSE1.GRP"

// デフォルトハイスコア
#define DEFAULT_HI_SCORE (76500)

// 画面・マップ定数
#define PHYS_W (1440)         // 物理演算用内部マップ(ピクセルアスペクト1:1)
#define PHYS_H (1024)

#define DISP_W (1024)         // 実グラフィック画面用ビットマップ(ピクセルアスペクト1.41:1)
#define DISP_H (1024)

// カメラ稼働範囲
#define CAM_X_MIN (181)       // 256/2 * 1.41
#define CAM_X_MAX (1259)      // PHYS_W - CAM_X_MIN
#define CAM_Y_MIN (128)       // 256/2
#define CAM_Y_MAX (896)       // PHYS_H - CAM_Y_MIN

// 最高速度
#define MAX_SPEED (92)

// 自車クラス
typedef struct {

  // 物理演算用
  int32_t x;                // 物理世界のX座標 (0 ~ 1439 の16倍精度固定小数点)
  int32_t y;                // 物理世界のY座標 (0 ~ 1023 の16倍精度固定小数点)
  int16_t speed;            // 現在の速度 (0 ~ 91 の256倍精度固定小数点)
  int16_t angle;            // 車の向き（0 ~ 31 の256倍精度固定小数点）
  int16_t move_angle;       // 実際に進む向き（0 ~ 31 の256倍精度固定小数点）
  int32_t current_turn;     // 現在のステアリング回転力

  // 画面制御用
  int16_t cam_x;            // 表示画面の中央に位置する物理世界のX座標 (181 ~ 1259)
  int16_t cam_y;            // 表示画面の中央に位置する物理世界のY座標 (128 ~ 896)
  int16_t sp_x;             // 車体中心の表示画面上のX座標(0 ~ 255) *スプライト画面のオフセット(16)考慮なし
  int16_t sp_y;             // 車体中心の表示画面上のY座標(0 ~ 255) *スプライト画面のオフセット(16)考慮なし

#ifdef __3D_VIEW__
  // 3D描画用パラメータ
  int16_t cam_height;       // カメラの高さ (H) ：固定値(例:32など)またはジャンプ等で変動
  int16_t focal_length;     // 焦点距離 (F) ：視野角の広さ。基本固定値(例:128など)
  int16_t horizon_y;        // 水平線の画面Y座標：基本固定値(例:96)。ここより下を3D描画する
#endif

  // ラップカウント用
  uint32_t score;           // 現在のトータルスコア
  int16_t lap_count;        // 現在の周回数（1からスタート）
  int16_t next_checkpoint;  // 次に通過すべきチェックポイント番号（0〜3）
  int16_t last_gate;        // 最後に通ったチェックポイント
  int16_t wrong_way;        // 今道を外れているか
  int16_t is_drifting;      // 今ドリフト中か
  int16_t is_goal;          // すでにゴールラインに到達しているか
  uint32_t lap_times[6];    // スタート・ラップ時の vsync カウンタの値
  uint32_t lap_scores[6];   // 各ラップごとのスコア [0]は常にゼロ

} PLAYER_CAR;

// VSYNCハンドライベント通信用クラス
typedef struct {

  // カウンタ
  uint32_t vsync_counter;               // VSYNCが何回走ったかを数える基本カウンタ
  uint32_t drift_points_counter;        // ドリフトポイント表示カウントダウン用
  uint32_t lap_mes_counter;             // ラップメッセージ表示カウントダウン用
  uint32_t wrong_way_counter;           // コース外れ警告表示カウントダウン用
  uint32_t goal_counter;                // ゴール表示カウントダウン用

  // イベント通知
  int16_t event_refresh_hi_score;       // ハイスコア更新依頼用
  int16_t event_refresh_score;          // スコア更新依頼用
  int16_t event_refresh_lap_count;      // ラップカウント表示依頼用
  int16_t event_refresh_lap_mes;        // ラップメッセージ表示依頼用(1-5でラップ数相当)
  int16_t event_refresh_drift_points;   // ドリフトポイント表示依頼用
  int16_t event_refresh_wrong_way;      // コース外れ警告表示依頼用
  int16_t event_refresh_goal;           // ゴール表示依頼用

  // バッファ
  uint8_t hi_score_mes[ 32 ];           // ハイスコア表示用
  uint8_t score_mes[ 32 ];              // スコア表示用
  uint8_t drift_points_mes[ 32 ];       // ドリフトポイント表示用
  uint8_t lap_count_mes[ 32 ];          // ラップカウント表示用

} VSYNC_EVENT;

#ifdef __3D_VIEW__
typedef struct {
    int32_t start_rel_x;
    int32_t start_rel_y;
    int32_t step_x;
    int32_t step_y;
} RASTER_LINE_DATA;
#endif

#endif