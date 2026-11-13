# 索引色 PNG（PLTE + tRNS）缺陷修复报告

所有产物、复现脚本和比对工具都在 `repro/` 下；测试语料在
`tests/palette_corpus/`，自包含回归测试在 `tests/palette_index_test.c`。

## 1. 根因分析

索引色（PNG color type 3）解码时，spng 先把 PLTE/tRNS 预处理进一张
256 项的定长查找表 `ctx->decode_plte`，再用帧内的索引字节直接查表。

- **越界索引静默出错（核心缺陷）**：`expand_row()` 与
  `spng_decode_scanline()` 内联路径都没有把索引与 `plte.n_entries`
  比较。当帧内索引 >= PLTE 实际条目数时，索引落到 LUT 中未使用的槽位。
  因为整个 `spng_ctx` 是 `calloc` 出来的，这些槽位恰好是零，于是越界像素
  被悄悄输出成黑色不透明 `000000ff`，解码仍返回成功。它不触发 ASAN
  （读取仍在定长 union 内），但属于规范禁止的“越界语义读”。
- **右边缘彩边**：当越界索引出现在一行最后一个像素（亚字节打包、宽度
  非 8 对齐时最常见），右边缘就出现了这个本不该存在的黑色/异色像素。
  根因同样是缺少边界校验，而不是 padding 位处理错误（padding 位本身
  一直被正确忽略，见下方对照）。
- **tRNS 预处理越界**：预处理循环固定 `for(i=0;i<256;i++)`，对
  `i >= n_entries` 的不存在条目也执行 tRNS/缩放写入，依赖零初始化内存
  才“看起来正常”。tRNS 对有效条目的逐索引 alpha 逻辑本身是正确的
  （按索引取 `type3_alpha[i]`，缺失项回退 255），不存在按 RGB 匹配的
  串色，但循环边界必须收紧。

## 2. 修复内容（`spng/spng.c`）

1. `expand_row()` 改为返回 `int`，新增 `n_entries` 参数；RGBA8/RGB8
   标量循环对每个索引做 `entry >= n_entries` 校验，命中即返回
   `SPNG_EPLTE_IDX`。
2. ARM NEON 快路径在调用 SIMD 查表前先整行预检全部索引（SIMD 直接索引
   256 项 LUT，无法在向量内判断），再走原有向量化展开；尾部标量回退
   同样逐索引校验。
3. `spng_decode_scanline()` 的亚字节（1/2/4-bit）与 RGBA16 内联路径，
   在取 `get_sample()` 之后、查表之前统一加边界检查；错误经
   `decode_err()` 传播（上下文置为 INVALID，后续调用安全失败）。
4. 预处理循环收紧为 `for(i=0;i<plte.n_entries;i++)` 只处理真实条目；
   LUT 未使用的尾槽显式填安全默认（RGB=0、alpha=255），不再依赖
   calloc 零值。

错误码复用已定义但此前从未返回的 `SPNG_EPLTE_IDX`
（"invalid palette (PLTE) index"，值 35）。`SPNG_FMT_PNG`/`SPNG_FMT_RAW`
是索引透传格式、根本不查调色板表，按设计仍返回成功。

## 3. 复现图（`repro/` 与 `tests/palette_corpus/`）

有效图（必须与参考解码器逐像素一致）：

- `case1_trns_bleed.png` / `case1_trns_bleed_il.png`：4-bit、4 项 PLTE、
  每索引独立 alpha；条目 0/1 的 RGB 完全相同而 alpha 不同（0 vs 128），
  用于证明按索引而非按 RGB 上透明。含 Adam7 隔行版本。
- `case2_edge_fringe.png`（1-bit，宽 11，末字节 7 个 padding 位）、
  `case2b_edge_2bit.png`（2-bit，宽 7）：右边缘/padding 回归。
- `case4_short_trns.png`：tRNS 比 PLTE 短，缺失项须为 alpha 255。

畸形图（必须返回 `SPNG_EPLTE_IDX`，零越界读）：

- `case3_bad_index.png`（4-bit 含索引 3，PLTE 仅 2 项）及隔行版
  `case3_bad_index_il.png`；`case3b_bad_index_8bit.png`（8-bit 索引 200）；
  `case3c_bad_last.png` 与 `edge_bad.png`（越界索引位于最右像素）。

参考解码器 `ref_decode.py` 严格按 PNG 规范实现（5 种 filter、Adam7、
tRNS 逐索引、越界即报错、padding 位忽略），输出 RGBA8。

## 4. 修复前 vs 修复后比对数据

`repro/report_data.txt`（orig = 未修改的 v0.7.4，fixed = 当前修复版）：

| 图像 | 参考解码器 | orig 返回 | fixed 返回 | fixed 像素 |
|---|---|---|---|---|
| case1_trns_bleed(.il) | 接受 | 0 | 0 | IDENTICAL |
| case2_edge_fringe(.il) | 接受 | 0 | 0 | IDENTICAL |
| case2b_edge_2bit | 接受 | 0 | 0 | IDENTICAL |
| case4_short_trns | 接受 | 0 | 0 | IDENTICAL |
| case3_bad_index(.il) | 拒绝 | **0（静默出错）** | **35** | 已拒绝 |
| case3b_bad_index_8bit | 拒绝 | **0** | **35** | 已拒绝 |
| case3c_bad_last / edge_bad | 拒绝 | **0** | **35** | 已拒绝 |

回归与一致性汇总：

- 官方 PngSuite 全部 63 张索引色 PNG：**63/63 与参考逐像素一致**。
- 随机生成有效图（深度 1/2/4/8 × 隔行/非隔行 × tRNS 无/全长/短表，
  共 48 张）：**48/48 逐像素一致**。
- 三种展开格式分别比对上述 111 张：RGBA8、RGB8、RGBA16 均 **111/111 一致**。
- 随机畸形索引图 40 张（越界索引含末像素、覆盖全部位深与隔行）：
  **40/40 返回 SPNG_EPLTE_IDX**。
- ASAN+UBSAN 下 fuzz 目标跑全部畸形语料：**零崩溃、零越界读**
  （唯一 UBSan 报警来自第三方 miniz 内部的既有对齐问题，与本改动无关）。
- `tests/palette_index_test.c`：10/10 通过（已接入 `tests/meson.build`）。

## 5. 如何重跑

环境无 zlib 开发头，使用仓库 meson 本来就支持的 miniz 回退（miniz 2.2.0
已下载到 `repro/`）：

```
cd repro
# 回归测试
gcc -O2 -g -DSPNG_USE_MINIZ -I. -I../spng ../tests/palette_index_test.c \
    ../spng/spng.c miniz.c miniz_tdef.c miniz_tinfl.c miniz_zip.c -lm -o pal_test
./pal_test
# 像素比对
python3 gen_png.py && python3 fuzz_gen.py && python3 fuzz_bad.py
./harness batch/*.png ../tests/images/*3p*.png
python3 cmp_batch.py && python3 cmp_suite.py
```
