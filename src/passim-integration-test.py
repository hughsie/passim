#!/usr/bin/env python3
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import hashlib
import os
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.request
import urllib.error

try:
    import dbus
    import dbusmock
except ImportError:
    print("SKIP: python3-dbusmock not available", file=sys.stderr)
    sys.exit(77)


class PassimIntegrationTest(dbusmock.DBusTestCase):
    """Integration test that starts a real passimd with mock Avahi."""

    DAEMON_PORT = 27501
    TEST_CONTENT = b"test data for passim integration testing"
    TEST_BASENAME = "test-firmware.bin"
    STATUS_RUNNING = 3

    @classmethod
    def setUpClass(cls):
        cls.start_system_bus()
        cls.test_hash = hashlib.sha256(cls.TEST_CONTENT).hexdigest()
        cls.unresolvable_hash = "e" * 64
        cls.tmpdir = tempfile.mkdtemp(prefix="passim-test-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir, ignore_errors=True)

        builddir = os.environ.get("PASSIM_BUILDDIR", "")
        srcdir = os.environ.get("PASSIM_SRCDIR", "")

        cls._setup_directories()
        cls._seed_audit_log()
        cls._setup_sysconfpkgdir()
        cls._setup_config()
        cls._install_dbus_interface(srcdir)
        cls._start_mock_avahi()
        cls._start_daemon(builddir)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "daemon_proc") and cls.daemon_proc is not None:
            cls.daemon_proc.send_signal(signal.SIGINT)
            try:
                cls.daemon_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.daemon_proc.kill()
                cls.daemon_proc.wait()

        if hasattr(cls, "avahi_proc") and cls.avahi_proc is not None:
            cls.avahi_proc.kill()
            cls.avahi_proc.wait()

        super().tearDownClass()

    @classmethod
    def _assert_no_daemon_leaks(cls):
        """Fail if the daemon leaked memory allocated by passim itself.

        LeakSanitizer writes its report when the daemon exits, so this is only
        meaningful for a build configured with -Db_sanitize=address. Leaks
        inside system libraries are not reported against us, so only stack
        frames from passim sources count.
        """
        try:
            with open(cls.daemon_log, "rb") as f:
                log = f.read().decode(errors="replace")
        except OSError:
            return
        idx = log.find("LeakSanitizer")
        if idx == -1:
            return
        frames = [
            line.strip()
            for line in log[idx:].splitlines()
            if "/src/passim-" in line or "/libpassim/passim-" in line
        ]
        if frames:
            raise AssertionError(
                "daemon leaked memory allocated by passim:\n" + "\n".join(frames)
            )

    @staticmethod
    def _parse_config_h(path, key):
        with open(path, "r") as f:
            for line in f:
                if line.startswith(f"#define {key} "):
                    return line.split('"')[1]
        raise KeyError(f"{key} not found in {path}")

    @classmethod
    def _setup_directories(cls):
        cls.data_path = os.path.join(cls.tmpdir, "lib", "passim", "data")
        os.makedirs(cls.data_path, exist_ok=True)
        cls.test_file = os.path.join(
            cls.data_path, f"{cls.test_hash}-{cls.TEST_BASENAME}"
        )
        with open(cls.test_file, "wb") as f:
            f.write(cls.TEST_CONTENT)

        cls.doomed_hash = hashlib.sha256(cls.DOOMED_CONTENT).hexdigest()
        cls.doomed_file = os.path.join(
            cls.data_path, f"{cls.doomed_hash}-{cls.DOOMED_BASENAME}"
        )
        with open(cls.doomed_file, "wb") as f:
            f.write(cls.DOOMED_CONTENT)

        os.makedirs(os.path.join(cls.tmpdir, "log", "passim"), exist_ok=True)

    # indexed at startup, then deleted so that sending it always fails
    DOOMED_CONTENT = b"this file disappears before it is ever shared"
    DOOMED_BASENAME = "doomed.bin"

    SEEDED_DOWNLOAD_SIZE = 42000

    @classmethod
    def _seed_audit_log(cls):
        audit_file = os.path.join(cls.tmpdir, "log", "passim", "audit.log")
        fake_hash = "a" * 64
        line = f"2026-01-01T00:00:00Z SHARE hash={fake_hash},basename=seeded.bin,size={cls.SEEDED_DOWNLOAD_SIZE},ipaddr=127.0.0.1\n"
        with open(audit_file, "w") as f:
            f.write(line)

    UNRESOLVABLE_PEER = "Passim-Unresolvable"
    START_FAILS_PEER = "Passim-Start-Fails"

    SYSCONFPKG_CONTENT = b"sysconfpkgdir test file content"
    SYSCONFPKG_BASENAME = "sysconfpkg-test.bin"

    @classmethod
    def _setup_sysconfpkgdir(cls):
        cls.sysconfpkg_data = os.path.join(cls.tmpdir, "sysconfpkg-data")
        os.makedirs(cls.sysconfpkg_data, exist_ok=True)
        sysconfpkg_file = os.path.join(cls.sysconfpkg_data, cls.SYSCONFPKG_BASENAME)
        with open(sysconfpkg_file, "wb") as f:
            f.write(cls.SYSCONFPKG_CONTENT)
        cls.sysconfpkg_hash = hashlib.sha256(cls.SYSCONFPKG_CONTENT).hexdigest()

        passim_d = os.path.join(cls.tmpdir, "passim.d")
        os.makedirs(passim_d, exist_ok=True)

        fd, cls.sysconfpkg_conf = tempfile.mkstemp(
            suffix=".conf", prefix="passim-test-", dir=passim_d
        )
        with os.fdopen(fd, "w") as f:
            f.write(f"[passim]\nPath={cls.sysconfpkg_data}\n")

        fd2, missing_conf = tempfile.mkstemp(
            suffix=".conf", prefix="passim-missing-", dir=passim_d
        )
        with os.fdopen(fd2, "w") as f:
            f.write("[passim]\nPath=/nonexistent/path\n")

    @classmethod
    def _setup_config(cls):
        with open(os.path.join(cls.tmpdir, "passim.conf"), "w") as f:
            f.write(f"[daemon]\nPort={cls.DAEMON_PORT}\n")

    @classmethod
    def _install_dbus_interface(cls, srcdir):
        dbus_iface_dir = os.path.join(cls.tmpdir, "dbus-1", "interfaces")
        os.makedirs(dbus_iface_dir, exist_ok=True)
        shutil.copy2(
            os.path.join(srcdir, "org.freedesktop.Passim.xml"),
            os.path.join(dbus_iface_dir, "org.freedesktop.Passim.xml"),
        )

    @classmethod
    def _start_mock_avahi(cls):
        """Start a mock Avahi service on the mock system bus."""
        cls.avahi_proc = cls.spawn_server(
            "org.freedesktop.Avahi",
            "/",
            "org.freedesktop.Avahi.Server2",
            system_bus=True,
        )

        bus = cls.get_dbus(system_bus=True)
        avahi_root = bus.get_object("org.freedesktop.Avahi", "/")
        avahi_mock = dbus.Interface(avahi_root, dbusmock.MOCK_IFACE)

        avahi_mock.AddMethod(
            "org.freedesktop.Avahi.Server2",
            "EntryGroupNew",
            "",
            "o",
            'ret = "/org/freedesktop/Avahi/EntryGroup/1"',
        )

        avahi_mock.AddObject(
            "/org/freedesktop/Avahi/EntryGroup/1",
            "org.freedesktop.Avahi.EntryGroup",
            {},
            [],
        )

        eg_obj = bus.get_object(
            "org.freedesktop.Avahi",
            "/org/freedesktop/Avahi/EntryGroup/1",
        )
        eg_mock = dbus.Interface(eg_obj, dbusmock.MOCK_IFACE)

        for method, sig in [
            ("Reset", ""),
            ("AddService", "iiussssqaay"),
            ("AddServiceSubtype", "iiussss"),
            ("Commit", ""),
        ]:
            eg_mock.AddMethod("org.freedesktop.Avahi.EntryGroup", method, sig, "", "")

        test_hash_prefix = cls.test_hash[:60]
        unresolvable_prefix = cls.unresolvable_hash[:60]
        avahi_mock.AddMethod(
            "org.freedesktop.Avahi.Server2",
            "ServiceBrowserPrepare",
            "iissu",
            "o",
            f'if "_{test_hash_prefix}" in args[2]:\n'
            '    ret = "/org/freedesktop/Avahi/ServiceBrowser/1"\n'
            f'elif "_{unresolvable_prefix}" in args[2]:\n'
            '    ret = "/org/freedesktop/Avahi/ServiceBrowser/3"\n'
            "else:\n"
            '    ret = "/org/freedesktop/Avahi/ServiceBrowser/2"',
        )

        for sb_path in [
            "/org/freedesktop/Avahi/ServiceBrowser/1",
            "/org/freedesktop/Avahi/ServiceBrowser/2",
            "/org/freedesktop/Avahi/ServiceBrowser/3",
        ]:
            avahi_mock.AddObject(
                sb_path,
                "org.freedesktop.Avahi.ServiceBrowser",
                {},
                [],
            )
            sb_obj = bus.get_object("org.freedesktop.Avahi", sb_path)
            sb_mock = dbus.Interface(sb_obj, dbusmock.MOCK_IFACE)
            sb_mock.AddMethod(
                "org.freedesktop.Avahi.ServiceBrowser",
                "Free",
                "",
                "",
                "",
            )

        sb1_obj = bus.get_object(
            "org.freedesktop.Avahi",
            "/org/freedesktop/Avahi/ServiceBrowser/1",
        )
        sb1_mock = dbus.Interface(sb1_obj, dbusmock.MOCK_IFACE)
        sb1_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceBrowser",
            "Start",
            "",
            "",
            "import dbus\n"
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceBrowser", "ItemNew", "iisssu",'
            " [dbus.Int32(-1), dbus.Int32(0),"
            f' dbus.String("{cls.UNRESOLVABLE_PEER}"),'
            ' dbus.String("_cache._tcp"),'
            ' dbus.String("local"), dbus.UInt32(0)])\n'
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceBrowser", "ItemNew", "iisssu",'
            " [dbus.Int32(-1), dbus.Int32(0),"
            f' dbus.String("{cls.START_FAILS_PEER}"),'
            ' dbus.String("_cache._tcp"),'
            ' dbus.String("local"), dbus.UInt32(0)])\n'
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceBrowser", "ItemNew", "iisssu",'
            " [dbus.Int32(-1), dbus.Int32(0),"
            ' dbus.String("Passim-Peer"),'
            ' dbus.String("_cache._tcp"),'
            ' dbus.String("local"), dbus.UInt32(0)])\n'
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceBrowser", "AllForNow", "", [])',
        )

        sb2_obj = bus.get_object(
            "org.freedesktop.Avahi",
            "/org/freedesktop/Avahi/ServiceBrowser/2",
        )
        sb2_mock = dbus.Interface(sb2_obj, dbusmock.MOCK_IFACE)
        sb2_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceBrowser",
            "Start",
            "",
            "",
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceBrowser", "AllForNow", "", [])',
        )

        sb3_obj = bus.get_object(
            "org.freedesktop.Avahi",
            "/org/freedesktop/Avahi/ServiceBrowser/3",
        )
        sb3_mock = dbus.Interface(sb3_obj, dbusmock.MOCK_IFACE)
        sb3_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceBrowser",
            "Start",
            "",
            "",
            "import dbus\n"
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceBrowser", "ItemNew", "iisssu",'
            " [dbus.Int32(-1), dbus.Int32(0),"
            f' dbus.String("{cls.UNRESOLVABLE_PEER}"),'
            ' dbus.String("_cache._tcp"),'
            ' dbus.String("local"), dbus.UInt32(0)])\n'
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceBrowser", "AllForNow", "", [])',
        )

        avahi_mock.AddMethod(
            "org.freedesktop.Avahi.Server2",
            "ServiceResolverPrepare",
            "iisssiu",
            "o",
            f'if args[2] == "{cls.UNRESOLVABLE_PEER}":\n'
            '    ret = "/org/freedesktop/Avahi/ServiceResolver/2"\n'
            f'elif args[2] == "{cls.START_FAILS_PEER}":\n'
            '    ret = "/org/freedesktop/Avahi/ServiceResolver/3"\n'
            "else:\n"
            '    ret = "/org/freedesktop/Avahi/ServiceResolver/1"',
        )

        avahi_mock.AddObject(
            "/org/freedesktop/Avahi/ServiceResolver/2",
            "org.freedesktop.Avahi.ServiceResolver",
            {},
            [],
        )
        sr2_obj = bus.get_object(
            "org.freedesktop.Avahi",
            "/org/freedesktop/Avahi/ServiceResolver/2",
        )
        sr2_mock = dbus.Interface(sr2_obj, dbusmock.MOCK_IFACE)
        sr2_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceResolver",
            "Start",
            "",
            "",
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceResolver", "Failure", "s",'
            ' ["Timeout reached"])',
        )
        sr2_mock.AddMethod("org.freedesktop.Avahi.ServiceResolver", "Free", "", "", "")

        avahi_mock.AddObject(
            "/org/freedesktop/Avahi/ServiceResolver/1",
            "org.freedesktop.Avahi.ServiceResolver",
            {},
            [],
        )

        sr_obj = bus.get_object(
            "org.freedesktop.Avahi",
            "/org/freedesktop/Avahi/ServiceResolver/1",
        )
        sr_mock = dbus.Interface(sr_obj, dbusmock.MOCK_IFACE)
        emit_found = (
            "import dbus\n"
            "self.EmitSignal("
            '"org.freedesktop.Avahi.ServiceResolver", "Found",'
            ' "iissssisqaayu",'
            " [dbus.Int32(-1), dbus.Int32(0),"
            ' dbus.String("Passim-Peer"),'
            ' dbus.String("_cache._tcp"),'
            ' dbus.String("local"),'
            ' dbus.String("peer.local"), dbus.Int32(0),'
            ' dbus.String("127.0.0.1"),'
            f" dbus.UInt16({cls.DAEMON_PORT}),"
            ' dbus.Array([], signature="ay"),'
            " dbus.UInt32(0)])"
        )
        sr_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceResolver", "Start", "", "", emit_found
        )
        sr_mock.AddMethod("org.freedesktop.Avahi.ServiceResolver", "Free", "", "", "")

        # the signal is delivered, and only then does Start() itself fail
        avahi_mock.AddObject(
            "/org/freedesktop/Avahi/ServiceResolver/3",
            "org.freedesktop.Avahi.ServiceResolver",
            {},
            [],
        )
        sr3_obj = bus.get_object(
            "org.freedesktop.Avahi",
            "/org/freedesktop/Avahi/ServiceResolver/3",
        )
        sr3_mock = dbus.Interface(sr3_obj, dbusmock.MOCK_IFACE)
        sr3_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceResolver",
            "Start",
            "",
            "",
            emit_found + '\nraise dbus.exceptions.DBusException("Start failed")',
        )
        sr3_mock.AddMethod("org.freedesktop.Avahi.ServiceResolver", "Free", "", "", "")

    @classmethod
    def _start_daemon(cls, builddir):
        """Start the passimd binary."""
        daemon_path = os.path.join(builddir, "src", "passimd")
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ.get("DBUS_SYSTEM_BUS_ADDRESS", "")
        env["LOCALSTATEDIR"] = cls.tmpdir
        env["SYSCONFDIR"] = cls.tmpdir
        env["DATADIR"] = cls.tmpdir
        env["G_MESSAGES_DEBUG"] = "all"
        env["G_DEBUG"] = ""

        cls.daemon_log = os.path.join(cls.tmpdir, "passimd.log")
        cls.daemon_log_fd = open(cls.daemon_log, "wb")
        cls.addClassCleanup(cls.daemon_log_fd.close)
        # a cleanup so that it still runs if tearDownClass() raises, and after
        # the mock bus has stopped so that a leak never leaves it running
        cls.addClassCleanup(cls._assert_no_daemon_leaks)
        cls.daemon_proc = subprocess.Popen(
            [daemon_path, "--insecure"],
            env=env,
            stdout=cls.daemon_log_fd,
            stderr=subprocess.STDOUT,
        )

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                s = socket.create_connection(("127.0.0.1", cls.DAEMON_PORT), timeout=1)
                s.close()
                return
            except OSError:
                rc = cls.daemon_proc.poll()
                if rc is not None:
                    raise RuntimeError(f"passimd exited with code {rc}") from None
                time.sleep(0.2)

        raise TimeoutError(f"passimd did not start listening on port {cls.DAEMON_PORT}")

    def _daemon_log_offset(self):
        """Current size of the daemon log, to read only what happens next."""
        return os.path.getsize(self.daemon_log)

    def _daemon_log_since(self, offset, needle=None, timeout=1.0):
        """Daemon log written after @offset, waiting for @needle to show up."""
        deadline = time.monotonic() + timeout
        while True:
            with open(self.daemon_log, "rb") as f:
                f.seek(offset)
                log = f.read().decode(errors="replace")
            if needle is None or needle in log:
                return log
            if time.monotonic() > deadline:
                return log
            time.sleep(0.05)

    def _audit_log(self):
        path = os.path.join(self.tmpdir, "log", "passim", "audit.log")
        with open(path, "r") as f:
            return f.read()

    def _https_get(self, path, expected_status=200):
        """Make an HTTPS GET request to the daemon."""
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        url = f"https://127.0.0.1:{self.DAEMON_PORT}{path}"
        req = urllib.request.Request(url)
        try:
            with urllib.request.urlopen(req, context=ctx, timeout=10) as resp:
                body = resp.read()
                if expected_status != resp.status:
                    self.fail(
                        f"Expected {expected_status}, got {resp.status} for {path}"
                    )
                return resp.status, body, resp.headers
        except urllib.error.HTTPError as e:
            with e:
                body = e.read()
                if expected_status != e.code:
                    self.fail(
                        f"Expected {expected_status}, got {e.code} for {path}: "
                        f"{body.decode(errors='replace')}"
                    )
                return e.code, body, e.headers

    # -- HTTP tests --

    def test_index_page(self):
        status, body, headers = self._https_get("/")
        self.assertEqual(status, 200)
        html = body.decode()
        self.assertIn(self.TEST_BASENAME, html)

    def test_download_valid(self):
        path = f"/{self.TEST_BASENAME}?sha256={self.test_hash}"
        status, body, headers = self._https_get(path)
        self.assertEqual(status, 200)
        self.assertEqual(body, self.TEST_CONTENT)
        self.assertIn("attachment", headers.get("Content-Disposition", ""))

    def test_no_query_string(self):
        status, body, _ = self._https_get("/somefile", expected_status=400)
        self.assertEqual(status, 400)

    def test_sha256_required(self):
        status, body, _ = self._https_get("/somefile?foo=bar", expected_status=400)
        self.assertEqual(status, 400)

    def test_duplicate_sha256(self):
        h = "a" * 64
        status, body, _ = self._https_get(
            f"/file?sha256={h}&sha256={h}", expected_status=400
        )
        self.assertEqual(status, 400)

    def test_malformed_sha256(self):
        status, body, _ = self._https_get("/file?sha256=notahex", expected_status=406)
        self.assertEqual(status, 406)

    def test_invalid_localhost_param(self):
        status, body, _ = self._https_get(
            f"/file?sha256={self.test_hash}&localhost=maybe",
            expected_status=400,
        )
        self.assertEqual(status, 400)

    def test_unknown_hash(self):
        unknown = "b" * 64
        status, body, _ = self._https_get(
            f"/file?sha256={unknown}", expected_status=404
        )
        self.assertEqual(status, 404)

    def test_share_failure_not_counted(self):
        """A file that could not be sent must not count as having been shared."""
        bus = self.get_dbus(system_bus=True)
        proxy = bus.get_object("org.freedesktop.Passim", "/")
        props = dbus.Interface(proxy, dbus.PROPERTIES_IFACE)
        saving = int(props.Get("org.freedesktop.Passim", "DownloadSaving"))

        if os.path.exists(self.doomed_file):
            os.unlink(self.doomed_file)
        path = f"/{self.DOOMED_BASENAME}?sha256={self.doomed_hash}"
        status, _, _ = self._https_get(path, expected_status=500)
        self.assertEqual(status, 500)

        self.assertNotIn(self.doomed_hash, self._audit_log())
        self.assertEqual(
            saving, int(props.Get("org.freedesktop.Passim", "DownloadSaving"))
        )

    def test_sysconfpkgdir_item(self):
        path = f"/{self.SYSCONFPKG_BASENAME}?sha256={self.sysconfpkg_hash}"
        status, body, _ = self._https_get(path)
        self.assertEqual(status, 200)
        self.assertEqual(body, self.SYSCONFPKG_CONTENT)

    # -- D-Bus tests --

    def test_dbus_get_items(self):
        bus = self.get_dbus(system_bus=True)
        proxy = bus.get_object("org.freedesktop.Passim", "/")
        iface = dbus.Interface(proxy, "org.freedesktop.Passim")
        items = iface.GetItems()
        self.assertIsInstance(items, dbus.Array)
        self.assertGreater(len(items), 0)

        found = False
        for item_dict in items:
            if "hash" in item_dict:
                h = str(item_dict["hash"])
                if h == self.test_hash:
                    found = True
                    break
        self.assertTrue(found, f"Test item with hash {self.test_hash} not found")

    def test_dbus_properties(self):
        bus = self.get_dbus(system_bus=True)
        proxy = bus.get_object("org.freedesktop.Passim", "/")
        props = dbus.Interface(proxy, dbus.PROPERTIES_IFACE)

        version = str(props.Get("org.freedesktop.Passim", "DaemonVersion"))
        self.assertTrue(len(version) > 0)

        status = int(props.Get("org.freedesktop.Passim", "Status"))
        self.assertEqual(status, self.STATUS_RUNNING)

        name = str(props.Get("org.freedesktop.Passim", "Name"))
        self.assertTrue(name.startswith("Passim-"))

        uri = str(props.Get("org.freedesktop.Passim", "Uri"))
        self.assertIn(str(self.DAEMON_PORT), uri)

    def test_download_saving(self):
        bus = self.get_dbus(system_bus=True)
        proxy = bus.get_object("org.freedesktop.Passim", "/")
        props = dbus.Interface(proxy, dbus.PROPERTIES_IFACE)
        saving = int(props.Get("org.freedesktop.Passim", "DownloadSaving"))
        self.assertGreaterEqual(saving, self.SEEDED_DOWNLOAD_SIZE)

    def test_dbus_publish_and_unpublish(self):
        publish_content = b"published via dbus integration test"
        publish_basename = "publish-test.bin"
        publish_hash = hashlib.sha256(publish_content).hexdigest()

        bus = self.get_dbus(system_bus=True)
        proxy = bus.get_object("org.freedesktop.Passim", "/")
        iface = dbus.Interface(proxy, "org.freedesktop.Passim")

        with tempfile.NamedTemporaryFile() as tmp:
            tmp.write(publish_content)
            tmp.flush()
            fd = os.open(tmp.name, os.O_RDONLY)
            try:
                attrs = dbus.Dictionary(
                    {
                        "filename": dbus.String(publish_basename),
                        "share-limit": dbus.UInt32(5),
                        "max-age": dbus.UInt32(3600),
                    },
                    signature="sv",
                )
                iface.Publish(dbus.types.UnixFd(fd), attrs)
            finally:
                os.close(fd)

        items = iface.GetItems()
        found = False
        for item_dict in items:
            if "hash" in item_dict:
                if str(item_dict["hash"]) == publish_hash:
                    found = True
                    break
        self.assertTrue(found, "Published item not found in GetItems")

        path = f"/{publish_basename}?sha256={publish_hash}"
        status, body, _ = self._https_get(path)
        self.assertEqual(status, 200)
        self.assertEqual(body, publish_content)

        iface.Unpublish(publish_hash)

        items = iface.GetItems()
        found = False
        for item_dict in items:
            if "hash" in item_dict:
                if str(item_dict["hash"]) == publish_hash:
                    found = True
                    break
        self.assertFalse(found, "Unpublished item still in GetItems")

    def test_client_api(self):
        helper = os.environ.get("PASSIM_CLIENT_TEST_HELPER", "")
        if not helper or not os.path.exists(helper):
            self.skipTest("PASSIM_CLIENT_TEST_HELPER not set or not found")
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ.get("DBUS_SYSTEM_BUS_ADDRESS", "")
        result = subprocess.run(
            [helper],
            env=env,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0)

    def test_dbus_unpublish_not_found(self):
        bus = self.get_dbus(system_bus=True)
        proxy = bus.get_object("org.freedesktop.Passim", "/")
        iface = dbus.Interface(proxy, "org.freedesktop.Passim")

        with self.assertRaises(dbus.exceptions.DBusException):
            iface.Unpublish("c" * 64)

    # -- CLI tests --

    def _run_cli(self, args, cwd=None):
        cli = os.environ.get("PASSIM_CLI", "")
        if not cli or not os.path.exists(cli):
            self.skipTest("PASSIM_CLI not set or not found")
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ.get("DBUS_SYSTEM_BUS_ADDRESS", "")
        return subprocess.run(
            [cli] + args,
            env=env,
            capture_output=True,
            timeout=30,
            cwd=cwd,
        )

    def test_cli_version(self):
        result = self._run_cli(["--version"])
        self.assertEqual(result.returncode, 0)
        stdout = result.stdout.decode()
        self.assertIn("client version", stdout)
        self.assertIn("daemon version", stdout)

    def test_cli_status(self):
        result = self._run_cli(["status"])
        self.assertEqual(result.returncode, 0)
        stdout = result.stdout.decode()
        self.assertIn(self.TEST_BASENAME, stdout)

    def test_cli_publish_and_unpublish(self):
        cli_content = b"passim cli publish test data"
        cli_hash = hashlib.sha256(cli_content).hexdigest()
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(cli_content)
            tmp_path = tmp.name
        try:
            result = self._run_cli(["publish", tmp_path])
            self.assertEqual(result.returncode, 0)
            stdout = result.stdout.decode()
            self.assertIn("Published", stdout)
        finally:
            os.unlink(tmp_path)

        result = self._run_cli(["unpublish", cli_hash])
        self.assertEqual(result.returncode, 0)
        self.assertIn("Unpublished", result.stdout.decode())

    def test_cli_publish_share_limit(self):
        """The optional third argument is the share limit, not the count."""
        content = b"passim cli publish share-limit test data"
        content_hash = hashlib.sha256(content).hexdigest()
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(content)
            tmp_path = tmp.name
        try:
            result = self._run_cli(["publish", tmp_path, "3600", "44"])
            self.assertEqual(result.returncode, 0, result.stderr.decode())

            bus = self.get_dbus(system_bus=True)
            proxy = bus.get_object("org.freedesktop.Passim", "/")
            iface = dbus.Interface(proxy, "org.freedesktop.Passim")
            published = None
            for item_dict in iface.GetItems():
                if str(item_dict.get("hash", "")) == content_hash:
                    published = item_dict
                    break
            self.assertIsNotNone(published, "published item not found")
            self.assertEqual(int(published["max-age"]), 3600)
            self.assertEqual(int(published["share-limit"]), 44)
            self.assertEqual(int(published.get("share-count", 0)), 0)
        finally:
            os.unlink(tmp_path)
            self._run_cli(["unpublish", content_hash])

    def test_cli_publish_invalid_number(self):
        """A malformed or out of range number must not be published."""
        content = b"passim cli publish invalid number test data"
        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            tmp.write(content)
            tmp.flush()
            for args in (
                ["abc"],
                ["0"],
                ["0", "0"],
                ["4294967296"],
                ["3600", "0"],
                ["3600", "44x"],
                ["3600", "-1"],
                ["3600", "4294967296"],
            ):
                with self.subTest(args=args):
                    result = self._run_cli(["publish", tmp.name] + args)
                    self.assertGreater(result.returncode, 0, "killed by a signal")
                    self.assertNotIn("Published", result.stdout.decode())

        bus = self.get_dbus(system_bus=True)
        proxy = bus.get_object("org.freedesktop.Passim", "/")
        iface = dbus.Interface(proxy, "org.freedesktop.Passim")
        hashes = [str(item_dict.get("hash", "")) for item_dict in iface.GetItems()]
        self.assertNotIn(hashlib.sha256(content).hexdigest(), hashes)

    def test_cli_unpublish_bad_hash(self):
        result = self._run_cli(["unpublish", "d" * 64])
        self.assertNotEqual(result.returncode, 0)

    def test_download_via_peer(self):
        path = f"/{self.TEST_BASENAME}?sha256={self.test_hash}&localhost=false"
        status, body, headers = self._https_get(path)
        self.assertEqual(status, 200)
        self.assertEqual(body, self.TEST_CONTENT)

    def test_peer_resolve_failure_fallback(self):
        """One peer failing to resolve must not abandon the other peers."""
        offset = self._daemon_log_offset()
        path = f"/{self.TEST_BASENAME}?sha256={self.test_hash}&localhost=false"
        status, body, _ = self._https_get(path)
        self.assertEqual(status, 200)
        self.assertEqual(body, self.TEST_CONTENT)

        log = self._daemon_log_since(offset, needle="chosen address")
        self.assertIn(self.UNRESOLVABLE_PEER, log)
        self.assertIn("failed to resolve service, ignoring:", log)
        self.assertIn("chosen address", log)

    def test_peer_start_failure(self):
        """Start() failing after the peer resolved must not return twice."""
        offset = self._daemon_log_offset()
        path = f"/{self.TEST_BASENAME}?sha256={self.test_hash}&localhost=false"
        status, body, _ = self._https_get(path)
        self.assertEqual(status, 200)
        self.assertEqual(body, self.TEST_CONTENT)

        log = self._daemon_log_since(offset, needle="failed to start resolver")
        self.assertIn(self.START_FAILS_PEER, log)
        self.assertIn("failed to start resolver", log)
        self.assertNotIn("CRITICAL", log)
        self.assertIsNone(self.daemon_proc.poll(), "daemon exited")

    def test_peer_resolve_failure(self):
        """A peer that cannot be resolved must not take the daemon down."""
        offset = self._daemon_log_offset()
        path = f"/unresolvable.bin?sha256={self.unresolvable_hash}"
        status, _, _ = self._https_get(path, expected_status=404)
        self.assertEqual(status, 404)

        # freeing the failed resolver must not return on the task a second
        # time: that is a critical, and passimd makes criticals fatal
        log = self._daemon_log_since(offset, needle="CRITICAL")
        self.assertNotIn("CRITICAL", log)
        self.assertIsNone(self.daemon_proc.poll(), "daemon exited")

    def test_cli_download(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_cli(
                ["download", self.TEST_BASENAME, self.test_hash], cwd=tmpdir
            )
            self.assertEqual(result.returncode, 0)
            stdout = result.stdout.decode()
            self.assertIn("Saved", stdout)
            downloaded = os.path.join(tmpdir, self.TEST_BASENAME)
            with open(downloaded, "rb") as f:
                self.assertEqual(f.read(), self.TEST_CONTENT)

    def test_cli_no_command(self):
        """With no command we show the help rather than reading past argv."""
        result = self._run_cli([])
        self.assertGreater(result.returncode, 0, "killed by a signal")
        self.assertIn("publish", result.stderr.decode())

    def test_cli_unknown_command(self):
        result = self._run_cli(["badcommand"])
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
