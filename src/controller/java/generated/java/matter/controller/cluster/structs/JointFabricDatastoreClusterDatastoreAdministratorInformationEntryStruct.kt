/*
 *
 *    Copyright (c) 2023 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
package matter.controller.cluster.structs

import matter.controller.cluster.*
import matter.tlv.ContextSpecificTag
import matter.tlv.Tag
import matter.tlv.TlvReader
import matter.tlv.TlvWriter

class JointFabricDatastoreClusterDatastoreAdministratorInformationEntryStruct(
  val nodeID: ULong,
  val friendlyName: String,
  val vendorID: UShort,
  val icac: ByteArray,
) {
  override fun toString(): String = buildString {
    append("JointFabricDatastoreClusterDatastoreAdministratorInformationEntryStruct {\n")
    append("\tnodeID : $nodeID\n")
    append("\tfriendlyName : $friendlyName\n")
    append("\tvendorID : $vendorID\n")
    append("\ticac : $icac\n")
    append("}\n")
  }

  fun toTlv(tlvTag: Tag, tlvWriter: TlvWriter) {
    tlvWriter.apply {
      startStructure(tlvTag)
      put(ContextSpecificTag(TAG_NODE_ID), nodeID)
      put(ContextSpecificTag(TAG_FRIENDLY_NAME), friendlyName)
      put(ContextSpecificTag(TAG_VENDOR_ID), vendorID)
      put(ContextSpecificTag(TAG_ICAC), icac)
      endStructure()
    }
  }

  companion object {
    private const val TAG_NODE_ID = 1
    private const val TAG_FRIENDLY_NAME = 2
    private const val TAG_VENDOR_ID = 3
    private const val TAG_ICAC = 4

    fun fromTlv(
      tlvTag: Tag,
      tlvReader: TlvReader,
    ): JointFabricDatastoreClusterDatastoreAdministratorInformationEntryStruct {
      tlvReader.enterStructure(tlvTag)
      val nodeID = tlvReader.getULong(ContextSpecificTag(TAG_NODE_ID))
      val friendlyName = tlvReader.getString(ContextSpecificTag(TAG_FRIENDLY_NAME))
      val vendorID = tlvReader.getUShort(ContextSpecificTag(TAG_VENDOR_ID))
      val icac = tlvReader.getByteArray(ContextSpecificTag(TAG_ICAC))

      tlvReader.exitContainer()

      return JointFabricDatastoreClusterDatastoreAdministratorInformationEntryStruct(
        nodeID,
        friendlyName,
        vendorID,
        icac,
      )
    }
  }
}
