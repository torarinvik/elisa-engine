import unittest
from macos_deployment_target import deployment_targets, format_version


class DeploymentTargetTests(unittest.TestCase):
    def test_universal_targets_and_legacy_commands(self):
        output = 'Load command 1\n cmd LC_BUILD_VERSION\n platform 1\n minos 27.0\n sdk 27.0\n'
        output += 'Load command 2\n cmd LC_VERSION_MIN_MACOSX\n version 13.2.1\n sdk 14.0\n'
        self.assertEqual(deployment_targets(output), [(27, 0, 0), (13, 2, 1)])
        self.assertEqual(format_version(max(deployment_targets(output))), '27.0')

    def test_invalid_or_foreign_targets_fail(self):
        for output in ('', 'Load command 0\n cmd LC_BUILD_VERSION\n platform 2\n minos 26.0\n',
                'Load command 0\n cmd LC_BUILD_VERSION\n platform 1\n minos broken\n'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                deployment_targets(output)
