Assets Builder Tool - 资源文件生成工具
====================================================

## Overview (概述)

这个工具允许您轻松创建自定义的 `assets.bin` 文件，用于更改 ESP32 设备上的背景图片。

This tool allows you to easily create custom `assets.bin` files to change background images on ESP32 devices.

## Features (功能)

✓ 支持自定义亮色背景图片 (Light background image support)
✓ 支持自定义暗色背景图片 (Dark background image support)  
✓ 支持自定义字体文件 (Custom font file support)
✓ 自动生成 assets.bin 二进制文件 (Auto-generate assets.bin)
✓ 支持多种图片格式 (PNG, JPG, BMP, GIF)
✓ 直观的图形化界面 (User-friendly GUI)
✓ 配置预览功能 (Configuration preview)

## Installation (安装)

### Requirements (需求)

```bash
# Python 3.7+
python --version

# Install required package
pip install pillow
```

### Setup

1. 确保您有 Python 3.7 或更高版本
2. 安装依赖包：`pip install pillow`
3. 运行脚本：`python scripts/assets_builder_simple.py`

## Usage (使用方法)

### GUI Mode (图形界面模式)

```bash
# Run the GUI application
python scripts/assets_builder_simple.py
```

### Steps (步骤)

1. **Settings Tab (设置标签)**
   - Click "Browse" for Light Background Image (点击"Browse"选择亮色背景)
   - Click "Browse" for Dark Background Image (点击"Browse"选择暗色背景)
   - Click "Browse" for Custom Font File (点击"Browse"选择自定义字体)
   - Select Output Directory (选择输出目录)

2. **Preview Tab (预览标签)**
   - View your current configuration (查看当前配置)
   - Check image resolution and format (检查图片分辨率和格式)

3. **Info Tab (信息标签)**
   - Read detailed documentation (阅读详细文档)

4. **Build (构建)**
   - Click "Build Assets.bin" button (点击"生成 Assets.bin"按钮)
   - Wait for the build to complete (等待生成完成)
   - Open output folder to see results (打开输出文件夹查看结果)

## Supported Formats (支持的格式)

### Image Formats (图片格式)
- PNG (.png) - 推荐 (Recommended)
- JPEG (.jpg, .jpeg)
- BMP (.bmp)
- GIF (.gif)

### Font Formats (字体格式)
- Binary Font (.bin)
- TrueType Font (.ttf)
- OpenType Font (.otf)

## Output Files (输出文件)

After building, you'll find:

```
output_directory/
├── assets.bin          # Main binary file (主二进制文件)
├── assets/            # Asset folder (资源文件夹)
│   ├── light_bg.png   # Light background
│   ├── dark_bg.png    # Dark background
│   └── [font_name]    # Custom font file
```

## File Format Details (文件格式详情)

### assets.bin Structure

```
┌─────────────────────┬──────────────────────┐
│   Header (12 bytes)  │  File Metadata + Data │
├─────────────────────┼──────────────────────┤
│ num_files (4B)      │ mmap_table           │
│ checksum (4B)       │ (32*num_files bytes) │
│ data_length (4B)    │                      │
│                     │ merged_file_data     │
└─────────────────────┴──────────────────────┘
```

### File Table Entry (32 bytes per file)

```
Offset   Size   Description
0        32     Filename (null-terminated)
32       4      File size (little-endian)
36       4      Data offset (little-endian)
40       2      Image width (0 if not image)
42       2      Image height (0 if not image)
```

## Command Line Usage (命令行使用)

### Build from existing files

```bash
# Using the build_default_assets.py script (more advanced)
cd scripts
python build_default_assets.py \
    --sdkconfig ../sdkconfig \
    --output ../build/generated_assets.bin \
    --builtin_text_font font_puhui_basic_14_1
```

## Tips and Best Practices (提示和最佳实践)

### Image Recommendations (图片建议)

1. **Resolution (分辨率)**
   - Match your device display resolution (与您的设备显示分辨率匹配)
   - Common sizes: 240x240, 320x240, 480x320, 800x480

2. **Format (格式)**
   - Use PNG for best quality and transparency support (使用PNG获得最佳质量和透明度支持)
   - Reduce file size with JPEG if needed (如果需要，用JPEG减少文件大小)
   - Convert animated GIFs to static PNG if needed (如果需要，将动画GIF转换为静态PNG)

3. **Colors (颜色)**
   - Ensure good contrast for text readability (确保文本可读性的良好对比度)
   - Test both light and dark backgrounds (测试亮色和暗色背景)

### Font Recommendations (字体建议)

1. **Binary Format (二进制格式)**
   - Use pre-compiled .bin fonts for best performance (使用预编译的.bin字体以获得最佳性能)
   - Available in xiaozhitech SDK (在xiaozhitech SDK中可用)

2. **Font Size (字体大小)**
   - Recommended: 14pt, 16pt, 20pt, 30pt
   - Ensure readability on your device display (确保设备显示上的可读性)

## Flashing assets.bin to Device (将assets.bin刷入设备)

### Using esptool.py

```bash
# If your device uses partition table v2
esptool.py write_flash \
    --flash_mode dio \
    --flash_freq 80m \
    0x380000 build/generated_assets.bin

# Replace 0x380000 with your actual assets partition offset
# Check sdkconfig for: CONFIG_PARTITION_TABLE_OFFSET
```

### Using idf.py (ESP-IDF)

```bash
# Flash specific partition
idf.py flash -p /dev/ttyUSB0 -b 921600 \
    assets build/generated_assets.bin
```

## Troubleshooting (故障排除)

### Issue: "Image format not supported" (不支持的图片格式)

**Solution:**
- Convert image to PNG format (将图片转换为PNG格式)
- Use an image editor like GIMP or online tools (使用GIMP或在线工具)

### Issue: "assets.bin file too large" (assets.bin文件过大)

**Solution:**
- Reduce image resolution (降低图片分辨率)
- Use JPEG format instead of PNG (使用JPEG而不是PNG)
- Remove transparency (移除透明度)

### Issue: "Build fails with checksum error" (构建失败，校验和错误)

**Solution:**
- Ensure all files are readable (确保所有文件都可读)
- Check available disk space (检查可用磁盘空间)
- Run tool as administrator if on Windows (如果在Windows上，以管理员身份运行)

### Issue: "File path contains special characters" (文件路径包含特殊字符)

**Solution:**
- Move files to path without special characters (将文件移动到不包含特殊字符的路径)
- Use ASCII characters only (仅使用ASCII字符)

## Advanced Usage (高级用法)

### Building from sdkconfig

```bash
# Use the advanced build script to build from sdkconfig settings
python scripts/build_default_assets.py \
    --sdkconfig sdkconfig \
    --output build/generated_assets.bin \
    --builtin_text_font font_puhui_basic_14_1 \
    --emoji_collection twemoji_32 \
    --esp_sr_model_path managed_components/espressif__esp-sr/model \
    --xiaozhi_fonts_path managed_components/xiaozhi-fonts
```

## API Integration (API集成)

### In C++ Code

```cpp
// Load assets from assets.bin
Assets::GetInstance().InitializePartition();

// Get asset data
void* asset_data;
size_t asset_size;
if (Assets::GetInstance().GetAssetData("light_bg.png", asset_data, asset_size)) {
    // Use asset_data and asset_size
}
```

## Configuration (配置)

### Partition Table Configuration (分区表配置)

In `sdkconfig`, ensure you have the assets partition defined:

```ini
# Name     Type   SubType  Offset  Size
assets    data   unknown  0x380000 0x400000
```

## Version History (版本历史)

### v1.0 (Current)
- Initial release (初始版本)
- GUI support with browse and preview (支持GUI、浏览和预览)
- PNG, JPG, BMP, GIF image format support (支持PNG、JPG、BMP、GIF图片格式)
- Font file support (字体文件支持)
- assets.bin generation (assets.bin生成)

## License (许可证)

This tool is part of the XiaoZhi project and follows the same license terms.

## Support (支持)

For issues and questions:
- Check the troubleshooting section (查看故障排除部分)
- Review the docs/ folder in the project (查看项目中的docs/文件夹)
- Check README files in the main project (查看主项目中的README文件)

## FAQ (常见问题)

**Q: Can I modify assets.bin after flashing?**
A: Yes, you can generate a new assets.bin and flash it again without reflashing the firmware.

**Q: What's the maximum file size for assets.bin?**
A: Depends on your partition size. Default is 4MB (0x400000). Check your sdkconfig.

**Q: Can I include multiple fonts?**
A: Currently, the tool supports one font per build. You can regenerate with different fonts as needed.

**Q: Do I need to rebuild the firmware after changing assets?**
A: No! assets.bin is separate from firmware. Just flash the binary to the assets partition.

---

For more information, see the main project documentation.
更多信息请查看主项目文档。
