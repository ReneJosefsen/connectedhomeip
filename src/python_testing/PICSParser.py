import os
import argparse
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--pics', required=True)
args = parser.parse_args()

basePath = os.path.dirname(__file__)
PICSPathStr = args.PICS

if not PICSPathStr.endswith('/'):
    PICSPathStr += '/'

# Load PICS XML templates
xmlFileList = os.listdir(PICSPathStr)

# print(xmlFileList)

for fileName in xmlFileList:

    # If not an xml file, ignore it
    if not fileName.lower().endswith('.xml'):
        print(f"[red]Ignoring \"{fileName}\"")
        continue

    # Custom handler for base xml
    if fileName.lower() == 'base.xml':
        print(f"[red]Ignoring \"{fileName}\"")
        continue

    # Open the PICS XML file
    try:
        # Open the XML PICS template file
        print(f"Open \"{PICSPathStr}{fileName}\"")
        tree = ET.parse(f"{PICSPathStr}{fileName}")
        root = tree.getroot()
    except ET.ParseError:
        print(f"[red]Could not find \"{fileName}\"")

    # Usage PICS
    usageNode = root.find('usage')
    for picsItem in usageNode:
        itemNumberElement = picsItem.find('itemNumber')
        supportElement = picsItem.find('support')
        print(f"{itemNumberElement.text} - {supportElement.text}")

    # Feature PICS
    featureNode = root.find("./clusterSide[@type='server']/features")
    for picsItem in featureNode:
        itemNumberElement = picsItem.find('itemNumber')
        supportElement = picsItem.find('support')
        print(f"{itemNumberElement.text} - {supportElement.text}")

    # Attributes PICS
    serverAttributesNode = root.find("./clusterSide[@type='server']/attributes")
    for picsItem in serverAttributesNode:
        itemNumberElement = picsItem.find('itemNumber')
        supportElement = picsItem.find('support')
        print(f"{itemNumberElement.text} - {supportElement.text}")

    # AcceptedCommandList PICS
    serverCommandsReceivedNode = root.find("./clusterSide[@type='server']/commandsReceived")
    for picsItem in serverCommandsReceivedNode:
        itemNumberElement = picsItem.find('itemNumber')
        supportElement = picsItem.find('support')
        print(f"{itemNumberElement.text} - {supportElement.text}")

    # GeneratedCommandList PICS
    serverCommandsGeneratedNode = root.find("./clusterSide[@type='server']/commandsGenerated")
    for picsItem in serverCommandsGeneratedNode:
        itemNumberElement = picsItem.find('itemNumber')
        supportElement = picsItem.find('support')
        print(f"{itemNumberElement.text} - {supportElement.text}")

    # Event PICS
    serverEventsNode = root.find("./clusterSide[@type='server']/Events")
    for picsItem in serverEventsNode:
        itemNumberElement = picsItem.find('itemNumber')
        supportElement = picsItem.find('support')
        print(f"{itemNumberElement.text} - {supportElement.text}")
