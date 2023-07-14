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
    aclResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl)])
    console.print(aclResponse[0][Clusters.Objects.AccessControl][Clusters.Objects.AccessControl.Attributes.Acl])


class TC_DeviceValidation(MatterBaseTest):
    @async_test_body
    async def test_descriptor(self):

        listOfSupportedEndpoints = []
        partsListFromWildcardRead = []

        listOfSupportedServers = []
        serverListFromWildcardRead = []

        listOfSupportedAttributes = []
        attributeListFromWildcardRead = []

        eventListFromWildcardRead = []

        listOfCommandsFromSDK = []
        listOfSupportedCommands = []
        commandListFromWildcardRead = []

        # Create console to print
        global console
        console = Console()

        # Run descriptor validation test
        dev_ctrl = self.default_controller

        # Perform wildcard read to get all attributes from device
        console.print("[blue]Performing wildcard read on device")
        wildcardResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [('*')])
        # console.print(wildcardResponse)

        # Limit the ACL to check for command support later in the script
        # This is done here, so we only need to do it once.
        console.print("[blue]Setting ACL entry to only allow ACL cluster access")

        # Read the current ACL configuration
        aclResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl)])
        # console.print(aclResponse)
        acl = aclResponse[0][Clusters.Objects.AccessControl][Clusters.Objects.AccessControl.Attributes.Acl]
        storedAclTargets = acl[0].targets
        acl[0].targets = [Clusters.AccessControl.Structs.AccessControlTargetStruct(cluster=31)]

        # Adjust ACL to only allow access to ACL cluster
        # This will make is possible to check if a command is supported,
        # based on the error code returned by the device
        await dev_ctrl.WriteAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl(acl))])
        aclResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [(0, Clusters.AccessControl.Attributes.Acl)])
        console.print(aclResponse[0][Clusters.Objects.AccessControl][Clusters.Objects.AccessControl.Attributes.Acl])

        # Read parts list of device and manually add endpoint 0 to parts list
        partsListFromWildcardRead = wildcardResponse[0][Clusters.Objects.Descriptor][Clusters.Objects.Descriptor.Attributes.PartsList]
        # Add endpoint 0 to the parts list, since this is not returned by the device
        partsListFromWildcardRead.insert(0, 0)
        console.print(f"[blue]Received PartsList of device: {partsListFromWildcardRead}")

        # Loop through each endpoint in the response from the wildcard read
        for endpoint in wildcardResponse:
            console.print(f"[blue]Colleting data for endpoint: {endpoint}")

            # Capture the found endpoint from wildcard read
            listOfSupportedEndpoints.append(endpoint)

            # Clear the server lists to be used for comparison.
            listOfSupportedServers.clear()
            serverListFromWildcardRead.clear()

            # Loop through the servers on the specific endpoint in the response from the wildcard read
            for server in wildcardResponse[endpoint]:
                console.print(f"[blue]Colleting data for cluster: {server.id:#06X} - {server}")
                # console.print(f"Server ID: {server.id}")

                # Store the server in a list to compare
                listOfSupportedServers.append(server.id)

                # Capture the serverList attribute from the descriptor cluster.
                # This is captured here, since it then does not need to perform an additional read.
                if Clusters.Objects.Descriptor == server:
                    serverListFromWildcardRead = wildcardResponse[endpoint][server][Clusters.Objects.Descriptor.Attributes.ServerList]

                # Clear the attribute and command lists to be used for comparison.
                listOfSupportedAttributes.clear()
                attributeListFromWildcardRead.clear()
                eventListFromWildcardRead.clear()
                commandListFromWildcardRead.clear()

                # Loop through the attributes on the specific server in the response from the wildcard read
                for attribute in wildcardResponse[endpoint][server]:
                    # console.print(f"[blue]Attribute: {attribute}")

                    if Clusters.Attribute.DataVersion == attribute:
                        # console.print(f"[yellow]Ignoring DataVersion")
                        continue

                    # console.print(f"Attribute ID: {attribute.attribute_id}")
                    # console.print(wildcardResponse[endpoint][server][attribute])

                    # Capture the attribute id to compare against attributeList
                    listOfSupportedAttributes.append(attribute.attribute_id)

                    # Collect the AttributeList
                    if attribute.attribute_id == 65531:
                        # Capture the attribute list from the specific cluster
                        # This is captured here, since it then does not need to perform an additional read.
                        attributeListFromWildcardRead = wildcardResponse[endpoint][server][attribute]

                    # Collect the EventList
                    elif attribute.attribute_id == 65530:
                        # Capture the command list from the specific cluster
                        # This is captured here, since it then does not need to perform an additional read.
                        eventListFromWildcardRead = wildcardResponse[endpoint][server][attribute]

                    # Collect the AcceptedCommandList
                    elif attribute.attribute_id == 65529:
                        # Capture the command list from the specific cluster
                        # This is captured here, since it then does not need to perform an additional read.
                        commandListFromWildcardRead = wildcardResponse[endpoint][server][attribute]

                # Sort lists
                attributeListFromWildcardRead.sort()
                listOfSupportedAttributes.sort()

                # -- Verify attributes
                console.print("[blue]Verify AttributeList")
                console.print(attributeListFromWildcardRead)
                console.print(listOfSupportedAttributes)

                if attributeListFromWildcardRead != listOfSupportedAttributes:
                    # Revert ACL and trigger failure
                    await RevertACL(self, dev_ctrl, acl, storedAclTargets)
                    asserts.fail("The list of received list of attributes does not match attributeList ❌")

                console.print("[green]AttributeList check passed ✅")

                # -- Verify accepted commands
                listOfCommandsFromSDK.clear()
                listOfSupportedCommands.clear()

                try:
                    listOfCommandsFromSDK = [func for func in dir(server.Commands) if not func.startswith("__")]
                except Exception:
                    # console.print("No commands in cluster, moving on")
                    continue

                # console.print(listOfCommandsFromSDK)

                for command in listOfCommandsFromSDK:
                    commandClass = getattr(server.Commands, f"{command}")
                    # console.print(commandClass)
                    # console.print(commandClass.command_id)

                    if commandClass.is_client is False:
                        # console.print("Not handling response commands, moving on")
                        continue

                    try:
                        if commandClass == chip.clusters.Objects.AdministratorCommissioning.Commands.OpenBasicCommissioningWindow or commandClass == chip.clusters.Objects.AdministratorCommissioning.Commands.OpenCommissioningWindow or commandClass == chip.clusters.Objects.AdministratorCommissioning.Commands.RevokeCommissioning:
                            await dev_ctrl.SendCommand(self.dut_node_id, endpoint, commandClass(), timedRequestTimeoutMs=10)
                        else:
                            await dev_ctrl.SendCommand(self.dut_node_id, endpoint, commandClass())
                    except chip.interaction_model.InteractionModelError as e:
                        # console.print(e.status)

                        if e.status == Status.UnsupportedAccess:
                            listOfSupportedCommands.append(commandClass.command_id)

                # Sort lists
                attributeListFromWildcardRead.sort()
                listOfSupportedCommands.sort()

                console.print("[blue]Verify AcceptedCommandList")
                console.print(commandListFromWildcardRead)
                console.print(listOfSupportedCommands)

                if commandListFromWildcardRead != listOfSupportedCommands:
                    # Revert ACL and trigger failure
                    await RevertACL(self, dev_ctrl, acl, storedAclTargets)
                    asserts.fail("The checked list of commands does not match the AcceptedCommandList ❌")

                console.print("[green]AcceptedCommandList check passed ✅")

            # Sort lists
            serverListFromWildcardRead.sort()
            listOfSupportedServers.sort()

            console.print(f"[blue]Verify ServerList on endpoint: {endpoint}")
            console.print(serverListFromWildcardRead)
            console.print(listOfSupportedServers)

            if serverListFromWildcardRead != listOfSupportedServers:
                # Revert ACL and trigger failure
                await RevertACL(self, dev_ctrl, acl, storedAclTargets)
                asserts.fail("The received list of servers does not match ServerList ❌")

            console.print("[green]ServerList check passed ✅")

        # Revert ACL back to the initial state
        await RevertACL(self, dev_ctrl, acl, storedAclTargets)

        # Sort lists
        partsListFromWildcardRead.sort()
        listOfSupportedEndpoints.sort()
        console.print("[blue]Verify PartsList of device: ")
        console.print(partsListFromWildcardRead)
        console.print(listOfSupportedEndpoints)

        asserts.assert_equal(partsListFromWildcardRead, listOfSupportedEndpoints, "The list of received list of parts does not match PartsList ❌")
        console.print("[green]PartsList check passed ✅")

        # Read events
        # TODO: Determine how to verify the received events
        # deviceEventResponse = await dev_ctrl.ReadEvent(self.dut_node_id, [('*')])
        # console.print(deviceEventResponse)


if __name__ == "__main__":
    default_matter_test_main()
