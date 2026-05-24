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
    if len(contour_pts) < 2:
        return contour_pts
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
    print("Generating perfectly aligned kerb stripes (Outer/Inner Separation)...")
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
    # ★【修正の核心】外側・内側のフチを完全に分離してストライプを引く
    # ----------------------------------------------------
    road_contour_img = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
    road_contour_img[road_mask] = 255
    
    # RETR_TREE で階層構造として輪郭を抽出する
    # これにより、ドーナツ状のコースの「外周（最外郭）」と「内周（穴）」が完全に分離されます
    contours, hierarchy = cv2.findContours(road_contour_img, cv2.RETR_TREE, cv2.CHAIN_APPROX_NONE)
    
    if len(contours) >= 1:
        outer_contour_pts = None
        inner_contour_pts = None
        
        # 通常、もっとも面積が大きいのが外周、その次（または最外郭の子階層）が内周になります
        # 安全のために面積ベースで上位2つを仕分けます
        sorted_contours = sorted(contours, key=cv2.contourArea, reverse=True)
        
        outer_contour_pts = sorted_contours[0].reshape(-1, 2)
        if len(sorted_contours) > 1:
            inner_contour_pts = sorted_contours[1].reshape(-1, 2)
            
        # それぞれを2ピクセル等間隔の綺麗な独立パスへリサンプリング
        step_pixel = 2.0
        equidistant_outer = resample_contour_equidistant(outer_contour_pts, step=step_pixel)
        
        if inner_contour_pts is not None:
            equidistant_inner = resample_contour_equidistant(inner_contour_pts, step=step_pixel)
        else:
            equidistant_inner = equidistant_outer  # 万が一内周がない場合のセーフティ

        # 独立したそれぞれのフチに対する距離マップ（マスク側ピクセルがどちらに所属するかを判定用）
        outer_img = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
        cv2.drawContours(outer_img, [sorted_contours[0]], -1, 255, 1)
        dist_to_outer = cv2.distanceTransform(255 - outer_img, cv2.DIST_L2, 5)
        
        if len(sorted_contours) > 1:
            inner_img = np.zeros((PHYS_H, PHYS_W), dtype=np.uint8)
            cv2.drawContours(inner_img, [sorted_contours[1]], -1, 255, 1)
            dist_to_inner = cv2.distanceTransform(255 - inner_img, cv2.DIST_L2, 5)
        else:
            dist_to_inner = np.full((PHYS_H, PHYS_W), 9999.0, dtype=np.float32)

        # 縁石マスク内のすべてのピクセル座標を取得
        kerb_y, kerb_x = np.where(kerb_mask)
        
        print("Mapping kerb pixels to separated equidistant contours...")
        
        for y, x in zip(kerb_y, kerb_x):
            # この縁石ピクセルが「外周のフチ」と「内周のフチ」のどっちに近いか判定
            if dist_to_outer[y, x] <= dist_to_inner[y, x]:
                # 外周（アウター側）のパスから一番近い点を探す
                distances = (equidistant_outer[:, 0] - x) ** 2 + (equidistant_outer[:, 1] - y) ** 2
                closest_idx = np.argmin(distances)
                actual_distance_pixels = closest_idx * step_pixel
            else:
                # 内周（インナー側）のパスから一番近い点を探す
                distances = (equidistant_inner[:, 0] - x) ** 2 + (equidistant_inner[:, 1] - y) ** 2
                closest_idx = np.argmin(distances)
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
    print("Kerb lines fixed perfectly with Outer/Inner separation!")

if __name__ == "__main__":
    main()