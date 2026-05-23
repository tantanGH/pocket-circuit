# elf2x68k インストールパス
ifndef ELF2X68K_DIR
	ELF2X68K_DIR=/opt/homebrew/Cellar/elf2x68k/20260331
endif

# デフォルトサフィックスを削除
.SUFFIXES:

# バージョン
VERSION = 010

# 実行ファイル名
TARGET_FILE_000 = POCKETCT.X
TARGET_FILE_060 = POCKETCT060.X

# ドキュメントファイル名
DOCUMENT_FILE = POCKETCT.TXT

# アセットファイル名
ASSET_FILES = course1.dat course1.grp

# 配布パッケージファイル名 
PACKAGE_FILE = ../PCTCT$(VERSION).ZIP

# 各種コマンド短縮名
CC = m68k-xelf-gcc
AS = m68k-xelf-as

# ヘッダ検索パス
INCLUDE_FLAGS = -I${ELF2X68K_DIR}/lib/gcc/m68k-elf/13.4.0/include \
				-I${ELF2X68K_DIR}/m68k-elf/include \
				-I../libs/libajoy/include

# リンカオプションとライブラリ検索パス
LDFLAGS =  -Wl,-s -Wl,--gc-sections -L../libs/libajoy/lib

# *.c ソースファイル
C_SRCS = main.c

# *.s ソースファイル
ASM_SRCS = 

# *.h source files
HEADER_SRCS = keyboard.h crtc.h pattern.h gpalette.h sincos.h pattern.h pocketct.h

# リンク対象のライブラリファイル
LIBS = -lajoy

# 中間ファイル生成用ディレクトリ
INTERMEDIATE_DIR_000 = _build000
INTERMEDIATE_DIR_060 = _build060

# オブジェクトファイル
OBJS_000 =	$(addprefix $(INTERMEDIATE_DIR_000)/,$(patsubst %.c,%.o,$(C_SRCS))) \
	   	    $(addprefix $(INTERMEDIATE_DIR_000)/,$(patsubst %.s,%.o,$(ASM_SRCS)))
OBJS_060 =	$(addprefix $(INTERMEDIATE_DIR_060)/,$(patsubst %.c,%.o,$(C_SRCS))) \
	   	    $(addprefix $(INTERMEDIATE_DIR_060)/,$(patsubst %.s,%.o,$(ASM_SRCS)))

# コンパイルフラグ
COMMON_FLAGS_000 = -m68000 -O2 -msoft-float -specs=nano.specs $(INCLUDE_FLAGS) -fno-defer-pop
CFLAGS_000 = $(COMMON_FLAGS_000)
COMMON_FLAGS_060 = -m68060 -O2 -msoft-float -specs=nano.specs $(INCLUDE_FLAGS) -fno-defer-pop
CFLAGS_060 = $(COMMON_FLAGS_060)

# デフォルトのターゲット
all : ${INTERMEDIATE_DIR_000}/$(TARGET_FILE_000) ${INTERMEDIATE_DIR_060}/$(TARGET_FILE_060)

# 中間生成物の削除
clean : 
	rm -f $(PACKAGE_FILE)
	rm -rf $(INTERMEDIATE_DIR_000)
	rm -rf $(INTERMEDIATE_DIR_060)

# 実行ファイルの生成
${INTERMEDIATE_DIR_000}/$(TARGET_FILE_000) : $(OBJS_000)
	mkdir -p $(INTERMEDIATE_DIR_000)
	$(CC) $(COMMON_FLAGS_000) $(LDFLAGS) -o ${INTERMEDIATE_DIR_000}/${TARGET_FILE_000} $(OBJS_000) $(LIBS)
${INTERMEDIATE_DIR_060}/$(TARGET_FILE_060) : $(OBJS_060)
	mkdir -p $(INTERMEDIATE_DIR_060)
	$(CC) $(COMMON_FLAGS_060) $(LDFLAGS) -o ${INTERMEDIATE_DIR_060}/${TARGET_FILE_060} $(OBJS_060) $(LIBS)

# *.c ソースのコンパイル
$(INTERMEDIATE_DIR_000)/%.o : %.c Makefile $(HEADER_SRCS)
	mkdir -p $(INTERMEDIATE_DIR_000)
	$(CC) $(CFLAGS_000) -c $*.c -o $(INTERMEDIATE_DIR_000)/$*.o
$(INTERMEDIATE_DIR_060)/%.o : %.c Makefile $(HEADER_SRCS)
	mkdir -p $(INTERMEDIATE_DIR_060)
	$(CC) $(CFLAGS_060) -c $*.c -o $(INTERMEDIATE_DIR_060)/$*.o

# 配布用パッケージ作成
package:
	zip -j ${PACKAGE_FILE} \
			${DOCUMENT_FILE} \
			${INTERMEDIATE_DIR_000}/${TARGET_FILE_000} \
			${INTERMEDIATE_DIR_060}/${TARGET_FILE_060} \
			${ASSET_FILES}