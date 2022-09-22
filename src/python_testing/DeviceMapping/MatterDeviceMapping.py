import sys
import random
import asyncio
import builtins
import atexit
import logging
import json
import pathlib
import os
import xml.etree.ElementTree as ET

from rich import print
from rich.pretty import pprint
from rich import pretty
from rich import inspect
from rich.console import Console
import coloredlogs

from binascii import unhexlify

from chip import ChipDeviceCtrl
from chip.ChipStack import *
import chip.clusters as Clusters
import chip.FabricAdmin
import chip.CertificateAuthority
import chip.logging

_fabricAdmins = None
certificateAuthorityManager = None

basePath = os.path.dirname(__file__)
clusterInfoInputPathStr = os.path.join(basePath, 'clusterData', 'Specification_version master da1249d.json')
xmlTemplatePathStr = os.path.join(basePath, 'PICS', 'XML_version master Version_12/')
outputPathStr = os.path.join(basePath, 'output/')

serverTag = ".S"
featureTag = ".F"
attributeTag = ".A"
commandTag = ".C"
acceptedCommandTag = ".Rsp"
generatedCommandTag = ".Tx"


def StackShutdown():
    certificateAuthorityManager.Shutdown()
    builtins.chipStack.Shutdown()

def GenerateDevicePicsXmlFiles(clusterName, clusterPicsCode, featurePicsList, attributePicsList, acceptedCommandPicsList, generatedCommandPicsList):
    
    xmlPath = xmlTemplatePathStr
    fileName = ""

    # Map clusters to common XML template if needed
    deviceManagementClusterList = ["Basic Information Cluster", "Node Operational Credentials Cluster", "Network Commissioning Cluster"]
    administratorCommissioningCluster = "Administrator Commissioning Cluster"
    onOffCluster = "On/Off"
    
    print(clusterName)
    
    if any(x in clusterName for x in deviceManagementClusterList):
        clusterName = "Device Management"

        # Determine if device management has already been handled and needs to use the outputted XML file
        for outputFolderFileName in os.listdir(outputPathStr):
            if clusterName in outputFolderFileName:
                xmlPath = outputPathStr
                fileName = outputFolderFileName
                break

    elif administratorCommissioningCluster in clusterName:
        clusterName = "Multiple Fabrics"
    
    elif onOffCluster == clusterName:
        clusterName = clusterName.replace("/","-")

    # Determine if there is a match for the cluster name
    if fileName == "":
        for file in xmlFileList:
            if clusterName.lower() in file.lower():
                fileName = file
                break
        else:
            console.print(f"[red]Could not find matching file for \"{clusterName}\"")
            return

    try:
        # Open the XML PICS template file
        console.print(f"Open \"{xmlPath}{fileName}\"")
        tree = ET.parse(f"{xmlPath}{fileName}")
        root = tree.getroot()
    except:
        console.print(f"[red]Could not find \"{fileName}\"")
        return

    # Usage PICS
    #console.print(clusterPicsCode)
    usageNode = root.find('usage')
    for picsItem in usageNode:
        itemNumberElement = picsItem.find('itemNumber')
        
        console.print(f"Searching for {clusterPicsCode}.S")

        if itemNumberElement.text == f"{clusterPicsCode}.S":
            console.print("Found usage PICS value in XML template")
            supportElement = picsItem.find('support')
            #console.print(f"Support: {supportElement.text}")
            supportElement.text = "true"

    # Feature PICS
    #console.print(featurePicsList)
    featureNode = root.find("./clusterSide[@type='server']/features")
    for picsItem in featureNode:
        itemNumberElement = picsItem.find('itemNumber')

        console.print(f"Searching for {itemNumberElement.text}")

        if f"{itemNumberElement.text}" in featurePicsList:
            console.print("Found feature PICS value in XML template")
            supportElement = picsItem.find('support')
            supportElement.text = "true"

    # Attributes PICS
    #TODO: Only check if list is not empty
    #console.print(attributePicsList)
    serverAttributesNode = root.find("./clusterSide[@type='server']/attributes")
    for picsItem in serverAttributesNode:
        itemNumberElement = picsItem.find('itemNumber')

        console.print(f"Searching for {itemNumberElement.text}")

        if f"{itemNumberElement.text}" in attributePicsList:
            console.print("Found attribute PICS value in XML template")
            supportElement = picsItem.find('support')
            supportElement.text = "true"

    # AcceptedCommandList PICS
    #TODO: Only check if list is not empty
    #console.print(acceptedCommandPicsList)
    serverCommandsReceivedNode = root.find("./clusterSide[@type='server']/commandsReceived")
    for picsItem in serverCommandsReceivedNode:
        itemNumberElement = picsItem.find('itemNumber')

        console.print(f"Searching for {itemNumberElement.text}")

        if f"{itemNumberElement.text}" in acceptedCommandPicsList:
            console.print("Found acceptedCommand PICS value in XML template")
            supportElement = picsItem.find('support')
            supportElement.text = "true"

    # GeneratedCommandList PICS
    #console.print(generatedCommandPicsList)
    #TODO: Only check if list is not empty
    serverCommandsGeneratedNode = root.find("./clusterSide[@type='server']/commandsGenerated")
    for picsItem in serverCommandsGeneratedNode:
        itemNumberElement = picsItem.find('itemNumber')

        console.print(f"Searching for {itemNumberElement.text}")

        if f"{itemNumberElement.text}" in generatedCommandPicsList:
            console.print("Found generatedCommand PICS value in XML template")
            supportElement = picsItem.find('support')
            supportElement.text = "true"

    # Write XML file
    tree.write(f"{outputPathStr}/{fileName}")

#### Defines ####

# List of globale attributes (server)
# Does not read ClusterRevision [0xFFFD] (not relevant), EventList [0xFFFA] (Provisional)  
featureMapAttributeId = "0xFFFC"
attributeListAttributeId = "0xFFFB"
acceptedCommandListAttributeId = "0xFFF9"
generatedCommandListAttributeId = "0xFFF8"

# Endpoint define
rootNodeEndpointID = 0

#### Load cluster info
inputJson = {}
clusterInfoDict = {}

pprint(clusterInfoInputPathStr)
pprint(xmlTemplatePathStr)
pprint(outputPathStr)

with open(clusterInfoInputPathStr, 'r') as clusterInfoInputFile:
    clusterInfoJson = json.load(clusterInfoInputFile)

for cluster in clusterInfoJson["ClusterIdentifiers"]:
    clusterInfoDict[clusterInfoJson["ClusterIdentifiers"][f"{cluster}"]["Identifier"].lower()] = {
        "Name":cluster,
        "PICS_Code":clusterInfoJson["ClusterIdentifiers"][f"{cluster}"]["PICS Code"],
    }

#### Load PICS XML templates
xmlFileList = os.listdir(xmlTemplatePathStr)

# Setup output path
outputPath = pathlib.Path(outputPathStr)
if not outputPath.exists():
    outputPath.mkdir()
else:
    for file in outputPath.iterdir():
        pathlib.Path(file).unlink()

# Init CHIP
chip.native.Init()

#### Init & Commissioning ####
pretty.install(indent_guides=True, expand_all=True)

console = Console()

coloredlogs.install(level='DEBUG')
chip.logging.RedirectToPythonLogging()
logging.getLogger().setLevel(logging.WARN)

console.print(f"Setting up device mapper")

# Init chip stack, load admins from storage and create a controller
chipStack = ChipStack(persistentStoragePath="/tmp/repl-storage.json", enableServerInteractions=False)
certificateAuthorityManager = chip.CertificateAuthority.CertificateAuthorityManager(chipStack, chipStack.GetStorageManager())

certificateAuthorityManager.LoadAuthoritiesFromStorage()

if (len(certificateAuthorityManager.activeCaList) == 0):
    ca = certificateAuthorityManager.NewCertificateAuthority()
    ca.NewFabricAdmin(vendorId=0xFFF1, fabricId=1)
elif (len(certificateAuthorityManager.activeCaList[0].adminList) == 0):
    certificateAuthorityManager.activeCaList[0].NewFabricAdmin(vendorId=0xFFF1, fabricId=1)

caList = certificateAuthorityManager.activeCaList

devCtrl = caList[0].adminList[0].NewController()

builtins.devCtrl = devCtrl
atexit.register(StackShutdown)
clusterHandler = devCtrl.GetClusterHandler()

#'''
# Thread commissioning
nodeID = random.randint(1,9999)
console.print(f"[blue]Commissioning to Thread node with id {nodeID}")
activeDataSetHex = "000300001902085b34dead5b34beef051000112233445566778899aabbccddeeff01025b34"
activeDataSet = unhexlify(activeDataSetHex.replace("\n","").replace(" ",""))
descriminator = 3840
setupCode = 20202021
devCtrl.CommissionThread(descriminator, setupCode, nodeID, activeDataSet)
#'''

'''
# Reuse already commissioned node id
nodeID = 5759
console.print(f"[blue]Use already commissioned node with id {nodeID}")
'''

#### Device mapping ####
console.print(f"[blue]Perform device mapping")
# Determine how many endpoints to map
# Test step 1 - Read parts list

partsListResponse = asyncio.run(devCtrl.ReadAttribute(nodeID, [(rootNodeEndpointID, Clusters.Descriptor.Attributes.PartsList)]))
partsList = partsListResponse[0][Clusters.Descriptor][Clusters.Descriptor.Attributes.PartsList]

# Add endpoint 0 to the parts list, since this is not returned by the device
partsList.insert(0, 0)
console.print(partsList)

for endpoint in partsList:
    # Test step 2 - Map each available endpoint
    console.print(f"Mapping endpoint: {endpoint}")

    # Read device list (Not required)
    deviceListResponse = asyncio.run(devCtrl.ReadAttribute(nodeID, [(endpoint, Clusters.Descriptor.Attributes.DeviceTypeList)]))
    # TODO: Print the list and not just the first element
    console.print(f"Device Type: {deviceListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.DeviceTypeList][0].type}")

    # Read server list
    serverListResponse = asyncio.run(devCtrl.ReadAttribute(nodeID, [(endpoint, Clusters.Descriptor.Attributes.ServerList)]))
    #console.print(serverListResponse)
    serverList = serverListResponse[endpoint][Clusters.Descriptor][Clusters.Descriptor.Attributes.ServerList]
    #console.print(serverList)

    for server in serverList:
        
        featurePicsList = []
        attributePicsList = []
        acceptedCommandListPicsList = []
        generatedCommandListPicsList = []

        #console.print(clusterHandler.GetClusterInfoById(server))
        clusterClass = getattr(Clusters, clusterHandler.GetClusterInfoById(server)['clusterName'])
        clusterName = clusterInfoDict[f"0x{server:04x}"]["Name"]
        PICS_Code = clusterInfoDict[f"0x{server:04x}"]["PICS_Code"]
        console.print(f"{clusterName} - {PICS_Code}")

        # Print PICS for specific server from dict
        #console.print(clusterInfoDict[f"0x{server:04x}"])

        ## Read feature map
        featureMapResponse = asyncio.run(devCtrl.ReadAttribute(nodeID, [(endpoint, clusterClass.Attributes.FeatureMap)]))
        #console.print(f"FeatureMap: {featureMapResponse[endpoint][clusterClass][clusterClass.Attributes.FeatureMap]}")

        featureMapValue = featureMapResponse[endpoint][clusterClass][clusterClass.Attributes.FeatureMap]
        featureMapBitString = "{:08b}".format(featureMapValue).lstrip("0")
        for bitLocation in range(len(featureMapBitString)):
            if featureMapValue >> bitLocation & 1 == 1:
                #console.print(f"{PICS_Code}{serverTag}{featureTag}{bitLocation:02x}")
                featurePicsList.append(f"{PICS_Code}{serverTag}{featureTag}{bitLocation:02x}")

        console.print("Collected feature PICS:")
        console.print(featurePicsList)

        # Read attribute list
        attributeListResponse = asyncio.run(devCtrl.ReadAttribute(nodeID, [(endpoint, clusterClass.Attributes.AttributeList)]))
        attributeList = attributeListResponse[endpoint][clusterClass][clusterClass.Attributes.AttributeList]
        #console.print(f"AttributeList: {attributeList}")

        # Convert attribute to PICS code
        for attribute in attributeList:
            if (attribute != 0xfff8 and attribute != 0xfff9 and attribute != 0xfffa and attribute != 0xfffb and attribute != 0xfffc and attribute != 0xfffd):
                #console.print(f"{PICS_Code}{serverTag}{attributeTag}{attribute:04x}")
                attributePicsList.append(f"{PICS_Code}{serverTag}{attributeTag}{attribute:04x}")
            '''
            else:
                console.print(f"[yellow]Ignore global attribute 0x{attribute:04x}")
            '''

        console.print("Collected attribute PICS:")
        console.print(attributePicsList)

        # Read AcceptedCommandList
        acceptedCommandListResponse = asyncio.run(devCtrl.ReadAttribute(nodeID, [(endpoint, clusterClass.Attributes.AcceptedCommandList)]))
        acceptedCommandList = acceptedCommandListResponse[endpoint][clusterClass][clusterClass.Attributes.AcceptedCommandList]
        #console.print(f"AcceptedCommandList: {acceptedCommandList}")

        # Convert accepted command to PICS code
        for acceptedCommand in acceptedCommandList:
            #console.print(f"{PICS_Code}{serverTag}{commandTag}{acceptedCommand:02x}{acceptedCommandTag}")
            acceptedCommandListPicsList.append(f"{PICS_Code}{serverTag}{commandTag}{acceptedCommand:02x}{acceptedCommandTag}")

        console.print("Collected accepted command PICS:")
        console.print(acceptedCommandListPicsList)

        # Read GeneratedCommandList
        generatedCommandListResponse = asyncio.run(devCtrl.ReadAttribute(nodeID, [(endpoint, clusterClass.Attributes.GeneratedCommandList)]))
        generatedCommandList = generatedCommandListResponse[endpoint][clusterClass][clusterClass.Attributes.GeneratedCommandList]
        #console.print(f"GeneratedCommandList: {generatedCommandList}")

        # Convert accepted command to PICS code
        for generatedCommand in generatedCommandList:
            #console.print(f"{PICS_Code}{serverTag}{commandTag}{generatedCommand:02x}{generatedCommandTag}")
            generatedCommandListPicsList.append(f"{PICS_Code}{serverTag}{commandTag}{generatedCommand:02x}{generatedCommandTag}")

        console.print("Collected generated command PICS:")
        console.print(generatedCommandListPicsList)

        # Write the collected PICS to a PICS XML file
        GenerateDevicePicsXmlFiles(clusterName, PICS_Code, featurePicsList, attributePicsList, acceptedCommandListPicsList, generatedCommandListPicsList)
