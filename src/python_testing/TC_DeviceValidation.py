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

import sys
import os
# Add path to get access to matter_testing_support.py from the parent folder
sys.path.append(os.path.dirname(sys.path[0]))

from matter_testing_support import MatterBaseTest, default_matter_test_main, async_test_body
from chip.interaction_model import Status
import chip.exceptions
import chip.clusters as Clusters
from chip.clusters.Types import NullValue
import logging
from mobly import asserts

from rich.console import Console

console = None


async def RevertACL(self, dev_ctrl, acl, storedAclTargets):
    console.print("Reverting ACL entry to original state")
    
    # Revert ACL to the original state
    acl[0].targets = storedAclTargets
    # acl[0].targets = NullValue
    # console.print(acl)

    await dev_ctrl.WriteAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl(acl))])
    deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl)])
    console.print(deviceResponse[0][Clusters.Objects.AccessControl][Clusters.Objects.AccessControl.Attributes.Acl])


class TC_DeviceValidation(MatterBaseTest):
    @async_test_body
    async def test_descriptor(self):

        readPartsList = []
        receivedParts = []

        readListOfServers = []
        readServerList = []

        readListOfAttributes = []
        readAttributeList = []

        commandInClusterList = []
        commandListFromDevice = []
        readCommandList = []

        # Create console to print
        global console
        console = Console()

        # Run descriptor validation test
        dev_ctrl = self.default_controller

        '''
        # DEBUG REVERT ACL
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl)])
        acl = deviceResponse[0][Clusters.Objects.AccessControl][Clusters.Objects.AccessControl.Attributes.Acl]
        acl[0].targets = NullValue
        await dev_ctrl.WriteAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl(acl))])
        # DBEUG REVERT END
        '''

        # Perform wildcard read to get all attributes from device
        console.print("[blue]Performing wildcard read on device")
        deviceAttributeResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [('*')])
        console.print(deviceAttributeResponse)

        # Limit the ACL to check for command support later in the script
        # This is done here, so we only need to do it once.
        console.print("[blue]Setting ACL entry to only allow ACL cluster access")

        # Read the current ACL configuration
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl)])
        # console.print(deviceResponse)
        acl = deviceResponse[0][Clusters.Objects.AccessControl][Clusters.Objects.AccessControl.Attributes.Acl]
        storedAclTargets = acl[0].targets
        acl[0].targets = [Clusters.AccessControl.Structs.AccessControlTargetStruct(cluster=31)]

        # Adjust ACL to only allow access to ACL cluster
        # This will make is possible to check if a command is supported,
        # based on the error code returned by the device
        await dev_ctrl.WriteAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl(acl))])
        deviceResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl)])
        console.print(deviceResponse[0][Clusters.Objects.AccessControl][Clusters.Objects.AccessControl.Attributes.Acl])

        # Read parts list of device and manually add endpoint 0 to parts list
        readPartsList = deviceAttributeResponse[0][Clusters.Objects.Descriptor][Clusters.Objects.Descriptor.Attributes.PartsList]
        # Add endpoint 0 to the parts list, since this is not returned by the device
        readPartsList.insert(0, 0)
        console.print(f"[blue]Received PartsList of device: {readPartsList}")

        # Loop through each endpoint in the response from the wildcard read
        for endpoint in deviceAttributeResponse:
            console.print(f"[blue]Colleting data for endpoint: {endpoint}")

            # Capture the found endpoint from wildcard read
            receivedParts.append(endpoint)

            # Clear the server lists to be used for comparison.
            readListOfServers.clear()
            readServerList.clear()

            # Loop through the servers on the specific endpoint in the response from the wildcard read
            for server in deviceAttributeResponse[endpoint]:
                console.print(f"[blue]Colleting data for cluster: {server}")
                # console.print(f"Server ID: {server.id}")

                # Store the server in a list to compare
                readListOfServers.append(server.id)

                # Capture the serverList attribute from the descriptor cluster.
                # This is captured here, since it then does not need to perform an additional read.
                if Clusters.Objects.Descriptor == server:
                    readServerList = deviceAttributeResponse[endpoint][server][Clusters.Objects.Descriptor.Attributes.ServerList]

                # Clear the attribute and command lists to be used for comparison.
                readListOfAttributes.clear()
                readAttributeList.clear()
                readCommandList.clear()

                # Loop through the attributes on the specific server in the response from the wildcard read
                for attribute in deviceAttributeResponse[endpoint][server]:
                    # console.print(f"[blue]Attribute: {attribute}")

                    if Clusters.Attribute.DataVersion == attribute:
                        # console.print(f"[yellow]Ignoring DataVersion")
                        continue

                    # console.print(f"Attribute ID: {attribute.attribute_id}")
                    # console.print(deviceAttributeResponse[endpoint][server][attribute])

                    # Capture the attribute id to compare against attributeList
                    readListOfAttributes.append(attribute.attribute_id)

                    # Collect the attribute and command list
                    if attribute.attribute_id == 65531:
                        # Capture the attribute list from the specific cluster
                        # This is captured here, since it then does not need to perform an additional read.
                        readAttributeList = deviceAttributeResponse[endpoint][server][attribute]
                    elif attribute.attribute_id == 65529:
                        # Capture the command list from the specific cluster
                        # This is captured here, since it then does not need to perform an additional read.
                        readCommandList = deviceAttributeResponse[endpoint][server][attribute]

                # Sort lists
                readAttributeList.sort()
                readListOfAttributes.sort()

                console.print(f"[blue]Verify AttributeList")
                console.print(readAttributeList)
                console.print(readListOfAttributes)

                if readAttributeList != readListOfAttributes:
                    # Revert ACL and trigger failure
                    await RevertACL(self, dev_ctrl, acl, storedAclTargets)
                    asserts.fail("The list of received list of attributes does not match attributeList ❌")

                console.print("[green]AttributeList check passed ✅")

                commandInClusterList.clear()
                commandListFromDevice.clear()

                try:
                    commandInClusterList = [func for func in dir(server.Commands) if not func.startswith("__")]
                except Exception:
                    # console.log("No commands in cluster, moving on")
                    continue

                # console.log(commandInClusterList)

                for command in commandInClusterList:
                    commandClass = getattr(server.Commands, f"{command}")
                    # console.log(commandClass)
                    # console.log(commandClass.command_id)

                    if commandClass.is_client is False:
                        # console.log("Not handling response commands, moving on")
                        continue

                    try:
                        if commandClass == chip.clusters.Objects.AdministratorCommissioning.Commands.OpenBasicCommissioningWindow or commandClass == chip.clusters.Objects.AdministratorCommissioning.Commands.OpenCommissioningWindow or commandClass == chip.clusters.Objects.AdministratorCommissioning.Commands.RevokeCommissioning:
                            deviceResponse = await dev_ctrl.SendCommand(self.dut_node_id, endpoint, commandClass(), timedRequestTimeoutMs=10)
                        else:
                            deviceResponse = await dev_ctrl.SendCommand(self.dut_node_id, endpoint, commandClass())
                    except chip.interaction_model.InteractionModelError as e:
                        # console.print(e.status)

                        if e.status == Status.UnsupportedAccess:
                            commandListFromDevice.append(commandClass.command_id)

                # Sort lists
                readAttributeList.sort()
                commandListFromDevice.sort()

                console.print(f"[blue]Verify AcceptedCommandList")
                console.print(readCommandList)
                console.print(commandListFromDevice)

                if readCommandList != commandListFromDevice:
                    # Revert ACL and trigger failure
                    await RevertACL(self, dev_ctrl, acl, storedAclTargets)
                    asserts.fail("The checked list of commands does not match the AcceptedCommandList ❌")

                console.print("[green]AcceptedCommandList check passed ✅")

            # Sort lists
            readServerList.sort()
            readListOfServers.sort()

            console.print(f"[blue]Verify ServerList on endpoint: {endpoint}")
            console.print(readServerList)
            console.print(readListOfServers)

            if readServerList != readListOfServers:
                # Revert ACL and trigger failure
                await RevertACL(self, dev_ctrl, acl, storedAclTargets)
                asserts.fail("The received list of servers does not match ServerList ❌")

            console.print("[green]ServerList check passed ✅")

        # Revert ACL back to the initial state
        await RevertACL(self, dev_ctrl, acl, storedAclTargets)

        # Sort lists
        readPartsList.sort()
        receivedParts.sort()
        console.print("[blue]Verify PartsList of device: ")
        console.print(readPartsList)
        console.print(receivedParts)

        asserts.assert_equal(readPartsList, receivedParts, "The list of received list of parts does not match PartsList ❌")
        console.print("[green]PartsList check passed ✅")

        # Read events
        # TODO: Determine how to verify the received events
        # deviceEventResponse = await dev_ctrl.ReadEvent(self.dut_node_id, [('*')])
        # console.print(deviceEventResponse)


if __name__ == "__main__":
    default_matter_test_main()
