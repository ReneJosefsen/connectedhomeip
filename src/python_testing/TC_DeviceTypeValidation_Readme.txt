### Setup ###
The script depends on the device_type_requirements json files (currently) present in the test-plan repo: https://github.com/CHIP-Specifications/chip-test-plans/tree/master/tools/device_type_requirements, in order to validate that a device have implemented the mandated elements stated in the device type specification.

### How to run ###
In order to use the script the Python CHIP controller must be build, use the instructions at https://github.com/project-chip/connectedhomeip/blob/master/docs/guides/python_chip_controller_building.md#building

cd into "connectedhomeip" folder

Activate the Python virtual environment using "source out/python_env/bin/activate"

If the "DUT" has not been commissioned this can be done by passing in the configuration in the following way:
python3 'src/python_testing/TC_DeviceTypeValidation.py' --device-type-data "/Users/renejosefsen/Developer/GitData/connectedhomeip/src/python_testing/device_type_requirements" --commissioning-method ble-thread --discriminator <DESCRIMINATOR> --passcode <PASSCODE> --thread-dataset-hex <DATASET_AS_HEX> --paa-trust-store-path "credentials/development/paa-root-certs"

If a "DUT" has alreasy ben commissioned, run the script using the following command: 
python3 'src/python_testing/TC_DeviceTypeValidation.py' --device-type-data "/Users/renejosefsen/Developer/GitData/connectedhomeip/src/python_testing/device_type_requirements"

- --device-type-data is the absolute path to the location of the device_type_requirements json files.