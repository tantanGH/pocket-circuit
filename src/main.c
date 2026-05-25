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

// 自機
static volatile PLAYER_CAR car = { 0 };

// VSYNCイベント
static volatile VSYNC_EVENT vsync_event = { 0 };

// 8x8 normal font data
static struct iocs_fntbuf font_data_8x8[ 256 ];

// 8x8 bold font data
static struct iocs_fntbuf font_data_8x8_bold[ 256 ];

// アナログコントローラモードか
static int16_t use_analog_controller = 0;

// サイバースティック読み出し用バッファ
static uint16_t ajoy_buffer[5];

// エラーメッセージ出力用バッファ
static uint8_t error_mes[ 256 ];

// プレーヤーオブジェクト初期化
static void init_player_car(volatile PLAYER_CAR *car, int16_t start_x, int16_t start_y, int16_t start_angle) {

  // 物理演算用パラメータ初期化
  car->x = (int32_t)start_x << 4;       // 1440x1024 解像度を16倍固定小数点で
  car->y = (int32_t)start_y << 4;       // 1440x1024 解像度を16倍固定小数点で

  car->speed = 0 << 8;                  // 32段階速度を256倍固定小数点で

  car->angle = start_angle << 8;        // 32段階角度を256倍固定小数点で
  car->move_angle = start_angle << 8;   // 32段階角度を256倍固定小数点で

  car->current_turn = 0;

  // 画面制御用パラメータ初期化
  car->cam_x = start_x;
  if (car->cam_x < CAM_X_MIN) car->cam_x = CAM_X_MIN;
  if (car->cam_x > CAM_X_MAX) car->cam_x = CAM_X_MAX;

  car->cam_y = start_y;
  if (car->cam_y < CAM_Y_MIN) car->cam_y = CAM_Y_MIN;
  if (car->cam_y > CAM_Y_MAX) car->cam_y = CAM_Y_MAX;

  // スプライト位置初期化
  car->sp_x = 128;    // 256x256 画面の中央絶対座標だけどスプライト特有のオフセットは考慮せず
  car->sp_y = 128;

  // スコア・ラップ
  car->score = 0;
  car->lap_count = 0;
  car->next_checkpoint = 1;
  car->last_gate = 0;
  car->wrong_way = 0;
  car->is_goal = 0;

  for (int16_t i = 0; i < 6; i++) {
    car->lap_times[i] = 0;
    car->lap_scores[i] = 0;
  }

}

//
//  VSYNCイベント初期化
//
static void init_vsync_event(volatile VSYNC_EVENT* vsync_event) {

  // カウンタ初期化
  vsync_event->vsync_counter = 0;
  vsync_event->drift_points_counter = 0;
  vsync_event->lap_mes_counter = 0;
  vsync_event->wrong_way_counter = 0;
  vsync_event->goal_counter = 0;

  // イベントフラグ初期化
  vsync_event->event_refresh_hi_score = 0;
  vsync_event->event_refresh_score = 0;
  vsync_event->event_refresh_drift_points = 0;
  vsync_event->event_refresh_lap_count = 0;
  vsync_event->event_refresh_lap_mes = 0;
  vsync_event->event_refresh_wrong_way = 0;
  vsync_event->event_refresh_goal = 0;

  // 表示メッセージ用バッファ初期化
  vsync_event->hi_score_mes[0] = '\0';
  vsync_event->score_mes[0] = '\0';
  vsync_event->lap_count_mes[0] = '\0';
  vsync_event->drift_points_mes[0] = '\0';
}

//
//  sprintf(buf,"%0Xd",val) の置き換え用
//
static void int_to_ascii_right(uint8_t* buf, int16_t digits, uint32_t val) {

  // 文字列の末尾（ヌル文字）をセット
  buf[digits] = '\0';

  // 右詰め（下の桁）から逆順にバッファを埋めていく
  for (int16_t i = digits - 1; i >= 0; i--) {
    if (val > 0 || i == digits - 1) { 
      // 値がまだ残っている、または一の位のときは数字に変換
      buf[i] = '0' + (val % 10);
      val /= 10;
    } else {
      // 値がもう無くなった（上位の余った桁）はスペースで埋める
      buf[i] = ' ';
    }
  }
}

//
//  sprintf(buf, "+%dpt.", val) の置き換え用
//
static void int_to_drift_pt_mes(uint8_t* buf, int16_t digits, uint32_t val) {
  
  // まず全体をスペースで初期化し、最後にヌル文字を置く
  for (int16_t i = 0; i < digits; i++) {
    buf[i] = ' ';
  }
  buf[digits] = '\0';

  // 文字列の組み立ては、右端（後ろ）から逆順に進める
  int16_t p = digits - 1;

  // 末尾の定数文字 "pt." を後ろからセット（pをデクリメントしながら配置）
  if (p >= 0) buf[p--] = '.';
  if (p >= 0) buf[p--] = 't';
  if (p >= 0) buf[p--] = 'p';

  // 数値部分を1の位から順にセット
  // 値が 0 になっても、最低1回は '0' を出力させる（val == 0 のとき対策）
  do {
    if (p >= 0) {
      buf[p--] = '0' + (val % 10);
      val /= 10;
    }
  } while (val > 0 && p >= 0);

  // 数字のすぐ左隣に、プラス記号 '+' をセット
  if (p >= 0) {
    buf[p--] = '+';
  }
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
//  8x8 フォントでテキスト画面に文字列を出力する
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

  // 自車スプライトの表示

  // グラフィック座標（car.sp_x）を、スプライト座標（+16）へ変換し、
  // さらに32x32スプライトの左上基準（-16）を引く
  int16_t sp_base_x = car.sp_x; 
  int16_t sp_base_y = car.sp_y;
  
  // 32段階回転のどのパターンを使うか
  // 1方向ごとに4パターン使う
  uint16_t pattern_base = 0x100 + ((car.angle >> 8) << 2);    // 内部的に256倍精度なのを32段階に戻してから4倍

  // 自車の表示 (スプライト0-4)
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


  // グラフィック画面のスクロール

  // 物理的な「画面左上端」を求める（中心cam_x - 画面物理幅の半分181）
  int16_t screen_left = car.cam_x - CAM_X_MIN;
  int16_t screen_top  = car.cam_y - CAM_Y_MIN;

  // ハードウェアの解像度（1024x1024）へ投影（リニア変換）
  int16_t gvram_scrl_x = ((int32_t)screen_left * DISP_W) / PHYS_W;
  int16_t gvram_scrl_y = screen_top; // 縦は1:1なのでそのまま

  // レジスタへ反映
  GR0_SCRL[0] = gvram_scrl_x & 1023;
  GR0_SCRL[1] = gvram_scrl_y & 1023;


  // ハイスコア表示更新イベント
  if (vsync_event.event_refresh_hi_score) {
    put_text_8x8(72,0,3,1,(uint8_t*)vsync_event.hi_score_mes);
    vsync_event.event_refresh_hi_score = 0;
  }

  // スコア表示更新イベント
  if (vsync_event.event_refresh_score) {
    put_text_8x8(184,0,3,1,(uint8_t*)vsync_event.score_mes);
    vsync_event.event_refresh_score = 0;
  }

  // ラップ表示更新イベント
  if (vsync_event.event_refresh_lap_count) {
    put_text_8x8(32,248,3,1,(uint8_t*)vsync_event.lap_count_mes);
    vsync_event.event_refresh_lap_count = 0;
  }

  // ドリフトポイント表示イベント
  if (vsync_event.event_refresh_drift_points) {
    put_text_8x8(96,64,3,1,(uint8_t*)vsync_event.drift_points_mes);
    vsync_event.event_refresh_drift_points = 0;
  }

  // ドリフトポイント消去カウントダウン
  if (vsync_event.drift_points_counter > 0) {
    if (vsync_event.drift_points_counter == 1) {
      put_text_8x8(96,64,3,1,"          ");      
    }
    vsync_event.drift_points_counter--;
  }

  // コース外れ警告表示イベント
  if (vsync_event.event_refresh_wrong_way) {
    put_text_8x8(96,48,3,1,"WRONG WAY");
    vsync_event.event_refresh_wrong_way = 0;
  }

  // コース外れ警告消去カウントダウン
  if (vsync_event.wrong_way_counter > 0) {
    if (vsync_event.wrong_way_counter == 1) {
      put_text_8x8(96,48,3,1,"         ");
    }
    vsync_event.wrong_way_counter--;
  }
 
  // ラップメッセージ表示イベント
  if (vsync_event.event_refresh_lap_mes) {
    switch (vsync_event.event_refresh_lap_mes) {
      case 1: put_text_8x8(96,48,3,1," 1ST LAP "); break;
      case 2: put_text_8x8(96,48,3,1," 2ND LAP "); break;
      case 3: put_text_8x8(96,48,3,1," 3RD LAP "); break;
      case 4: put_text_8x8(96,48,3,1," 4TH LAP "); break;
      case 5: put_text_8x8(96,48,3,1,"FINAL LAP"); break;
    }
    vsync_event.event_refresh_lap_mes = 0;
  }

  // ラップメッセージ消去カウントダウン
  if (vsync_event.lap_mes_counter > 0) {
    if (vsync_event.lap_mes_counter == 1) {
      put_text_8x8(96,48,3,1,"         ");
    }
    vsync_event.lap_mes_counter--;
  }

  // ゴール表示イベント
  if (vsync_event.event_refresh_goal) {
    put_text_8x8(104,48,3,1,"GOAL!!!");
    vsync_event.event_refresh_goal = 0;
  }

  // ゴール表示消去イベント
  if (vsync_event.goal_counter > 0) {
    if (vsync_event.goal_counter == 1) {
      put_text_8x8(104,48,3,1,"       ");
    }
    vsync_event.goal_counter--;
  }
  
  // 基本カウンタ
  vsync_event.vsync_counter++;
}

//
//  PCG / スプライトの初期化 (スーパーバイザモードになっていること)
//
static void init_sp_pcg() {

  // VBLANK待ち
  WAIT_VBLANK;

  // SP:ON TX:ON GS4:ON
  *VDC_R2 |= 0x70;                        

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
//  テキストパレットの初期化
//
static void init_t_palette() {
  _iocs_tpalet2(0, 0x0000);
  _iocs_tpalet2(1, ((210/8)<<11) | (( 30/8)<<6) | ((255/8)<<1) | 1);
  _iocs_tpalet2(2, ((210/8)<<11) | ((255/8)<<6) | (( 30/8)<<1) | 1);
  _iocs_tpalet2(3, 0xffff);
}

//
//  ファイルをメモリに丸ごとロード
//
static uint8_t* load_file(const char* filename, uint32_t size) {
  FILE* fp = fopen(filename, "rb");
  if (fp == NULL) {
    return NULL;
  }
  uint8_t* buf = (uint8_t*)malloc(size);
  if (buf != NULL) {
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

// グラフィック画面OFF
static void graphic_off() {
  *VDC_R2 &= 0xffe0;    
}

// グラフィック画面ON
static void graphic_on() {
  // SP:ON TX:ON GS4:ON(1024x1024)
  *VDC_R2 |= 0x70; 
}

//
//  1024x1024の4bitパック(512KB)を、GVRAM(16bit/1dot)へ64bit(32bit*2)単位で展開
//
static void deploy_graphics(const uint8_t* packed_grp) {

  uint32_t* dest = (uint32_t*)GVRAM;
  const uint16_t* src = (const uint16_t*)packed_grp;

  // 512KBのパックデータは、16bit（4ドット分）が 262,144 個並んでいる
  // これを1ループで2ワード（4ドット）ずつ処理
  for (uint32_t i = 0; i < 256*1024; i++) {

    uint16_t packed = *src++; // 4ドット分(16bit)読み込み
        
    // シフト演算で16bit GVRAMデータ×2（32bit×2）を作る
    uint32_t g1 = ((packed & 0xF000) <<  4) | ((packed & 0x0F00) >> 8); // ドット0,1
    uint32_t g2 = ((packed & 0x00F0) << 12) | ((packed & 0x000F));      // ドット2,3
        
    *dest++ = g1; // 2ドット分書き込み
    *dest++ = g2; // 2ドット分書き込み
  }

}

//
//  NOW LOADING の表示
//
static void put_loading_messages() {
  put_text_8x8(0,0,1,1,"POCKET CIRCUIT PRO-68K");
  put_text_8x8(0,16,1,1,"VERSION "VERSION);
  usleep(200000);
  if (use_analog_controller) {
    put_text_8x8(0,32,3,1," - AJOY.X WAS FOUND.");
    put_text_8x8(0,48,3,1," - USE AN ANALOG CONTROLLER.");
    usleep(200000);
  } else {
    put_text_8x8(0,32,3,1," - AJOY.X WAS NOT FOUND.");
    put_text_8x8(0,48,3,1," - USE A DIGITAL CONTROLLER.");
    usleep(200000);    
  }
  put_text_8x8(0,64,3,1," - LOADING COURSE DATA ...");
}

//
//  テキストラベル表示
//
static void init_text_labels() {
  put_text_8x8(8,0,2,1,"HI SCORE");
  put_text_8x8(144,0,2,1,"SCORE");
  put_text_8x8(8,248,2,1,"LAP");
}

//
//  ゲーム開始待機画面 (未使用)
//
static int16_t wait_game_start() {

  put_text_8x8(64,48,3,1,"PUSH ANY BUTTON");

  for (;;) {
    if (_iocs_b_keysns() != 0) {
      int16_t scan_code = _iocs_b_keyinp() >> 8;
      if (scan_code == KEY_SCAN_CODE_SPACE) {
        break;
      } else if (scan_code == KEY_SCAN_CODE_ESC) {
        return -1;
      }
    }
    if (use_analog_controller) {
      ajoy_read(ajoy_buffer);
      if ((ajoy_buffer[4] & 0xfff) != 0x0fff) {
        break;
      }
    } else {
      if ((_iocs_joyget(0) & 0x60) != 0x60) break;
    }
  }

  put_text_8x8(64,48,3,1,"               ");

  return 0;
}

//
//  ゲームオーバー待機画面(リザルト表示)
//
static int16_t wait_game_over() {

  // グラフィックOFF
  graphic_off();

  // テキスト画面クリア
  _dos_c_cls_al();

  put_text_8x8(80,8,2,1,"- RESULT - ");

  static uint8_t mes[256];
  uint32_t total_lap_time = 0;
  uint32_t total_score = 0;
  for (int16_t i = 1; i <= 5; i++) {
    uint32_t lap_time = (car.lap_times[i] - car.lap_times[i - 1]) * 1000000 / 55458;
    uint32_t lap_score = car.lap_scores[i];
    total_lap_time += lap_time;
    total_score += lap_score;
    usleep(500000);
    sprintf(mes,"LAP%d '%02d:%02d.%03d +%dpt.",
              i, lap_time / 60000, (lap_time % 60000) / 1000, lap_time % 1000, lap_score);
    put_text_8x8(32,16 + i * 24,3,1,mes);
  }

  usleep(500000);
  sprintf(mes,"TOTAL '%02d:%02d.%03d +%dpt.",
            total_lap_time / 60000, (total_lap_time % 60000) / 1000, total_lap_time % 1000, total_score);
  put_text_8x8(24,168,1,1,mes);

  usleep(500000);
  put_text_8x8(64,200,3,1,"PUSH ANY BUTTON");

  // 入力待ち
  for (;;) {
    if (_iocs_b_keysns() != 0) {
      int16_t scan_code = _iocs_b_keyinp() >> 8;
      if (scan_code == KEY_SCAN_CODE_SPACE) {
        break;
      } else if (scan_code == KEY_SCAN_CODE_ESC) {
        return -1;
      }
    }
    if (use_analog_controller) {
      ajoy_read(ajoy_buffer);
      if ((ajoy_buffer[4] & 0xfff) != 0x0fff) {
        break;
      }
    } else {
      if ((_iocs_joyget(0) & 0x60) != 0x60) break;
    }
  }

  return 0;
}

//
//  main
//
int32_t main(int32_t argc, uint8_t* argv[]) {

  // アプリケーションの終了コード
  int32_t rc = 1;

  // ユーザースタックポインタ保存用
  int32_t usp = -1;

  // VSYNC割り込み使用開始したよフラグ
  int16_t vsync = 0;

  // エラーメッセージ初期化
  error_mes[0] = '\0';

  // AJOY.X常駐チェック
  if (ajoy_isavailable()) {
    use_analog_controller = 1;
  }

  // random seed初期化
  srand(_iocs_ontime().sec);

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

  // テキストパレット初期化
  init_t_palette();

  // ハイスコア初期設定
  uint32_t hi_score = DEFAULT_HI_SCORE;

  // 物理マップへのポインタ
  uint8_t* course_data = NULL;
  uint8_t* physical_map = NULL;

  // 描画データへのポインタ
  uint8_t* grp_data = NULL;

  // NOW LOADING
  put_loading_messages();

  // コース物理データのロード
  course_data = load_file(COURSE_PHYS_DATA_FILE, PHYS_W * PHYS_H);
  if (course_data == NULL) {
    strcpy(error_mes, "コースデータ(.DAT)の読み込みに失敗しました。");
    goto exit;
  }
  physical_map = course_data + 32;    // 専用32バイトはヘッダ それ以降に物理マップデータ(1440x1024)

  // コースグラフィックデータのロード
  grp_data = load_file(COURSE_DISP_DATA_FILE, DISP_W * (DISP_W / 2));
  if (grp_data == NULL) {
    strcpy(error_mes, "コースグラフィックデータ(.GRP)の読み込みに失敗しました。");
    goto exit;
  }
    
  // コースグラフィックデータをGVRAMに展開
  graphic_off();
  deploy_graphics(grp_data);
  free(grp_data); // 表示用の一時バッファは用済みなので解放
  grp_data = NULL;

  // ゲームループ
game_start:

  // テキストクリア
  _dos_c_cls_al();

  // グラフィックON
  graphic_on();

  // VSYNCイベントステータス初期化
  init_vsync_event(&vsync_event);

  // ドリフトポイント初期化
  uint16_t drift_combo = 0;           // 現在の連続ドリフトフレーム数（コンボ）
  uint32_t current_drift_points = 0;  // 1回のドリフトで今溜まっている暫定点

  // カメラ位置初期化
  uint16_t s_x     = *(uint16_t*)(course_data + 4);
  uint16_t s_y     = *(uint16_t*)(course_data + 6);
  uint16_t s_angle = *(uint16_t*)(course_data + 8);
  init_player_car(&car, s_x, s_y, s_angle);

  // テキストラベル初期化
  init_text_labels();

  // ハイスコア設定と表示依頼
  int_to_ascii_right((uint8_t*)vsync_event.hi_score_mes,8,hi_score);
  vsync_event.event_refresh_hi_score = 1;

  // スコア初期化と表示依頼
  uint32_t score = 0;
  int_to_ascii_right((uint8_t*)vsync_event.score_mes,8,score);
  vsync_event.event_refresh_score = 1;

  // ゲームオーバー判定フラグ
  int16_t game_over = 0;

  // VSYNC割り込み開始
  if (_iocs_vdispst((uint8_t*)refresh_screen, 0, 1) != 0) {
    strcpy(error_mes, "VSYNC割り込みが使用中です。");
    goto exit;
  }
  vsync = 1;

  // タイマーAリセット(これをしないとハードリセット直後のVSYNC割り込みが正常にスタートしない)
  reset_timer_a();

  // 開始待ち
//  if (wait_game_start() != 0) {
//    rc = 0;
//    goto exit;
//  }

  // ゲームメインループ
  while (!game_over) {

    // 現在のVSYNCカウンタ
    uint32_t current_vsync = vsync_event.vsync_counter;

    // ESCキーが押されたら終了 (8フレームごとのチェック)
    if (!(current_vsync & 7) && _iocs_b_keysns() != 0) {
      int16_t scan_code = _iocs_b_keyinp() >> 8;
      if (scan_code == KEY_SCAN_CODE_ESC) {
        goto exit;
      }
    }
  
    // 既にゴール済みなら車体を右へすいーっと動かす
    if (car.is_goal) {
      car.sp_x += 4;
      if (car.sp_x > 256 + 32) {
        // 画面外に出たらゲーム終了、リザルト確認へ
        game_over = 1;
      }
      goto skip_physics;  // このフレームでは物理を動かさない
    }

    // ----------------------------------------------------------------
    // 1. 足元の路面属性をチェックし、「現在の最高速度制限」を決める
    // ----------------------------------------------------------------
    int16_t map_x = car.x >> 4;
    int16_t map_y = car.y >> 4;
    uint32_t map_index = ((uint32_t)map_y * 1440) + map_x;
    uint8_t surface_attr = physical_map[map_index] & 7;

    int16_t speed_limit = MAX_SPEED << 8; // デフォルトの道路上での最高速度
    if (surface_attr == 6) {
      // グラベル（砂利）
      speed_limit = 24 << 8; 
    } else if (surface_attr == 4 || surface_attr == 5) {
      // 縁石なら少し制限
      speed_limit = 64 << 8;
    } else if (surface_attr == 0 || surface_attr == 1) {
      // 芝生
      speed_limit = 32 << 8;
    }

    // ----------------------------------------------------------------
    // 2. コントローラの入力を「目標速度」として取得
    // ----------------------------------------------------------------
    int16_t throttle_raw;
    int16_t lever_raw;

    // スロットル生データ(0-255)取得
    if (use_analog_controller) {
      // アナログコントローラ
      ajoy_read(ajoy_buffer);
      throttle_raw = ajoy_buffer[2];
      lever_raw = ajoy_buffer[1] - 128;
    } else {
      // デジタルジョイパッド
      uint8_t j = *((volatile uint8_t*)(0x0e9a001));
      throttle_raw = (!(j & 0x20)) ? 255 : 0;
      lever_raw = (!(j & 4)) ? -(MAX_SPEED) : (!(j & 8)) ? MAX_SPEED : 0;
    }
    
    // 255 - MAX_SPEED 以上のとき、手前に引かれている（アクセルON）と判定
    int16_t accel_amount = 0;
    if (throttle_raw >= (255 - MAX_SPEED)) {
      accel_amount = throttle_raw - (255 - MAX_SPEED); // 引くほど値が大きくなる（0 〜 MAX_SPEED）
    }

    int16_t target_speed = accel_amount << 8;     // 共通レンジにつき剰余省略
    if (target_speed > speed_limit) {
      target_speed = speed_limit; // コントローラ全開でも、路面リミッターで頭打ちにする
    }

    // ----------------------------------------------------------------
    // 3. 実際の車の速度（car.speed）を目標速度に向けて近づける（慣性）
    // ----------------------------------------------------------------

    // 現在の速度と目標速度の差分を計算
    int16_t speed_diff = target_speed - car.speed;
    if (speed_diff > 0) {

      // [加速中]

      // 加速トルク
      int16_t torque = (speed_diff >> 3); // 基本は差分比例
        
      // 車の速度が極端に遅いときはスタック防止用に最低保証トルクを上乗せする
      if (car.speed < (16 << 8)) {
        torque += (1 << 8);
      } else {
        torque += 1;        // 通常域は最低でも「+1」する
      }

      car.speed += torque;
      if (car.speed > target_speed) {
        // リミッター
        car.speed = target_speed;
      }

    } else if (speed_diff < 0) {

      // [減速中]   
      car.speed += (speed_diff >> 5) - 4;
      if (car.speed < target_speed) {
        // リミッター
        car.speed = target_speed;
      }

    }

    // ================================================================
    // 4. 車の向き（angle：0〜8191）の更新
    // ================================================================
    int32_t target_turn = 0;  // 目標回転力
    
    // アナログレバーの左右の遊びを考慮
    if (lever_raw < -15 || lever_raw > 15) {
      // 目標回転力        
      target_turn = (int32_t)lever_raw * (int32_t)(car.speed >> 8);
    }
    
    // 現在回転力を目標回転力にじわっと近づける
    car.current_turn += (target_turn - car.current_turn) >> 3;
    if (car.current_turn < -10 || car.current_turn > 10) {        
        car.angle += (int16_t)(car.current_turn >> 7);    // >>8だと重ステ
        car.angle = (car.angle + 8192) & 8191; 
    }

    // ================================================================
    // 5. 進む向き（move_angle）の遅れ（ヨー・ドリフト）計算
    // ================================================================
    int16_t angle_diff = car.angle - car.move_angle;
    
    // 8192の世界での巡回補正
    if (angle_diff > 4096)  angle_diff -= 8192;
    if (angle_diff < -4096) angle_diff += 8192;

    // タイヤの引き寄せ力（グリップ力）の計算
    int16_t current_speed_raw = car.speed >> 8;
    
    // 低速時は完全にカチッとグリップさせる
    int16_t grip_power = 256; 
    
    // 滑り出す速度閾値
    if (current_speed_raw > 35) {
      // スピードが出てる時はグリップが低下する
      grip_power = 40; 
    }

    // 実際の進行方向（move_angle）を、車の向き（angle）に向けて「grip_power」の歩幅で引き寄せる
    if (angle_diff > 0) {
      car.move_angle = (car.move_angle + grip_power) & 8191;
      // 行き過ぎ防止の条件も、新しい grip_power の値と比較するように統一
      if (angle_diff < grip_power) car.move_angle = car.angle;
    } else if (angle_diff < 0) {
      car.move_angle = (car.move_angle - grip_power) & 8191;
      if (-angle_diff < grip_power) car.move_angle = car.angle;
    }

    // ================================================================
    // 6. 移動計算（実際に進む方向 move_angle でテーブル引き）
    // ================================================================
    int16_t move_table_idx = car.move_angle >> 8; // 32方向に落とす

    // 事前に計算してあった256倍精度cos/sinルックアップテーブルを使う
    car.x += ((int32_t)car.speed * cos_table[move_table_idx]) >> 16;
    car.y += ((int32_t)car.speed * sin_table[move_table_idx]) >> 16;

    // 16倍世界から通常のドット座標へ
    map_x = car.x >> 4;
    map_y = car.y >> 4;

    // カメラ座標を決定
    car.cam_x = map_x;
    if (car.cam_x < CAM_X_MIN) car.cam_x = CAM_X_MIN;
    if (car.cam_x > CAM_X_MAX) car.cam_x = CAM_X_MAX;

    car.cam_y = map_y;
    if (car.cam_y < CAM_Y_MIN) car.cam_y = CAM_Y_MIN;
    if (car.cam_y > CAM_Y_MAX) car.cam_y = CAM_Y_MAX;

    // カメラの位置を基準にクランプ
    int16_t min_x = car.cam_x - CAM_X_MIN; 
    int16_t max_x = car.cam_x + CAM_X_MIN; 
    int16_t min_y = car.cam_y - CAM_Y_MIN;
    int16_t max_y = car.cam_y + CAM_Y_MIN;
    
    // 世界の端のフチのガード
    if (map_x < min_x) { map_x = min_x; car.x = (int32_t)map_x << 4; }
    if (map_x > max_x) { map_x = max_x; car.x = (int32_t)map_x << 4; }
    if (map_y < min_y) { map_y = min_y; car.y = (int32_t)map_y << 4; }
    if (map_y > max_y) { map_y = max_y; car.y = (int32_t)map_y << 4; }

    // 画面の中心位置 128 (ピクセル) + (車とカメラの物理距離)
    // ただし、物理距離をピクセル単位にスケール変換する必要がある(横長ドットのため)
    car.sp_x = 128 + (int16_t)(((int32_t)(map_x - car.cam_x) * 256) / 362);
    car.sp_y = 128 + (map_y - car.cam_y);

    // スコア増分計算
    // 1. 滑り角（abs(angle_diff)）を取得
    angle_diff = car.angle - car.move_angle;
    if (angle_diff > 4096)  angle_diff -= 8192;
    if (angle_diff < -4096) angle_diff += 8192;
    int16_t slip_angle = abs(angle_diff);

    // 2. ドリフト成立条件のチェック
    // 「速度が一定以上」かつ「滑り角が一定（例: 32方向基準で1方向分=256）以上」
    // かつ「足元が道路または縁石（surface_attrが3, 4, 5）」のとき
    if ((car.speed >> 8) > 10 && slip_angle > 256 && 
        (surface_attr == 3 || surface_attr == 4 || surface_attr == 5)) {
      
      drift_combo++;

      // 滑り角（slip_angle: 0〜4096）の解像度を大幅に削る
      uint16_t slip_direction_count = slip_angle >> 8; 

      // スピード（最大90）× 角度のズレ（最大16）
      uint32_t raw_frame_point = (uint32_t)(car.speed >> 8) * slip_direction_count;

      // 「1フレームあたり最大で数点」レベルまで右シフトで縮小
      uint32_t base_point = raw_frame_point >> 5;

      // 1以上点数が入るなら、それを「10点単位」に変換して加算
      if (base_point > 0) {
          current_drift_points += (base_point * 10);
      }

    } else {

      // ------------------------------------------------------------
      // ドリフト終了時（直線に戻った、または速度が落ちた、コースアウトした）
      // ------------------------------------------------------------
      if (current_drift_points > 0) {
            
        // 終了時に道路（3,4）または縁石（5）の上にいるかチェック
        if (!car.wrong_way && (surface_attr == 3 || surface_attr == 4 || surface_attr == 5)) {
                
          // 道路上ならポイント獲得
          if (drift_combo > 60) {
            current_drift_points += 500; // ロングコンボ
          }
                
          // 獲得ポイント表示をVSYNCハンドラに依頼
          int_to_drift_pt_mes((uint8_t*)vsync_event.drift_points_mes, 8, current_drift_points);
          vsync_event.event_refresh_drift_points = 1;
          vsync_event.drift_points_counter = 50; 

          // トータルスコアに追加し、VSYNCハンドラに表示更新依頼
          car.score += current_drift_points;
          int_to_ascii_right((uint8_t*)vsync_event.score_mes, 8, car.score);                
          vsync_event.event_refresh_score = 1;

          // ラップごとのスコアにも加算しておく
          car.lap_scores[car.lap_count] += current_drift_points;

          // もしハイスコアを更新した場合は、そちらも表示更新依頼
          if (car.score > hi_score) {
            hi_score = car.score;
            strcpy((uint8_t*)vsync_event.hi_score_mes, (uint8_t*)vsync_event.score_mes);
            vsync_event.event_refresh_hi_score = 1;
          }

        } else {

          // 芝生や砂利にハミ出して終了した場合は0pt.
          int_to_drift_pt_mes((uint8_t*)vsync_event.drift_points_mes, 8, 0); 
          vsync_event.event_refresh_drift_points = 1;
          vsync_event.drift_points_counter = 25;      // 通常の半分の時間だけ +0pt. を表示
        }

        // 次のドリフトのために状態をリセット
        current_drift_points = 0;
        drift_combo = 0;
      }
    }

    // ================================================================
    // 7. チェックポイント・ラップ通過確認
    // ================================================================
    uint8_t current_gate = physical_map[map_index] >> 4; // 0〜4

    if (current_gate > 0) {

      if (current_gate != car.last_gate) {
            
        // 新しいゲートに突入した瞬間なので、last_gateを更新
        car.last_gate = current_gate;

        // 自分が次に通過すべきゲート番号（最初は1）と一致したか？
        if (current_gate == car.next_checkpoint) {

          car.wrong_way = 0;
            
          if (car.next_checkpoint == 1) {
            // 【ゲート1：スタートラインを通過した時】
            if (car.lap_count == 0) {
              // ゲーム開始直後の最初の通過
              car.lap_times[0] = vsync_event.vsync_counter;
              car.lap_count = 1;
              car.next_checkpoint = 2;
              int_to_ascii_right((uint8_t*)vsync_event.lap_count_mes, 4, car.lap_count);   
              vsync_event.event_refresh_lap_count = 1;
              vsync_event.event_refresh_lap_mes = 1;
              vsync_event.lap_mes_counter = 50;
            } else if (car.lap_count >= 5) {
              // ゴールした
              car.lap_times[5] = vsync_event.vsync_counter;
              car.angle = 0;
              car.move_angle = 0;
              car.is_goal = 1;
              vsync_event.event_refresh_goal = 1;
              vsync_event.goal_counter = 200;
            } else {
              // 2, 3, 4, 5周目の通過
              car.lap_times[car.lap_count++] = vsync_event.vsync_counter;
              car.next_checkpoint = 2;
              int_to_ascii_right((uint8_t*)vsync_event.lap_count_mes, 4, car.lap_count);   
              vsync_event.event_refresh_lap_count = 1;
              vsync_event.event_refresh_lap_mes = car.lap_count;
              vsync_event.lap_mes_counter = 50;
            } 
          } else if (car.next_checkpoint == 4) {
            // 最終ゲートを踏んだら、次はスタートライン（ゲート1）を待つ
            car.next_checkpoint = 1;
          } else {
            // ゲート2, 3を順調にクリア
            car.next_checkpoint++;
          }
        } else {

          // コースを外れた場合は、現在溜まっているドリフトの暫定ポイントを没収
          if (current_drift_points > 0) {
            current_drift_points = 0;
            drift_combo = 0;
          }
          vsync_event.event_refresh_wrong_way = 1;
          vsync_event.wrong_way_counter = 50;
          car.wrong_way = 1;
        }
      }

    } else {
      car.last_gate = 0;
    }

skip_physics:
    // 物理計算の画面描画追い越しガード
    while (vsync_event.vsync_counter == current_vsync) {
    }
    
  } // ゲームメインループここまで

  // VSYNC割り込み利用停止
  if (vsync > 0) {
    _iocs_vdispst(0, 0, 0);
    vsync = 0;
  }

  // リザルト表示待機画面
  if (game_over) {
    if (wait_game_over() != 0) {
      rc = 0;
      goto exit;
    }
  }

  // 一呼吸
  usleep(500000);

  goto game_start;
  

exit:

  // VSYNC割り込み利用停止
  if (vsync > 0) {
    _iocs_vdispst(0, 0, 0);
    vsync = 0;
  }

  // バッファ解放
  if (course_data != NULL) {
    free(course_data);
    course_data = NULL;
  }
  if (grp_data != NULL) {
    free(grp_data);
    grp_data = NULL;
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

  // 表示すべきエラーメッセージがあれば表示する
  if (strlen(error_mes) > 0) {
    puts(error_mes);
  } else {
    rc = 0;
  }

  // 終了
  return rc;
}
