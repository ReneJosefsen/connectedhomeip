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

import os
import json
import argparse
from matter_testing_support import MatterBaseTest, default_matter_test_main, async_test_body
import chip.clusters as Clusters
from mobly import asserts
from rich.console import Console

console = None

simpleDeviceTypes = [771]


def getPicsElementId(picsStr, elementTypeStr, elementIdLen):
    elementIdStrIndex = picsStr.find(elementTypeStr) + len(elementTypeStr)
    elementId = picsStr[elementIdStrIndex:elementIdStrIndex + elementIdLen]
    return int(elementId, 16)


class TC_DeviceTypeValidation(MatterBaseTest):
    @async_test_body
    async def test_TC_DeviceTypeValidation(self):

        # Test defines
        featurePicsStr = ".S.F"
        featureIdSize = 2
        attributePicsStr = ".S.A"
        attributeIdSize = 4
        commandPicsStr = ".S.C"
        commandIdSize = 2

        # Endpoint define
        rootNodeEndpointID = 0

        # Create console to print
        global console
        console = Console()

        # Run descriptor validation test
        devCtrl = self.default_controller

        # Device Type dict
        deviceTypeRequirementFileDict = {}
        deviceTypeFromDeviceDict = {}

        # Store the device type requirement JSON list
        deviceTypeRequirementFileList = os.listdir(deviceTypeRequirementInputPathStr)
        # console.log(deviceTypeRequirementFileList)

        for deviceTypeRequirementFileName in deviceTypeRequirementFileList:
            file = f"{deviceTypeRequirementInputPathStr}{deviceTypeRequirementFileName}"
            with open(file, 'r') as deviceTypeRequirementFile:
                # console.log(file)

                deviceTypeRequirementData = json.load(deviceTypeRequirementFile)
                deviceTypeID = deviceTypeRequirementData['id']

                # console.print(f"{deviceTypeID} - {int(deviceTypeID, 16)} - {deviceTypeRequirementFileName}")

                deviceTypeRequirementFileDict[int(deviceTypeID, 16)] = {
                    "fileName": deviceTypeRequirementFileName,
                }
        # console.print(deviceTypeRequirementFileDict)

        # Determine if any endpoint are duplicate, which is needed for base device requirement verification
        wildcardDescriptorResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(Clusters.Descriptor.Attributes.DeviceTypeList), (Clusters.Descriptor.Attributes.PartsList)])
        # console.print(wildcardDescriptorResponse)

        for endpoint in wildcardDescriptorResponse:
            for deviceTypeStruct in wildcardDescriptorResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.DeviceTypeList]:
                if deviceTypeStruct.deviceType not in deviceTypeFromDeviceDict:
                    deviceTypeFromDeviceDict[deviceTypeStruct.deviceType] = [endpoint]
                else:
                    deviceTypeFromDeviceDict[deviceTypeStruct.deviceType].append(endpoint)
        # console.print(deviceTypeFromDeviceDict)

        # Get the parts list of endpoint 0 to determine which endpoint to validate
        partsList = wildcardDescriptorResponse[rootNodeEndpointID][Clusters.Descriptor][Clusters.Descriptor.Attributes.PartsList]

        # Verify that parts list on endpoint 0 is not empty
        asserts.assert_true(partsList, "PartsList is empty ❌")

        # Add endpoint 0 to the parts list, since this is not returned by the device, to validate root node as well
        partsList.insert(0, 0)

        console.print(f"PartsList: {partsList}")

        for endpoint in partsList:
            console.print(f"[blue]Endpoint: {endpoint}")

            deviceTypeListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Descriptor.Attributes.DeviceTypeList)])
            # console.print(deviceTypeListResponse)
            deviceTypeList = deviceTypeListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.DeviceTypeList]

            for deviceTypeStruct in deviceTypeList:
                console.print(f"[blue]DeviceType: {deviceTypeStruct.deviceType}")
                console.print(f"[blue]Revision: {deviceTypeStruct.revision}")

                file = f"{deviceTypeRequirementInputPathStr}{deviceTypeRequirementFileDict[deviceTypeStruct.deviceType]['fileName']}"
                # console.print(file)

                with open(file, 'r') as deviceTypeRequirementFile:
                    deviceTypeRequirementData = json.load(deviceTypeRequirementFile)

                    # Revision
                    requiredRevision = int(deviceTypeRequirementData['revision'])
                    console.print(f"Mandated device type revision: {requiredRevision}")
                    asserts.assert_equal(requiredRevision, deviceTypeStruct.revision, "DeviceType revision does not match ❌")
                    console.print("[green]Revision validation successful ✅")

                    # Server clusteres
                    requiredServerClusters = deviceTypeRequirementData['server_clusters']
                    # console.print(requiredServerClusters)

                    # Read server list
                    serverListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Descriptor.Attributes.ServerList)])
                    serverList = serverListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.ServerList]
                    console.print(f"Server List: {serverList}")

                    # console.print(deviceTypeRequirementData)

                    for cluster in requiredServerClusters:
                        # Check if server cluster is available
                        clusterId = int(cluster['id'], 16)
                        console.print(f"Mandated server cluster: {clusterId}")

                        # Check if cluster id is in the server list
                        asserts.assert_true(clusterId in serverList, f"Cluster ({cluster['id']}) not found in server list ❌")
                        console.print("[green]Cluster validation successful ✅")

                        clusterClass = getattr(Clusters, devCtrl.GetClusterHandler().GetClusterInfoById(clusterId)['clusterName'])
                        # console.print(devCtrl.GetClusterHandler().GetClusterInfoById(clusterId))

                        # Check required elements on the server
                        mandatoryFeatures = cluster["mandatory_features"]

                        # Since excluded_features might not be in all files, if not present, assume empty
                        try:
                            excludedFeatures = cluster["excluded_features"]
                        except KeyError:
                            excludedFeatures = []

                        if mandatoryFeatures or excludedFeatures:

                            # Read feature map from DUT
                            featureMapResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.FeatureMap)])
                            featureMap = featureMapResponse[endpoint][clusterClass][clusterClass.Attributes.FeatureMap]
                            console.print(f"FeatureMap: {featureMap} / {featureMap:#010b}")

                            # Check if mandated feature is supported
                            for feature in mandatoryFeatures:
                                console.print(f"Mandated feature PICS: {feature['pics_code']}")

                                featureBitId = getPicsElementId(feature['pics_code'], featurePicsStr, featureIdSize)

                                featureBitState = (featureMap >> featureBitId) & 1
                                console.print(f"[blue]FeatureBit ({featureBitId}) state {featureBitState}")

                                asserts.assert_true(featureBitState == 1, f"Mandated feature ({featureBitId:#04x}) not found in feature map ❌")
                                console.print("[green]Feature validation successful ✅")

                            for feature in excludedFeatures:
                                console.print(f"Excluded feature PICS: {feature['pics_code']}")

                                try:
                                    featureBitId = int(feature["id"], 16)
                                except KeyError:
                                    featureBitId = getPicsElementId(feature['pics_code'], featurePicsStr, featureIdSize)

                                featureBitState = (featureMap >> featureBitId) & 1
                                console.print(f"[blue]FeatureBit ({featureBitId}) state {featureBitState}")

                                asserts.assert_true(featureBitState == 0, f"Excluded feature ({featureBitId:#04x}) found in feature map ❌")
                                console.print("[green]Feature validation successful ✅")

                        mandatoryAttributes = cluster["mandatory_attributes"]

                        # Since excluded_attributes might not be in all files, if not present, assume empty
                        try:
                            excludedAttributes = cluster["excluded_attributes"]
                        except KeyError:
                            excludedAttributes = []

                        if mandatoryAttributes or excludedAttributes:

                            # Read attribute list
                            attributeListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.AttributeList)])
                            attributeList = attributeListResponse[endpoint][clusterClass][clusterClass.Attributes.AttributeList]
                            console.print(f"AttributeList: {attributeList}")

                            for attribute in mandatoryAttributes:
                                console.print(f"Mandated attribute PICS: {attribute['pics_code']}")
                                attributeId = getPicsElementId(attribute['pics_code'], attributePicsStr, attributeIdSize)

                                console.print(f"[blue]AttributeID: {attributeId}")
                                asserts.assert_true(attributeId in attributeList, f"Mandated attribute ({attributeId:#06x}) not found in attribute list ❌")
                                console.print("[green]Attribute validation successful ✅")

                            for attribute in excludedAttributes:
                                console.print(f"Excluded attribute PICS: {attribute['pics_code']}")

                                try:
                                    attributeId = int(attribute["id"], 16)
                                except KeyError:
                                    attributeId = getPicsElementId(attribute['pics_code'], attributePicsStr, attributeIdSize)

                                console.print(f"[blue]AttributeID: {attributeId}")
                                asserts.assert_true(attributeId not in attributeList, f"Excluded attribute ({attributeId:#06x}) found in attribute list ❌")
                                console.print("[green]Attribute validation successful ✅")

                        mandatoryCommands = cluster["mandatory_commands"]

                        # Since excluded_commands might not be in all files, if not present, assume empty
                        try:
                            excludedCommands = cluster["excluded_commands"]
                        except KeyError:
                            excludedCommands = []

                        if mandatoryCommands or excludedCommands:

                            acceptedCommandListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.AcceptedCommandList)])
                            acceptedCommandList = acceptedCommandListResponse[endpoint][clusterClass][clusterClass.Attributes.AcceptedCommandList]
                            console.print(f"AcceptedCommandList: {acceptedCommandList}")

                            generatedCommandListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, clusterClass.Attributes.GeneratedCommandList)])
                            generatedCommandList = generatedCommandListResponse[endpoint][clusterClass][clusterClass.Attributes.GeneratedCommandList]
                            console.print(f"GeneratedCommandList: {generatedCommandList}")

                            for command in mandatoryCommands:
                                console.print(f"Mandated command PICS: {command['pics_code']}")
                                commandId = getPicsElementId(command['pics_code'], commandPicsStr, commandIdSize)

                                console.print(f"[blue]CommandID: {commandId}")

                                if ".Rsp" in command['pics_code']:
                                    asserts.assert_true(commandId in acceptedCommandList, f"Mandated command ({commandId:#04x}) not found in accepted command lists ❌")
                                    console.print("[green]Accepted command validation successful ✅")
                                elif ".Tx" in command['pics_code']:
                                    asserts.assert_true(commandId in generatedCommandList, f"Mandated command ({commandId:#04x}) not found in generated command lists ❌")
                                    console.print("[green]Generated command validation successful ✅")
                                else:
                                    asserts.assert_true(False, "[red]Invalid PICS code ❌")

                            for command in excludedCommands:
                                console.print(f"Excluded command PICS: {command['pics_code']}")

                                try:
                                    commandId = int(command["id"], 16)
                                except KeyError:
                                    commandId = getPicsElementId(command['pics_code'], commandPicsStr, commandIdSize)

                                console.print(f"[blue]CommandID: {commandId}")

                                if ".Rsp" in command['pics_code']:
                                    asserts.assert_true(commandId not in acceptedCommandList, f"Excluded command ({commandId:#04x}) found in accepted command lists ❌")
                                    console.print("[green]Accepted command validation successful ✅")
                                elif ".Tx" in command['pics_code']:
                                    asserts.assert_true(commandId not in generatedCommandList, f"Excluded command ({commandId:#04x}) found in generated command lists ❌")
                                    console.print("[green]Generated command validation successful ✅")
                                else:
                                    asserts.assert_true(False, "[red]Invalid PICS code ❌")

                    # Client clusters
                    requiredClientClusters = deviceTypeRequirementData['client_clusters']
                    # console.print(requiredClientClusters)

                    # If any mandated clients, read client list and validate
                    if requiredClientClusters:
                        # Read client list
                        clientListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Descriptor.Attributes.ClientList)])
                        clientList = clientListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.ClientList]
                        console.print(f"Client List: {clientList}")

                        for cluster in requiredClientClusters:
                            # Check if client cluster is available
                            clusterId = int(cluster['id'], 16)
                            console.print(f"Mandated client cluster: {clusterId}")

                            # Check if cluster id is in the server list
                            asserts.assert_true(clusterId in clientList, f"Cluster ({cluster['id']}) not found in client list ❌")
                            console.print("[green]Cluster validation successful ✅")

                    # --- Verify Base Device Type requirements

                    # Descriptor is mandatory as a part of base device
                    descriptorClusterId = devCtrl.GetClusterHandler().ListClusterInfo()['Descriptor']['clusterId']
                    console.print(f"Mandated base device cluster: {descriptorClusterId}")
                    asserts.assert_true(descriptorClusterId in serverList, f"Descriptor cluster ({descriptorClusterId:#04x}) not found in server list ❌")
                    console.print("[green]Cluster validation successful ✅")

                    # If device type is a simple device and client is supported, binding is mandatory
                    bindingClusterId = devCtrl.GetClusterHandler().ListClusterInfo()['Binding']['clusterId']

                    # Determine if clientlist has been read else do it now
                    try:
                        clientListForBaseDevice = clientList
                    except UnboundLocalError:
                        # Read client list
                        clientListResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Descriptor.Attributes.ClientList)])
                        clientListForBaseDevice = clientListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.ClientList]
                        # console.print(f"Client List: {clientListForBaseDevice}")

                    # Check if binding is present is both conditions are true
                    if (deviceTypeStruct.deviceType in simpleDeviceTypes) and clientListForBaseDevice:
                        console.print(f"Mandated base device cluster: {bindingClusterId}")
                        asserts.assert_true(bindingClusterId in serverList, f"Binding cluster ({bindingClusterId:#04x}) not found in server list ❌")
                        console.print("[green]Cluster validation successful ✅")

                    # If identify cluster is present, verify that QRY feature is not supported
                    identifyClusterId = devCtrl.GetClusterHandler().ListClusterInfo()['Identify']['clusterId']
                    if identifyClusterId in serverList:
                        console.print(f"Excluded base device feature QRY in cluster: {identifyClusterId}")

                        featureMapResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Identify.Attributes.FeatureMap)])
                        featureMap = featureMapResponse[endpoint][Clusters.Identify][Clusters.Identify.Attributes.FeatureMap]
                        # console.print(f"FeatureMap: {featureMap} / {featureMap:#010b}")

                        # QRY feature at index 0
                        featureBitId = 0
                        featureBitState = (featureMap >> featureBitId) & 1
                        # console.print(f"[blue]FeatureBit ({featureBitId}) state {featureBitState}")
                        asserts.assert_true(featureBitState == 0, f"Excluded base device QRY feature ({featureBitId:#04x}) found in feature map ❌")
                        console.print("[green]Feature validation successful ✅")

                    # If an endpoint’s device type is a duplicated on another endpoint, verify descriptor support TagList feature
                    for deviceTypeOnEndpoint in deviceTypeFromDeviceDict:
                        if (endpoint in deviceTypeFromDeviceDict[deviceTypeOnEndpoint]) and len(deviceTypeFromDeviceDict[deviceTypeOnEndpoint]) > 1:
                            console.print(f"Mandated base device feature TAGLIST in cluster: {descriptorClusterId}")

                            featureMapResponse = await devCtrl.ReadAttribute(self.dut_node_id, [(endpoint, Clusters.Descriptor.Attributes.FeatureMap)])
                            featureMap = featureMapResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.FeatureMap]
                            console.print(f"FeatureMap: {featureMap} / {featureMap:#010b}")

                            # TagList feature at index 0
                            featureBitId = 0
                            featureBitState = (featureMap >> featureBitId) & 1
                            # console.print(f"[blue]FeatureBit ({featureBitId}) state {featureBitState}")
                            asserts.assert_true(featureBitState == 1, f"Mandated base device TAGLIST feature ({featureBitId:#04x}) not found in feature map ❌")
                            console.print("[green]Feature validation successful ✅")
                    

parser = argparse.ArgumentParser()
parser.add_argument('--device-type-data', required=True)
args, unknown = parser.parse_known_args()

deviceTypeRequirementInputPathStr = args.device_type_data
if not deviceTypeRequirementInputPathStr.endswith('/'):
    deviceTypeRequirementInputPathStr += '/'

if __name__ == "__main__":
    default_matter_test_main()
