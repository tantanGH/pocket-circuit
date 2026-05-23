#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <x68k/dos.h>
#include <x68k/iocs.h>

#include <ajoy.h>

#include "pocketct.h"
#include "sincos.h"
#include "pattern.h"
#include "gpalette.h"
#include "keyboard.h"
#include "crtc.h"

// 自機カメラ
static PLAYER_CAMERA car = { (PHYS_W / 2) << 4, (PHYS_H / 2) << 4, 0, 0, 0 };

// 8x8 normal font data
static struct iocs_fntbuf font_data_8x8[ 256 ];

// 8x8 bold font data
static struct iocs_fntbuf font_data_8x8_bold[ 256 ];

// クイック乱数用シード
static uint32_t quickrand_seed = 2463534242;

// 高速乱数インライン関数
static inline uint32_t quickrand(void) {
    quickrand_seed ^= (quickrand_seed << 13);
    quickrand_seed ^= (quickrand_seed >> 17);
    quickrand_seed ^= (quickrand_seed << 5);
    return quickrand_seed;
}

//
//  タイマーA強制リセット 
//  これをしておかないと、ハードリセット後にVSYNC割り込みがすぐに開始しない
//
static void reset_timer_a() {
  _iocs_b_bpoke((uint8_t*)0xe88019,0x00);  // stop Timer-A  
  _iocs_b_bpoke((uint8_t*)0xe8801f,0x01);  // set Timer-A counter        
  _iocs_b_bpoke((uint8_t*)0xe88019,0x18);  // restart Timer-A    
}

//
//  VSYNC割り込みハンドラ
//
static void __attribute__((interrupt)) refresh_screen() {

  // 自車の表示(スプライト0-4)
int16_t sp_base_x = car.sp_x - 16;
  int16_t sp_base_y = car.sp_y - 16;
  uint16_t pattern_base = 0x100 + car.angle * 4;

  // 自車の表示 (スプライト0-4) ★car.sp_x/y をベースに田の字配置
  SP_SCRL[  0 ] = sp_base_x;
  SP_SCRL[  1 ] = sp_base_y;
  SP_SCRL[  2 ] = pattern_base + 0;
  SP_SCRL[  3 ] = 3;

  SP_SCRL[  4 ] = sp_base_x + 16;
  SP_SCRL[  5 ] = sp_base_y;
  SP_SCRL[  6 ] = pattern_base + 1;
  SP_SCRL[  7 ] = 3;

  SP_SCRL[  8 ] = sp_base_x;
  SP_SCRL[  9 ] = sp_base_y + 16;
  SP_SCRL[ 10 ] = pattern_base + 2;
  SP_SCRL[ 11 ] = 3;

  SP_SCRL[ 12 ] = sp_base_x + 16;
  SP_SCRL[ 13 ] = sp_base_y + 16;
  SP_SCRL[ 14 ] = pattern_base + 3;
  SP_SCRL[ 15 ] = 3;

  // 背景グラフィック画面のスクロール ★car.cam_x/y を使う
  int16_t gvram_x = ((int32_t)car.cam_x * 1024) / 1440;

  GR0_SCRL[0] = gvram_x & 1023;
  GR0_SCRL[1] = car.cam_y & 1023;
}

//
//  8x8 フォントデータ初期化 (スーパーバイザモードになっていること)
//
static void init_font_8x8() {

  for (int16_t i = 0; i < 256; i++) {

    // 8x8 regular font
    font_data_8x8[i].xl = 8;
    font_data_8x8[i].yl = 8;
    memcpy(font_data_8x8[i].buffer, FONT_ADDR_8x8 + FONT_BYTES_8x8 * i, FONT_BYTES_8x8);

    // 8x8 bold font
    font_data_8x8_bold[i].xl = 8;
    font_data_8x8_bold[i].yl = 8;
    memcpy(font_data_8x8_bold[i].buffer, FONT_ADDR_8x8 + FONT_BYTES_8x8 * i, FONT_BYTES_8x8);
    for (int16_t j = 0; j < FONT_BYTES_8x8; j++) {
      font_data_8x8_bold[i].buffer[j] |= ( font_data_8x8_bold[i].buffer[j] >> 1 ) & 0xff;
    }

  }
  
}

//
//  put text in 8x8 font
//
static void put_text_8x8(uint16_t x, uint16_t y, uint16_t color, uint16_t bold, const uint8_t* text) {

  int16_t len = strlen(text);

  for (int16_t i = 0; i < len; i++) {
    struct iocs_fntbuf* font_data = (bold == FONT_BOLD) ? 
                                    &(font_data_8x8_bold[text[i]]) : 
                                    &(font_data_8x8[text[i]]);
    if (color & 0x01) {
      _iocs_tcolor(1);
      _iocs_textput(x + i * 8, y, font_data);
    }
    if (color & 0x02) {
      _iocs_tcolor(2);
      _iocs_textput(x + i * 8, y, font_data);    
    }
  }

}

//
//  PCG / スプライトの初期化 (スーパーバイザモードになっていること)
//
static void init_sp_pcg() {

  // VBLANK待ち
  WAIT_VBLANK;

  // SP:ON TX:ON GR0:ON
  *VDC_R2 |= 0x61;                        

  // Priority TX > SP > GR
  *VDC_R1 = (*VDC_R1 & 0xff) | 0x1200;    
  
  // BG0スクロール位置初期化
  BG0_SCRL[0] = 0;
  BG0_SCRL[1] = 0;
  BG1_SCRL[0] = 0;
  BG1_SCRL[1] = 0;

  // スプライト全消去(128枚)
  for (int16_t i = 0; i < 128; i++) {
    SP_SCRL[ i * 4 + 3 ] = 0;               
  }

  // PCGパターン(ブロック) 16x16が4つ * 32方向で128個使う
  for (int16_t i = 0; i < 128; i++) {
    memcpy((void*)&PCG[i * 64], (void*)(&sp_pattern_data[i * 64]), 128);
  }
  
  // スプライトパレット設定
  for (int16_t i = 0; i < 16; i++) {
    PAL_BLK1[i] = sp_palette_data[i];
  }

  *BG_CTRL = 0x200;   // SP/BG ON, BG1-BGTEXT0, BG0-BGTEXT1, BG1 OFF, BG0 OFF
}

//
//  グラフィックパレットの初期化
//
static void init_g_palette() {
  for (int16_t i = 0; i < 16; i++) {
    _iocs_gpalet(i, graphic_palette_colors[i]);
  }
}

//
// ファイルをメモリに丸ごとロード
//
static uint8_t* load_file(const char* filename, uint32_t size) {
  FILE* fp = fopen(filename, "rb");
  if (!fp) {
      printf("Error: Cannot open %s\n", filename);
      return NULL;
  }
  uint8_t* buf = (uint8_t*)malloc(size);
  if (buf) {
    size_t read_len = 0;
    do {
      size_t len = fread(buf + read_len, 1, size - read_len, fp);
      if (len == 0) break;
      read_len += len;
    } while (read_len < size);
    fclose(fp);
  }
  return buf;
}

//
// 1024x1024の4bitパック(512KB)を、GVRAM(16bit/1dot)へ64bit(32bit*2)単位で一気に展開
//
static void deploy_graphics(const uint8_t* packed_grp) {

  uint32_t* dest = (uint32_t*)GVRAM;
  const uint16_t* src = (const uint16_t*)packed_grp;
    
  // 512KBのパックデータは、16bit（4ドット分）が 262,144 個並んでいる
  // これを1ループで2ワード（4ドット）ずつ処理
  for (uint32_t i = 0; i < 256*1024; i++) {

    uint16_t packed = *src++; // 4ドット分(16bit)読み込み
        
    // 64bit展開テーブル、またはシフト演算で16bit VRAMデータ×2（32bit×2）を作る
    uint32_t g1 = ((packed & 0xF000) <<  4) | ((packed & 0x0F00) >> 8); // ドット0,1
    uint32_t g2 = ((packed & 0x00F0) << 12) | ((packed & 0x000F));      // ドット2,3
        
    *dest++ = g1; // 32bitバスへ一発書き込み（2ドット分）
    *dest++ = g2; // 32bitバスへ一発書き込み（2ドット分）
  }
}

//
//  main
//
int32_t main(int32_t argc, uint8_t* argv[]) {

  // アプリケーションの終了コード
  int32_t rc = 1;

  // ユーザースタックポインタ保存用
  int32_t usp = -1;

  // VSYNC割り込み使用開始したかフラグ
  int16_t vsync = 0;

  // AJOY.X常駐チェック
  if (!ajoy_isavailable()) goto exit;

  // random seed初期化
  srand(_iocs_ontime().sec);
  quickrand_seed = rand();

  // オリジナルのファンクションキー表示モード保存
  int32_t funckey_mode = _dos_c_fnkmod(-1);

  // ファンクションキー表示OFF
  _dos_c_fnkmod(3);

  // カーソル表示OFF
  _dos_c_curoff();

  // 256x256 16色モード (グラフィック横1024)
  _iocs_crtmod(2);
  _iocs_g_clr_on();

  // テキスト消去
  _dos_c_cls_al();
 
  // スーパーバイザ移行
  usp = _iocs_b_super(0);

  // 8x8 フォントの初期化
  init_font_8x8();

  // SP/PCGの初期化
  init_sp_pcg();

  // グラフィックパレット初期化
  init_g_palette();

  // テキストラベル初期化
  //init_text_labels();

  // ハイスコア初期設定
  //uint32_t hi_score = DEFAULT_HI_SCORE;

  // 物理マップへのポインタ
  uint8_t* physical_map = NULL;

  // 描画データへのポインタ
  uint8_t* packed_grp = NULL;

  // ゲームループ
game_start:

  // ハイスコア表示(値の初期化はしない)
  //put_hi_score(hi_score);

  // スコア初期化
  //uint32_t score = 0;
  //put_score(score);

  // ファイルからデータをロード
  physical_map = load_file("course1.dat", PHYS_W * PHYS_H);
  packed_grp = load_file("course1.grp", DISP_W * (DISP_W / 2));
    
  if (!physical_map || !packed_grp) {
    printf("Load failed.\n");
    goto exit;
  }
    
  // グラフィックをGVRAMに展開
  deploy_graphics(packed_grp);
  free(packed_grp); // 表示用の一時バッファは用済みなので解放
  packed_grp = NULL;

  // サイバースティック読み出し用バッファ
  uint16_t ajoy_buffer[5];

  // ゲームオーバー判定フラグ
  int16_t game_over = 0;

  // 汎用カウンタ
  uint32_t counter = 0;

  // VSYNC割り込み開始
  if (_iocs_vdispst((uint8_t*)refresh_screen, 0, 1) != 0) {
    printf("VSYNC割り込みが使用中です。\n");
    goto exit;
  }
  vsync = 1;

  // タイマーAリセット(これをしないとハードリセット直後のVSYNC割り込みが正常にスタートしない)
  reset_timer_a();

  // 16倍世界（固定小数点）でのそれぞれの限界値を定義
  // 1440 << 4 = 23040,  1024 << 4 = 16384
  const int32_t MAX_X_16 = 1440 << 4;
  const int32_t MAX_Y_16 = 1024 << 4;

  // ゲームメインループ
  while (!game_over) {

    // ESCキーが押されたら終了 (8フレームごとのチェック)
    if ((counter & 7) == 0 && _iocs_b_keysns() != 0) {
      int16_t scan_code = _iocs_b_keyinp() >> 8;
      if (scan_code == KEY_SCAN_CODE_ESC) {
        goto exit;
      }
    }

    //  read analog stick data
    //      buffer[0] ... stick up/down (0-255)
    //      buffer[1] ... stick left/right (0-255)
    //      buffer[2] ... throttle up/down (0-255)
    //      buffer[3] ... option up/down
    //      buffer[4] ... trigger -|-|-|-|A|B|A'|B'|A+A'|B+B'|C|D|E1|E2|START|SELECT
    ajoy_read(ajoy_buffer);

    // スロットルによるスピード調整
    int16_t throttle_raw = ajoy_buffer[2];
    
    // 128 + 遊び30 = 158 以上のとき、手前に引かれている（アクセルON）と判定
    int16_t accel_amount = 0;
    if (throttle_raw >= 158) {
        accel_amount = throttle_raw - 158; // 引くほど値が大きくなる（0 〜 97）
    }
    // 16倍世界の速度（0 〜 48）へ滑らかに無段階マッピング
    car.speed = (accel_amount * 48) / 97;

    // 128 - 遊び30 = 98 以下のとき、奥に倒されている（ブレーキON）と判定
    int16_t brake_amount = 0;
    if (throttle_raw <= 98) {
      brake_amount = 98 - throttle_raw; // 奥へ倒すほど値が大きくなる（0 〜 98）
    }

    // もしブレーキがONなら、その量に応じて速度を減速させる（例）
    if (brake_amount > 0) {
      // ブレーキ量（0〜98）を、1フレームあたりの減速度（16倍世界の値で0〜4など）に変換
      int16_t deceleration = (brake_amount * 4) / 98;
      car.speed -= deceleration;
      if (car.speed < 0) car.speed = 0;
    }

    // レバーのX軸（左右）で角度インデックスを微調整
    int16_t ax = ajoy_buffer[1] - 128;
    if (ax < -15 || ax > 15) { // センター付近の「遊び」を確保
        int32_t turn_power = (int32_t)ax * car.speed;
        
        // 分母を「512」から「4096（0x1000）」または「2048」に跳ね上げます！
        // これにより、旋回スピードが一律で 1/4 〜 1/8 にマイルド化します。
        car.sub_angle += (turn_power / 2048); 
        
        car.sub_angle &= 511; // 0〜511の範囲に丸める
        car.angle = car.sub_angle >> 4; // 0〜31のインデックス（VSYNCハンドラがこれを参照）
    }

    // 1. 移動と物理世界のクリッピング（1440x1024ベース）
    car.x += ((int32_t)car.speed * cos_table[car.angle]) >> 8;
    car.y += ((int32_t)car.speed * sin_table[car.angle]) >> 8;

    if (car.x < 0) car.x = 0;
    if (car.x >= (1440 << 4)) car.x = (1440 << 4) - 1;
    if (car.y < 0) car.y = 0;
    if (car.y >= (1024 << 4)) car.y = (1024 << 4) - 1;

    // 等倍ドット座標を一時マッピング
    int16_t map_x = car.x >> 4;
    int16_t map_y = car.y >> 4;

    // 2. カメラ座標を世界の端でクランプ（構造体メンバーへ直接格納）
    car.cam_x = map_x;
    if (car.cam_x < 128)  car.cam_x = 128;
    if (car.cam_x > 1312) car.cam_x = 1312;

    car.cam_y = map_y;
    if (car.cam_y < 128)  car.cam_y = 128;
    if (car.cam_y > 896)  car.cam_y = 896;

    // 3. 自車の画面スプライト表示座標（構造体メンバーへ直接格納）
    car.sp_x = 128 + (map_x - car.cam_x);
    car.sp_y = 128 + (map_y - car.cam_y);

    WAIT_VBLANK;
    
  } // ゲームメインループここまで

  // VSYNC割り込み利用停止
  if (vsync > 0) {
    _iocs_vdispst(0, 0, 0);
    vsync = 0;
  }

  // ハイスコア書き換え
  //if (score > hi_score) {
  //  hi_score = score;
  //}

  // 一呼吸
  usleep(500);

  goto game_start;


exit:

  // VSYNC割り込み利用停止
  if (vsync > 0) {
    _iocs_vdispst(0, 0, 0);
    vsync = 0;
  }

  // バッファ解放
  if (physical_map != NULL) {
    free(physical_map);
    physical_map = NULL;
  }
  if (packed_grp != NULL) {
    free(packed_grp);
    packed_grp = NULL;
  }

  // ユーザーモードに復帰
  if (usp > 0) {
    _iocs_b_super(usp);
    usp = -1;
  }

  // 画面モードをリセット
  _iocs_crtmod(16);
  _iocs_g_clr_on();

  // ファンクションキー表示をリセット
  if (funckey_mode >= 0) {
    _dos_c_fnkmod(funckey_mode);
  }

  // カーソル表示ON
  _dos_c_curon();

  // キーバッファフラッシュ
  _dos_kflushio(0xff);

  // 終了
  return rc;
}
