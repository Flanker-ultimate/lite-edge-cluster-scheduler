# Environment Setup Guide for YOLOv5 on Huawei Ascend NPU

## Prerequisites

All steps in this guide must be executed as the root user.

---

## 1. Install Ascend CANN Toolkit

### 1.1 Locate Installation Package

The installation package `Ascend-cann-toolkit_6.0.1_linux-aarch64.run` should be located at `/home/ubuntu/huawei`.

### 1.2 View Installation Options

To view available installation options, execute:

```bash
./Ascend-cann-toolkit_6.0.1_linux-aarch64.run --help
```

**Available Options:**
```
Usage: ./Ascend-cann-toolkit_6.0.1_linux-aarch64.run [options]
Options:
  --help | -h                       Print this message
  --info                            Print embedded info : title, default target directory, embedded script ...
  --list                            Print the list of files in the archive
  --check                           Checks integrity and version dependency of the archive
  --quiet                           Quiet install mode, skip human-computer interactions
  --nox11                           Do not spawn an xterm
  --noexec                          Do not run embedded script
  --extract=<path>                  Extract directly to a target directory (absolute or relative)
                                    Usually used with --noexec to just extract files without running
  --tar arg1 [arg2 ...]             Access the contents of the archive through the tar command
  --install                         Install run mode
  --uninstall                       Uninstall
  --upgrade                         Upgrade
  --devel                           Install devel mode
  --version                         Show version
  --install-for-all                 Install for all users
  --install-path=<path>             Install to specific path
  --chip=<chip_type>                Appoint chip type, must be Ascend310, Ascend910, Ascend310P or Ascend310-minirc
  --feature-list=<feature>          Upgrade feature separately, must be Acclibs
  --alternative                     Show all supported features
  --whitelist=<feature_type>        Install features, must be atc, devtools, nnrt, nnae
  --full                            Install full mode
```

### 1.3 Install CANN Toolkit

**IMPORTANT:** The `--install-for-all` option is **required** to ensure proper functionality in multi-user environments. Omitting this option may cause issues when multiple users access the system.

**Installation command:**
```bash
./Ascend-cann-toolkit_6.0.1_linux-aarch64.run --install --install-for-all
```

### 1.4 Configure Environment Variables

Add the following lines to your `~/.bashrc` file:

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export LD_LIBRARY_PATH=/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/devlib/:$LD_LIBRARY_PATH
```

Apply the changes:
```bash
source ~/.bashrc
```

---

## 2. Install Python 3.8

### 2.1 Version Requirements

**Minimum required version:** Python 3.8 or higher

Python 3.8+ is required to support the necessary NumPy versions for this project.

### 2.2 Download Python Source

Download the Python source distribution to `/home/ubuntu/huawei`:

```bash
cd /home/ubuntu/huawei
wget https://www.python.org/ftp/python/3.8.5/Python-3.8.5.tgz
```

### 2.3 Extract Source Archive

```bash
tar -xzvf Python-3.8.5.tgz
cd Python-3.8.5
```

### 2.4 Build and Install Python

**IMPORTANT:** The `--with-ssl` flag is **required** to prevent errors when using pip.

```bash
./configure --prefix=/usr/local/python3.8.5 --enable-loadable-sqlite-extensions --enable-shared --with-ssl
make -j 8
make install
```

### 2.5 Configure Python Environment

Add the following lines to your `~/.bashrc` file:

```bash
export LD_LIBRARY_PATH=/usr/local/python3.8.5/lib:$LD_LIBRARY_PATH
export PATH=/usr/local/python3.8.5/bin:$PATH
```

Apply the changes:
```bash
source ~/.bashrc
```

### 2.6 Update pip

```bash
pip3 install --upgrade pip
```

---

## 3. Install YOLOv5 and Dependencies

### 3.1 Clone YOLOv5 Repository

```bash
cd /home/ubuntu
git clone https://github.com/ultralytics/yolov5.git
cd yolov5
```

### 3.2 Install YOLOv5 Dependencies

```bash
pip3 install -r requirements.txt
```

---

## 4. Convert YOLOv5 Model to Ascend Format

### 4.1 Export to ONNX Format

Download the YOLOv5s weights and convert to ONNX format:

```bash
python3 export.py --weights yolov5s.pt --opset 12 --simplify --include onnx
```

### 4.2 Convert ONNX to OM Format

Use the ATC (Ascend Tensor Compiler) tool to convert the ONNX model to OM format for Ascend 310:

```bash
atc --input_shape="images:1,3,640,640" \
    --input_format=NCHW \
    --output="yolov5s" \
    --soc_version=Ascend310 \
    --framework=5 \
    --model="yolov5s.onnx" \
    --output_type=FP32
```

### 4.3 Deploy Model

Copy the converted model to the project directory:

```bash
cp yolov5s.om /home/ubuntu/huawei/yolov5-ascend/ascend/
```

### 4.4 Configure Label File (Optional)

If needed, edit the label file to match your specific class names:

```bash
vim /home/ubuntu/huawei/yolov5-ascend/ascend/yolov5.label
```

---

## 5. Install Project Dependencies

Navigate to the project directory and install dependencies:

```bash
cd /home/ubuntu/huawei/yolov5-ascend
pip3 install -r requirements.txt
```

---

## 6. Verification

After completing all steps above, the environment is ready for inference operations.

---

## Troubleshooting

### Python Runtime Library Errors

If you encounter Python runtime library errors during inference, reinstall the system dependencies:

```bash
apt-get install -y gcc g++ make cmake zlib1g zlib1g-dev openssl \
    libsqlite3-dev libssl-dev libffi-dev unzip pciutils net-tools \
    libblas-dev gfortran libblas3
```

Then rebuild Python 3.8:

```bash
cd /home/ubuntu/huawei/Python-3.8.5
./configure --prefix=/usr/local/python3.8.5 --enable-loadable-sqlite-extensions --enable-shared --with-ssl
make -j 8
make install
```

**Note:** After rebuilding Python, you do **not** need to reinstall pip dependencies. You can proceed directly to running the inference script.

---

## Summary

Your environment is now configured for YOLOv5 inference on Huawei Ascend NPU. You may proceed with running the continuous inference system as described in the main documentation.

