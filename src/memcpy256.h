#ifndef __H_MEMCPY256__
#define __H_MEMCPY256__
static inline void memcpy256(void *dst, const void *src, int width_dots, int height_lines, int dst_pitch_bytes) {
    // 💡 お使いの環境（GAS/MIT構文）に合わせて完全に書き直したバースト転送
    // 転送元と転送先の左右の向き、レジスタ名の「%%」、LEAの括弧、即値などをすべて完全適合させています。

    __asm__ __volatile__ (
        "move.l  %0, %%a0\n\t"     // a0 = src
        "move.l  %1, %%a1\n\t"     // a1 = dst
        "move.l  %2, %%d0\n\t"     // d0 = height_lines
        "subq.l  #1, %%d0\n\t"     // dbra用に1引く

        "1:\n\t"
        // -----------------------------------------------------------
        // 64バイト(8長語)バースト × 8回 = 512バイト(横256ドット分)
        // MIT構文では movem.l の向きが [レジスタ, (a1)] ではなく [(a1), レジスタ] になります。
        // またインラインアセンブラ内ではレジスタ名に「%%」を重ねる必要があります。
        // -----------------------------------------------------------
        ".rept 8\n\t"
        "movem.l (%%a0)+, %%d1-%%d7/%%a2\n\t"
        "movem.l %%d1-%%d7/%%a2, (%%a1)\n\t"
        "lea     (%%a1, 32), %%a1\n\t"          // 💡 MIT構文の LEA 記述に修正
        "movem.l (%%a0)+, %%d1-%%d7/%%a2\n\t"
        "movem.l %%d1-%%d7/%%a2, (%%a1)\n\t"
        "lea     (%%a1, 32), %%a1\n\t"          // 💡 MIT構文の LEA 記述に修正
        ".endr\n\t"
        // -----------------------------------------------------------

        // GVRAMの次のラインの先頭へアドレスを調整
        "move.l  %3, %%d1\n\t"     // d1 = dst_pitch_bytes
        "sub.l   #512, %%d1\n\t"   // 💡 subaではなく通常のsubでd1から512を引く(MIT対応)
        "add.l   %%d1, %%a1\n\t"   // 💡 addaではなく通常のaddでa1にd1を足す(MIT対応)

        "dbra    %%d0, 1b\n\t"     // 💡 ローカルラベル「1b」へループ

        : // 出力（なし）
        : "g"(src), "g"(dst), "g"(height_lines), "g"(dst_pitch_bytes)
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "memory"
    );
}

static inline void memcpy256i(void *dst, const void *src, int width_dots, int height_lines, int dst_pitch_bytes) {
    // 💡 1ラインおきにスキップして転送するインターレース（走査線）版バースト転送
    // height_lines には 64 を渡して使用します。

    __asm__ __volatile__ (
        "move.l  %0, %%a0\n\t"     // a0 = src (計算済みの64ライン分が詰まったバッファ)
        "move.l  %1, %%a1\n\t"     // a1 = dst (GVRAMの描画開始アドレス)
        "move.l  %2, %%d0\n\t"     // d0 = height_lines (64が入る)
        "subq.l  #1, %%d0\n\t"     // dbra用に1引く

        "1:\n\t"
        // -----------------------------------------------------------
        // 64バイト(8長語)バースト × 8回 = 512バイト(横256ドット分)を転送
        // -----------------------------------------------------------
        ".rept 8\n\t"
        "movem.l (%%a0)+, %%d1-%%d7/%%a2\n\t"
        "movem.l %%d1-%%d7/%%a2, (%%a1)\n\t"
        "lea     (%%a1, 32), %%a1\n\t"
        "movem.l (%%a0)+, %%d1-%%d7/%%a2\n\t"
        "movem.l %%d1-%%d7/%%a2, (%%a1)\n\t"
        "lea     (%%a1, 32), %%a1\n\t"
        ".endr\n\t"
        // -----------------------------------------------------------

        // 💡 【インターレース補正】
        // 通常は、直前の転送で進んだ512バイト分を dst_pitch_bytes から引いて、
        // 「すぐ次のラインの先頭」へアドレスを合わせていましたが、
        // インターレース版では「1ライン丸ごと黒い筋としてスキップ（放置）」するため、
        // さらに「dst_pitch_bytes（1ライン分のバイト数）」を丸ごと余分に加算して跳び越えます。
        
        "move.l  %3, %%d1\n\t"     // d1 = dst_pitch_bytes
        "sub.l   #512, %%d1\n\t"   // 通常の次ライン先頭への調整幅
        "add.l   %3, %%d1\n\t"     // 🌟 1ライン飛ばすために、もう1ライン分のピッチを上乗せ！
        "add.l   %%d1, %%a1\n\t"   // a1(GVRAM)のアドレスを2ライン先へジャンプさせる

        "dbra    %%d0, 1b\n\t"     // ループ

        : // 出力（なし）
        : "g"(src), "g"(dst), "g"(height_lines), "g"(dst_pitch_bytes)
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "memory"
    );
}
#endif