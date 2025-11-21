# 故障排除指南

## 问题 1: --qext 参数似乎无效

### 验证 QEXT 是否编译进去

运行以下命令检查：
```cmd
opusenc.exe --help | findstr qext
```

应该能看到：
```
--qext             Enable QEXT for frequencies above 20kHz (requires CELT-only mode)
```

### 测试 QEXT 是否工作

#### 方法 1: 使用 --qext 选项

```cmd
opusenc.exe --qext --bitrate 320 --comp 10 input.wav output.opus
```

**注意：** 即使使用 `--qext`，QEXT 也只在特定条件下启用：
- 需要高码率（建议 ≥256 kbps）
- 编码器会自动选择模式（SILK/Hybrid/CELT）
- 只有在 CELT-only 模式下 QEXT 才真正生效

#### 方法 2: 使用 --set-ctl-int 强制启用

```cmd
opusenc.exe --set-ctl-int 4056=1 --bitrate 320 --comp 10 input.wav output.opus
```

其中 `4056` 是 `OPUS_SET_QEXT_REQUEST` 的值。

### 为什么 QEXT 可能不生效？

1. **编码器没有选择 CELT-only 模式**
   - Opus 会根据输入自动选择 SILK（语音）、Hybrid（混合）或 CELT（音乐）
   - QEXT 只在 CELT-only 模式下工作
   - 低码率时通常不会选择 CELT-only

2. **码率太低**
   - QEXT 需要额外的比特来编码扩展频段
   - 建议至少 256 kbps（立体声）

3. **输入文件本身没有 >20kHz 的内容**
   - 如果源文件已经被限制到 20kHz，QEXT 没有内容可编码

### 验证 QEXT 是否真的工作

#### 步骤 1: 准备测试文件

使用有高频内容的文件（如 96kHz 采样率的 FLAC）：
```cmd
ffmpeg -i input_96k.flac -ar 48000 -ac 2 input_48k.wav
```

#### 步骤 2: 编码（带和不带 QEXT）

```cmd
# 不带 QEXT
opusenc.exe --bitrate 320 --comp 10 input_48k.wav output_no_qext.opus

# 带 QEXT
opusenc.exe --qext --bitrate 320 --comp 10 input_48k.wav output_qext.opus
```

#### 步骤 3: 比较文件大小

QEXT 版本通常会稍大一些（如果真的编码了扩展频段）：
```cmd
dir output_*.opus
```

#### 步骤 4: 解码并分析频谱

```cmd
# 解码
opusdec.exe output_qext.opus decoded_qext.wav

# 生成频谱图
ffmpeg -i decoded_qext.wav -lavfi showspectrumpic=s=1920x1080:legend=1 spectrum_qext.png
```

查看频谱图，如果 QEXT 工作，应该能看到 20kHz 以上有内容。

---

## 问题 2: opusdec 解码有底噪

### 可能的原因

1. **Dithering（抖动）**
   - opusdec 默认启用 dithering 来减少量化噪声
   - 这会添加低电平的随机噪声

2. **Resampler 问题**
   - 如果输入和输出采样率不匹配，resampler 可能引入噪声

3. **浮点精度问题**
   - 不同的解码器可能使用不同的精度

### 解决方案

#### 禁用 Dithering

opusdec 没有直接的命令行选项禁用 dithering，但可以修改源码或使用其他解码器。

#### 使用 ffmpeg 解码对比

```cmd
# 用 ffmpeg 解码
ffmpeg -i output.opus -ar 48000 -ac 2 decoded_ffmpeg.wav

# 用 opusdec 解码
opusdec.exe output.opus decoded_opusdec.wav

# 比较
ffmpeg -i decoded_ffmpeg.wav -i decoded_opusdec.wav -filter_complex "[0:a][1:a]amerge=inputs=2,showwaves=s=1920x1080:mode=line" comparison.png
```

#### 检查采样率匹配

确保解码时使用正确的采样率：
```cmd
# 检查 opus 文件信息
ffprobe output.opus

# 解码时指定采样率
opusdec.exe --rate 48000 output.opus decoded.wav
```

#### 测试不同的解码器

```cmd
# opusdec
opusdec.exe input.opus output_opusdec.wav

# ffmpeg
ffmpeg -i input.opus output_ffmpeg.wav

# opus-tools 的 opusrtp（如果有）
# 或使用其他支持 Opus 的工具
```

### 底噪分析

使用音频编辑器（如 Audacity）打开解码文件：
1. 选择静音部分
2. 查看频谱分析
3. 测量噪声底（Noise Floor）

正常的 Opus 解码噪声底应该在 -90dB 以下。如果高于这个值，可能有问题。

---

## 问题 3: 如何确认 QEXT 真的在工作？

### 最可靠的方法：使用 opus_demo

opus_demo 是 Opus 库自带的测试工具，支持强制 CELT-only 模式：

```cmd
# 转换到 PCM
ffmpeg -i input.wav -f s16le -ar 48000 -ac 2 input.pcm

# 编码（CELT-only + QEXT）
opus_demo.exe -e restricted-celt 48000 2 320000 -complexity 10 -bandwidth FB -qext input.pcm output.opus

# 解码
opus_demo.exe -d 48000 2 output.opus decoded.pcm

# 转回 WAV
ffmpeg -f s16le -ar 48000 -ac 2 -i decoded.pcm decoded.wav
```

### 检查编码器输出

编译时添加调试输出，或使用 `--save-range` 选项：
```cmd
opusenc.exe --qext --save-range range.txt --bitrate 320 input.wav output.opus
```

查看 range.txt 文件，看是否有 QEXT 相关的信息。

---

## 推荐的 QEXT 工作流程

### 高质量音乐编码（48kHz）

```cmd
# 1. 准备输入（确保有高频内容）
ffmpeg -i input.flac -ar 48000 -ac 2 input.wav

# 2. 编码（使用 set-ctl-int 强制 QEXT）
opusenc.exe --set-ctl-int 4056=1 --bitrate 384 --comp 10 input.wav output.opus

# 3. 解码
opusdec.exe output.opus decoded.wav

# 4. 验证频谱
ffmpeg -i decoded.wav -lavfi showspectrumpic=s=1920x1080:legend=1:color=intensity spectrum.png
```

### 超高质量（96kHz 源）

如果源文件是 96kHz，使用 opus_demo：

```cmd
# 1. 转换
ffmpeg -i input_96k.flac -f s16le -ar 96000 -ac 2 input.pcm

# 2. 编码
opus_demo.exe -e restricted-celt 96000 2 512000 -complexity 10 -bandwidth FB -qext input.pcm output.opus

# 3. 解码
opus_demo.exe -d 96000 2 output.opus decoded.pcm

# 4. 转回
ffmpeg -f s16le -ar 96000 -ac 2 -i decoded.pcm decoded.wav
```

---

## 调试技巧

### 1. 检查编译定义

确认 ENABLE_QEXT 被定义：
```cmd
# 在 CMakeLists.txt 中应该有：
add_definitions(-DENABLE_QEXT)
```

### 2. 添加调试输出

修改 opusenc.c，在 QEXT 设置后添加 printf：
```c
#ifdef ENABLE_QEXT
  if (enable_qext) {
    ret = ope_encoder_ctl(enc, OPUS_SET_QEXT(1));
    printf("DEBUG: QEXT set, result: %d\n", ret);  // 添加这行
    if (ret != OPE_OK) {
      fprintf(stderr, "Warning: OPUS_SET_QEXT failed: %s\n", ope_strerror(ret));
    }
  }
#endif
```

### 3. 使用 --set-ctl-int 测试

直接用 CTL 值测试：
```cmd
# OPUS_SET_QEXT_REQUEST = 4056
opusenc.exe --set-ctl-int 4056=1 --bitrate 320 input.wav output.opus
```

如果这个方法工作但 `--qext` 不工作，说明是命令行解析的问题。

### 4. 检查返回值

如果 QEXT 设置失败，可能是因为：
- 编码器不支持（libopusenc 版本太旧）
- 模式不兼容（不是 CELT-only）
- 码率太低

---

## 常见问题 FAQ

### Q: 为什么文件大小没有明显变化？

A: QEXT 只编码 >20kHz 的内容。如果源文件本身没有这些频率，QEXT 不会增加文件大小。

### Q: 所有播放器都支持 QEXT 吗？

A: 标准 Opus 播放器可以播放 QEXT 文件，但会忽略扩展频段。只有支持 QEXT 的解码器才能完整解码。

### Q: QEXT 会降低音质吗？

A: 在低码率下可能会，因为比特被分配给了扩展频段。建议只在高码率（≥320 kbps）使用 QEXT。

### Q: 如何知道我的 Opus 库是否支持 QEXT？

A: 检查 opus_defines.h 中是否有 `OPUS_SET_QEXT_REQUEST`。如果有，就支持。

---

## 需要帮助？

如果问题仍未解决，请提供：
1. 完整的命令行
2. opusenc/opusdec 的输出
3. 输入文件的信息（ffprobe 输出）
4. 编译时的 CMake 配置
