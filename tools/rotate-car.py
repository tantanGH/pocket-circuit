from PIL import Image
import os

def generate_clean_transparent_map(input_image_path, output_filename="car_rotation_map_clean.png", threshold=20):
    # 1. 元画像の読み込み
    img = Image.open(input_image_path)
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    w, h = img.size
    print(f"Original Image Size: {w}x{h}")
    
    # 核心①：一番左上(0,0)のドットの色を基準（ベース背景色）にする
    base_color = img.getpixel((0, 0))
    print(f"Base Background Color: {base_color[:3]}")
    print(f"Applying Color Threshold: {threshold}")

    # 核心②：全ピクセルを走査し、基準色に近い色をすべて完全透明にする
    new_data = []
    datas = img.getdata()
    
    for item in datas:
        # item[0]=R, item[1]=G, item[2]=B
        # 基準色との「差の絶対値」をそれぞれ計算する
        diff_r = abs(item[0] - base_color[0])
        diff_g = abs(item[1] - base_color[1])
        diff_b = abs(item[2] - base_color[2])
        
        # RGBすべての差が、指定した許容値（threshold）以下なら背景とみなす
        if diff_r <= threshold and diff_g <= threshold and diff_b <= threshold:
            new_data.append((0, 0, 0, 0)) # 完全透明（RGBAすべて0）
        else:
            new_data.append(item) # 車体本体はそのまま保持
            
    # 透明化したデータに差し替え
    img.putdata(new_data)
    
    # 2. 短辺を基準に、中央を正方形にクリッピング
    short_side = min(w, h)
    left = (w - short_side) // 2
    top = (h - short_side) // 2
    right = left + short_side
    bottom = top + short_side
    
    square_img = img.crop((left, top, right, bottom))
    
    # 3. 縦長キャンバスの作成
    ANGLES = 32
    canvas_w = short_side
    canvas_h = short_side * ANGLES
    stitched_image = Image.new('RGBA', (canvas_w, canvas_h), (0, 0, 0, 0))
    
    # 4. 32回まわしながらペタペタ貼り付け
    for i in range(ANGLES):
        angle_deg = -i * (360.0 / ANGLES)
        
        rotated_img = square_img.rotate(
            angle=angle_deg, 
            resample=Image.Resampling.BILINEAR, 
            expand=False
        )
        
        paste_y = i * short_side
        # 自身をマスクとして指定して透過貼り付け
        stitched_image.paste(rotated_img, (0, paste_y), rotated_img)
        
    # 5. 保存
    stitched_image.save(output_filename)
    print(f"Successfully saved clean map as '{output_filename}'!")

if __name__ == "__main__":
    input_file = "car1.png" 
    
    if os.path.exists(input_file):
        # threshold=20 で消えないノイズがある場合は、25 や 30 に上げてみてください
        # 逆に車の赤いボディまで消え始めたら、15 などに下げてください
        generate_clean_transparent_map(input_file, threshold=20)
    else:
        print(f"Error: {input_file} not found.")