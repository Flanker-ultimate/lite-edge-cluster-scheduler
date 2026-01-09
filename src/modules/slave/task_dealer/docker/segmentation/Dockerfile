FROM python:3.10-slim

WORKDIR /app

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY segment.py /app/segment.py

ENV INPUT_DIR=/app/input \
    OUTPUT_DIR=/app/output \
    SERVICE_NAME=Segmentation \
    SCAN_INTERVAL=1.0 \
    STABILITY_CHECK=0.05 \
    MAX_WAIT=5.0

CMD ["python", "/app/segment.py"]
