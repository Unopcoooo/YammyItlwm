#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import subprocess

R = Path(__file__).resolve().parents[1]
O = R / "build/host-tests"
O.mkdir(parents=True, exist_ok=True)
S = R
IW = S / "itlwm/awdl"
T = S / "tests/awdl"
I_U = R / "tests/awdl/fixtures"
I_T = R / "tests/awdl/fixtures"

tests = [
    ("native-peer-cache-control", T / "native_peer_cache_control_test.cpp", []),
    ("native-receive-policy", T / "native_receive_policy_test.cpp", []),
    ("native-tx-terminal", T / "native_tx_terminal_test.cpp", []),
    ("native-discovery-session", T / "native_discovery_session_test.cpp", []),
    ("ibss-peer-runtime", T / "ibss_peer_runtime_test.cpp", [I_U]),
    ("ibss-data-tx", T / "ibss_data_tx_test.cpp", [I_U]),
    ("mdns-multicast", T / "mdns_multicast_test.cpp", [I_T]),
    ("mdns-adapter", T / "mdns_adapter_test.cpp", [I_T]),
    ("native-umi-frame", T / "native_umi_test.cpp", []),
    ("native-umi-runtime", T / "native_umi_runtime_test.cpp", []),
    ("native-election-params", T / "native_election_params_test.cpp", []),
    ("native-aw-window", T / "native_aw_window_test.cpp", []),
]

results = []
for name, source, includes in tests:
    binary = O / name
    command = [
        "clang++", "-std=c++14", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(IW),
    ]
    command += ["-I" + str(path) for path in includes]
    command += [str(source), "-o", str(binary)]
    compile_result = subprocess.run(command, capture_output=True, text=True)
    (O / (name + "-compile.log")).write_text(compile_result.stdout + compile_result.stderr)
    results.append({
        "name": name + "-compile",
        "command": command,
        "exit": compile_result.returncode,
        "stdout": compile_result.stdout,
        "stderr": compile_result.stderr,
    })
    if compile_result.returncode:
        break
    run_command = [str(binary)]
    run_result = subprocess.run(run_command, capture_output=True, text=True)
    (O / (name + ".log")).write_text(run_result.stdout + run_result.stderr)
    results.append({
        "name": name,
        "command": run_command,
        "exit": run_result.returncode,
        "stdout": run_result.stdout,
        "stderr": run_result.stderr,
    })
    if run_result.returncode:
        break

(O / "test-results.json").write_text(json.dumps(results, indent=2) + "\n")
assert all(item["exit"] == 0 for item in results)
assert len(results) == 24

source_hashes = {}
for path in sorted((O / "source").rglob("*")):
    if path.is_file():
        source_hashes[str(path.relative_to(O))] = hashlib.sha256(path.read_bytes()).hexdigest()
(O / "source-hashes.json").write_text(json.dumps(source_hashes, indent=2) + "\n")
print(json.dumps({"tests": len(results), "test_exit": 0,
                  "source_files": len(source_hashes)}, indent=2))
