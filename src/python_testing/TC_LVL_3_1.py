#
#    Copyright (c) 2022 Project CHIP Authors
#    All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

from matter_testing_support import MatterBaseTest, default_matter_test_main, async_test_body
from chip.interaction_model import Status
import chip.clusters as Clusters
from chip.clusters.Types import NullValue
import logging
from mobly import asserts
import time

class TC_LVL_3_1(MatterBaseTest):
    @async_test_body
    async def test_TC_LVL_3_1(self):

        ### Step 1

        logging.info("Pre-condition: Commissioning, already done")
        dev_ctrl = self.default_controller

        logging.info("Pre-condition (from YAML): Send On Command")
        # OO.S.C01.Rsp

        logging.info("Pre-condition (from YAML): Check on/off attribute value is true after on command")
        # OO.S.A0000

        logging.info("Pre-condition (from YAML): write default value of OnOffTransitionTime attribute")
        # LVL.S.A0010
        # writeAttribute - OnOffTransitionTime - 0

        logging.info("Step 1: TH writes 0x00 to the Options attribute")
        # LVL.S.A000f(Options)
        deviceResponse = await dev_ctrl.WriteAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.Options(0x00))])
        logging.info(deviceResponse)
        # No verification in Test Plan

        logging.info("Step 1a: TH writes NULL to the OnLevel attribute")
        # LVL.S.A0011(OnLevel)
        deviceResponse = await dev_ctrl.WriteAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.OnLevel(NullValue))])
        logging.info(deviceResponse)
        # Verify that write request was successful.

        ### Step 2: tests for MoveToLevelWithOnOff (starting with DUT in OFF state)

        logging.info("Step 2a: TH sends Off command to DUT")
        # LVL.S.C04.Rsp(MoveToLevelWithOnOff) & OO.S.C00.Rsp(Off)
        await dev_ctrl.SendCommand(self.dut_node_id, 1, Clusters.OnOff.Commands.Off())
        # Verify DUT responds with a successful (value 0x00) status response.

        logging.info("Step 2b: TH sends a MoveToLevelWithOnOff command to DUT, with Level =50 and TransitionTime =0 (immediate)")
        # LVL.S.C04.Rsp(MoveToLevelWithOnOff)
        await dev_ctrl.SendCommand(self.dut_node_id, 1, Clusters.LevelControl.Commands.MoveToLevelWithOnOff(level=50, transitionTime=0, optionsMask=0, optionsOverride=0))
        # Verify DUT responds with a successful (value 0x00) status response.

        logging.info("Step 2c: TH reads OnOff attribute (On/Off cluster) from DUT")
        # LVL.S.C04.Rsp(MoveToLevelWithOnOff) & OO.S.A0000(OnOff)
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.OnOff.Attributes.OnOff)])
        onOffValue = deviceResponse[1][Clusters.Objects.OnOff][Clusters.Objects.OnOff.Attributes.OnOff]
        logging.info(f"On/Off value: {onOffValue}")
        # The value of OnOff has to be TRUE.

        logging.info("Step 2d: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C04.Rsp(MoveToLevelWithOnOff) & LVL.S.A0000(CurrentLevel)
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.CurrentLevel)])
        currentLevelFromDut = deviceResponse[1][Clusters.Objects.LevelControl][Clusters.Objects.LevelControl.Attributes.CurrentLevel]
        logging.info(f"CurrentLevel: {currentLevelFromDut}")
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value given in 2b
        
        ### Step 3: steps to make sure DUT is on and at level 50 (for steps 4a etc) in case !LVL.S.C04.Rsp(MoveToLevelWithOnOff)

        logging.info("Step 3a: TH sends On command to DUT")
        # OO.S.C01.Rsp(On)
        await dev_ctrl.SendCommand(self.dut_node_id, 1, Clusters.OnOff.Commands.On())
        # Verify DUT responds with a successful (value 0x00) status response.

        logging.info("Step 3b: TH sends a MoveToLevel command to DUT, with Level =50 and TransitionTime =0 (immediate)")
        # LVL.S.C00.Rsp(MoveToLevel)
        await dev_ctrl.SendCommand(self.dut_node_id, 1, Clusters.LevelControl.Commands.MoveToLevel(level=50, transitionTime=0, optionsMask=0, optionsOverride=0))
        # Verify DUT responds with a successful (value 0x00) status response.

        logging.info("Step 3c: TH reads CurrentLevel attribute from DUT")
        # LVL.S.A0000(CurrentLevel)
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.CurrentLevel)])
        currentLevel = deviceResponse[1][Clusters.Objects.LevelControl][Clusters.Objects.LevelControl.Attributes.CurrentLevel]
        logging.info(f"CurrentLevel: {currentLevel}")
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value given in 3b.

        ### Step 4: tests for MoveToLevel (with transition time)

        logging.info("Step 4a: TH sends a MoveToLevel command to the DUT with Level = 200 and TransitionTime = 300 (30 s). This means the level should increase by 150 units in 30s, so 5 units/s.")
        # LVL.S.C00.Rsp(MoveToLevel)
        await dev_ctrl.SendCommand(self.dut_node_id, 1, Clusters.LevelControl.Commands.MoveToLevel(level=200, transitionTime=300, optionsMask=0, optionsOverride=0))
        # Verify DUT responds with a successful (value 0x00) status response

        logging.info("Step 4b: After 10 seconds, TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel) & LVL.S.M.VarRate
        time.sleep(10)
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.CurrentLevel)])
        currentLevel = deviceResponse[1][Clusters.Objects.LevelControl][Clusters.Objects.LevelControl.Attributes.CurrentLevel]
        logging.info(f"CurrentLevel: {currentLevel}")
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value 100 (50+10*5).

        logging.info("Step 4c: After another 10 seconds, TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel) & LVL.S.M.VarRate
        time.sleep(10)
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.CurrentLevel)])
        currentLevel = deviceResponse[1][Clusters.Objects.LevelControl][Clusters.Objects.LevelControl.Attributes.CurrentLevel]
        logging.info(f"CurrentLevel: {currentLevel}")
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value 150 (50+20*5).

        logging.info("Step 4d: After another 10 seconds, TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel) & LVL.S.M.VarRate
        time.sleep(10)
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.CurrentLevel)])
        currentLevel = deviceResponse[1][Clusters.Objects.LevelControl][Clusters.Objects.LevelControl.Attributes.CurrentLevel]
        logging.info(f"CurrentLevel: {currentLevel}")
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value 200 (50+30*5).

        logging.info("Step 4e: After another 5 seconds, TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel) & LVL.S.M.VarRate
        time.sleep(5)
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.CurrentLevel)])
        currentLevelFromDut = deviceResponse[1][Clusters.Objects.LevelControl][Clusters.Objects.LevelControl.Attributes.CurrentLevel]
        logging.info(f"CurrentLevel: {currentLevelFromDut}")
        # Verify that the DUT response indicates that the CurrentLevel attribute is now stable at the value 200.

        logging.info("Step 4f: TH reads CurrentLevel attribute from DUT (after DUT has finished the transition)")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel) & !LVL.S.M.VarRate
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.LevelControl.Attributes.CurrentLevel)])
        currentLevelFromDut = deviceResponse[1][Clusters.Objects.LevelControl][Clusters.Objects.LevelControl.Attributes.CurrentLevel]
        logging.info(f"CurrentLevel: {currentLevelFromDut}")
        # Verify that the DUT response indicates that the CurrentLevel attribute is at the value 200.

        ### Step 5: tests for Options-bits (ExecuteIfOff=false)

        logging.info("Step 5a: TH writes 0x00 to the Options attribute")
        # LVL.S.A000f(Options)
        # no error response

        logging.info("Step 5b: TH reads the Options attribute from the DUT")
        # LVL.S.A000f(Options)
        # Verify that the DUT response contains a bitmap, with value 0x00.

        logging.info("Step 5c: TH sends On command to DUT")
        # OO.S.C01.Rsp(On)
        # No verification in Test Plan

        logging.info("Step 5d: TH sends a MoveToLevel command to the DUT with: Level = 100, TransitionTime = 0 (immediate), OptionsMask = 0x00 & OptionsOverride = 0x00")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device moves to the given level.

        logging.info("Step 5e: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value given in 5d.

        logging.info("Step 5f: TH sends Off command to DUT")
        # OO.S.C00.Rsp(Off)
        # No verification in Test Plan

        logging.info("Step 5g: TH sends a MoveToLevel command to the DUT with: Level = 120, TransitionTime = 0 (immediate), OptionsMask = 0x00 & OptionsOverride = 0x00")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device remains in the off state.

        logging.info("Step 5h: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute (still) has the value given in 5d.

        logging.info("Step 5i: TH sends a MoveToLevel command to the DUT with: Level = 140, TransitionTime = 0 (immediate), OptionsMask = 0x01 & OptionsOverride = 0x00")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device remains in the off state.

        logging.info("Step 5j: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute (still) has the value given in 5d.

        logging.info("Step 5k: TH sends a MoveToLevel command to the DUT with: Level = 160, TransitionTime = 0 (immediate), OptionsMask = 0x01 & OptionsOverride = 0x01")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device remains in the off state.

        logging.info("Step 5l: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value given in 5k.

        ### Step 6: tests for Options-bits (ExecuteIfOff=true)

        logging.info("Step 6a: TH writes 0x01 to the Options attribute")
        # LVL.S.A000f(Options)
        # No verification in Test Plan
        
        logging.info("Step 6b: TH reads the Options attribute from the DUT")
        # LVL.S.A000f(Options)
        # Verify that the DUT response contains a bitmap, with value 0x01.
        
        logging.info("Step 6c: No verification in Test Plan")
        # OO.S.C01.Rsp(On)
        # TH sends On command to DUT

        logging.info("Step 6d: TH sends a MoveToLevel command to the DUT with: Level = 100, TransitionTime = 0 (immediate), OptionsMask = 0x00 & OptionsOverride = 0x00")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device moves to the given level.

        logging.info("Step 6e: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value given in 6d.

        logging.info("Step 6f: TH sends Off command to DUT")
        # OO.S.C00.Rsp(Off)
        # No verification in Test Plan

        logging.info("Step 6g: TH sends a MoveToLevel command to the DUT with: Level = 120, TransitionTime = 0 (immediate), OptionsMask = 0x00 & OptionsOverride = 0x00")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device remains in the off state.

        logging.info("Step 6h: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value given in 6g.

        logging.info("Step 6i: TH sends a MoveToLevel command to the DUT with: Level = 140, TransitionTime = 0 (immediate), OptionsMask = 0x01 & OptionsOverride = 0x00")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device remains in the off state.

        logging.info("Step 6j: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute (still) has the value given in 6g.

        logging.info("Step 6k: TH sends a MoveToLevel command to the DUT with: Level = 160, TransitionTime = 0 (immediate), OptionsMask = 0x01 & OptionsOverride = 0x01")
        # LVL.S.C00.Rsp(MoveToLevel)
        # Verify DUT responds with a successful (value 0x00) status response and that the device remains in the off state.

        logging.info("Step 6l: TH reads CurrentLevel attribute from DUT")
        # LVL.S.C00.Rsp(MoveToLevel) & LVL.S.A0000(CurrentLevel)
        # Verify that the DUT response indicates that the CurrentLevel attribute has the value given in 6k.

        '''
        vendor_name = await self.read_single_attribute(dev_ctrl, self.dut_node_id, 0, Clusters.Basic.Attributes.VendorName)
        logging.info("Found VendorName: %s" % (vendor_name))
        asserts.assert_equal(vendor_name, "Grundfos A/S", "VendorName must be Grundfos A/S!")
        '''


if __name__ == "__main__":
    default_matter_test_main()
