#ifndef __H_POCKETCT__
#define __H_POCKETCT__

#include <stdint.h>

#define VERSION "0.1.2 (2026/05/23)"

// デフォルトハイスコア
#define DEFAULT_HI_SCORE (76500)

// 画面・マップ定数
#define PHYS_W (1440)
#define PHYS_H (1024)
#define DISP_W (1024)
#define DISP_H (1024)

typedef struct {
  // --- 1. 物理演算用（固定小数点） ---
  int32_t x;            // 物理世界のX座標 (0 ~ 1439 の16倍精度)
  int32_t y;            // 物理世界のY座標 (0 ~ 1023 の16倍精度)
  int16_t speed;        // 現在の速度 (256倍精度)

  int16_t angle;        // 車の向き（0 ~ 31 の16倍精度）
  int16_t move_angle;   // 実際に進む向き（0 ~ 31 の16倍精度）
  
  int32_t current_turn; // 現在のステアリング回転力
  int16_t is_spinning;  // スピン状態フラグ

  // --- 2. 画面制御用（等倍ドット座標） ---
  int16_t cam_x;        // 表示画面の中央に位置する物理世界のX座標 (0 ~ 1439)
  int16_t cam_y;        // 表示画面の中央に位置する物理世界のY座標 (0 ~ 1023)
  int16_t sp_x;         // 車体左上隅の表示画面上のX座標(0 ~ 255) *スプライト画面のオフセット考慮なし
  int16_t sp_y;         // 車体左上隅の


  // --- 1. 物理演算用（16倍固定小数点） ---
    int32_t x;          // 世界のX座標（0 〜 1440<<4）
    int32_t y;          // 世界のY座標（0 〜 1024<<4）
    int16_t speed;      // 現在の速度（0 〜 48）
    int16_t angle;      // 描画角度（0 〜 31）
    int16_t sub_angle;  // 内部高精度角度（0 〜 511）
    int16_t move_angle; // 実際に進む方向。最初は angle と同じ。
    int32_t current_turn; // 現在のステアリング回転力
    int16_t is_spinning;  // スピン状態フラグ

    // --- 2. 画面制御用（等倍ドット座標） ---
    int16_t cam_x;      // 世界の中でのカメラのX座標（128 〜 1312）
    int16_t cam_y;      // 世界の中でのカメラのY座標（128 〜 896）
    int16_t sp_x;       // 画面上（256x256）のスプライトX表示位置
    int16_t sp_y;       // 画面上（256x256）のスプライトY表示位置
} PLAYER_CAMERA;

#endif