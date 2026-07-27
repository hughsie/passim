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
        cls.tmpdir = tempfile.mkdtemp(prefix="passim-test-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir, ignore_errors=True)

        builddir = os.environ.get("PASSIM_BUILDDIR", "")
        srcdir = os.environ.get("PASSIM_SRCDIR", "")

        config_h = os.path.join(builddir, "config.h")
        cls.sysconfdir = cls._parse_config_h(config_h, "PACKAGE_SYSCONFDIR")
        cls.localstatedir = cls._parse_config_h(config_h, "PACKAGE_LOCALSTATEDIR")
        cls.datadir = cls._parse_config_h(config_h, "PACKAGE_DATADIR")

        cls._setup_directories()
        cls._seed_audit_log()
        cls._setup_sysconfpkgdir()
        cls._setup_config()
        cls._setup_tls_backups()
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

    @staticmethod
    def _parse_config_h(path, key):
        with open(path, "r") as f:
            for line in f:
                if line.startswith(f"#define {key} "):
                    return line.split('"')[1]
        raise KeyError(f"{key} not found in {path}")

    @classmethod
    def _setup_directories(cls):
        cls.data_path = os.path.join(cls.localstatedir, "lib", "passim", "data")
        os.makedirs(cls.data_path, exist_ok=True)
        cls.test_file = os.path.join(
            cls.data_path, f"{cls.test_hash}-{cls.TEST_BASENAME}"
        )
        cls.addClassCleanup(cls._remove_if_exists, cls.test_file)
        with open(cls.test_file, "wb") as f:
            f.write(cls.TEST_CONTENT)

        cls.log_path = os.path.join(cls.tmpdir, "logs")
        os.makedirs(cls.log_path, exist_ok=True)

    SEEDED_DOWNLOAD_SIZE = 42000

    @classmethod
    def _seed_audit_log(cls):
        audit_file = os.path.join(cls.log_path, "audit.log")
        fake_hash = "a" * 64
        line = f"2026-01-01T00:00:00Z SHARE hash={fake_hash},basename=seeded.bin,size={cls.SEEDED_DOWNLOAD_SIZE},ipaddr=127.0.0.1\n"
        with open(audit_file, "w") as f:
            f.write(line)

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

        passim_d = os.path.join(cls.sysconfdir, "passim.d")
        os.makedirs(passim_d, exist_ok=True)

        fd, cls.sysconfpkg_conf = tempfile.mkstemp(
            suffix=".conf", prefix="passim-test-", dir=passim_d
        )
        cls.addClassCleanup(cls._remove_if_exists, cls.sysconfpkg_conf)
        with os.fdopen(fd, "w") as f:
            f.write(f"[passim]\nPath={cls.sysconfpkg_data}\n")

        fd2, missing_conf = tempfile.mkstemp(
            suffix=".conf", prefix="passim-missing-", dir=passim_d
        )
        cls.addClassCleanup(cls._remove_if_exists, missing_conf)
        with os.fdopen(fd2, "w") as f:
            f.write("[passim]\nPath=/nonexistent/path\n")

    @classmethod
    def _setup_config(cls):
        conf_dir = cls.sysconfdir
        os.makedirs(conf_dir, exist_ok=True)
        cls.conf_file = os.path.join(conf_dir, "passim.conf")
        conf_existed = os.path.exists(cls.conf_file)
        if conf_existed:
            cls.conf_backup = os.path.join(cls.tmpdir, "passim.conf.bak")
            shutil.copy2(cls.conf_file, cls.conf_backup)
            cls.addClassCleanup(shutil.move, cls.conf_backup, cls.conf_file)
        else:
            cls.addClassCleanup(cls._remove_if_exists, cls.conf_file)
        with open(cls.conf_file, "w") as f:
            f.write(f"[daemon]\nPort={cls.DAEMON_PORT}\n")

    @staticmethod
    def _remove_if_exists(path):
        if os.path.exists(path):
            os.unlink(path)

    @classmethod
    def _setup_tls_backups(cls):
        tls_dir = os.path.join(cls.localstatedir, "lib", "passim")
        for fn in ("secret.key", "cert.pem"):
            path = os.path.join(tls_dir, fn)
            if os.path.exists(path):
                backup = os.path.join(cls.tmpdir, fn + ".bak")
                shutil.copy2(path, backup)
                cls.addClassCleanup(shutil.move, backup, path)
            else:
                cls.addClassCleanup(cls._remove_if_exists, path)

    @classmethod
    def _install_dbus_interface(cls, srcdir):
        dbus_iface_dir = os.path.join(cls.datadir, "dbus-1", "interfaces")
        os.makedirs(dbus_iface_dir, exist_ok=True)
        dbus_iface_dst = os.path.join(
            dbus_iface_dir, "org.freedesktop.Passim.xml"
        )
        dbus_iface_src = os.path.join(srcdir, "org.freedesktop.Passim.xml")
        if not os.path.exists(dbus_iface_dst):
            cls.addClassCleanup(cls._remove_if_exists, dbus_iface_dst)
            shutil.copy2(dbus_iface_src, dbus_iface_dst)

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
            eg_mock.AddMethod(
                "org.freedesktop.Avahi.EntryGroup", method, sig, "", ""
            )

        test_hash_prefix = cls.test_hash[:60]
        avahi_mock.AddMethod(
            "org.freedesktop.Avahi.Server2",
            "ServiceBrowserPrepare",
            "iissu",
            "o",
            f'if "_{test_hash_prefix}" in args[2]:\n'
            '    ret = "/org/freedesktop/Avahi/ServiceBrowser/1"\n'
            'else:\n'
            '    ret = "/org/freedesktop/Avahi/ServiceBrowser/2"',
        )

        for sb_path in [
            "/org/freedesktop/Avahi/ServiceBrowser/1",
            "/org/freedesktop/Avahi/ServiceBrowser/2",
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
            'import dbus\n'
            'self.EmitSignal('
            '"org.freedesktop.Avahi.ServiceBrowser", "ItemNew", "iisssu",'
            ' [dbus.Int32(-1), dbus.Int32(0),'
            ' dbus.String("Passim-Peer"),'
            ' dbus.String("_cache._tcp"),'
            ' dbus.String("local"), dbus.UInt32(0)])\n'
            'self.EmitSignal('
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
            'self.EmitSignal('
            '"org.freedesktop.Avahi.ServiceBrowser", "AllForNow", "", [])',
        )

        avahi_mock.AddMethod(
            "org.freedesktop.Avahi.Server2",
            "ServiceResolverPrepare",
            "iisssiu",
            "o",
            'ret = "/org/freedesktop/Avahi/ServiceResolver/1"',
        )

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
        sr_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceResolver",
            "Start",
            "",
            "",
            'import dbus\n'
            'self.EmitSignal('
            '"org.freedesktop.Avahi.ServiceResolver", "Found",'
            ' "iissssisqaayu",'
            ' [dbus.Int32(-1), dbus.Int32(0),'
            ' dbus.String("Passim-Peer"),'
            ' dbus.String("_cache._tcp"),'
            ' dbus.String("local"),'
            ' dbus.String("peer.local"), dbus.Int32(0),'
            ' dbus.String("127.0.0.1"),'
            f' dbus.UInt16({cls.DAEMON_PORT}),'
            ' dbus.Array([], signature="ay"),'
            ' dbus.UInt32(0)])',
        )
        sr_mock.AddMethod(
            "org.freedesktop.Avahi.ServiceResolver", "Free", "", "", ""
        )

    @classmethod
    def _start_daemon(cls, builddir):
        """Start the passimd binary."""
        daemon_path = os.path.join(builddir, "src", "passimd")
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ.get(
            "DBUS_SYSTEM_BUS_ADDRESS", ""
        )
        env["LOGS_DIRECTORY"] = cls.log_path
        env["G_MESSAGES_DEBUG"] = "all"
        env["G_DEBUG"] = ""

        cls.daemon_proc = subprocess.Popen(
            [daemon_path, "--insecure"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                s = socket.create_connection(
                    ("127.0.0.1", cls.DAEMON_PORT), timeout=1
                )
                s.close()
                return
            except OSError:
                rc = cls.daemon_proc.poll()
                if rc is not None:
                    raise RuntimeError(
                        f"passimd exited with code {rc}"
                    ) from None
                time.sleep(0.2)

        raise TimeoutError(
            f"passimd did not start listening on port {cls.DAEMON_PORT}"
        )

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
        status, body, _ = self._https_get(
            "/somefile?foo=bar", expected_status=400
        )
        self.assertEqual(status, 400)

    def test_duplicate_sha256(self):
        h = "a" * 64
        status, body, _ = self._https_get(
            f"/file?sha256={h}&sha256={h}", expected_status=400
        )
        self.assertEqual(status, 400)

    def test_malformed_sha256(self):
        status, body, _ = self._https_get(
            "/file?sha256=notahex", expected_status=406
        )
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
        env["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ.get(
            "DBUS_SYSTEM_BUS_ADDRESS", ""
        )
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
        env["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ.get(
            "DBUS_SYSTEM_BUS_ADDRESS", ""
        )
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

    def test_cli_unpublish_bad_hash(self):
        result = self._run_cli(["unpublish", "d" * 64])
        self.assertNotEqual(result.returncode, 0)

    def test_download_via_peer(self):
        path = f"/{self.TEST_BASENAME}?sha256={self.test_hash}&localhost=false"
        status, body, headers = self._https_get(path)
        self.assertEqual(status, 200)
        self.assertEqual(body, self.TEST_CONTENT)

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

    def test_cli_unknown_command(self):
        result = self._run_cli(["badcommand"])
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
