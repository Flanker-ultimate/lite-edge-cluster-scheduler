#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# by [jackhanyuan](https://github.com/jackhanyuan) 07/03/2022
# import faulthandler
# faulthandler.enable()
import argparse
import glob
import os
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import acl
import cv2
import numpy as np
import torch
from PIL import Image
from torchvision.ops import nms

FILE = Path(__file__).resolve()
ROOT = FILE.parents[0]  # Root directory
if str(ROOT) not in sys.path:
    sys.path.append(str(ROOT))  # add ROOT to PATH
ROOT = Path(os.path.relpath(ROOT, Path.cwd()))  # relative

from acl_net import Net
from constant import (
    ACL_ERROR_NONE,
    ACL_MEM_MALLOC_HUGE_FIRST,
    ACL_MEMCPY_DEVICE_TO_HOST,
    ACL_MEMCPY_HOST_TO_DEVICE,
    IMG_EXT,
)

buffer_method = {
    "in": acl.mdl.get_input_size_by_index,
    "out": acl.mdl.get_output_size_by_index,
}


def check_ret(message, ret):
    if ret != ACL_ERROR_NONE:
        raise Exception("{} failed ret={}".format(message, ret))


def xywh2xyxy(x):
    # Convert nx4 boxes from [x, y, w, h] to [x1, y1, x2, y2] where xy1=top-left, xy2=bottom-right
    y = x.clone() if isinstance(x, torch.Tensor) else np.copy(x)
    y[:, 0] = x[:, 0] - x[:, 2] / 2  # top left x
    y[:, 1] = x[:, 1] - x[:, 3] / 2  # top left y
    y[:, 2] = x[:, 0] + x[:, 2] / 2  # bottom right x
    y[:, 3] = x[:, 1] + x[:, 3] / 2  # bottom right y
    return y


def box_iou(box1, box2):
    # https://github.com/pytorch/vision/blob/master/torchvision/ops/boxes.py
    """
    Return intersection-over-union (Jaccard index) of boxes.
    Both sets of boxes are expected to be in (x1, y1, x2, y2) format.
    Arguments:
        box1 (Tensor[N, 4])
        box2 (Tensor[M, 4])
    Returns:
        iou (Tensor[N, M]): the NxM matrix containing the pairwise
            IoU values for every element in boxes1 and boxes2
    """

    def box_area(box):
        # box = 4xn
        return (box[2] - box[0]) * (box[3] - box[1])

    area1 = box_area(box1.T)
    area2 = box_area(box2.T)

    # inter(N,M) = (rb(N,M,2) - lt(N,M,2)).clamp(0).prod(2)
    inter = (
        (
            torch.min(box1[:, None, 2:], box2[:, 2:])
            - torch.max(box1[:, None, :2], box2[:, :2])
        )
        .clamp(0)
        .prod(2)
    )
    return inter / (
        area1[:, None] + area2 - inter
    )  # iou = inter / (area1 + area2 - inter)


def non_max_suppression(
    prediction,
    conf_thres=0.25,
    iou_thres=0.45,
    classes=None,
    agnostic=False,
    multi_label=False,
    labels=(),
    max_det=300,
):
    """Runs Non-Maximum Suppression (NMS) on inference results

    Returns:
         list of detections, on (n,6) tensor per image [xyxy, conf, cls]
    """

    nc = prediction.shape[2] - 5  # number of classes
    xc = prediction[..., 4] > conf_thres  # candidates

    # Checks
    assert 0 <= conf_thres <= 1, (
        f"Invalid Confidence threshold {conf_thres}, valid values are between 0.0 and 1.0"
    )
    assert 0 <= iou_thres <= 1, (
        f"Invalid IoU {iou_thres}, valid values are between 0.0 and 1.0"
    )

    # Settings
    min_wh, max_wh = 2, 4096  # (pixels) minimum and maximum box width and height
    max_nms = 30000  # maximum number of boxes into torchvision.ops.nms()
    time_limit = 10.0  # seconds to quit after
    redundant = True  # require redundant detections
    multi_label &= nc > 1  # multiple labels per box (adds 0.5ms/img)
    merge = False  # use merge-NMS

    t = time.time()
    output = [torch.zeros((0, 6), device=prediction.device)] * prediction.shape[0]
    for xi, x in enumerate(prediction):  # image index, image inference
        # Apply constraints
        # x[((x[..., 2:4] < min_wh) | (x[..., 2:4] > max_wh)).any(1), 4] = 0  # width-height
        x = x[xc[xi]]  # confidence

        # Cat apriori labels if autolabelling
        if labels and len(labels[xi]):
            l = labels[xi]
            v = torch.zeros((len(l), nc + 5), device=x.device)
            v[:, :4] = l[:, 1:5]  # box
            v[:, 4] = 1.0  # conf
            v[range(len(l)), l[:, 0].long() + 5] = 1.0  # cls
            x = torch.cat((x, v), 0)

        # If none remain process next image
        if not x.shape[0]:
            continue

        # Compute conf
        x[:, 5:] *= x[:, 4:5]  # conf = obj_conf * cls_conf

        # Box (center x, center y, width, height) to (x1, y1, x2, y2)
        box = xywh2xyxy(x[:, :4])

        # Detections matrix nx6 (xyxy, conf, cls)
        if multi_label:
            i, j = (x[:, 5:] > conf_thres).nonzero(as_tuple=False).T
            x = torch.cat((box[i], x[i, j + 5, None], j[:, None].float()), 1)
        else:  # best class only
            conf, j = x[:, 5:].max(1, keepdim=True)
            x = torch.cat((box, conf, j.float()), 1)[conf.view(-1) > conf_thres]

        # Filter by class
        if classes is not None:
            x = x[(x[:, 5:6] == torch.tensor(classes, device=x.device)).any(1)]

        # Apply finite constraint
        # if not torch.isfinite(x).all():
        #     x = x[torch.isfinite(x).all(1)]

        # Check shape
        n = x.shape[0]  # number of boxes
        if not n:  # no boxes
            continue
        elif n > max_nms:  # excess boxes
            x = x[x[:, 4].argsort(descending=True)[:max_nms]]  # sort by confidence

        # Batched NMS
        c = x[:, 5:6] * (0 if agnostic else max_wh)  # classes
        boxes, scores = x[:, :4] + c, x[:, 4]  # boxes (offset by class), scores
        i = nms(boxes, scores, iou_thres)  # NMS
        if i.shape[0] > max_det:  # limit detections
            i = i[:max_det]
        if merge and (1 < n < 3e3):  # Merge NMS (boxes merged using weighted mean)
            # update boxes as boxes(i,4) = weights(i,n) * boxes(n,4)
            iou = box_iou(boxes[i], boxes) > iou_thres  # iou matrix
            weights = iou * scores[None]  # box weights
            x[i, :4] = torch.mm(weights, x[:, :4]).float() / weights.sum(
                1, keepdim=True
            )  # merged boxes
            if redundant:
                i = i[iou.sum(1) > 1]  # require redundancy

        output[xi] = x[i]
        if (time.time() - t) > time_limit:
            print(f"WARNING: NMS time limit {time_limit}s exceeded")
            break  # time limit exceeded

    return output


def clip_coords(boxes, img_shape):
    # Clip bounding xyxy bounding boxes to image shape (height, width)
    boxes[:, 0].clamp_(0, img_shape[1])  # x1
    boxes[:, 1].clamp_(0, img_shape[0])  # y1
    boxes[:, 2].clamp_(0, img_shape[1])  # x2
    boxes[:, 3].clamp_(0, img_shape[0])  # y2


def scale_coords(img1_shape, coords, img0_shape, ratio_pad=None):
    # Rescale coords (xyxy) from img1_shape to img0_shape
    if ratio_pad is None:  # calculate from img0_shape
        gain = min(
            img1_shape[0] / img0_shape[0], img1_shape[1] / img0_shape[1]
        )  # gain  = old / new
        pad = (
            (img1_shape[1] - img0_shape[1] * gain) / 2,
            (img1_shape[0] - img0_shape[0] * gain) / 2,
        )  # wh padding
    else:
        gain = ratio_pad[0][0]
        pad = ratio_pad[1]

    coords[:, [0, 2]] -= pad[0]  # x padding
    coords[:, [1, 3]] -= pad[1]  # y padding
    coords[:, :4] /= gain
    clip_coords(coords, img0_shape)
    return coords


def resize_img(input_img, target_size, padding=True):
    if padding:
        old_size = input_img.shape[0:2]
        ratio = min(float(target_size[i]) / (old_size[i]) for i in range(len(old_size)))
        new_size = tuple([int(i * ratio) for i in old_size])
        img_new = cv2.resize(input_img, (new_size[1], new_size[0]))
        pad_w = target_size[1] - new_size[1]
        pad_h = target_size[0] - new_size[0]
        top, bottom = pad_h // 2, pad_h - (pad_h // 2)
        left, right = pad_w // 2, pad_w - (pad_w // 2)
        resized_img = cv2.copyMakeBorder(
            img_new, top, bottom, left, right, cv2.BORDER_CONSTANT, None, (0, 0, 0)
        )
    else:
        resized_img = cv2.resize(input_img, (target_size[1], target_size[0]))
    return resized_img


def load_label(label_name):
    label_lookup_path = label_name
    with open(label_lookup_path, "r") as f:
        label_contents = f.readlines()

    labels = np.array(list(map(lambda x: x.strip(), label_contents)))
    return labels


def preprocess(
    img_data,
    input_shape=(320, 320),
    image_format="BGR",
    channel_first=False,
    mean=[0.0, 0.0, 0.0],
    std=[255.0, 255, 255.0],
    fp16=False,
    padding=True,
):
    # Use context manager to ensure file handle is closed promptly
    with Image.open(img_data) as image_file:
        image_file = image_file.convert("RGB")
        org_img = np.array(image_file)
        # image_file = image_file.resize(input_shape)
        img = np.array(image_file)
    # rgb to bgr，改变通道顺序
    # print("before preprocess result:", img.shape, img.dtype, np.min(img), np.max(img))
    if image_format == "BGR":
        org_img = org_img[:, :, ::-1]
        img = img[:, :, ::-1]
    img = resize_img(img, input_shape, padding)
    shape = img.shape
    if fp16:
        img = img.astype("float16")
    else:
        img = img.astype("float32")
    # print("after preprocess result:", img.shape, img.dtype, np.min(img), np.max(img))
    img[:, :, 0] -= mean[0]
    img[:, :, 1] -= mean[1]
    img[:, :, 2] -= mean[2]
    img[:, :, 0] /= std[0]
    img[:, :, 1] /= std[1]
    img[:, :, 2] /= std[2]
    img = img.reshape([1] + list(shape))
    if channel_first:
        img = img.transpose([0, 3, 1, 2])
    if fp16:
        img_bytes = np.frombuffer(img.tobytes(), np.float16)
    else:
        img_bytes = np.frombuffer(img.tobytes(), np.float32)
    # print("after preprocess result:", img.shape, img.dtype, np.min(img), np.max(img))
    return org_img, img, img_bytes


def draw_box(image, boxes, names, scores, show_label=True):
    image_h, image_w, _ = image.shape

    for i, box in enumerate(boxes):
        box = np.array(box[:4], dtype=np.int32)  # xyxy

        line_width = int(3)
        txt_color = (255, 255, 255)
        box_color = (58, 56, 255)

        p1, p2 = (box[0], box[1]), (box[2], box[3])
        image = cv2.rectangle(image, p1, p2, box_color, line_width)

        if show_label:
            tf = max(line_width - 1, 1)  # font thickness
            box_label = "%s: %.2f" % (names[i], scores[i])
            w, h = cv2.getTextSize(
                box_label, 0, fontScale=line_width / 3, thickness=tf
            )[0]  # text width, height
            outside = p1[1] - h - 3 >= 0  # label fits outside box
            p2 = p1[0] + w, p1[1] - h - 3 if outside else p1[1] + h + 3

            image = cv2.rectangle(image, p1, p2, box_color, -1, cv2.LINE_AA)  # filled
            image = cv2.putText(
                image,
                box_label,
                (p1[0], p1[1] - 2 if outside else p1[1] + h + 2),
                0,
                line_width / 3,
                txt_color,
                thickness=tf,
                lineType=cv2.LINE_AA,
            )
    return image


def increment_path(path, exist_ok=False, sep="", mkdir=False):
    # Increment file or directory path, i.e. runs/exp --> runs/exp{sep}2, runs/exp{sep}3, ... etc.
    path = Path(path)  # os-agnostic
    if path.exists() and not exist_ok:
        path, suffix = (
            (path.with_suffix(""), path.suffix) if path.is_file() else (path, "")
        )
        dirs = glob.glob(f"{path}{sep}*")  # similar paths
        matches = [re.search(rf"%s{sep}(\d+)" % path.stem, d) for d in dirs]
        i = [int(m.groups()[0]) for m in matches if m]  # indices
        n = max(i) + 1 if i else 2  # increment number
        path = Path(f"{path}{sep}{n}{suffix}")  # increment path
    if mkdir:
        path.mkdir(parents=True, exist_ok=True)  # make directory
    return path


def parse_opt():
    parser = argparse.ArgumentParser(
        description="YOLOv5 Continuous Inference on Huawei Ascend NPU",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
        Example usage:
          # 使用相对路径示例
          python detect_yolov5_ascend.py --input-dir ../../../../data/input_images --output-dir ../../../../data/inference_results --output-format all

          # 或者绝对路径
          python detect_yolov5_ascend.py --input-dir /path/to/project/data/input_images --output-dir /path/to/project/data/inference_results
                """,
    )

    # Required arguments
    parser.add_argument(
        "--input-dir",
        type=str,
        required=True,
        help="Input directory where IPv4-named folders with images will be created",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        required=True,
        help="Output directory for inference results",
    )

    # Output format control
    parser.add_argument(
        "--output-format",
        type=str,
        default="all",
        choices=["label", "image", "all"],
        help='Output format: "label" (only .txt files), "image" (only images with boxes), "all" (both)',
    )

    # Model parameters
    parser.add_argument(
        "--weights",
        type=str,
        default=str(ROOT / "ascend/yolov5s.om"),
        help="Path to model weights (.om file)",
    )
    parser.add_argument(
        "--labels",
        type=str,
        default=str(ROOT / "ascend/yolov5.label"),
        help="Path to label file",
    )
    parser.add_argument(
        "--imgsz",
        nargs="+",
        type=int,
        default=[640, 640],
        help="Inference size [height, width]",
    )

    # Device parameters
    parser.add_argument(
        "--device", type=int, default=0, help="NPU device ID (e.g., 0 or 1)"
    )

    # Detection parameters
    parser.add_argument(
        "--conf-thres",
        type=float,
        default=0.25,
        help="Confidence threshold for detections",
    )
    parser.add_argument(
        "--iou-thres", type=float, default=0.45, help="NMS IoU threshold"
    )
    parser.add_argument(
        "--max-det", type=int, default=1000, help="Maximum detections per image"
    )
    parser.add_argument(
        "--agnostic-nms", action="store_true", help="Class-agnostic NMS"
    )

    # Timing parameters
    parser.add_argument(
        "--stability-check",
        type=float,
        default=0.01,
        help="Interval in seconds between file size stability checks (default: 0.01)",
    )
    parser.add_argument(
        "--max-wait",
        type=float,
        default=5.0,
        help="Maximum time in seconds to wait for a file to stabilize (default: 5.0)",
    )
    parser.add_argument(
        "--scan-interval",
        type=float,
        default=1.0,
        help="Interval in seconds between directory scans (default: 1.0)",
    )

    opt = parser.parse_args()
    return opt


def get_all_images_in_directory(directory):
    """Recursively get all image files in directory and subdirectories."""
    image_extensions = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff"}
    image_files = []

    if not os.path.exists(directory):
        return []

    for root, dirs, files in os.walk(directory):
        for file in files:
            if os.path.splitext(file)[1].lower() in image_extensions:
                full_path = os.path.join(root, file)
                # Get relative path from input directory
                rel_path = os.path.relpath(full_path, directory)
                image_files.append((full_path, rel_path))

    return image_files


def is_file_ready(file_path, stability_check_interval=0.01, max_wait=5.0):
    """
    Check if a file is completely written by monitoring size stability.

    Args:
        file_path: Path to the file to check
        stability_check_interval: Time between size checks (seconds)
        max_wait: Maximum time to wait for stability (seconds)

    Returns:
        True if file is stable and ready, False otherwise
    """
    try:
        if not os.path.exists(file_path):
            return False

        elapsed = 0
        while elapsed < max_wait:
            size1 = os.path.getsize(file_path)
            time.sleep(stability_check_interval)
            size2 = os.path.getsize(file_path)

            if size1 == size2 and size1 > 0:
                # File size is stable and non-zero
                return True

            elapsed += stability_check_interval

        # Timeout reached
        return False
    except Exception as e:
        return False


def process_image(
    image_path,
    rel_path,
    net,
    labels,
    input_size,
    conf_thres,
    iou_thres,
    agnostic_nms,
    max_det,
    fileter_classes,
    opt,
    input_dir,
    output_dir,
):
    """Process a single image and save results."""
    t1 = time.perf_counter()

    # Preprocess and run inference
    org_img, image_npy, image_bytes = preprocess(
        image_path, input_shape=input_size, image_format="BGR", channel_first=True
    )
    print(
        f"Received image: {rel_path}, original size = {org_img.shape[1]}x{org_img.shape[0]}"
    )

    result = net.run([image_bytes])
    pred = np.frombuffer(bytearray(result[0]), dtype=np.float32)

    # Reshape based on input size
    if input_size[0] == 640 and input_size[1] == 640:
        pred = pred.reshape(1, 25200, -1)  # 640 x 640
    else:
        # For other sizes, calculate anchor count
        pred = pred.reshape(1, -1, pred.shape[0] // (1 * (pred.shape[0] // 85)))

    # Apply NMS
    pred = torch.tensor(pred)
    pred = non_max_suppression(
        pred, conf_thres, iou_thres, fileter_classes, agnostic_nms, max_det=max_det
    )

    s = ""
    boxes = []
    names = []
    scores = []
    detections = []

    for i, det in enumerate(pred):  # detections per image
        if len(det):
            # Rescale boxes from img_size to im0 size
            det[:, :4] = scale_coords(input_size, det[:, :4], org_img.shape).round()

            # Print results
            for c in det[:, -1].unique():
                n = (det[:, -1] == c).sum()  # detections per class
                s += f"{n} {labels[int(c)]}{'s' * (n > 1)}, "  # add to string

            # Collect results
            for *xyxy, conf, cls in reversed(det):
                c = int(cls)  # integer class
                name = labels[c]
                box = [int(xyxy[0]), int(xyxy[1]), int(xyxy[2]), int(xyxy[3])]
                score = float(conf)

                boxes.append(box)
                names.append(name)
                scores.append(score)
                detections.append((cls, xyxy, conf))

    # Save results based on output_format
    img_name = os.path.basename(rel_path)
    img_name_noext = os.path.splitext(img_name)[0]
    img_ext = os.path.splitext(img_name)[1]

    # Get the subdirectory structure (e.g., "192.168.1.1")
    subdir = os.path.dirname(rel_path)

    # Save label file if needed (create empty file even if no detections)
    if opt.output_format in ["label", "all"]:
        label_dir = os.path.join(output_dir, "label", subdir)
        os.makedirs(label_dir, exist_ok=True)
        txt_path = os.path.join(label_dir, img_name_noext + ".txt")

        with open(txt_path, "w") as f:
            for cls, xyxy, conf in detections:
                line = (cls, *xyxy, conf)
                f.write(("%g " * len(line)).rstrip() % line + "\n")

    # Save image with boxes if needed
    if opt.output_format in ["image", "all"]:
        image_dir = os.path.join(output_dir, "image", subdir)
        os.makedirs(image_dir, exist_ok=True)
        output_path = os.path.join(image_dir, img_name)

        out_img = org_img.copy()
        if len(boxes) > 0:
            out_img = draw_box(out_img, boxes, names, scores)
        cv2.imwrite(output_path, out_img)

    t2 = time.perf_counter()
    return s, t2 - t1


if __name__ == "__main__":
    opt = parse_opt()

    # Create input and output directories
    input_dir = Path(opt.input_dir)
    output_dir = Path(opt.output_dir)
    input_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("YOLOv5 Continuous Inference System")
    print("=" * 60)
    print(f"Input directory:      {input_dir}")
    print(f"Output directory:     {output_dir}")
    print(f"Output format:        {opt.output_format}")
    print(f"Stability check:      {opt.stability_check}s")
    print(f"Max wait per file:    {opt.max_wait}s")
    print(f"Scan interval:        {opt.scan_interval}s")
    print("=" * 60)

    # Initialize ACL
    print("\nACL Init:")
    ret = acl.init()
    check_ret("acl.init", ret)
    device_id = opt.device

    # Load model
    print(f"Loading model: {opt.weights}")
    model_path = str(opt.weights)
    net = None

    try:
        net = Net(device_id, model_path)

        # Load labels
        label_path = opt.labels
        labels = load_label(label_path)
        input_size = tuple(opt.imgsz) if isinstance(opt.imgsz, list) else opt.imgsz

        conf_thres = opt.conf_thres
        iou_thres = opt.iou_thres
        agnostic_nms = opt.agnostic_nms
        max_det = opt.max_det
        fileter_classes = None

        print(
            f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Model loaded successfully"
        )
        print(f"Input size: {input_size}")
        print(f"Number of classes: {len(labels)}")
        print()
        print("Starting continuous detection...")
        print("Press Ctrl+C to stop")
        print("-" * 60)

        total_processed = 0
        scan_count = 0

        # Continuous scanning loop
        while True:
            scan_count += 1

            # Scan for all images in input directory
            image_list = get_all_images_in_directory(str(input_dir))

            if len(image_list) > 0:
                print(f"\n[Scan #{scan_count}] Found {len(image_list)} images")

                # Process each image
                batch_start_time = time.perf_counter()
                skipped_count = 0

                for idx, (image_path, rel_path) in enumerate(image_list, 1):
                    try:
                        # Check if file still exists
                        if not os.path.exists(image_path):
                            print(
                                f"  [{idx}/{len(image_list)}] SKIP: {rel_path} (file disappeared)"
                            )
                            skipped_count += 1
                            continue

                        # Wait for file to be completely written (size stability check)
                        if not is_file_ready(
                            image_path, opt.stability_check, opt.max_wait
                        ):
                            print(
                                f"  [{idx}/{len(image_list)}] SKIP: {rel_path} (file not ready/timeout)"
                            )
                            skipped_count += 1
                            continue

                        # Process image
                        detection_result, process_time = process_image(
                            image_path,
                            rel_path,
                            net,
                            labels,
                            input_size,
                            conf_thres,
                            iou_thres,
                            agnostic_nms,
                            max_det,
                            fileter_classes,
                            opt,
                            input_dir,
                            output_dir,
                        )

                        # Delete source image after processing
                        os.remove(image_path)

                        total_processed += 1
                        result_str = (
                            detection_result if detection_result else "No detections"
                        )

                    except Exception as e:
                        print(
                            f"  [{idx}/{len(image_list)}] ERROR processing {rel_path}: {e}"
                        )
                        skipped_count += 1

                batch_time = time.perf_counter() - batch_start_time
                print(f"Batch processing completed in {batch_time:.2f}s")
                if skipped_count > 0:
                    print(
                        f"Processed: {len(image_list) - skipped_count}/{len(image_list)} images ({skipped_count} skipped)"
                    )
                print(f"Total processed: {total_processed} images")

            # Wait before next scan
            time.sleep(opt.scan_interval)

    except KeyboardInterrupt:
        print("\n\nStopping detection...")
        print(f"Total images processed: {total_processed}")
        print("=" * 60)

    except Exception as e:
        print(f"\nError: {e}")
        import traceback

        traceback.print_exc()

    finally:
        if net:
            net.destroy()
        print("Resources released. Exiting.")
