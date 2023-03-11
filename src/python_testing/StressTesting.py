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
from chip.clusters.Attribute import TypedAttributePath, SubscriptionTransaction
import logging
import queue
import asyncio
from threading import Event
from mobly import asserts

from rich.console import Console

console = Console()

class AttributeChangeAccumulator:
    def __init__(self, name: str, expected_attribute: Clusters.ClusterAttributeDescriptor, output: queue.Queue):
        self._name = name
        self._output = output
        self._expected_attribute = expected_attribute

    def __call__(self, path: TypedAttributePath, transaction: SubscriptionTransaction):
        if path.AttributeType == self._expected_attribute:
            data = transaction.GetAttribute(path)

            value = {
                'name': self._name,
                'endpoint': path.Path.EndpointId,
                'attribute': path.AttributeType,
                'value': data
            }
            console.print("Got subscription report on client %s for %s: %s" % (self.name, path.AttributeType, data))
            self._output.put(value)

    @property
    def name(self) -> str:
        return self._name

class ResubscriptionCatcher:
    def __init__(self, name):
        self._name = name
        self._got_resubscription_event = Event()

    def __call__(self, transaction: SubscriptionTransaction, terminationError, nextResubscribeIntervalMsec):
        self._got_resubscription_event.set()
        console.print("Got resubscription on client %s" % self.name)

    @property
    def name(self) -> str:
        return self._name

    @property
    def caught_resubscription(self) -> bool:
        return self._got_resubscription_event.is_set()

class DescriptorTest(MatterBaseTest):
    @async_test_body
    async def test_subscription_stress(self):

        # Create queue
        output_queue = queue.Queue()

        # Run descriptor validation test
        dev_ctrl = self.default_controller
        
        #### Stress using reads
        # for x in range(5):
        #     deviceAttributeResponse = await dev_ctrl.ReadAttribute(self.dut_node_id, [('*')])
        #     console.print(deviceAttributeResponse)

        subscription = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.OnOff.Attributes.OnOff)],
                                             reportInterval=(0, 1), keepSubscriptions=False)

        # subscription = await dev_ctrl.ReadAttribute(self.dut_node_id, [(1, Clusters.OnOff.Attributes.OnOff)])
        console.print(subscription)

        attribute_handler = AttributeChangeAccumulator(
            name=dev_ctrl.name, expected_attribute=Clusters.OnOff.Attributes.OnOff, output=output_queue)
        subscription.SetAttributeUpdateCallback(attribute_handler)

        resub_catcher = ResubscriptionCatcher(name=dev_ctrl.name)
        subscription.SetResubscriptionAttemptedCallback(resub_catcher)

        while True:
            pass

if __name__ == "__main__":
    default_matter_test_main()
