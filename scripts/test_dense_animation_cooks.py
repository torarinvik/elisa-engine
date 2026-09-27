"""Check declared dense animation rates and importer restrictions."""
import tempfile
import unittest
from pathlib import Path
import asset_cooks

class DenseAnimationCookTests(unittest.TestCase):
    def test_rates_forward_and_default_stays_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve();(root/"rig.glb").touch()
            declaration={"importer":"glb","source":"rig.glb","asset_path":"rig.glb","output":"build/rig.pkg"}
            default=asset_cooks.asset_cook_command(root,declaration,0)[2]
            self.assertNotIn("--animation-sample-rate",default)
            for rate in (30,60,120):
                command=asset_cooks.asset_cook_command(root,{**declaration,"animation_sample_rate":rate},0)[2]
                self.assertEqual(command[command.index("--animation-sample-rate")+1],str(rate))

    def test_invalid_rates_and_other_importers_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve();(root/"rig.glb").touch()
            declaration={"importer":"glb","source":"rig.glb","asset_path":"rig.glb","output":"build/rig.pkg"}
            for rate in (True,False,0,90,121,"120",120.0):
                with self.assertRaises(asset_cooks.BuildConfigurationError):
                    asset_cooks.asset_cook_command(root,{**declaration,"animation_sample_rate":rate},0)
            for importer in ("fbx","gltf","image","images"):
                with self.assertRaises(asset_cooks.BuildConfigurationError):
                    asset_cooks.asset_cook_command(root,{**declaration,"importer":importer,"animation_sample_rate":120},0)

if __name__=="__main__":
    unittest.main()
