# YOLOv5 Continuous Inference System - Usage Guide

## Overview

This system provides continuous YOLOv5 object detection on Huawei Ascend NPU with automatic directory monitoring and batch processing capabilities.

## Components

1. **detect_yolov5_ascend.py** - Main inference script with continuous scanning
2. **simulate_image_sender.py** - Test utility to simulate image transmission

---

## 1. Main Detection Script: `detect_yolov5_ascend.py`

### Features

- ✅ Continuous directory scanning
- ✅ Automatic batch processing
- ✅ Configurable output formats (label/image/all)
- ✅ IPv4-based folder structure support
- ✅ Automatic image deletion after processing
- ✅ Transfer completion safeguards
- ✅ Full command-line help

### Command-Line Arguments

#### Required Arguments

| Argument | Type | Description |
|----------|------|-------------|
| `--input-dir` | string | Input directory where IPv4-named folders with images will be created |
| `--output-dir` | string | Output directory for inference results |

#### Output Control

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `--output-format` | choice | `all` | Output format: `label` (only .txt), `image` (only images), `all` (both) |

#### Model Parameters

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `--weights` | string | `ascend/yolov5s.om` | Path to model weights (.om file) |
| `--labels` | string | `ascend/yolov5.label` | Path to label file |
| `--imgsz` | int[] | `[640, 640]` | Inference size [height, width] |

#### Device Parameters

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `--device` | int | `0` | NPU device ID (e.g., 0 or 1) |

#### Detection Parameters

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `--conf-thres` | float | `0.25` | Confidence threshold for detections |
| `--iou-thres` | float | `0.45` | NMS IoU threshold |
| `--max-det` | int | `1000` | Maximum detections per image |
| `--agnostic-nms` | flag | `False` | Class-agnostic NMS |

#### Timing Parameters

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `--stability-check` | float | `0.01` | Interval (seconds) between file size stability checks |
| `--max-wait` | float | `5.0` | Maximum time (seconds) to wait for a file to stabilize |
| `--scan-interval` | float | `1.0` | Interval (seconds) between directory scans |

### Usage Examples

#### Example 1: Basic usage with all outputs
```bash
python detect_yolov5_ascend.py \
    --input-dir ./input \
    --output-dir ./output \
    --output-format all
```

#### Example 2: Label-only output with custom model
```bash
python detect_yolov5_ascend.py \
    --input-dir /data/input \
    --output-dir /data/output \
    --output-format label \
    --weights ./models/yolov5m.om
```

#### Example 3: Image-only output with custom thresholds
```bash
python detect_yolov5_ascend.py \
    --input-dir ./input \
    --output-dir ./output \
    --output-format image \
    --conf-thres 0.3 \
    --iou-thres 0.5
```

#### Example 4: Fast local transfers with quick stability checks
```bash
python detect_yolov5_ascend.py \
    --input-dir ./input \
    --output-dir ./output \
    --stability-check 0.005 \
    --max-wait 2.0
```

### Directory Structure

#### Input Structure
```
input/
├── 192.168.1.100/
│   ├── image_0001.tif
│   ├── image_0002.tif
│   └── image_0003.tif
├── 10.0.0.50/
│   ├── photo_001.jpg
│   └── photo_002.jpg
└── 172.16.0.10/
    └── sample.png
```

#### Output Structure (when --output-format=all)
```
output/
├── label/
│   ├── 192.168.1.100/
│   │   ├── image_0001.txt
│   │   ├── image_0002.txt
│   │   └── image_0003.txt
│   ├── 10.0.0.50/
│   │   ├── photo_001.txt
│   │   └── photo_002.txt
│   └── 172.16.0.10/
│       └── sample.txt
└── image/
    ├── 192.168.1.100/
    │   ├── image_0001.tif
    │   ├── image_0002.tif
    │   └── image_0003.tif
    ├── 10.0.0.50/
    │   ├── photo_001.jpg
    │   └── photo_002.jpg
    └── 172.16.0.10/
        └── sample.png
```

#### Output Structure (when --output-format=label)
```
output/
└── label/
    ├── 192.168.1.100/
    │   └── image_0001.txt
    └── 10.0.0.50/
        └── photo_001.txt
```

#### Output Structure (when --output-format=image)
```
output/
└── image/
    ├── 192.168.1.100/
    │   └── image_0001.tif
    └── 10.0.0.50/
        └── photo_001.jpg
```

### Label File Format

Label files (.txt) contain one detection per line in the format:
```
class_id x1 y1 x2 y2 confidence
```

Example:
```
0 245 189 456 342 0.89
2 100 50 200 150 0.76
```

### Workflow

1. **Initialization**: Creates input/output directories, loads model
2. **Scanning Loop**: Continuously scans input directory for images
3. **Stability Check**: Verifies each file is completely transferred by monitoring size stability
4. **Batch Processing**: Processes all ready images
5. **Result Saving**: Saves labels/images based on `--output-format`
6. **Cleanup**: Deletes processed images from input directory
7. **Repeat**: Returns to step 2

### Stopping the Script

Press `Ctrl+C` to gracefully stop the detection process.

---

## 2. Image Sender Simulator: `simulate_image_sender.py`

### Purpose

Simulates an external program that sends images to the input directory for testing purposes.

### Command-Line Arguments

#### Required Arguments

| Argument | Type | Description |
|----------|------|-------------|
| `--source-dir` | string | Source directory containing images to send |
| `--target-dir` | string | Target input directory where IPv4 folders will be created |

#### Optional Arguments

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `--num-ips` | int | `10` | Number of random IPv4 addresses to generate |
| `--num-images` | int | `100` | Number of images to send per IPv4 address |
| `--send-interval` | float | `0.05` | Time interval (seconds) between sending images |
| `--ip-interval` | float | `0.0` | Time interval (seconds) between processing different IPs |

### Usage Examples

#### Example 1: Send 50 images to 5 IPs
```bash
python simulate_image_sender.py \
    --source-dir ./test_images \
    --target-dir ./input \
    --num-ips 5 \
    --num-images 50
```

#### Example 2: Slow transmission simulation
```bash
python simulate_image_sender.py \
    --source-dir /data/images \
    --target-dir /data/input \
    --num-ips 10 \
    --num-images 100 \
    --send-interval 0.1 \
    --ip-interval 2.0
```

#### Example 3: Rapid transmission test
```bash
python simulate_image_sender.py \
    --source-dir ./images \
    --target-dir ./input \
    --num-ips 20 \
    --num-images 200 \
    --send-interval 0.01
```

### Behavior

1. Reads all images from source directory
2. Generates N unique random IPv4 addresses
3. For each IPv4:
   - Creates a folder with the IPv4 name in target directory
   - Copies M images from source directory
   - Waits specified interval between each image
4. Reports transmission statistics

---

## Complete Testing Workflow

### Step 1: Prepare Test Images

Create a directory with sample images:
```bash
mkdir -p test_images
# Copy some test images to test_images/
```

### Step 2: Start Detection System (Terminal 1)

```bash
python detect_yolov5_ascend.py \
    --input-dir ./input \
    --output-dir ./output \
    --output-format all
```

### Step 3: Start Image Sender (Terminal 2)

```bash
python simulate_image_sender.py \
    --source-dir ./test_images \
    --target-dir ./input \
    --num-ips 10 \
    --num-images 100 \
    --send-interval 0.05
```

### Step 4: Monitor Results

Watch the detection system process images and check the output directory for results.

### Step 5: Stop Detection System

Press `Ctrl+C` in Terminal 1 when testing is complete.

---

## Performance Tuning

### Stability Check Interval (`--stability-check`)

- **Very fast (0.005-0.01s)**: Maximum responsiveness for local transfers
- **Default (0.01s)**: Recommended for most cases, good balance
- **Slower (0.02-0.05s)**: Less CPU overhead, suitable for slow networks

### Max Wait Time (`--max-wait`)

- **Short (1-2s)**: Quick timeout for fast local transfers
- **Default (5s)**: Works for most scenarios
- **Long (10-30s)**: For very large files or slow network transfers

### Scan Interval (`--scan-interval`)

- **Lower values (0.5-1.0s)**: More responsive, higher CPU usage
- **Higher values (2.0-5.0s)**: Lower CPU overhead, slower response

### Confidence Threshold (`--conf-thres`)

- **Lower values (0.15-0.25)**: More detections, more false positives
- **Higher values (0.3-0.5)**: Fewer detections, higher precision

---

## Troubleshooting

### Issue: Images not being processed

**Solution**: Check that:
- Input directory exists and is readable
- Image files have valid extensions (.jpg, .png, .tif, etc.)
- Wait time is sufficient for image transfer

### Issue: Model loading fails

**Solution**: Verify:
- Model file (.om) exists at specified path
- NPU device ID is correct
- ACL runtime is properly installed

### Issue: Output directories not created

**Solution**: Ensure:
- Output directory path is valid
- User has write permissions
- Disk space is available

---

## Notes

1. **Image Deletion**: Processed images are automatically deleted from the input directory
2. **Supported Formats**: .jpg, .jpeg, .png, .bmp, .tif, .tiff
3. **Thread Safety**: Single-threaded design ensures predictable behavior
4. **Error Handling**: Failed images are logged but don't stop processing
5. **Memory Management**: Images processed one at a time to manage memory

---

## Requirements

- Python 3.6+
- OpenCV (cv2)
- PyTorch
- NumPy
- Pillow
- Huawei Ascend ACL runtime
- Compiled YOLOv5 model (.om format)
