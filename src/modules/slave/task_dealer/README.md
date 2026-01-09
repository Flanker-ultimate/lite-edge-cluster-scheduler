# Task Dealer

This folder groups task handlers for the slave node.

## Layout

- `可执行程序/`: executable-style handlers (native binaries or scripts)
- `docker/`: containerized handlers

Current content:

- `可执行程序/yolov5-ascend/`: YOLOv5 Ascend inference program
- `docker/segmentation/`: simple image segmentation container

## Segmentation Docker

### Build image

```bash
cd src/modules/slave/task_dealer/docker/segmentation

docker build -t edge-segmentation:latest .
```

### Run locally

```bash
docker run --rm --name seg_demo \
  -v /path/to/input:/app/input \
  -v /path/to/output:/app/output \
  -e SERVICE_NAME=Segmentation \
  edge-segmentation:latest
```

The container scans:

```
<input_dir>/_sub_reqs_ready/<ServiceName>/<seq>__<sub_req_id>/<client_ip>/<file>
```

Outputs are written to:

```
<output_dir>/mask/<client_ip>/<file>.png
```

## slave_backend.json example (container)

Add a service entry like the following:

```json
{
  "services": {
    "Segmentation": {
      "backend": "container",
      "agent_autostart": true,
      "input_dir": "workspace/slave/data/Segmentation/input",
      "output_dir": "workspace/slave/data/Segmentation/output",
      "result_dir": "workspace/slave/data/Segmentation/output/mask",
      "start_cmd": "docker run --rm --name seg_${DEVICE_ID} -v ${INPUT_DIR}:/app/input -v ${OUTPUT_DIR}:/app/output -e SERVICE_NAME=Segmentation edge-segmentation:latest"
    }
  }
}
```

Notes:

- `ServiceName` must match `tasktype` and the directory name under `_sub_reqs_ready`.
- The container assumes FIFO ordering by sub-req directory name.
