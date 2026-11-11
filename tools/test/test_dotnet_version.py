# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Check real MinVer/MSBuild contracts in an isolated Git repository; no hardware."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
DOTNET = os.environ.get("DOTNET", "dotnet")


class DotnetVersionTests(unittest.TestCase):
    def test_git_versions_and_explicit_overrides(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in ("host/dotnet/PicoPwm", "host/direct/src", "host/web/src"):
                shutil.copytree(ROOT / relative, root / relative, ignore=shutil.ignore_patterns("bin", "obj"))
            project = root / "host/dotnet/PicoPwm/PicoPwm.csproj"
            env = {key: value for key, value in os.environ.items()
                   if not key.lower().startswith(("minver", "git_"))}
            env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)

            def run(*args):
                result = subprocess.run(args, cwd=root, env=env, text=True, capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout

            def git(*args):
                return run("git", "-c", "user.name=Version Test", "-c", "user.email=version@example.invalid",
                           "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false",
                           "-c", f"core.hooksPath={os.devnull}", *args)

            def properties(override=None):
                args = [DOTNET, "msbuild", str(project), "-nologo", "-verbosity:quiet", "-target:MinVer",
                        "-getProperty:Version,PackageVersion,AssemblyVersion,FileVersion,InformationalVersion"]
                if override is not None:
                    args.append(f"-p:MinVerVersionOverride={override}")
                return json.loads(run(*args))["Properties"]

            def assert_version(expected, override=None):
                actual = properties(override)
                self.assertEqual(expected, actual["Version"])
                self.assertEqual(expected, actual["PackageVersion"])
                self.assertEqual(expected, actual["InformationalVersion"])
                numeric = expected.split("-", 1)[0].split("+", 1)[0]
                self.assertEqual(numeric + ".0", actual["FileVersion"])
                self.assertEqual(numeric.split(".")[0] + ".0.0.0", actual["AssemblyVersion"])

            git("init", "--quiet")
            git("add", "host")
            git("commit", "--quiet", "-m", "Version fixture")
            run(DOTNET, "restore", str(project), "--nologo", "--verbosity", "quiet")
            assert_version("0.0.0-dev")
            git("tag", "v1.2.3")
            assert_version("1.2.3")
            # A non-prefixed tag must not replace the repository's v-prefixed release.
            git("tag", "9.0.0")
            assert_version("1.2.3")
            project.write_text(project.read_text(encoding="utf-8") + "\n", encoding="utf-8")
            assert_version("1.2.3")
            git("add", str(project.relative_to(root)))
            git("commit", "--quiet", "-m", "Post-release fixture")
            assert_version("1.2.4-dev.1")
            assert_version("2.3.4", override="2.3.4")
            assert_version("2.3.4-test.123.1", override="2.3.4-test.123.1")
            env["MinVerVersionOverride"] = "2.3.4-test.456.1"
            assert_version("2.3.4-test.456.1")
            del env["MinVerVersionOverride"]
            (root / ".git").rename(root / "hidden-git-metadata")
            assert_version("3.4.5-test.1", override="3.4.5-test.1")


if __name__ == "__main__":
    unittest.main()
