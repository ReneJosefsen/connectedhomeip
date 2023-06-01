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
import json
import argparse
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

class DeviceTypeValidatorTest(MatterBaseTest):
    @async_test_body
    async def test_descriptor(self):

        # Endpoint define
        rootNodeEndpointID = 0

        # Create console to print
        global console
        console = Console()

        # Run descriptor validation test
        devCtrl = self.default_controller

        # Device Type dict
        deviceTypeDict = {}

        # Store the device type requirement JSON list
        deviceTypeRequirementFileList = os.listdir(deviceTypeRequirementInputPathStr)
        #console.log(deviceTypeRequirementJson)
        for deviceTypeRequirementFileName in deviceTypeRequirementFileList:
            
            file = f"{deviceTypeRequirementInputPathStr}{deviceTypeRequirementFileName}"
            with open(file, 'r') as deviceTypeRequirementFile:
                deviceTypeRequirementData = json.load(deviceTypeRequirementFile)
                deviceTypeID = deviceTypeRequirementData['id']

                #console.print(f"{deviceTypeID} - {int(deviceTypeID, 16)} - {deviceTypeRequirementFileName}")

                deviceTypeDict[int(deviceTypeID, 16)] = {
                    "fileName":deviceTypeRequirementFileName,
                }

        #console.print(deviceTypeDict)
        partsListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(rootNodeEndpointID, Clusters.Descriptor.Attributes.PartsList)])
        partsList = partsListResponse[0][Clusters.Descriptor][Clusters.Descriptor.Attributes.PartsList]
        
        '''
        partsList = [1]
        deviceType = [22, 267]
        '''
        # Add endpoint 0 to the parts list, since this is not returned by the device
        partsList.insert(0, 0)
    
        for endpoint in partsList:
            console.print(f"[blue]Endpoint: {endpoint}")

            deviceTypeListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Descriptor.Attributes.DeviceTypeList)])
            console.print(deviceTypeListResponse)
            deviceTypeList = deviceTypeListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.DeviceTypeList]

            for deviceTypeStruct in deviceTypeList:
                console.print(f"[blue]DeviceType: {deviceTypeStruct.deviceType}")
            
                file = f"{deviceTypeRequirementInputPathStr}{deviceTypeDict[deviceTypeStruct.deviceType]['fileName']}"
                console.print(file)

                with open(file, 'r') as deviceTypeRequirementFile:
                    deviceTypeRequirementData = json.load(deviceTypeRequirementFile)

                    # Server clusteres
                    requiredServerClusters = deviceTypeRequirementData["server_clusters"]
                    #console.print(requiredServerClusters)

                    # Read server list
                    serverListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Descriptor.Attributes.ServerList)])
                    serverList = serverListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.ServerList]
                    console.print(serverList)

                    #console.print(deviceTypeRequirementData)

                    for cluster in requiredServerClusters:
                        # Check if server cluster is available
                        clusterID = int(cluster["id"], 16)
                        console.print(clusterID)

                        # Check if cluster id is in the server list
                        if clusterID not in serverList:
                            console.print(f"[red]Cluster {clusterID} not found in server list")

                        asserts.assert_true(clusterID in serverList, f"Cluster {clusterID} not found in server list")
                        
                        clusterClass = getattr(Clusters, devCtrl.GetClusterHandler().GetClusterInfoById(clusterID)['clusterName'])
                        #console.print(devCtrl.GetClusterHandler().GetClusterInfoById(clusterID))

                        # Check required elements on the server
                        mandatoryFeatures = cluster["mandatory_features"]
                        if len(mandatoryFeatures) > 0:

                            # Read feature map from DUT
                            featureMapResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.FeatureMap)])
                            featureMap = featureMapResponse[endpoint][clusterClass][clusterClass.Attributes.FeatureMap]
                            console.print(f"FeatureMap: {featureMap}")

                            # Check if mandated feature is supported
                            for feature in mandatoryFeatures:
                                console.print(feature["pics_code"])

                        mandatoryAttributes = cluster["mandatory_attributes"]
                        if len(mandatoryAttributes) > 0:

                            # Read attribute list
                            attributeListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.AttributeList)])
                            attributeList = attributeListResponse[endpoint][clusterClass][clusterClass.Attributes.AttributeList]
                            console.print(f"AttributeList: {attributeList}")

                            for attribute in mandatoryAttributes:
                                console.print(attribute["pics_code"])

                        mandatoryCommands = cluster["mandatory_commands"]
                        if len(mandatoryCommands) > 0:

                            acceptedCommandListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.AcceptedCommandList)])
                            acceptedCommandList = acceptedCommandListResponse[endpoint][clusterClass][clusterClass.Attributes.AcceptedCommandList]
                            console.print(f"AcceptedCommandList: {acceptedCommandList}")

                            generatedCommandListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.GeneratedCommandList)])
                            generatedCommandList = generatedCommandListResponse[endpoint][clusterClass][clusterClass.Attributes.GeneratedCommandList]
                            console.print(f"GeneratedCommandList: {generatedCommandList}")

                            for command in cluster["mandatory_commands"]:
                                console.print(command["pics_code"])
                    



parser = argparse.ArgumentParser()
parser.add_argument('--device-type-data', required=True)
args, unknown = parser.parse_known_args()

deviceTypeRequirementInputPathStr = args.device_type_data
if not deviceTypeRequirementInputPathStr.endswith('/'):
    deviceTypeRequirementInputPathStr += '/'

if __name__ == "__main__":
    default_matter_test_main()
