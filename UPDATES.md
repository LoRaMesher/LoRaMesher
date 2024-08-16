# Updates

## 1.0.0

### New Routing Table Protocol (BMX6)

Inspired by the following article: [BMX6](https://ieeexplore.ieee.org/document/7300903)

#### New parameters added to LoRaMesher.h file.

- LM_QUALITY_WINDOWS_SIZE: This parameter will be used to calculate the link quality. The link quality will be calculated as the average of the last x received hello packets. The default value is 100. This value can be changed in the LoRaMesher.h file.

- LM_MAX_HOPS: This parameter will be used to calculate the maximum number of hops that the packet can do. The default value is 10. This value can be changed in the LoRaMesher.h file.

- LM_REDUCED_FACTOR_HOP_COUNT: This parameter will be used to reduce the metric value with each iteration. The default value is 0.97. This value can be changed in the LoRaMesher.h file.

- LM_MAX_METRIC is the maximum metric value that the packet can have. The default value is 255. Which will be the maximum size of an uint8_t. This value CANNOT be changed since the metric is an uint8_t.

#### Implementation of the new routing table protocol.

- Now the hello packets will be send periodically to the neighbors. This packet will be as a keep alive message and to transmit to their neighbors their quality link. Additionally, the hello packet will send ri (received link quality), for the neighbors to update their ti (transmit link quality).

- The routing table will be send only when a new node is added to the routing table. And if received a routing table from a neighbor, the routing table will be updated only if the received routing table is newer than the current routing table. A new field has been added to the routing table which is the sequence number. This number will be incremented every time the routing table is updated. This way the nodes will know if the routing table is newer or not.

- The routing table will be send to the neighbors only if the routing table has been updated.

- Received link quality (ri) will be stored in the routing table. The link quality will be calculated as the average of the last x received hello packets. This last x values will be named as the LM_QUALITY_WINDOWS_SIZE. The default value is 100. This value can be changed in the LoRaMesher.h file.
- If timeout is reached, ri will be [0, 255] multiple ri= max(0, ri-(255/LM_QUALITY_WINDOWS_SIZE)) will be done.
- If received a hello packet from a neighbor ri = min(255, ri+(255/LM_QUALITY_WINDOWS_SIZE))
- Routing table will contain a field that will be not send that will say if a packet has been received from the neighbor in the last x seconds. When the routine is executed, this field will be updated to false. If a hello packet is received from the neighbor, this field will be updated to true. If the field is false, the node ri will be updated with the ri= max(0, ri-(255/LM_QUALITY_WINDOWS_SIZE)).

- When receiving a hello packet, the snr and rssi will be stored in the routing table. ONLY INFO. This info will not be used at the moment.

- The routing table entries will be updated with a Vector Metric (VM) which will be calculated by the link quality, the number of hops and the snr and rssi values. This VM will be used to calculate the best path to the destination node.
- mn,l is the metric of the link between the node n and the node l.
- With ql = ri+ti/2

- F(Hope Count) will be calculated as follows:
    - F(HC) = LM_REDUCED_FACTOR_HOP_COUNT * Hop Count * LM_MAX_METRIC
        "doing nothing else than reducing the updated metric value by the minimal possible constant factor of 3% with each iteration, thus every additional hop."


The calculated metric will be calculated as follows:
    - M = min(F(HC), LM_MAX_METRIC / sqrt((LM_MAX_METRIC / mn,l)^2 + (LM_MAX_METRIC / ql)^2))

- A new timeout with lower value has been added to the routing nodes. 
- When I receive a routing table message from a neighbor, I will get the quality link from that neighbor, and I will calculate for each entry that has send me the new M (metric), if this new metric is better than the current metric, I will update the routing table with the new metric and the new next hop. If the new metric is the same or worse than the current metric, I will not update the node entry.

- The routing table will have the following parameters, (s) means that the parameter will be send to the neighbors, (n) means that the parameter will be not send to the neighbors.
- The routing table will have the following parameters:
    - Destination (s)
    - Metric (s)
    - Role (s)
    - Hop Count (s)
    - Next Hop / Via (n)
    - Received Link Quality (n) [Only for the neighbors]
    - Transmitted Link Quality (n) [Only for the neighbors]
    - Timeout (n)

- Whenever a message from a node is received, the routing table timeout will be updated. If the timeout is reached, the node will be removed from the routing table.