import numpy as np
import cv2

# 定数定義（固定仕様）
PHYS_W, PHYS_H = 1440, 1024
DISP_W, DISP_H = 1024, 1024

# パレット・属性値（c & 7 = 属性、最上位bit = 輝度）
ATTR_LAWN = 1    # 芝生 (0001)
ATTR_ROAD = 3    # 道路 (0011)

def main():
    # ----------------------------------------------------
    # 1. 物理モデルデータの生成 (1440x1024, 1byte/dot)
    # ----------------------------------------------------
    # 初期状態はすべて芝生
    phys_map = np.full((PHYS_H, PHYS_W), ATTR_LAWN, dtype=np.uint8)
    
    # テスト用の単純な楕円コースを描画 (太い線を引く)
    # センター: (720, 512), 長軸・短軸: (500, 350), 線幅: 120ピクセル
    center = (PHYS_W // 2, PHYS_H // 2)
    cv2.ellipse(phys_map, center, (500, 350), 0, 0, 360, int(ATTR_ROAD), thickness=120)
    
    # 物理バイナリ書き出し (1440 * 1024 = 1,474,560 bytes)
    with open("course1.dat", "wb") as f:
        f.write(phys_map.tobytes())
    print("Successfully generated: course1.dat")

    # ----------------------------------------------------
    # 2. 表示用データの生成 (1024x1024, 4bitパック = 512KB)
    # ----------------------------------------------------
    # 物理マップの「横方向だけ」を1/1.4117倍（896x1024）にリサイズ
    # ※ドット絵のモアレを防ぐため、ニアレストネイバーではなく線形補間を使用し、後でキャスト
    disp_resized_w = 896
    disp_resized = cv2.resize(phys_map, (disp_resized_w, DISP_H), interpolation=cv2.INTER_LINEAR)
    
    # 1024x1024のキャンバス（初期値：芝生）を作り、中央に配置して左右にマージン（各64ドット）を作る
    disp_map = np.full((DISP_H, DISP_W), ATTR_LAWN, dtype=np.uint8)
    margin_x = (DISP_W - disp_resized_w) // 2  # (1024 - 896) // 2 = 64
    disp_map[:, margin_x:margin_x + disp_resized_w] = disp_resized

    # 2ドットを1バイトにパック (4bit + 4bit)
    # 上位4bitに偶数ドット、下位4bitに奇数ドット
    packed_disp = np.zeros((DISP_H, DISP_W // 2), dtype=np.uint8)
    for y in range(DISP_H):
        high_nibble = disp_map[y, 0::2] << 4
        low_nibble  = disp_map[y, 1::2] & 0x0F
        packed_disp[y, :] = high_nibble | low_nibble

    # 表示用バイナリ書き出し (1024 * 512 = 524,288 bytes)
    with open("course1.grp", "wb") as f:
        f.write(packed_disp.tobytes())
    print("Successfully generated: course1.grp")

if __name__ == "__main__":
    main()
