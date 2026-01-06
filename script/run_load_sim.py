#!/usr/bin/env python3
import argparse
import json
import os
import random
import sys
import threading
import time


CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(CURRENT_DIR, ".."))
LOG_DIR = os.path.join(PROJECT_ROOT, "workspace", "slave", "log")
STATE_FILE = os.path.join(LOG_DIR, "load_sim_state.json")
LOAD_SIM_DIR = os.path.join(PROJECT_ROOT, "load_simulate")

os.makedirs(LOG_DIR, exist_ok=True)

sys.path.insert(0, LOAD_SIM_DIR)
try:
    from workloads import WorkloadExecutor
except Exception as exc:
    raise SystemExit(f"failed to import load_simulate workloads: {exc}")


class LoadState:
    def __init__(self, path: str):
        self.path = path
        self._lock = threading.Lock()
        self.state = {"active_io": 0, "active_net": 0, "active_yolo": 0, "timestamp_ms": 0}

    def set_flag(self, key: str, value: int):
        with self._lock:
            self.state[key] = value
            self.state["timestamp_ms"] = int(time.time() * 1000)
            tmp = self.path + ".part"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(self.state, f, ensure_ascii=False)
            os.replace(tmp, self.path)


def run_loop(name: str, flag_key: str, func, state: LoadState, min_d: int, max_d: int, min_idle: int, max_idle: int):
    while True:
        time.sleep(random.randint(min_idle, max_idle))
        duration = random.randint(min_d, max_d)
        state.set_flag(flag_key, 1)
        try:
            func(duration)
        finally:
            state.set_flag(flag_key, 0)


def main():
    parser = argparse.ArgumentParser(description="Random load simulator")
    parser.add_argument("--enable-io", action="store_true", help="enable IO workload")
    parser.add_argument("--enable-net", action="store_true", help="enable network workload")
    parser.add_argument("--enable-yolo", action="store_true", help="enable YOLO workload (simulated)")
    parser.add_argument("--min-duration", type=int, default=5, help="min workload duration (s)")
    parser.add_argument("--max-duration", type=int, default=15, help="max workload duration (s)")
    parser.add_argument("--min-idle", type=int, default=5, help="min idle gap (s)")
    parser.add_argument("--max-idle", type=int, default=15, help="max idle gap (s)")
    parser.add_argument("--net-bandwidth-mb", type=int, default=20, help="network workload bandwidth (MB/s)")
    args = parser.parse_args()

    state = LoadState(STATE_FILE)
    threads = []

    if args.enable_io:
        t = threading.Thread(
            target=run_loop,
            args=(
                "io",
                "active_io",
                WorkloadExecutor.task_io_stress,
                state,
                args.min_duration,
                args.max_duration,
                args.min_idle,
                args.max_idle,
            ),
            daemon=True,
        )
        threads.append(t)

    if args.enable_net:
        def net_task(duration):
            WorkloadExecutor.task_network_stress(duration, protocol="http", target_bandwidth_mb=args.net_bandwidth_mb)
        t = threading.Thread(
            target=run_loop,
            args=(
                "net",
                "active_net",
                net_task,
                state,
                args.min_duration,
                args.max_duration,
                args.min_idle,
                args.max_idle,
            ),
            daemon=True,
        )
        threads.append(t)

    if args.enable_yolo:
        t = threading.Thread(
            target=run_loop,
            args=(
                "yolo",
                "active_yolo",
                WorkloadExecutor.task_yolo_inference,
                state,
                args.min_duration,
                args.max_duration,
                args.min_idle,
                args.max_idle,
            ),
            daemon=True,
        )
        threads.append(t)

    if not threads:
        raise SystemExit("no workloads enabled")

    for t in threads:
        t.start()

    print("load simulation running, state file:", STATE_FILE)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
