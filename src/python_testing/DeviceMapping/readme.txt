### Setup ###
The script uses the json based data model in order to convert cluster identifiers into PICS Codes. The file script expects the file to be located in a folder named "clusterData", the file can be downloaded here: https://groups.csa-iot.org/wg/matter-csg/document/27290 

The script uses the PICS XML templates for generate the PICS output, the templates is expected to be located in "PICS/<folderFromCauseway>", the PICS templates can be downloaded here:
https://groups.csa-iot.org/wg/matter-csg/document/26122

### Expected folder structure ###
- MatterDeviceMapping.py (script)
- clusterData (folder)
-- Data model in JSON format
- PICS (folder)
-- XML_version master Version_12 (folder)
--- PICS_tamples in XML format
- output (folder)
-- Generated PICS in XML format

Alternatively, if a different struce is desired, the following variables can be modified to point to the desired paths:
clusterInfoInputPathStr (Path to data model in JSON format)
xmlTemplatePathStr (Path to PICS XML templates)
outputPathStr (Output path of modified PICS files)

### How to run ###
In order to use the script the Python CHIP controller must be build, use the instructions at https://github.com/project-chip/connectedhomeip/blob/master/docs/guides/python_chip_controller_building.md#building

cd into "connectedhomeip" folder

Activate the Python virtual environment using "source out/python_env/bin/activate"

If the "DUT" has not been commissioned this can be done by passing in the configuration in the following way:
"sudo python3 '/Users/renejosefsen/Downloads/Matter Device Mapping/MatterDeviceMapping.py' --commissioning-method ble-thread --discriminator <DESCRIMINATOR> --passcode <PASSCODE> --thread-dataset-hex <DATASET_AS_HEX> --paa-trust-store-path credentials/development/paa-root-certs"

If a "DUT" has alreasy ben commissioned, run the script using the following command: 
"sudo python3 'src/python_testing/DeviceMapping/MatterDeviceMapping.py'"