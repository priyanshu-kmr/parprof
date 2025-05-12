#!/usr/bin/env python3
"""End-to-end tests: run parprof on real programs and check its report.

Usage: PARPROF_BUILD=build python3 tests/test_parprof.py   (or run `ctest`)
"""
import csv
import os
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

BUILD = os.path.abspath(os.environ.get("PARPROF_BUILD", "build"))
PARPROF = os.path.join(BUILD, "parprof")


def run(*args, env=None, timeout=120):
    """Runs parprof and returns (exit code, report text)."""
    full_env = {k: v for k, v in os.environ.items() if not k.startswith("OMP_")}
    full_env.update(env or {})
    p = subprocess.run([PARPROF, *args], capture_output=True, text=True, env=full_env, timeout=timeout)
    return p.returncode, p.stderr


def read_text(path):
    with open(path) as f:
        return f.read().strip()


def demo(name):
    return os.path.join(BUILD, name)


def thread_rows(report):
    """The per-thread table rows: lines that start with a TID."""
    return [line.split() for line in report.splitlines() if line.strip()[:1].isdigit()]


class ProcessHandling(unittest.TestCase):
    def test_passes_through_exit_code(self):
        code, report = run("sh", "-c", "exit 3")
        self.assertEqual(code, 3)
        self.assertIn("exited with code 3", report)

    def test_reports_signal_death(self):
        code, report = run("sh", "-c", "kill -TERM $$")
        self.assertEqual(code, 128 + signal.SIGTERM)
        self.assertIn("killed by signal 15", report)

    def test_missing_program(self):
        code, report = run("/no/such/program")
        self.assertEqual(code, 127)
        self.assertIn("No such file or directory", report)

    def test_program_flags_are_not_parsed_as_ours(self):
        code, _ = run("sh", "-c", "exit 0", "-g", "--scale")
        self.assertEqual(code, 0)

    def test_ctrl_c_stops_program_but_still_reports(self):
        # Ctrl-C goes to the whole foreground process group: parprof must survive it.
        p = subprocess.Popen([PARPROF, "sleep", "10"], stderr=subprocess.PIPE, text=True,
                             start_new_session=True)
        time.sleep(0.5)
        os.killpg(p.pid, signal.SIGINT)
        _, report = p.communicate(timeout=10)
        self.assertEqual(p.returncode, 128 + signal.SIGINT)
        self.assertIn("killed by signal 2", report)

    def test_sets_passive_wait_policy_unless_told_not_to(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "env")
            run("sh", "-c", f"echo $OMP_WAIT_POLICY > {out}")
            self.assertEqual(read_text(out), "passive")
            run("--keep-wait-policy", "sh", "-c", f"echo ${{OMP_WAIT_POLICY:-unset}} > {out}")
            self.assertEqual(read_text(out), "unset")
            run("sh", "-c", f"echo $OMP_WAIT_POLICY > {out}", env={"OMP_WAIT_POLICY": "active"})
            self.assertEqual(read_text(out), "active")  # the user's choice wins

    def test_no_fd_leak_across_runs(self):
        # The program prints how many fds parprof (its parent) has open; must not grow per run.
        p = subprocess.run([PARPROF, "--gpu", "--scale", "1,1,1,1", "sh", "-c", "ls /proc/$PPID/fd | wc -l"],
                           capture_output=True, text=True, timeout=60)
        counts = p.stdout.split()
        self.assertEqual(len(counts), 4)
        self.assertEqual(len(set(counts)), 1, f"fd count grew: {counts}")

    def test_bad_options(self):
        self.assertEqual(run()[0], 2)
        self.assertEqual(run("--scale", "1,x", "true")[0], 2)
        self.assertEqual(run("--interval", "0", "true")[0], 2)


class Measurements(unittest.TestCase):
    def test_sees_every_thread(self):
        code, report = run(demo("false_sharing"), "--fixed", env={"OMP_NUM_THREADS": "4"})
        self.assertEqual(code, 0)
        self.assertEqual(len(thread_rows(report)), 4)

    def test_hardware_counters(self):
        _, report = run(demo("false_sharing"), "--fixed", env={"OMP_NUM_THREADS": "1"})
        self.assertNotIn("IPC n/a", report, "perf_event_open counters unavailable")

    def test_timeline_csv(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "timeline.csv")
            code, _ = run("--csv", path, "-i", "5", demo("imbalance"), env={"OMP_NUM_THREADS": "4"})
            self.assertEqual(code, 0)
            with open(path) as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(set(rows[0]), {"t_ms", "tid", "cpu_pct", "core"})
            self.assertEqual(len({r["tid"] for r in rows}), 4)
            self.assertTrue(any(float(r["cpu_pct"]) > 50 for r in rows))


class Diagnoses(unittest.TestCase):
    """Each demo's bug must be found, and its fixed version must come out clean."""

    def check_demo(self, name, finding, env=None):
        code, report = run(demo(name), env=env)
        self.assertEqual(code, 0)
        self.assertIn(f"[{finding}]", report)
        code, report = run(demo(name), "--fixed", env=env)
        self.assertEqual(code, 0)
        self.assertIn("no problems detected", report)

    def test_false_sharing(self):
        self.check_demo("false_sharing", "low-ipc")

    def test_load_imbalance(self):
        self.check_demo("imbalance", "load-imbalance")

    def test_load_imbalance_without_smt_effects(self):
        self.check_demo("imbalance", "load-imbalance", env={"OMP_NUM_THREADS": "4"})

    def test_lock_contention(self):
        self.check_demo("contention", "lock-contention")

    def test_oversubscription(self):
        threads = str(4 * os.cpu_count())
        _, report = run(demo("imbalance"), "--fixed", env={"OMP_NUM_THREADS": threads})
        self.assertIn("[cpu-oversubscription]", report)

    def test_scaling_confirms_false_sharing(self):
        _, report = run("--scale", "1,8", demo("false_sharing"))
        self.assertIn("[cache-contention]", report)
        _, report = run("--scale", "1,8", demo("false_sharing"), "--fixed")
        self.assertIn("no cache contention detected", report)


@unittest.skipUnless(os.path.exists(demo("gpu_busy")) and shutil.which("nvidia-smi"), "no CUDA GPU")
class Gpu(unittest.TestCase):
    def test_gpu_sampling(self):
        code, report = run("--gpu", demo("gpu_busy"))
        self.assertEqual(code, 0)
        line = next(l for l in report.splitlines() if l.startswith("gpu:"))
        peak = int(line.split("peak ")[1].split("%")[0])
        self.assertGreater(peak, 50)


if __name__ == "__main__":
    unittest.main(verbosity=2)
