#!/usr/bin/env python3
"""
Assets Builder GUI - Simple version (No external drag-drop library required)

This tool allows users to select background images and fonts
to easily create custom assets.bin files.

Features:
- Browse and select light/dark background images
- Select custom fonts
- Preview of selected assets
- Build assets.bin with custom configuration
- Support for image format conversion (PNG/JPG/BMP)

Requirements:
    pip install pillow

Usage:
    python assets_builder_simple.py
"""

import os
import sys
import json
import struct
from pathlib import Path
from PIL import Image
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import threading


class AssetsBuilderSimple:
    def __init__(self, root):
        self.root = root
        self.root.title("Assets Builder - 资源文件生成工具")
        self.root.geometry("900x800")
        self.root.configure(bg='#f5f5f5')
        self.root.resizable(True, True)
        
        # Variables
        self.light_bg_path = tk.StringVar()
        self.dark_bg_path = tk.StringVar()
        self.font_path = tk.StringVar()
        self.output_dir = tk.StringVar(value=str(Path.home() / "assets_output"))
        
        self.build_in_progress = False
        
        # Setup GUI
        self.setup_ui()
        
    def setup_ui(self):
        """Setup the user interface"""
        # Create main notebook for tabs
        notebook = ttk.Notebook(self.root)
        notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Settings tab
        settings_frame = ttk.Frame(notebook, padding="10")
        notebook.add(settings_frame, text="Settings (设置)")
        self._create_settings_tab(settings_frame)
        
        # Preview tab
        preview_frame = ttk.Frame(notebook, padding="10")
        notebook.add(preview_frame, text="Preview (预览)")
        self._create_preview_tab(preview_frame)
        
        # Info tab
        info_frame = ttk.Frame(notebook, padding="10")
        notebook.add(info_frame, text="Info (信息)")
        self._create_info_tab(info_frame)
        
        # Status bar
        status_frame = tk.Frame(self.root, bg='#e8e8e8', relief=tk.SUNKEN, height=30)
        status_frame.pack(fill=tk.X, side=tk.BOTTOM)
        status_frame.pack_propagate(False)
        
        self.status_label = tk.Label(
            status_frame,
            text="Ready",
            font=("Arial", 9),
            bg='#e8e8e8',
            fg='#333',
            anchor=tk.W,
            padx=10
        )
        self.status_label.pack(fill=tk.X, expand=True)
        
        self.progress = ttk.Progressbar(
            status_frame,
            mode='indeterminate',
            length=100
        )
        self.progress.pack(side=tk.RIGHT, padx=10, pady=5)
    
    def _create_settings_tab(self, parent):
        """Create settings tab"""
        # Light Background
        light_frame = tk._tkinter_tk.LabelFrame(
            parent,
            text="Light Background Image (亮色背景)",
            font=("Arial", 10, "bold"),
            padx=10,
            pady=10
        )
        light_frame.pack(fill=tk.X, pady=10)
        
        light_entry = tk.Entry(
            light_frame,
            textvariable=self.light_bg_path,
            font=("Arial", 9),
            width=60
        )
        light_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        light_btn = tk.Button(
            light_frame,
            text="Browse",
            command=lambda: self.browse_image(self.light_bg_path, "Light Background"),
            bg='#3498db',
            fg='white',
            padx=15
        )
        light_btn.pack(side=tk.LEFT, padx=5)
        
        light_clear = tk.Button(
            light_frame,
            text="Clear",
            command=lambda: self.light_bg_path.set(""),
            bg='#95a5a6',
            fg='white',
            padx=10
        )
        light_clear.pack(side=tk.LEFT, padx=2)
        
        # Dark Background
        dark_frame = tk._tkinter_tk.LabelFrame(
            parent,
            text="Dark Background Image (暗色背景)",
            font=("Arial", 10, "bold"),
            padx=10,
            pady=10
        )
        dark_frame.pack(fill=tk.X, pady=10)
        
        dark_entry = tk.Entry(
            dark_frame,
            textvariable=self.dark_bg_path,
            font=("Arial", 9),
            width=60
        )
        dark_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        dark_btn = tk.Button(
            dark_frame,
            text="Browse",
            command=lambda: self.browse_image(self.dark_bg_path, "Dark Background"),
            bg='#3498db',
            fg='white',
            padx=15
        )
        dark_btn.pack(side=tk.LEFT, padx=5)
        
        dark_clear = tk.Button(
            dark_frame,
            text="Clear",
            command=lambda: self.dark_bg_path.set(""),
            bg='#95a5a6',
            fg='white',
            padx=10
        )
        dark_clear.pack(side=tk.LEFT, padx=2)
        
        # Font
        font_frame = tk._tkinter_tk.LabelFrame(
            parent,
            text="Custom Font File (自定义字体)",
            font=("Arial", 10, "bold"),
            padx=10,
            pady=10
        )
        font_frame.pack(fill=tk.X, pady=10)
        
        font_entry = tk.Entry(
            font_frame,
            textvariable=self.font_path,
            font=("Arial", 9),
            width=60
        )
        font_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        font_btn = tk.Button(
            font_frame,
            text="Browse",
            command=lambda: self.browse_font(),
            bg='#3498db',
            fg='white',
            padx=15
        )
        font_btn.pack(side=tk.LEFT, padx=5)
        
        font_clear = tk.Button(
            font_frame,
            text="Clear",
            command=lambda: self.font_path.set(""),
            bg='#95a5a6',
            fg='white',
            padx=10
        )
        font_clear.pack(side=tk.LEFT, padx=2)
        
        # Output Directory
        output_frame = tk._tkinter_tk.LabelFrame(
            parent,
            text="Output Directory (输出目录)",
            font=("Arial", 10, "bold"),
            padx=10,
            pady=10
        )
        output_frame.pack(fill=tk.X, pady=10)
        
        output_entry = tk.Entry(
            output_frame,
            textvariable=self.output_dir,
            font=("Arial", 9),
            width=60
        )
        output_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        output_btn = tk.Button(
            output_frame,
            text="Browse",
            command=self.browse_output_dir,
            bg='#3498db',
            fg='white',
            padx=15
        )
        output_btn.pack(side=tk.LEFT, padx=5)
        
        # Action Buttons
        button_frame = tk.Frame(parent, bg='#f5f5f5')
        button_frame.pack(fill=tk.X, pady=20)
        
        build_btn = tk.Button(
            button_frame,
            text="Build Assets.bin",
            command=self.build_assets,
            bg='#27ae60',
            fg='white',
            font=("Arial", 12, "bold"),
            padx=30,
            pady=12
        )
        build_btn.pack(side=tk.LEFT, padx=10)
        
        clear_btn = tk.Button(
            button_frame,
            text="Clear All",
            command=self.clear_all,
            bg='#e74c3c',
            fg='white',
            font=("Arial", 11),
            padx=20,
            pady=10
        )
        clear_btn.pack(side=tk.LEFT, padx=5)
        
        open_btn = tk.Button(
            button_frame,
            text="Open Output Folder",
            command=self.open_output_folder,
            bg='#f39c12',
            fg='white',
            font=("Arial", 11),
            padx=20,
            pady=10
        )
        open_btn.pack(side=tk.LEFT, padx=5)
    
    def _create_preview_tab(self, parent):
        """Create preview tab"""
        # Configuration display
        config_frame = tk._tkinter_tk.LabelFrame(
            parent,
            text="Current Configuration (当前配置)",
            font=("Arial", 10, "bold"),
            padx=10,
            pady=10
        )
        config_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        self.preview_text = tk.Text(
            config_frame,
            height=15,
            font=("Courier", 10),
            bg='#f0f0f0',
            relief=tk.SUNKEN,
            borderwidth=1,
            wrap=tk.WORD
        )
        
        scrollbar = tk.Scrollbar(config_frame, command=self.preview_text.yview)
        self.preview_text.config(yscrollcommand=scrollbar.set)
        
        self.preview_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # Refresh button
        refresh_btn = tk.Button(
            parent,
            text="Refresh Preview",
            command=self.update_preview,
            bg='#16a085',
            fg='white',
            padx=15
        )
        refresh_btn.pack(pady=10)
        
        self.update_preview()
    
    def _create_info_tab(self, parent):
        """Create info tab"""
        info_text = tk.Text(
            parent,
            height=25,
            font=("Courier", 9),
            bg='#f0f0f0',
            relief=tk.SUNKEN,
            borderwidth=1,
            wrap=tk.WORD
        )
        info_text.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        info_content = """
Assets Builder - 资源文件生成工具
=====================================

此工具用于创建自定义的 assets.bin 文件。

特性 (Features):
- 支持自定义亮色背景图片 (Light background image support)
- 支持自定义暗色背景图片 (Dark background image support)
- 支持自定义字体文件 (Custom font file support)
- 自动生成 assets.bin 二进制文件 (Auto-generate assets.bin)
- 支持多种图片格式 (PNG, JPG, BMP)

使用步骤 (Usage Steps):
1. 点击"Browse"按钮选择亮色背景图片（可选）
2. 点击"Browse"按钮选择暗色背景图片（可选）
3. 点击"Browse"按钮选择字体文件（可选）
4. 选择输出目录
5. 点击"Build Assets.bin"生成资源文件

输出文件 (Output Files):
- assets.bin: 二进制资源文件
- assets/: 资源文件夹（包含原始文件）

支持的图片格式 (Supported Image Formats):
- PNG (.png)
- JPEG (.jpg, .jpeg)
- BMP (.bmp)
- GIF (.gif)

支持的字体格式 (Supported Font Formats):
- Binary Font (.bin)
- TrueType Font (.ttf)
- OpenType Font (.otf)

注意事项 (Notes):
- 至少需要选择一个文件
- 建议使用 PNG 格式的图片（支持透明度）
- 图片分辨率建议大于等于目标设备分辨率

版本 (Version): 1.0
作者 (Author): Espressif Systems
        """
        
        info_text.insert(1.0, info_content)
        info_text.config(state=tk.DISABLED)
    
    def browse_image(self, var, title):
        """Browse for an image file"""
        file_path = filedialog.askopenfilename(
            title=f"Select {title}",
            filetypes=[
                ("Images", "*.png *.jpg *.jpeg *.bmp *.gif"),
                ("PNG", "*.png"),
                ("JPEG", "*.jpg *.jpeg"),
                ("BMP", "*.bmp"),
                ("All", "*.*")
            ]
        )
        
        if file_path:
            var.set(file_path)
            self.update_preview()
    
    def browse_font(self):
        """Browse for a font file"""
        file_path = filedialog.askopenfilename(
            title="Select Font File",
            filetypes=[
                ("Font Files", "*.bin *.ttf *.otf"),
                ("Binary Font", "*.bin"),
                ("TrueType", "*.ttf"),
                ("OpenType", "*.otf"),
                ("All", "*.*")
            ]
        )
        
        if file_path:
            self.font_path.set(file_path)
            self.update_preview()
    
    def browse_output_dir(self):
        """Browse for output directory"""
        dir_path = filedialog.askdirectory(
            title="Select Output Directory"
        )
        if dir_path:
            self.output_dir.set(dir_path)
            self.update_preview()
    
    def update_preview(self):
        """Update preview text"""
        self.preview_text.config(state=tk.NORMAL)
        self.preview_text.delete(1.0, tk.END)
        
        preview_text = "Configuration Summary\n"
        preview_text += "=" * 70 + "\n\n"
        
        if self.light_bg_path.get():
            preview_text += "✓ Light Background Image:\n"
            preview_text += f"  {self.light_bg_path.get()}\n"
            try:
                img = Image.open(self.light_bg_path.get())
                preview_text += f"  Resolution: {img.size[0]} x {img.size[1]} px\n"
                preview_text += f"  Format: {img.format}\n"
            except:
                preview_text += "  [Image info not available]\n"
            preview_text += "\n"
        else:
            preview_text += "✗ Light Background Image: Not selected\n\n"
        
        if self.dark_bg_path.get():
            preview_text += "✓ Dark Background Image:\n"
            preview_text += f"  {self.dark_bg_path.get()}\n"
            try:
                img = Image.open(self.dark_bg_path.get())
                preview_text += f"  Resolution: {img.size[0]} x {img.size[1]} px\n"
                preview_text += f"  Format: {img.format}\n"
            except:
                preview_text += "  [Image info not available]\n"
            preview_text += "\n"
        else:
            preview_text += "✗ Dark Background Image: Not selected\n\n"
        
        if self.font_path.get():
            preview_text += "✓ Custom Font File:\n"
            preview_text += f"  {self.font_path.get()}\n"
            try:
                file_size = os.path.getsize(self.font_path.get()) / 1024
                preview_text += f"  Size: {file_size:.2f} KB\n"
            except:
                pass
            preview_text += "\n"
        else:
            preview_text += "✗ Custom Font File: Not selected\n\n"
        
        preview_text += "=" * 70 + "\n"
        preview_text += f"Output Directory:\n  {self.output_dir.get()}\n"
        
        self.preview_text.insert(1.0, preview_text)
        self.preview_text.config(state=tk.DISABLED)
    
    def clear_all(self):
        """Clear all selections"""
        self.light_bg_path.set("")
        self.dark_bg_path.set("")
        self.font_path.set("")
        self.update_preview()
    
    def build_assets(self):
        """Build assets.bin"""
        if not any([self.light_bg_path.get(), self.dark_bg_path.get(), self.font_path.get()]):
            messagebox.showwarning("Warning", "Please select at least one file!")
            return
        
        if self.build_in_progress:
            messagebox.showwarning("Warning", "Build already in progress!")
            return
        
        # Run in thread to avoid blocking UI
        thread = threading.Thread(target=self._build_assets_thread)
        thread.daemon = True
        thread.start()
    
    def _build_assets_thread(self):
        """Build assets in a separate thread"""
        try:
            self.build_in_progress = True
            self.progress.start()
            self.status_label.config(text="Building assets...", fg='#3498db')
            self.root.update()
            
            # Create output directory
            output_dir = Path(self.output_dir.get())
            output_dir.mkdir(parents=True, exist_ok=True)
            
            # Create assets directory structure
            assets_dir = output_dir / "assets"
            assets_dir.mkdir(parents=True, exist_ok=True)
            
            # Copy and convert files
            if self.light_bg_path.get():
                src = Path(self.light_bg_path.get())
                dst = assets_dir / "light_bg.png"
                self._copy_and_convert_image(src, dst)
            
            if self.dark_bg_path.get():
                src = Path(self.dark_bg_path.get())
                dst = assets_dir / "dark_bg.png"
                self._copy_and_convert_image(src, dst)
            
            if self.font_path.get():
                src = Path(self.font_path.get())
                dst = assets_dir / src.name
                self._copy_file(src, dst)
            
            # Generate assets.bin
            output_bin = output_dir / "assets.bin"
            self._pack_assets(assets_dir, output_bin)
            
            self.progress.stop()
            self.build_in_progress = False
            self.status_label.config(
                text=f"✓ Build complete! Output: {output_dir}",
                fg='#27ae60'
            )
            
            if messagebox.askyesno("Success", 
                f"Assets.bin created successfully!\n\n"
                f"Output: {output_dir}\n\n"
                f"Open folder now?"):
                self.open_output_folder()
            
        except Exception as e:
            self.progress.stop()
            self.build_in_progress = False
            self.status_label.config(text=f"✗ Error: {str(e)}", fg='#e74c3c')
            messagebox.showerror("Error", f"Failed to build assets:\n{str(e)}")
    
    def _copy_file(self, src, dst):
        """Copy a file"""
        with open(src, 'rb') as f_src:
            with open(dst, 'wb') as f_dst:
                f_dst.write(f_src.read())
    
    def _copy_and_convert_image(self, src, dst):
        """Copy and convert image to PNG if needed"""
        img = Image.open(src)
        if img.mode != 'RGBA':
            img = img.convert('RGBA')
        img.save(dst, 'PNG')
    
    def _pack_assets(self, assets_dir, output_bin):
        """Pack assets into binary format"""
        merged_data = bytearray()
        file_info_list = []
        
        # Get all files
        files = sorted([f for f in assets_dir.iterdir() if f.is_file()])
        
        for file_path in files:
            file_name = file_path.name
            file_size = file_path.stat().st_size
            offset = len(merged_data)
            
            file_info_list.append((file_name, offset, file_size, 0, 0))
            
            # Add 0x5A5A prefix
            merged_data.extend(b'\x5A' * 2)
            
            # Add file content
            with open(file_path, 'rb') as f:
                merged_data.extend(f.read())
        
        # Create mmap table
        total_files = len(file_info_list)
        mmap_table = bytearray()
        
        for file_name, offset, file_size, width, height in file_info_list:
            fixed_name = file_name.ljust(32, '\0')[:32]
            mmap_table.extend(fixed_name.encode('utf-8')[:32])
            mmap_table.extend(file_size.to_bytes(4, byteorder='little'))
            mmap_table.extend(offset.to_bytes(4, byteorder='little'))
            mmap_table.extend(width.to_bytes(2, byteorder='little'))
            mmap_table.extend(height.to_bytes(2, byteorder='little'))
        
        # Calculate checksum
        combined_data = mmap_table + merged_data
        checksum = sum(combined_data) & 0xFFFF
        
        # Build final binary
        combined_data_length = len(combined_data).to_bytes(4, byteorder='little')
        header_data = total_files.to_bytes(4, byteorder='little') + checksum.to_bytes(4, byteorder='little')
        final_data = header_data + combined_data_length + combined_data
        
        # Write output
        with open(output_bin, 'wb') as f:
            f.write(final_data)
    
    def open_output_folder(self):
        """Open output folder in file explorer"""
        output_dir = Path(self.output_dir.get())
        if output_dir.exists():
            import subprocess
            import platform
            
            if platform.system() == 'Windows':
                subprocess.Popen(f'explorer "{output_dir}"')
            elif platform.system() == 'Darwin':
                subprocess.Popen(['open', str(output_dir)])
            else:
                subprocess.Popen(['xdg-open', str(output_dir)])


def main():
    root = tk.Tk()
    app = AssetsBuilderSimple(root)
    root.mainloop()


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
