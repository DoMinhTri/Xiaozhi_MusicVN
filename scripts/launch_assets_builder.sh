#!/bin/bash
# Assets Builder Launcher - Linux/Mac Shell Script
# This script launches the Assets Builder GUI tool

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo ""
echo "================================================================================"
echo "Assets Builder - 资源文件生成工具"
echo "================================================================================"
echo ""

# Check if Python is installed
if ! command -v python3 &> /dev/null; then
    if ! command -v python &> /dev/null; then
        echo "ERROR: Python is not installed!"
        echo ""
        echo "Please install Python 3.7 or higher:"
        echo "  Ubuntu/Debian: sudo apt-get install python3 python3-pip"
        echo "  macOS: brew install python3"
        echo ""
        exit 1
    fi
    PYTHON_CMD="python"
else
    PYTHON_CMD="python3"
fi

echo "Using Python: $PYTHON_CMD"
$PYTHON_CMD --version
echo ""

# Check if Pillow is installed
if ! $PYTHON_CMD -c "import PIL" 2>/dev/null; then
    echo "================================================================================"
    echo "Installing required package: pillow"
    echo "================================================================================"
    echo ""
    
    $PYTHON_CMD -m pip install pillow --quiet
    
    if [ $? -ne 0 ]; then
        echo ""
        echo "ERROR: Failed to install pillow!"
        echo "Please run: $PYTHON_CMD -m pip install pillow"
        echo ""
        exit 1
    fi
    
    echo "Done!"
    echo ""
fi

# Launch the GUI application
echo "================================================================================"
echo "Launching Assets Builder GUI"
echo "================================================================================"
echo ""

$PYTHON_CMD "$SCRIPT_DIR/assets_builder_simple.py"

if [ $? -ne 0 ]; then
    echo ""
    echo "================================================================================"
    echo "ERROR: Application failed to launch!"
    echo "================================================================================"
    echo ""
    exit 1
fi
