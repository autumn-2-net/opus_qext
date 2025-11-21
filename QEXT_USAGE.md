# QEXT 使用指南

## ⚠️ 重要提示

QEXT 只在 **CELT-only 模式**下工作。标准的 opusenc 命令行工具**不支持强制 CELT-only 模式**。

## 为什么 --qext 不工作？

当你运行：
```cmd
opusenc.exe --qext --comp=10 --bitrate 256 op.wav tt.opus
```

虽然 `--qext` 选项被识别，但 opusenc 会根据输入自动选择编码模式（SILK/Hybrid/CELT）。如果它没有选择 CELT-only 模式，QEXT 会被内部禁用。

## 解决方案

有两种方法让 QEXT 真正工作：

### 方案 1: 使用 opus_demo（推荐用于 QEXT）

使用你构建的 `opus_demo.exe`（在 opus-main 项目中），它支持 `restricted-celt` 应用模式：

```cmd
# 48kHz, 立体声, 320 kbps, QEXT
opus_demo.exe -e restricted-celt 48000 2 320000 -complexity 10 -bandwidth FB -qext input.pcm output.opus

# 解码
opus_demo.exe -d 48000 2 output.opus decoded.pcm
```

**注意：** opus_demo 使用原始 PCM 文件，不是 WAV。

### 方案 2: 转换 WAV → PCM → 编码

```cmd
# 步骤 1: 用 ffmpeg 转换 WAV 到原始 PCM
ffmpeg -i op.wav -f s16le -ac 2 -ar 48000 op.pcm

# 步骤 2: 用 opus_demo 编码（带 QEXT）
opus_demo.exe -e restricted-celt 48000 2 320000 -complexity 10 -bandwidth FB -qext op.pcm output.opus

# 步骤 3: 解码
opus_demo.exe -d 48000 2 output.opus decoded.pcm

# 步骤 4: 转回 WAV
ffmpeg -f s16le -ar 48000 -ac 2 -i decoded.pcm decoded.wav
```

### 方案 3: 修改 opusenc 源码（高级）

如果你需要 opusenc 支持强制 CELT-only，需要修改源码添加一个选项来设置 `OPUS_SET_FORCE_MODE(MODE_CELT_ONLY)`。

这需要：
1. 添加 `--force-mode` 命令行选项
2. 在创建编码器后调用私有 API `OPUS_SET_FORCE_MODE`
3. 需要包含 `opus_private.h`

**不推荐**，因为这是私有 API，可能在不同版本中变化。

## 为什么 opusenc 不输出信息？

如果 opusenc 启动后立即退出且没有输出，可能的原因：

1. **输入文件格式问题** - 检查 WAV 文件是否有效
2. **缺少 DLL** - 确保所有必要的 DLL 在同一目录或 PATH 中
3. **崩溃** - 可能是构建问题

### 调试步骤：

```cmd
# 1. 测试基本功能（不用 QEXT）
opusenc.exe --bitrate 128 test.wav test.opus

# 2. 检查输入文件
ffprobe test.wav

# 3. 尝试更详细的输出（如果有 --verbose 选项）
opusenc.exe --verbose --bitrate 128 test.wav test.opus
```

## 推荐工作流程（使用 QEXT）

### 完整示例：48kHz 立体声，320 kbps，QEXT

```cmd
# 准备环境
set OPUS_DEMO=C:\Users\autumn\CLionProjects\opus_all\cmake-build-debug\opus_demo.exe
set FFMPEG=ffmpeg

# 1. 转换输入
%FFMPEG% -i input.flac -f s16le -ac 2 -ar 48000 input.pcm

# 2. 编码（QEXT）
%OPUS_DEMO% -e restricted-celt 48000 2 320000 -complexity 10 -bandwidth FB -qext input.pcm output.opus

# 3. 解码
%OPUS_DEMO% -d 48000 2 output.opus output.pcm

# 4. 转回音频格式
%FFMPEG% -f s16le -ar 48000 -ac 2 -i output.pcm output.wav

# 5. 验证频谱（检查是否有 >20kHz 内容）
%FFMPEG% -i output.wav -lavfi showspectrumpic=s=1920x1080:legend=1 spectrum.png
```

### 96kHz 示例（更高频率）

```cmd
# 1. 转换到 96kHz
ffmpeg -i input.flac -f s16le -ac 2 -ar 96000 input_96k.pcm

# 2. 编码（512 kbps 推荐）
opus_demo.exe -e restricted-celt 96000 2 512000 -complexity 10 -bandwidth FB -qext input_96k.pcm output_96k.opus

# 3. 解码
opus_demo.exe -d 96000 2 output_96k.opus output_96k.pcm

# 4. 转回 WAV
ffmpeg -f s16le -ar 96000 -ac 2 -i output_96k.pcm output_96k.wav
```

## QEXT 参数建议

| 采样率 | 声道 | 最小码率 | 推荐码率 | 最佳码率 | 频率范围 |
|--------|------|----------|----------|----------|----------|
| 48kHz  | Mono | 160 kbps | 192 kbps | 256 kbps | 0-24kHz  |
| 48kHz  | Stereo | 256 kbps | 320 kbps | 384 kbps | 0-24kHz  |
| 96kHz  | Mono | 256 kbps | 320 kbps | 448 kbps | 0-48kHz  |
| 96kHz  | Stereo | 384 kbps | 512 kbps | 640 kbps | 0-48kHz  |

## 验证 QEXT 是否工作

### 方法 1: 频谱分析

```cmd
# 生成频谱图
ffmpeg -i decoded.wav -lavfi showspectrumpic=s=1920x1080:legend=1:color=intensity spectrum.png
```

查看频谱图，应该能看到 20kHz 以上有内容（如果源文件有的话）。

### 方法 2: 比较文件大小

QEXT 编码的文件通常比标准 Opus 大 10-20%（相同码率下），因为包含了扩展频段数据。

### 方法 3: 检查编码器输出

opus_demo 会显示编码信息，包括是否使用了 QEXT。

## 常见问题

### Q: 为什么我听不出区别？

A: 
1. 确保源文件本身有 >20kHz 的内容
2. 使用高质量耳机/音响
3. 大多数人听不到 20kHz 以上的频率，但可能感受到"空气感"的差异

### Q: 所有播放器都支持吗？

A: 
- 用你构建的 QEXT 版本解码器：完全支持
- 标准 Opus 播放器：可以播放，但只有 0-20kHz（忽略扩展）
- 质量可能略低于不开 QEXT 的相同码率（因为比特分配给了扩展）

### Q: 什么时候应该使用 QEXT？

A:
- ✅ 高质量音乐存档
- ✅ 专业音频工作流
- ✅ 端到端受控环境
- ❌ 流媒体到未知播放器
- ❌ 低码率场景
- ❌ 源文件已经限制到 20kHz

## 总结

- **opusenc + --qext**: 选项存在但不会真正启用 QEXT（需要 CELT-only 模式）
- **opus_demo + restricted-celt + -qext**: 正确的 QEXT 使用方式
- **推荐码率**: ≥320 kbps @ 48kHz 立体声
- **验证方法**: 频谱分析查看 >20kHz 内容

如果你需要一个"一键式"的 QEXT 编码工具，可以考虑写一个包装脚本来自动化上述流程。
