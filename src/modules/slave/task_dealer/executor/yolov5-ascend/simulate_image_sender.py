#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Image Sender Simulator
Simulates external program sending images to input directory with IPv4-named folders.
"""

import argparse
import os
import random
import shutil
import time
from pathlib import Path


def generate_random_ipv4():
    """Generate a random IPv4 address."""
    return f"{random.randint(1, 255)}.{random.randint(0, 255)}.{random.randint(0, 255)}.{random.randint(1, 255)}"


def get_image_files(source_dir):
    """Get all image files from source directory."""
    image_extensions = {'.jpg', '.jpeg', '.png', '.bmp', '.tif', '.tiff'}
    image_files = []

    source_path = Path(source_dir)
    if not source_path.exists():
        raise ValueError(f"Source directory does not exist: {source_dir}")

    for file_path in source_path.iterdir():
        if file_path.is_file() and file_path.suffix.lower() in image_extensions:
            image_files.append(file_path)

    if not image_files:
        raise ValueError(f"No image files found in source directory: {source_dir}")

    return image_files


def send_images_for_ip(ip_address, source_images, target_dir, num_images, send_interval):
    """Send images to a specific IPv4 folder."""
    # Create folder for this IP
    ip_folder = Path(target_dir) / ip_address
    ip_folder.mkdir(parents=True, exist_ok=True)

    # Check if we have enough unique images
    available_images = len(source_images)
    if num_images > available_images:
        print(f"\n[WARNING] Requested {num_images} images but only {available_images} unique images available.")
        print(f"[WARNING] Will send all {available_images} unique images instead.")
        actual_num_images = available_images
    else:
        actual_num_images = num_images

    print(f"\n{'=' * 60}")
    print(f"Processing IP: {ip_address}")
    print(f"Target folder: {ip_folder}")
    print(f"Sending {actual_num_images} unique images at {send_interval}s intervals")
    print(f"{'=' * 60}")

    # Randomly select unique images (no replacement)
    selected_images = random.sample(source_images, actual_num_images)

    # Send images
    for i, source_img in enumerate(selected_images):
        # Generate destination filename with index
        dest_filename = f"{source_img.stem}_{i:04d}{source_img.suffix}"
        dest_path = ip_folder / dest_filename

        # Copy image to destination
        shutil.copy2(source_img, dest_path)

        print(f"  [{i+1}/{actual_num_images}] Sent: {dest_filename} ({source_img.name})")

        # Wait before sending next image (except for the last one)
        if i < actual_num_images - 1:
            time.sleep(send_interval)

    print(f"Completed sending {actual_num_images} unique images for {ip_address}")

    return actual_num_images  # Return the actual number of images sent


def parse_opt():
    parser = argparse.ArgumentParser(
        description='Image Sender Simulator for YOLOv5 Continuous Inference Testing',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Example usage:
  python simulate_image_sender.py --source-dir ./test_images --target-dir ./input --num-ips 5 --num-images 50
  python simulate_image_sender.py --source-dir /data/images --target-dir /data/input --num-ips 10 --num-images 100 --send-interval 0.1
        ''')

    # Required arguments
    parser.add_argument('--source-dir', type=str, required=True,
                        help='Source directory containing images to send')
    parser.add_argument('--target-dir', type=str, required=True,
                        help='Target input directory where IPv4 folders will be created')

    # Optional arguments with defaults
    parser.add_argument('--num-ips', type=int, default=10,
                        help='Number of random IPv4 addresses to generate (default: 10)')
    parser.add_argument('--num-images', type=int, default=100,
                        help='Number of images to send per IPv4 address (default: 100)')
    parser.add_argument('--send-interval', type=float, default=0.05,
                        help='Time interval in seconds between sending images (default: 0.05)')
    parser.add_argument('--ip-interval', type=float, default=0.0,
                        help='Time interval in seconds between processing different IPs (default: 0.0)')

    return parser.parse_args()


def main():
    opt = parse_opt()

    print("=" * 60)
    print("Image Sender Simulator")
    print("=" * 60)
    print(f"Source directory:     {opt.source_dir}")
    print(f"Target directory:     {opt.target_dir}")
    print(f"Number of IPs:        {opt.num_ips}")
    print(f"Images per IP:        {opt.num_images}")
    print(f"Send interval:        {opt.send_interval}s")
    print(f"IP interval:          {opt.ip_interval}s")
    print("=" * 60)

    # Get source images
    try:
        source_images = get_image_files(opt.source_dir)
        print(f"\nFound {len(source_images)} images in source directory")
    except Exception as e:
        print(f"Error: {e}")
        return

    # Create target directory if it doesn't exist
    target_path = Path(opt.target_dir)
    target_path.mkdir(parents=True, exist_ok=True)

    # Generate random IPv4 addresses
    ip_addresses = []
    for i in range(opt.num_ips):
        ip = generate_random_ipv4()
        # Ensure uniqueness
        while ip in ip_addresses:
            ip = generate_random_ipv4()
        ip_addresses.append(ip)

    print(f"\nGenerated {len(ip_addresses)} unique IPv4 addresses:")
    for i, ip in enumerate(ip_addresses, 1):
        print(f"  {i}. {ip}")

    # Start sending
    print("\n" + "=" * 60)
    print("Starting image transmission...")
    print("=" * 60)

    start_time = time.time()
    total_images = 0

    try:
        for idx, ip_address in enumerate(ip_addresses, 1):
            images_sent = send_images_for_ip(
                ip_address,
                source_images,
                opt.target_dir,
                opt.num_images,
                opt.send_interval
            )
            total_images += images_sent

            # Wait before processing next IP (except for the last one)
            if idx < len(ip_addresses) and opt.ip_interval > 0:
                print(f"\nWaiting {opt.ip_interval}s before next IP...")
                time.sleep(opt.ip_interval)

    except KeyboardInterrupt:
        print("\n\nStopped by user")

    except Exception as e:
        print(f"\nError during transmission: {e}")
        import traceback
        traceback.print_exc()

    finally:
        elapsed_time = time.time() - start_time
        print("\n" + "=" * 60)
        print("Transmission Summary")
        print("=" * 60)
        print(f"Total images sent:    {total_images}")
        print(f"Total time:           {elapsed_time:.2f}s")
        print(f"Average rate:         {total_images/elapsed_time:.2f} images/s")
        print("=" * 60)


if __name__ == "__main__":
    main()
