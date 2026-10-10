#!/usr/bin/env python3
"""Host tests in isolated temporary build/DBs; no live daemon or camera access."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
APP = ROOT.parents[1] / "wemo-matter-bridge" / "app-src"
def run(args, cwd):
    subprocess.run([str(x) for x in args], cwd=cwd, check=True)
with tempfile.TemporaryDirectory(prefix="netcam-bridge-tests-") as temporary:
    temp = Path(temporary)
    source = temp / "core"
    shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns("*.o", "*.so", "*.a"))
    run(["make", "clean"], source)
    run(["make", "-j4"], source)
    common = ["cc", "-g", "-I", "wemo_engine", "-I/usr/local/include/upnp"]
    for test in ("netcam_snapshot_test", "netcam_ipc_test"):
        run(common + ["tests/"+test+".c", "-o", temp/test, "-lsqlite3", "-lixml", "-pthread"], source)
        run([temp/test], source)
    run(common + ["-Dmain=wemo_daemon_main", "-c", "wemo_ctrl/wemo_ctrl_main.c", "-o", temp/"main.o"], source)
    objects = sorted(x for x in (source/"wemo_ctrl").glob("*.o") if x.name not in ("wemo_ctrl.o", "wemo_ctrl_main.o", "wemo_ipc_server.o"))
    run(common + ["tests/netcam_controller_test.c", temp/"main.o"] + objects + ["-o", temp/"controller", "-lupnp", "-lixml", "-lsqlite3", "-pthread"], source)
    (temp/"db").mkdir()
    run([temp/"controller", temp/"db"], source)
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", APP/"include", APP/"tests/sensor_endpoint_registry_test.cpp", APP/"src/matter/endpoint_registry.cpp", "-lsqlite3", "-o", temp/"registry"], source)
    run([temp/"registry",temp/"endpoints.db"],source)
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", APP/"include", APP/"tests/endpoint_identity_registry_test.cpp", APP/"src/matter/endpoint_identity_registry.cpp", "-lsqlite3", "-o", temp/"identity-registry"], source)
    run([temp/"identity-registry",temp/"endpoint_identity.db"],source)
    run(["g++", "-std=c++17", "-DHAVE_OPENWEMO_ENGINE=1", "-I", APP/"include", "-I", source/"wemo_engine", APP/"tests/netcam_adapter_test.cpp", APP/"src/adapters/wemo/wemo_adapter_openwemo.cpp", "-pthread", "-o", temp/"adapter"],source)
    run([temp/"adapter"],source)
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", APP/"include", APP/"tests/stack_lock_if_needed_test.cpp", "-o", temp/"stack-lock"],source)
    run([temp/"stack-lock"],source)
