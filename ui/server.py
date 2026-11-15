#!/usr/bin/env python3
"""Giao diện thử nghiệm: server cục bộ gọi các chương trình C++ trong build/.

Chỉ dùng thư viện chuẩn của Python. Chạy từ PowerShell ở thư mục gốc repo:

    python ui/server.py --open

Server chỉ lắng nghe ở 127.0.0.1 và chỉ chạy các chương trình khai báo trong
ui/programs.json. Xem README, mục "Giao diện thử nghiệm".
"""
from __future__ import annotations

import argparse
import atexit
import csv
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import webbrowser
from collections import OrderedDict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

UI_DIR = Path(__file__).resolve().parent
REPO_DIR = UI_DIR.parent
STATIC_DIR = UI_DIR / "static"
SAMPLE_DIR = REPO_DIR / "data" / "sample"
MANIFEST_PATH = UI_DIR / "programs.json"

MAX_BODY = 5 * 1024 * 1024      # giới hạn dữ liệu gửi lên (byte)
KEEP_RUNS = 20                  # số lần chạy gần nhất được giữ lại để tải kết quả
PREVIEW_MAX_N = 16              # chỉ xem trước ma trận kết quả khi n <= giá trị này
MAX_LOG_CHARS = 20000           # cắt bớt stdout/stderr dài
EXE_SUFFIX = ".exe" if os.name == "nt" else ""

PROBLEMS = ("dot", "matmul")
KINDS = ("seq", "par")
PROGRAM_PARAMS = ("b", "threads", "variant", "schedule", "pin")
SCHEDULES = ("static", "dynamic")
CSV_COLUMNS = ("problem", "variant", "n", "block", "threads", "sched", "pinned", "run",
               "seed", "t_init", "t_compute", "t_io", "t_total", "check")

ID_RE = re.compile(r"^[A-Za-z0-9_]+$")
SAMPLE_RE = re.compile(r"^[A-Za-z0-9_.-]+\.txt$")
HEADER_RE = re.compile(r"^\s*(dot|matmul)\s+(\d+)\s*$")
MATRIX_RESULT_RE = re.compile(r"^\s*matrix\s+(\d+)\s*$")
# Số thập phân mà cả JavaScript lẫn phép đọc >> của C++ đều hiểu giống nhau
NUMBER_RE = re.compile(r"^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$")

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".svg": "image/svg+xml",
}

CONFIG = argparse.Namespace(build_dir=REPO_DIR / "build", msys2_bin=None, timeout=120.0, port=8000)
RUN_LOCK = threading.Lock()      # chỉ chạy một chương trình mỗi lúc để không nhiễu nhau
RUNS: "OrderedDict[str, dict]" = OrderedDict()
RUNS_LOCK = threading.Lock()


class ApiError(Exception):
    """Lỗi trả về cho trình duyệt kèm mã HTTP."""

    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status


# ---------------------------------------------------------------- manifest

def load_manifest() -> dict:
    """Đọc và kiểm tra ui/programs.json. Đọc lại mỗi lần để sửa file là thấy ngay."""
    try:
        data = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ApiError(500, f"Không đọc được {MANIFEST_PATH.name}: {exc}") from exc

    seen = set()
    for prog in data.get("programs", []):
        pid = prog.get("id", "")
        if not isinstance(pid, str) or not ID_RE.match(pid) or pid in seen:
            raise ApiError(500, f"programs.json: id không hợp lệ hoặc bị trùng: {pid!r}")
        seen.add(pid)
        if prog.get("problem") not in PROBLEMS:
            raise ApiError(500, f"programs.json: {pid} có problem phải là một trong {PROBLEMS}")
        if prog.get("kind") not in KINDS:
            raise ApiError(500, f"programs.json: {pid} có kind phải là một trong {KINDS}")
        unknown = set(prog.get("params", [])) - set(PROGRAM_PARAMS)
        if unknown:
            raise ApiError(500, f"programs.json: {pid} có tham số không hỗ trợ: {sorted(unknown)}")
        if "variant" in prog.get("params", []) and not prog.get("variants"):
            raise ApiError(500, f"programs.json: {pid} có tham số variant nhưng thiếu danh sách variants")
    return data


def exe_path(pid: str) -> Path:
    return CONFIG.build_dir / f"{pid}{EXE_SUFFIX}"


def api_programs() -> dict:
    manifest = load_manifest()
    programs = []
    for prog in manifest.get("programs", []):
        exe = exe_path(prog["id"])
        programs.append({**prog, "built": exe.is_file(), "exe": str(exe)})
    return {
        "problems": manifest.get("problems", {}),
        "programs": programs,
        "build_dir": str(CONFIG.build_dir),
        "msys2_bin": str(CONFIG.msys2_bin) if CONFIG.msys2_bin else None,
    }


# ---------------------------------------------------------------- dữ liệu

def read_header(path: Path) -> tuple[str, int] | None:
    try:
        with path.open(encoding="utf-8", errors="replace") as f:
            m = HEADER_RE.match(f.readline())
    except OSError:
        return None
    return (m.group(1), int(m.group(2))) if m else None


def api_samples() -> dict:
    samples = []
    if SAMPLE_DIR.is_dir():
        for path in sorted(SAMPLE_DIR.glob("*.txt")):
            header = read_header(path)
            samples.append({
                "name": path.name,
                "problem": header[0] if header else None,
                "n": header[1] if header else None,
                "size": path.stat().st_size,
            })
    return {"samples": samples}


def sample_path(name: str) -> Path:
    if not SAMPLE_RE.match(name or ""):
        raise ApiError(400, "Tên file mẫu không hợp lệ.")
    path = SAMPLE_DIR / name
    if not path.is_file():
        raise ApiError(404, f"Không có file mẫu {name} trong data/sample/.")
    return path


def check_data_text(text: str, expected_problem: str) -> int:
    """Kiểm tra dữ liệu theo định dạng chung, trả về n."""
    lines = text.strip().splitlines()
    if not lines:
        raise ApiError(400, "Dữ liệu trống.")
    m = HEADER_RE.match(lines[0])
    if not m:
        raise ApiError(400, "Dòng đầu phải là 'dot <n>' hoặc 'matmul <n>'.")
    problem, n = m.group(1), int(m.group(2))
    if problem != expected_problem:
        raise ApiError(400, f"Dữ liệu thuộc bài '{problem}' nhưng chương trình thuộc bài '{expected_problem}'.")
    if n < 1:
        raise ApiError(400, "n phải lớn hơn 0.")
    tokens = " ".join(lines[1:]).split()
    need = 2 * n if problem == "dot" else 2 * n * n
    if len(tokens) != need:
        raise ApiError(400, f"Cần đúng {need} số sau dòng đầu, đang có {len(tokens)}.")
    for i, tok in enumerate(tokens, start=1):
        if not NUMBER_RE.match(tok) or not math.isfinite(float(tok)):
            raise ApiError(400, f"Giá trị thứ {i} không phải số hợp lệ: {tok!r}")
    return n


def to_int(value, name: str, low: int, high: int) -> int:
    try:
        number = int(value)
    except (TypeError, ValueError):
        raise ApiError(400, f"{name} phải là số nguyên.") from None
    if isinstance(value, float) and value != number:
        raise ApiError(400, f"{name} phải là số nguyên.")
    if not low <= number <= high:
        raise ApiError(400, f"{name} phải nằm trong [{low}, {high}].")
    return number


# ---------------------------------------------------------------- chạy chương trình

def register_run(pid: str) -> tuple[str, Path]:
    run_id = uuid.uuid4().hex[:12]
    run_dir = Path(tempfile.mkdtemp(prefix="pc_ui_"))
    with RUNS_LOCK:
        RUNS[run_id] = {"dir": run_dir, "program": pid}
        while len(RUNS) > KEEP_RUNS:
            _, old = RUNS.popitem(last=False)
            shutil.rmtree(old["dir"], ignore_errors=True)
    return run_id, run_dir


def cleanup_runs() -> None:
    with RUNS_LOCK:
        for info in RUNS.values():
            shutil.rmtree(info["dir"], ignore_errors=True)
        RUNS.clear()


def read_metrics(csv_path: Path) -> tuple[dict | None, list[str]]:
    if not csv_path.is_file():
        return None, ["Chương trình không ghi file CSV (--csv). Có thể vẫn là stub hoặc chưa theo hợp đồng."]
    with csv_path.open(encoding="utf-8", errors="replace", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return None, ["File CSV không có dòng dữ liệu nào."]
    row = rows[-1]
    warnings = []
    missing = [c for c in CSV_COLUMNS if c not in row]
    if missing:
        warnings.append(f"CSV thiếu cột: {', '.join(missing)}.")
    if len(rows) > 1:
        warnings.append(f"CSV có {len(rows)} dòng; hợp đồng là đúng một dòng mỗi lần chạy.")
    return row, warnings


def read_preview(out_path: Path) -> tuple[dict | None, list[str]]:
    if not out_path.is_file():
        return None, ["Chương trình không ghi file kết quả (--output)."]
    with out_path.open(encoding="utf-8", errors="replace") as f:
        head = f.readline().strip()
        if head.startswith("dot_result"):
            tokens = head.split()[1:] or f.readline().split()
            return {"type": "scalar", "value": tokens[0] if tokens else None}, []
        m = MATRIX_RESULT_RE.match(head)
        if m:
            n = int(m.group(1))
            if n > PREVIEW_MAX_N:
                return {"type": "matrix", "n": n, "rows": None}, []
            rows = [f.readline().split() for _ in range(n)]
            return {"type": "matrix", "n": n, "rows": rows}, []
        return {"type": "unknown", "head": head[:300]}, ["Dòng đầu file kết quả không đúng định dạng chung."]


def describe_returncode(rc: int) -> str | None:
    if rc == 0:
        return None
    unsigned = rc & 0xFFFFFFFF
    if unsigned == 0xC0000135:
        return ("Thiếu DLL khi chạy (thường là libgomp-1.dll). Kiểm tra tùy chọn --msys2-bin "
                "trỏ tới thư mục bin của MSYS2 UCRT64.")
    if unsigned >= 0xC0000000:
        return f"Chương trình bị dừng bất thường (mã 0x{unsigned:08X})."
    return f"Chương trình trả mã thoát {rc}."


def handle_run(body: dict) -> dict:
    programs = {p["id"]: p for p in load_manifest().get("programs", [])}
    pid = body.get("program")
    prog = programs.get(pid)
    if prog is None:
        raise ApiError(404, f"Không có chương trình {pid!r} trong programs.json.")
    exe = exe_path(pid)
    if not exe.is_file():
        raise ApiError(409, f"Chưa build {exe.name}. Chạy make trong MSYS2 UCRT64.")

    params = body.get("params") or {}
    data = body.get("data") or {}
    if not isinstance(params, dict) or not isinstance(data, dict):
        raise ApiError(400, "params và data phải là object.")
    allowed = prog.get("params", [])
    unknown = set(params) - set(allowed)
    if unknown:
        raise ApiError(400, f"{pid} không nhận tham số: {sorted(unknown)}.")

    argv = [str(exe)]
    source = data.get("source")
    if source == "seed":
        argv += ["--n", str(to_int(data.get("n"), "n", 1, 1_000_000_000)),
                 "--seed", str(to_int(data.get("seed", 42), "seed", 0, 2**63 - 1))]
    elif source == "sample":
        path = sample_path(data.get("sample", ""))
        header = read_header(path)
        if header is None or header[0] != prog["problem"]:
            raise ApiError(400, f"File mẫu {path.name} không phải dữ liệu của bài {prog['problem']}.")
        argv += ["--input", str(path)]
    elif source != "inline":
        raise ApiError(400, "data.source phải là seed, sample hoặc inline.")

    for key in allowed:
        value = params.get(key)
        if value is None or value == "":
            continue
        if key == "b":
            argv += ["--b", str(to_int(value, "b", 1, 1_000_000))]
        elif key == "threads":
            threads = to_int(value, "threads", 1, 1024)
            if prog.get("threads") and threads not in prog["threads"]:
                raise ApiError(400, f"{pid} chỉ nhận threads trong {prog['threads']}.")
            argv += ["--threads", str(threads)]
        elif key == "variant":
            if value not in prog["variants"]:
                raise ApiError(400, f"{pid} chỉ nhận variant trong {prog['variants']}.")
            argv += ["--variant", value]
        elif key == "schedule":
            if value not in SCHEDULES:
                raise ApiError(400, f"schedule phải là một trong {SCHEDULES}.")
            argv += ["--schedule", value]
        elif key == "pin":
            argv += ["--pin", "1" if value in (True, 1, "1", "true") else "0"]

    inline_text = None
    if source == "inline":
        inline_text = str(data.get("text", ""))
        check_data_text(inline_text, prog["problem"])

    run_id, run_dir = register_run(pid)
    if inline_text is not None:
        input_path = run_dir / "input.txt"
        input_path.write_text(inline_text.strip() + "\n", encoding="utf-8")
        argv += ["--input", str(input_path)]
    csv_path = run_dir / "metrics.csv"
    out_path = run_dir / "output.txt"
    argv += ["--run", "0", "--csv", str(csv_path), "--output", str(out_path)]
    if body.get("verify"):
        argv.append("--verify")

    env = os.environ.copy()
    if CONFIG.msys2_bin:
        env["PATH"] = str(CONFIG.msys2_bin) + os.pathsep + env.get("PATH", "")

    result = {"run_id": run_id, "program": pid, "cmd": subprocess.list2cmdline(argv)}
    with RUN_LOCK:
        start = time.perf_counter()
        try:
            proc = subprocess.run(argv, cwd=REPO_DIR, env=env, capture_output=True,
                                  timeout=CONFIG.timeout)
        except subprocess.TimeoutExpired:
            return {**result, "ok": False, "returncode": None,
                    "error": f"Quá thời gian {CONFIG.timeout:g} giây, đã dừng chương trình.",
                    "stdout": "", "stderr": "", "metrics": None, "result_preview": None,
                    "warnings": [], "wall": time.perf_counter() - start}
        except OSError as exc:
            return {**result, "ok": False, "returncode": None, "error": f"Không chạy được: {exc}",
                    "stdout": "", "stderr": "", "metrics": None, "result_preview": None,
                    "warnings": [], "wall": 0.0}
        wall = time.perf_counter() - start

    metrics, warnings = read_metrics(csv_path)
    preview, more = read_preview(out_path)
    warnings += more
    decode = lambda b: b.decode("utf-8", errors="replace")[-MAX_LOG_CHARS:]
    return {**result, "ok": proc.returncode == 0, "returncode": proc.returncode,
            "error": describe_returncode(proc.returncode), "stdout": decode(proc.stdout),
            "stderr": decode(proc.stderr), "metrics": metrics, "result_preview": preview,
            "output_available": out_path.is_file(), "warnings": warnings, "wall": wall}


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "PCTestUI/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("[ui] " + (fmt % args) + "\n")

    def host_ok(self) -> bool:
        host = (self.headers.get("Host") or "").lower()
        return host in (f"127.0.0.1:{CONFIG.port}", f"localhost:{CONFIG.port}")

    def send_bytes(self, data: bytes, ctype: str, status: int = 200, extra: dict | None = None):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, obj, status: int = 200):
        self.send_bytes(json.dumps(obj, ensure_ascii=False).encode("utf-8"),
                        "application/json; charset=utf-8", status)

    def dispatch(self, method: str):
        try:
            if not self.host_ok():
                raise ApiError(403, "Chỉ nhận yêu cầu tới 127.0.0.1 hoặc localhost.")
            path = unquote(urlparse(self.path).path)
            if method == "GET":
                self.route_get(path)
            elif path == "/api/run":
                self.send_json(handle_run(self.read_json()))
            else:
                raise ApiError(404, "Không có API này.")
        except ApiError as exc:
            self.send_json({"error": str(exc)}, exc.status)
        except Exception as exc:  # lỗi ngoài dự kiến: báo về trình duyệt thay vì đóng kết nối
            self.log_message("Lỗi server: %r", exc)
            self.send_json({"error": f"Lỗi server: {exc}"}, 500)

    def do_GET(self):
        self.dispatch("GET")

    def do_POST(self):
        self.dispatch("POST")

    def read_json(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BODY:
            # Đọc bỏ phần body (có giới hạn) để trình duyệt nhận được mã 413 thay vì lỗi mất kết nối
            remaining = length if length <= 64 * MAX_BODY else 0
            while remaining > 0:
                chunk = self.rfile.read(min(remaining, 1 << 16))
                if not chunk:
                    break
                remaining -= len(chunk)
            self.close_connection = True
            raise ApiError(413, f"Dữ liệu vượt {MAX_BODY // (1024 * 1024)} MB. Dùng nguồn sinh theo seed cho dữ liệu lớn.")
        try:
            body = json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise ApiError(400, "Body không phải JSON hợp lệ.") from None
        if not isinstance(body, dict):
            raise ApiError(400, "Body phải là một object JSON.")
        return body

    def route_get(self, path: str):
        if path == "/api/programs":
            self.send_json(api_programs())
        elif path == "/api/samples":
            self.send_json(api_samples())
        elif path.startswith("/api/samples/"):
            sample = sample_path(path.rsplit("/", 1)[-1])
            self.send_bytes(sample.read_bytes(), "text/plain; charset=utf-8")
        elif path.startswith("/api/output/"):
            run_id = path.rsplit("/", 1)[-1]
            with RUNS_LOCK:
                info = RUNS.get(run_id)
            out = info["dir"] / "output.txt" if info else None
            if out is None or not out.is_file():
                raise ApiError(404, "Không còn file kết quả của lần chạy này.")
            self.send_bytes(out.read_bytes(), "text/plain; charset=utf-8", extra={
                "Content-Disposition": f'attachment; filename="{info["program"]}_output.txt"'})
        elif path.startswith("/api/"):
            raise ApiError(404, "Không có API này.")
        else:
            self.send_static(path)

    def send_static(self, path: str):
        name = "index.html" if path in ("", "/") else path.lstrip("/")
        root = STATIC_DIR.resolve()
        target = (root / name).resolve()
        if root not in target.parents or not target.is_file():
            raise ApiError(404, "Không tìm thấy.")
        self.send_bytes(target.read_bytes(), CONTENT_TYPES.get(target.suffix, "application/octet-stream"))


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    parser = argparse.ArgumentParser(description="Server cho giao diện thử nghiệm của đồ án.")
    parser.add_argument("--port", type=int, default=8000, help="cổng lắng nghe (mặc định 8000)")
    parser.add_argument("--build-dir", default="build",
                        help="thư mục chứa file chạy, tương đối với thư mục gốc repo (mặc định build)")
    parser.add_argument("--msys2-bin", default=r"C:\msys64\ucrt64\bin",
                        help="thư mục bin của MSYS2 UCRT64, được thêm vào PATH để tìm libgomp-1.dll")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="giới hạn thời gian mỗi lần chạy, tính bằng giây (mặc định 120)")
    parser.add_argument("--open", action="store_true", help="tự mở trình duyệt")
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    CONFIG.build_dir = build_dir if build_dir.is_absolute() else REPO_DIR / build_dir
    msys2_bin = Path(args.msys2_bin)
    CONFIG.msys2_bin = msys2_bin if msys2_bin.is_dir() else None
    CONFIG.timeout = args.timeout
    CONFIG.port = args.port

    try:
        load_manifest()
    except ApiError as exc:
        print(f"Lỗi: {exc}", file=sys.stderr)
        return 1
    if CONFIG.msys2_bin is None:
        print(f"Cảnh báo: không thấy {args.msys2_bin}; chương trình dùng OpenMP có thể báo thiếu DLL.",
              file=sys.stderr)

    try:
        server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    except OSError as exc:
        print(f"Không mở được cổng {args.port}: {exc}. Thử --port khác.", file=sys.stderr)
        return 1
    atexit.register(cleanup_runs)

    url = f"http://127.0.0.1:{args.port}/"
    print(f"Giao diện thử nghiệm: {url}")
    print(f"Thư mục chương trình: {CONFIG.build_dir}")
    print("Nhấn Ctrl+C để dừng.", flush=True)
    if args.open:
        threading.Timer(0.5, webbrowser.open, args=(url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Đã dừng server.")
    finally:
        server.server_close()
        cleanup_runs()
    return 0


if __name__ == "__main__":
    sys.exit(main())
