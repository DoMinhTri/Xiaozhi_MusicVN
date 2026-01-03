#!/usr/bin/env python3
"""
Assets Builder GUI - Drag and Drop Tool for Creating assets.bin

This tool allows users to drag-and-drop background images and fonts
to easily create custom assets.bin files.

Features:
- Drag and drop light/dark background images
- Drag and drop custom fonts
- Preview of selected assets
- Build assets.bin with custom configuration
"""

import os
import sys
import json
import struct
import argparse
from pathlib import Path
from PIL import Image
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.dnd2 import DND_FILES, DND_TEXT
import threading
from datetime import datetime


class AssetsBuilderGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Assets Builder - 自定义资源生成工具")
        self.root.geometry("800x700")
        self.root.configure(bg='#f0f0f0')
        
        # Variables
        self.light_bg_path = tk.StringVar()
        self.dark_bg_path = tk.StringVar()
        self.font_path = tk.StringVar()
        self.output_dir = tk.StringVar(value=str(Path.home() / "assets_output"))
        
        # Setup GUI
        self.setup_ui()
        
    def setup_ui(self):
        """Setup the user interface"""
        # Title
        title_frame = tk.Frame(self.root, bg='#2c3e50')
        title_frame.pack(fill=tk.X)
        title_label = tk.Label(
            title_frame,
            text="资源生成工具 - Assets Builder",
            font=("Arial", 16, "bold"),
            bg='#2c3e50',
            fg='white',
            pady=10
        )
        title_label.pack()
        
        # Main content frame
        content_frame = tk.Frame(self.root, bg='#f0f0f0')
        content_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        # Light Background Section
        self._create_drop_zone(
            content_frame,
            "Light Background Image (亮色背景)",
            self.light_bg_path,
            "drag_light_bg"
        )
        
        # Dark Background Section
        self._create_drop_zone(
            content_frame,
            "Dark Background Image (暗色背景)",
            self.dark_bg_path,
            "drag_dark_bg"
        )
        
        # Font Section
        self._create_drop_zone(
            content_frame,
            "Custom Font File (自定义字体)",
            self.font_path,
            "drag_font"
        )
        
        # Output Directory Section
        output_frame = tk.LabelFrame(
            content_frame,
            text="Output Directory (输出目录)",
            font=("Arial", 11, "bold"),
            bg='white',
            padx=15,
            pady=10
        )
        output_frame.pack(fill=tk.X, pady=10)
        
        output_entry = tk.Entry(
            output_frame,
            textvariable=self.output_dir,
            font=("Arial", 10),
            width=60
        )
        output_entry.pack(side=tk.LEFT, fill=tk.X, expand=True)
        
        browse_btn = tk.Button(
            output_frame,
            text="Browse...",
            command=self.browse_output_dir,
            bg='#3498db',
            fg='white',
            padx=10
        )
        browse_btn.pack(side=tk.LEFT, padx=5)
        
        # Preview Section
        preview_frame = tk.LabelFrame(
            content_frame,
            text="Preview (预览)",
            font=("Arial", 11, "bold"),
            bg='white',
            padx=15,
            pady=10
        )
        preview_frame.pack(fill=tk.X, pady=10)
        
        self.preview_text = tk.Text(
            preview_frame,
            height=6,
            font=("Courier", 9),
            bg='#ecf0f1',
            relief=tk.SUNKEN,
            borderwidth=1
        )
        self.preview_text.pack(fill=tk.BOTH, expand=True)
        self.preview_text.config(state=tk.DISABLED)
        
        # Button Frame
        button_frame = tk.Frame(content_frame, bg='#f0f0f0')
        button_frame.pack(fill=tk.X, pady=20)
        
        build_btn = tk.Button(
            button_frame,
            text="Build Assets.bin (生成资源文件)",
            command=self.build_assets,
            bg='#27ae60',
            fg='white',
            font=("Arial", 12, "bold"),
            padx=20,
            pady=10
        )
        build_btn.pack(side=tk.LEFT, padx=5)
        
        clear_btn = tk.Button(
            button_frame,
            text="Clear All (清空)",
            command=self.clear_all,
            bg='#e74c3c',
            fg='white',
            font=("Arial", 11),
            padx=15,
            pady=8
        )
        clear_btn.pack(side=tk.LEFT, padx=5)
        
        # Progress bar
        self.progress = ttk.Progressbar(
            content_frame,
            mode='indeterminate',
            length=400
        )
        self.progress.pack(fill=tk.X, pady=10)
        
        # Status label
        self.status_label = tk.Label(
            content_frame,
            text="Ready to build assets",
            font=("Arial", 10),
            bg='#f0f0f0',
            fg='#2c3e50'
        )
        self.status_label.pack(fill=tk.X)
        
    def _create_drop_zone(self, parent, label, var, drop_id):
        """Create a drag-and-drop zone"""
        frame = tk.LabelFrame(
            parent,
            text=label,
            font=("Arial", 11, "bold"),
            bg='white',
            padx=15,
            pady=10
        )
        frame.pack(fill=tk.X, pady=10)
        
        # Drop zone
        drop_zone = tk.Frame(
            frame,
            bg='#ecf0f1',
            relief=tk.RIDGE,
            borderwidth=2,
            height=60
        )
        drop_zone.pack(fill=tk.BOTH, expand=True, pady=5)
        drop_zone.pack_propagate(False)
        
        # Text display
        text_label = tk.Label(
            drop_zone,
            text="Drag and drop file here\n(拖放文件到这里)",
            font=("Arial", 10),
            bg='#ecf0f1',
            fg='#7f8c8d'
        )
        text_label.pack(expand=True)
        
        # File info label
        info_label = tk.Label(
            frame,
            textvariable=var,
            font=("Arial", 9),
            bg='white',
            fg='#27ae60',
            wraplength=600,
            justify=tk.LEFT
        )
        info_label.pack(fill=tk.X, pady=5)
        
        # Browse button
        browse_btn = tk.Button(
            frame,
            text="Browse...",
            command=lambda: self.browse_file(var, drop_id),
            bg='#3498db',
            fg='white',
            padx=10
        )
        browse_btn.pack(pady=5)
        
        # Register drag-and-drop
        self.register_drop(drop_zone, var, drop_id)
        
        return frame
    
    def register_drop(self, widget, var, drop_id):
        """Register drag-and-drop for a widget"""
        def drop_handler(event):
            files = self.root.tk.splitlist(event.data)
            if files:
                file_path = files[0]
                # Remove curly braces if present
                file_path = file_path.strip('{}')
                var.set(file_path)
                self.update_preview()
        
        widget.drop_target_register(DND_FILES)
        widget.dnd_bind('<<Drop>>', drop_handler)
    
    def browse_file(self, var, file_type):
        """Browse for a file"""
        if file_type == "drag_light_bg":
            title = "Select Light Background Image"
            filetypes = [("Images", "*.png *.jpg *.jpeg *.bmp"), ("All", "*.*")]
        elif file_type == "drag_dark_bg":
            title = "Select Dark Background Image"
            filetypes = [("Images", "*.png *.jpg *.jpeg *.bmp"), ("All", "*.*")]
        else:  # drag_font
            title = "Select Font File"
            filetypes = [("Font Files", "*.bin *.ttf *.otf"), ("All", "*.*")]
        
        file_path = filedialog.askopenfilename(
            title=title,
            filetypes=filetypes
        )
        
        if file_path:
            var.set(file_path)
            self.update_preview()
    
    def browse_output_dir(self):
        """Browse for output directory"""
        dir_path = filedialog.askdirectory(title="Select Output Directory")
        if dir_path:
            self.output_dir.set(dir_path)
    
    def update_preview(self):
        """Update preview text"""
        self.preview_text.config(state=tk.NORMAL)
        self.preview_text.delete(1.0, tk.END)
        
        preview_text = "Configuration:\n"
        preview_text += "-" * 50 + "\n"
        
        if self.light_bg_path.get():
            preview_text += f"Light BG: {os.path.basename(self.light_bg_path.get())}\n"
        
        if self.dark_bg_path.get():
            preview_text += f"Dark BG:  {os.path.basename(self.dark_bg_path.get())}\n"
        
        if self.font_path.get():
            preview_text += f"Font:     {os.path.basename(self.font_path.get())}\n"
        
        preview_text += "-" * 50 + "\n"
        preview_text += f"Output:   {self.output_dir.get()}\n"
        
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
        
        # Run in thread to avoid blocking UI
        thread = threading.Thread(target=self._build_assets_thread)
        thread.start()
    
    def _build_assets_thread(self):
        """Build assets in a separate thread"""
        try:
            self.progress.start()
            self.status_label.config(text="Building assets...", fg='#3498db')
            self.root.update()
            
            # Create output directory
            output_dir = Path(self.output_dir.get())
            output_dir.mkdir(parents=True, exist_ok=True)
            
            # Create assets directory structure
            assets_dir = output_dir / "assets"
            assets_dir.mkdir(parents=True, exist_ok=True)
            
            # Copy files
            files_info = []
            
            if self.light_bg_path.get():
                src = Path(self.light_bg_path.get())
                dst = assets_dir / "light_bg.png"
                self._copy_and_convert_image(src, dst)
                files_info.append(("light_bg.png", dst))
            
            if self.dark_bg_path.get():
                src = Path(self.dark_bg_path.get())
                dst = assets_dir / "dark_bg.png"
                self._copy_and_convert_image(src, dst)
                files_info.append(("dark_bg.png", dst))
            
            if self.font_path.get():
                src = Path(self.font_path.get())
                dst = assets_dir / src.name
                self._copy_file(src, dst)
                files_info.append((src.name, dst))
            
            # Generate assets.bin
            output_bin = output_dir / "assets.bin"
            self._pack_assets(assets_dir, output_bin)
            
            self.progress.stop()
            self.status_label.config(
                text=f"✓ Assets built successfully! Output: {output_dir}",
                fg='#27ae60'
            )
            messagebox.showinfo("Success", f"Assets.bin created successfully!\n\nOutput: {output_dir}")
            
        except Exception as e:
            self.progress.stop()
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
        if img.mode == 'RGBA':
            img.save(dst, 'PNG')
        else:
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
            mmap_table.extend(fixed_name.encode('utf-8'))
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


def main():
    root = tk.Tk()
    
    # Try to enable drag and drop (requires tkinterdnd)
    try:
        from tkinterdnd2 import DND_FILES
        app = AssetsBuilderGUI(root)
    except ImportError:
        messagebox.showerror(
            "Missing Dependency",
            "tkinterdnd2 is required for drag-and-drop support.\n"
            "Install it with: pip install tkinterdnd2"
        )
        sys.exit(1)
    
    root.mainloop()


if __name__ == "__main__":
    main()
