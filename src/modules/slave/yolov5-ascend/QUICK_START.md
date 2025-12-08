# Quick Start Guide

## Minimal Setup

### 1. Detection Script (Most Common Usage)

```bash
# Basic usage - outputs both labels and images
python detect_yolov5_ascend.py \
    --input-dir ./input \
    --output-dir ./output

# Label files only
python detect_yolov5_ascend.py \
    --input-dir ./input \
    --output-dir ./output \
    --output-format label

# Images with bounding boxes only
python detect_yolov5_ascend.py \
    --input-dir ./input \
    --output-dir ./output \
    --output-format image
```

### 2. Test with Image Simulator

```bash
# Simulate 10 IPs sending 100 images each
python simulate_image_sender.py \
    --source-dir /path/to/test/images \
    --target-dir ./input
```

## Input/Output Format Summary

### Input Format
```
input/192.168.1.1/image.tif
```

### Output Format (--output-format=all)
```
output/label/192.168.1.1/image.txt
output/image/192.168.1.1/image.tif
```

### Output Format (--output-format=label)
```
output/label/192.168.1.1/image.txt
```

### Output Format (--output-format=image)
```
output/image/192.168.1.1/image.tif
```

## Key Features

- ✅ Continuous scanning (infinite loop)
- ✅ Automatic batch processing
- ✅ Configurable output (label/image/both)
- ✅ Safe file transfer detection
- ✅ Auto-deletion after processing
- ✅ Preserves IPv4 folder structure
- ✅ Full --help documentation

## Get Help

```bash
python detect_yolov5_ascend.py --help
python simulate_image_sender.py --help
```

See [USAGE.md](USAGE.md) for complete documentation.
