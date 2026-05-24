#ifndef __H_POCKETCT__
#define __H_POCKETCT__

#include <stdint.h>

#define VERSION "0.1.4 (2026/05/24)"

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

typedef struct {

  // --- 1. 物理演算用 ---
  int32_t x;            // 物理世界のX座標 (0 ~ 1439 の16倍精度固定小数点)
  int32_t y;            // 物理世界のY座標 (0 ~ 1023 の16倍精度固定小数点)
  int16_t speed;        // 現在の速度 (0 ~ 24 の256倍精度固定小数点)

  int16_t angle;        // 車の向き（0 ~ 31 の256倍精度固定小数点）
  int16_t move_angle;   // 実際に進む向き（0 ~ 31 の256倍精度固定小数点）
  
  int32_t current_turn; // 現在のステアリング回転力
  int16_t is_spinning;  // スピン状態フラグ

  // --- 2. 画面制御用 ---
  int16_t cam_x;        // 表示画面の中央に位置する物理世界のX座標 (181 ~ 1259)
  int16_t cam_y;        // 表示画面の中央に位置する物理世界のY座標 (128 ~ 896)
  int16_t sp_x;         // 車体中心の表示画面上のX座標(0 ~ 255) *スプライト画面のオフセット(16)考慮なし
  int16_t sp_y;         // 車体中心の表示画面上のY座標(0 ~ 255) *スプライト画面のオフセット(16)考慮なし

} PLAYER_CAMERA;

#endif