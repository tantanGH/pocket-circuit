import numpy as np
import cv2
import math

# 物理世界と表示世界の仕様
PHYS_W, PHYS_H = 1440, 1024
DISP_W, DISP_H = 1024, 1024

# パレット・属性値（下位3ビット: 0〜7）
COLOR_LAWN1  = 1; COLOR_LAWN2  = 2; COLOR_ROAD   = 3
COLOR_KERB_W = 4; COLOR_KERB_R = 5; COLOR_GRAVEL = 6
COLOR_LAWN1_H = 9; COLOR_LAWN2_H = 10; COLOR_ROAD_H = 11  # ★11を明るいスタートラインに使用
COLOR_KERB_W_H = 12; COLOR_KERB_R_H = 13; COLOR_GRAVEL_H = 14

def resample_contour_equidistant(contour_pts, step=2.0):
    if len(contour_pts) < 2:
        return contour_pts
    pts = np.vstack([contour_pts, contour_pts[0]])
    deltas = np.diff(pts, axis=0)
    segment_lengths = np.sqrt((deltas ** 2).sum(axis=1))
    cum_dist = np.insert(np.cumsum(segment_lengths), 0, 0.0)
    total_length = cum_dist[-1]
    target_dists = np.arange(0, total_length, step)
    resampled_x = np.interp(target_dists, cum_dist, pts[:, 0])
    resampled_y = np.interp(target_dists, cum_dist, pts[:, 1])
    return np.column_stack((resampled_x, resampled_y))

def main():
    print("Generating perfectly aligned kerb stripes with Checkpoints...")
    phys_map = np.full((PHYS_H, PHYS_W), COLOR_LAWN1, dtype=np.uint8)

    # 制御点
    control_points = [
        (200, 850), (500, 850), (900, 850), (1250, 850),
        (1300, 550), (1050, 250), (750, 300), (800, 550),
        (500, 500), (350, 200), (150, 500),
    ]
    
    # スプライン芯線の生成
    scale = 4
    mask_h, mask_w = PHYS_H // scale, PHYS_W // scale
    low_res_track = np.zeros((mask_h, mask_w), dtype=np.uint8)
    scaled_pts = np.array([[(pt[0] // scale, pt[1] // scale)] for pt in control_points], dtype=np.int32)
    cv2.polylines(low_res_track, [scaled_pts], isClosed=True, color=255, thickness=1, lineType=cv2.LINE_AA)
    smooth_line = cv2.resize(low_res_track, (PHYS_W, PHYS_H), interpolation=cv2.INTER_CUBIC)
    
    road_width = 130
    kerb_width = 14
    
    # グラベル配置
    gravel_zones = [(1350, 550), (350, 150), (100, 500)]
    for center in gravel_zones:
        cv2.circle(phys_map, center, 180, COLOR_GRAVEL, -1)

    # 距離変換マップ
    _, binary_line = cv2.threshold(smooth_line, 10, 255, cv2.THRESH_BINARY)
    dist_map = cv2.distanceTransform(255 - binary_line, cv2.DIST_L2, 5)
    
    # 各エリアのマスク作成
    road_mask = dist_map <= (road_width // 2)
    kerb_mask = (dist_map > (road_width // 2)) & (dist_map <= (road_width // 2 + kerb_width))
    black_line_mask = (dist_map > (road_width // 2 - 2)) & (dist_map <= (road_width // 2))

    # 道路をプロット
    phys_map[road_mask] = COLOR_ROAD

    # 縁石（ストライプ）の分離生成処理
    road_contour_img = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
    road_contour_img[road_mask] = 255
    contours, hierarchy = cv2.findContours(road_contour_img, cv2.RETR_TREE, cv2.CHAIN_APPROX_NONE)
    
    if len(contours) >= 1:
        sorted_contours = sorted(contours, key=cv2.contourArea, reverse=True)
        outer_contour_pts = sorted_contours[0].reshape(-1, 2)
        if len(sorted_contours) > 1:
            inner_contour_pts = sorted_contours[1].reshape(-1, 2)
        
        step_pixel = 2.0
        equidistant_outer = resample_contour_equidistant(outer_contour_pts, step=step_pixel)
        if inner_contour_pts is not None:
            equidistant_inner = resample_contour_equidistant(inner_contour_pts, step=step_pixel)
        else:
            equidistant_inner = equidistant_outer

        outer_img = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
        cv2.drawContours(outer_img, [sorted_contours[0]], -1, 255, 1)
        dist_to_outer = cv2.distanceTransform(255 - outer_img, cv2.DIST_L2, 5)
        
        if len(sorted_contours) > 1:
            inner_img = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
            cv2.drawContours(inner_img, [sorted_contours[1]], -1, 255, 1)
            dist_to_inner = cv2.distanceTransform(255 - inner_img, cv2.DIST_L2, 5)
        else:
            dist_to_inner = np.full((PHYS_H, PHYS_W), 9999.0, dtype=np.float32)

        kerb_y, kerb_x = np.where(kerb_mask)
        for y, x in zip(kerb_y, kerb_x):
            if dist_to_outer[y, x] <= dist_to_inner[y, x]:
                distances = (equidistant_outer[:, 0] - x) ** 2 + (equidistant_outer[:, 1] - y) ** 2
                closest_idx = np.argmin(distances)
                actual_distance_pixels = closest_idx * step_pixel
            else:
                distances = (equidistant_inner[:, 0] - x) ** 2 + (equidistant_inner[:, 1] - y) ** 2
                closest_idx = np.argmin(distances)
                actual_distance_pixels = closest_idx * step_pixel
            
            sector = (int(actual_distance_pixels) // 32) % 2
            if sector == 0:
                phys_map[y, x] = COLOR_KERB_R
            else:
                phys_map[y, x] = COLOR_KERB_W

    # 道路フチの黒線
    phys_map[black_line_mask] = 0
    
    # ====================================================================
    # ★【ハイブリッド版】ゲート1は垂直ライン、ゲート2〜4は確実な円形（丸）
    # ====================================================================
    print("Generating Hybrid Checkpoint Gates (Line & Circles)...")
    
    # ゲート1だけは今まで通りラインで引く (始点x, y, 終点x, y)
    # スタートライン(X=350)を垂直にまたぐ
    gate1_line = (350, 750, 350, 1000)
    
    # ゲート2、3、4は「中心座標(x,y)」と「半径(r)」で管理する（丸型座布団）
    # ドリフトではみ出しても絶対に踏めるよう、半径150〜180ピクセルの大きめの丸にします
    gate_circles = {
        2: ((1250, 550), 160),  # ゲート2: 第1コーナー抜けた先の東側ストレート
        3: ((750, 350),  180),  # ゲート3: 北側のヘアピンの頂点付近
        4: ((250, 350),  160)   # ゲート4: 最終コーナー手前の西側
    }

    # ---- 1. ゲート1（スタートライン）の描画 ----
    gate_mask1 = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
    cv2.line(gate_mask1, (gate1_line[0], gate1_line[1]), (gate1_line[2], gate1_line[3]), 255, thickness=40)
    # 道路の芯線から離れすぎないようにクリップ（芝生・縁石含む）
    target_pixels1 = (gate_mask1 == 255) & (dist_map <= 100)
    
    # 見栄えのため、道路上の部分だけを明るい道路色(11)にする
    phys_map[target_pixels1 & road_mask] = COLOR_ROAD_H
    # 物理マップの上位4ビットにゲートID=1を書き込み
    phys_map[target_pixels1] |= (1 << 4)


    # ---- 2. ゲート2、3、4（円形チェックエリア）の描画 ----
    for gate_id, (center, radius) in gate_circles.items():
        gate_mask_c = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
        # 指定座標に、判定用の真ん丸を描く
        cv2.circle(gate_mask_c, center, radius, 255, -1)
        
        # コース外に広がりすぎないよう、こちらもコース芯線から一定距離（例: 160px以内）にクリップ
        # これにより、隣の無関係な道路にハミ出すのを防ぎます
        target_pixels_c = (gate_mask_c == 255) & (dist_map <= 160)
        
        # 物理マップの上位4ビットにゲートID（2〜4）を合成
        # （ゲート2〜4は road_mask でクリップしないので、縁石や草の上でも100%踏めます！）
        phys_map[target_pixels_c] |= (gate_id << 4)

    # ====================================================================

    # ヘッダー作成
    start_x = 350 - 60
    start_y = 850
    start_angle_32 = 0

    header = bytearray(32)
    header[0:4] = b'MAP1'
    header[4:6] = int(start_x).to_bytes(2, 'big')
    header[6:8] = int(start_y).to_bytes(2, 'big')
    header[8:10] = int(start_angle_32).to_bytes(2, 'big')

    with open("course1.dat", "wb") as f:
        f.write(header)
        f.write(phys_map.tobytes())

    # ====================================================================
    # ★【修正版】表示用データの生成 (.grp 用)
    # ====================================================================
    disp_resized_w = DISP_H
    disp_resized = cv2.resize(phys_map, (disp_resized_w, DISP_H), interpolation=cv2.INTER_NEAREST)
    
    # 上位4ビットのゲート情報をクリアし、輝度情報を含む「下位4ビット（0x0F）」のみを残す！
    # スタートラインの 11 (COLOR_ROAD_H) もそのまま綺麗に残ります
    disp_resized = disp_resized & 0x0F

    disp_map = np.full((DISP_H, DISP_W), COLOR_LAWN1, dtype=np.uint8)
    margin_x = (DISP_W - disp_resized_w) // 2
    disp_map[:, margin_x:margin_x + disp_resized_w] = disp_resized

    # （以下、元のランダムノイズ・4bitパック処理へ続く）

    print("Applying random grain noise...")
    rand_matrix = np.random.rand(DISP_H, DISP_W)
    is_lawn = (disp_map == COLOR_LAWN1)
    disp_map[is_lawn & (rand_matrix < 0.25)] = COLOR_LAWN1_H
    disp_map[is_lawn & (rand_matrix >= 0.25) & (rand_matrix < 0.50)] = COLOR_LAWN2
    disp_map[is_lawn & (rand_matrix >= 0.50) & (rand_matrix < 0.75)] = COLOR_LAWN2_H

    is_gravel = (disp_map == COLOR_GRAVEL)
    disp_map[is_gravel & (rand_matrix < 0.50)] = COLOR_GRAVEL_H

    # 4bitパック
    packed_disp = np.zeros((DISP_H, DISP_W // 2), dtype=np.uint8)
    for y in range(DISP_H):
        packed_disp[y, :] = (disp_map[y, 0::2] << 4) | (disp_map[y, 1::2] & 0x0F)

    with open("course1.grp", "wb") as f:
        f.write(packed_disp.tobytes())
    print("Successfully generated track with 4 encoded checkpoint gates!")

if __name__ == "__main__":
    main()
