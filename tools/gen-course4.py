import numpy as np
import cv2
import math

# 物理世界と表示世界の仕様
PHYS_W, PHYS_H = 1440, 1024
DISP_W, DISP_H = 1024, 1024

# パレット・属性値
COLOR_LAWN1  = 1; COLOR_LAWN2  = 2; COLOR_ROAD   = 3
COLOR_KERB_W = 4; COLOR_KERB_R = 5; COLOR_GRAVEL = 6
COLOR_LAWN1_H = 9; COLOR_LAWN2_H = 10; COLOR_ROAD_H = 11
COLOR_KERB_W_H = 12; COLOR_KERB_R_H = 13; COLOR_GRAVEL_H = 14

def resample_contour_equidistant(contour_pts, step=2.0):
    """
    不均等な輪郭点列を、物理的に等間隔（デフォルト2ピクセル）の点列に再サンプリングする
    """
    # 閉路にするために終点と始点を繋ぐ
    pts = np.vstack([contour_pts, contour_pts[0]])
    
    # 各点間の距離を計算
    deltas = np.diff(pts, axis=0)
    segment_lengths = np.sqrt((deltas ** 2).sum(axis=1))
    
    # 始点からの累積距離を計算
    cum_dist = np.insert(np.cumsum(segment_lengths), 0, 0.0)
    total_length = cum_dist[-1]
    
    # 等間隔なサンプリングターゲット距離の配列を作成
    target_dists = np.arange(0, total_length, step)
    
    # 各ターゲット距離に対応する座標を線形補間
    resampled_x = np.interp(target_dists, cum_dist, pts[:, 0])
    resampled_y = np.interp(target_dists, cum_dist, pts[:, 1])
    
    return np.column_stack((resampled_x, resampled_y))

def main():
    print("Generating perfectly aligned kerb stripes (Equidistant Resampling)...")
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

    # ----------------------------------------------------
    # ★【修正の核心】等間隔化された輪郭パスから赤白を決定する
    # ----------------------------------------------------
    road_contour_img = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
    road_contour_img[road_mask] = 255
    contours, _ = cv2.findContours(road_contour_img, cv2.RETR_TREE, cv2.CHAIN_APPROX_NONE)
    
    if len(contours) > 0:
        main_contour = max(contours, key=cv2.contourArea)
        contour_pts = main_contour.reshape(-1, 2)
        
        # 1. 輪郭の生データを「物理的に2ピクセル等間隔」のきれいなパスにリサンプリング
        # これにより、インデックスの進みが実距離（ピクセル）と完全に一致します
        step_pixel = 2.0
        equidistant_contour = resample_contour_equidistant(contour_pts, step=step_pixel)
        
        # 縁石マスク内のすべてのピクセル座標を取得
        kerb_y, kerb_x = np.where(kerb_mask)
        
        print("Mapping kerb pixels to equidistant contour distances...")
        
        # メモリ効率と速度向上のため、チャンク分け、または一括で最近傍計算を行います
        # 縁石ピクセルの数が多いので、ブロードキャストではなく全点ループを最適化するか
        # 以下の方法で高速にargminを求めます
        for y, x in zip(kerb_y, kerb_x):
            # 等間隔パスの各点との二乗距離を計算
            distances = (equidistant_contour[:, 0] - x) ** 2 + (equidistant_contour[:, 1] - y) ** 2
            closest_idx = np.argmin(distances)
            
            # 実際の物理距離（ピクセル換算）を算出
            # インデックスが1進む＝2ピクセル進むという意味になる
            actual_distance_pixels = closest_idx * step_pixel
            
            # 32ドット（ピクセル）周期で赤白を決定
            sector = (int(actual_distance_pixels) // 32) % 2
            
            if sector == 0:
                phys_map[y, x] = COLOR_KERB_R  # 完全に単色の赤 (5)
            else:
                phys_map[y, x] = COLOR_KERB_W  # 完全に単色の白 (4)

    # 道路フチの黒線で引き締め
    phys_map[black_line_mask] = 0

    # 物理バイナリ保存
    with open("course1.dat", "wb") as f:
        f.write(phys_map.tobytes())

    # 表示用データの生成 (1024x1024 キャンバス構築 ＆ マージンへのノイズ)
    disp_resized_w = DISP_H
    disp_resized = cv2.resize(phys_map, (disp_resized_w, DISP_H), interpolation=cv2.INTER_NEAREST)
    
    disp_map = np.full((DISP_H, DISP_W), COLOR_LAWN1, dtype=np.uint8)
    margin_x = (DISP_W - disp_resized_w) // 2
    disp_map[:, margin_x:margin_x + disp_resized_w] = disp_resized

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
    print("Kerb lines fixed beautifully with exact spacing!")

if __name__ == "__main__":
    main()