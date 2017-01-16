/* MicroFlo - Flow-Based Programming for microcontrollers
 * Copyright (c) 2013 Jon Nordby <jononor@gmail.com>
 * MicroFlo may be freely distributed under the MIT license
 */

#include "microflo.h"


#define MICROFLO_VALID_NODEID(id) \
   (id >= Network::firstNodeId && id <= lastAddedNodeIndex)

#ifdef HOST_BUILD
#include <cstring>
#else
#include "string.h"
#endif


static const char MICROFLO_GRAPH_MAGIC[] = { 'u','C','/','F','l','o', '0', '1' };
ComponentLibrary *ComponentLibrary::instance = 0;

bool Packet::asBool() const {
    if (msg == MsgVoid) {
        return true;
    } else {
        return data.boolean;
    }
}
long Packet::asInteger() const {
    if (msg == MsgVoid) {
        return 0;
    } else {
        return data.lng;
    }
}
float Packet::asFloat() const {
    if (msg == MsgVoid) {
        return 0.0;
    } else {
        return data.flt;
    }
}

unsigned char Packet::asByte() const {
    if (msg == MsgVoid) {
        return 0;
    } else {
        return data.byte;
    }
}

bool Packet::operator==(const Packet& rhs) const {
    return msg == rhs.msg && memcmp(&data, &rhs.data, sizeof(PacketData)) == 0;
}

HostCommunication::HostCommunication()
    : network(0)
    , transport(0)
    , currentByte(0)
    , state(LookForHeader)
    , debugLevel(DebugLevelError)
{}

void HostCommunication::setup(Network *net, HostTransport *t) {
    network = net;
    transport = t;

    MICROFLO_DEBUG(this, DebugLevelInfo, DebugProgramStart);
    network->setNotificationHandler(this);
}


void HostCommunication::parseByte(char b) {

    buffer[currentByte++] = b;

    if (state == ParseHeader) {
        MICROFLO_DEBUG(this, DebugLevelVeryDetailed, DebugParseHeader);
        if (currentByte == sizeof(MICROFLO_GRAPH_MAGIC)) {

            if (memcmp(buffer, MICROFLO_GRAPH_MAGIC, sizeof(MICROFLO_GRAPH_MAGIC)) == 0) {
                MICROFLO_DEBUG(this, DebugLevelDetailed, DebugMagicMatched);
                const uint8_t cmd[] = { GraphCmdCommunicationOpen };
                transport->sendCommand(cmd, sizeof(cmd));
                state = ParseCmd;
            } else {
                MICROFLO_DEBUG(this, DebugLevelError, DebugMagicMismatch);
                state = Invalid;
            }
            currentByte = 0;
        }
    } else if (state == ParseCmd) {
        MICROFLO_DEBUG(this, DebugLevelVeryDetailed, DebugParseCommand);
        if (currentByte == MICROFLO_CMD_SIZE) {
            if (memcmp(buffer, MICROFLO_GRAPH_MAGIC, sizeof(MICROFLO_GRAPH_MAGIC)) == 0) {
                MICROFLO_DEBUG(this, DebugLevelDetailed, DebugMagicMatched);
                const uint8_t cmd[] = { GraphCmdCommunicationOpen };
                transport->sendCommand(cmd, sizeof(cmd));
                // already in ParseCmd state
            } else {
                parseCmd();
            }
            currentByte = 0;
        }
    } else if (state == LookForHeader) {
        MICROFLO_DEBUG(this, DebugLevelVeryDetailed, DebugParseLookForHeader);
        if (b == MICROFLO_GRAPH_MAGIC[0]) {
            state = ParseHeader;
            buffer[0] = b;
            currentByte = 1;
        } else {
            currentByte = 0;
        }

    } else if (state == Invalid) {
        MICROFLO_DEBUG(this, DebugLevelError, DebugParserInvalidState);
        // try to recover
        currentByte = 0;
        state = LookForHeader;
    } else {
        MICROFLO_DEBUG(this, DebugLevelError,DebugParserUnknownState);
        // try to recover
        currentByte = 0;
        state = LookForHeader;
    }
}

void HostCommunication::parseCmd() {

    GraphCmd cmd = (GraphCmd)buffer[0];
    if (cmd == GraphCmdEnd) {
        MICROFLO_DEBUG(this, DebugLevelDetailed, DebugEndOfTransmission);
        const uint8_t cmd[] = { GraphCmdTransmissionEnded };
        transport->sendCommand(cmd, sizeof(cmd));
        state = LookForHeader;
    } else if (cmd == GraphCmdReset) {
        network->reset();
    } else if (cmd == GraphCmdStopNetwork) {
        network->stop();
    } else if (cmd == GraphCmdStartNetwork) {
        network->start();

    } else if (cmd == GraphCmdCreateComponent) {
        MICROFLO_DEBUG(this, DebugLevelDetailed, DebugComponentCreateStart);
        Component *c = ComponentLibrary::get()->create((MicroFlo::ComponentId)buffer[1]);
        MICROFLO_DEBUG(this, DebugLevelDetailed, DebugComponentCreateEnd);
        network->addNode(c, buffer[2]);
    } else if (cmd == GraphCmdRemoveNode) {
        //MICROFLO_DEBUG(this, DebugLevelDetailed, DebugComponentCreateStart);
        network->removeNode(buffer[1]);
    } else if (cmd == GraphCmdConnectNodes) {
        MICROFLO_DEBUG(this, DebugLevelDetailed, DebugConnectNodesStart);
        network->connect(buffer[1], buffer[3], buffer[2], buffer[4]);
    } else if (cmd == GraphCmdDisconnectNodes) {
        //MICROFLO_DEBUG(this, DebugLevelDetailed, DebugDisconnectNodesStart);
        network->disconnect(buffer[1], buffer[3], buffer[2], buffer[4]);
    } else if (cmd == GraphCmdSendPacket) {
        const Msg packetType = (Msg)buffer[3];
        Packet p;
        if (packetType == MsgBracketStart || packetType == MsgBracketEnd
                || packetType == MsgVoid) {
            p = Packet(packetType);
        } else if (packetType == MsgInteger) {
            // TODO: move into readInt32 function, take endianness into account
            const long val = buffer[4] + ((long)(buffer[5])<<8) + ((long)(buffer[6])<<16) + ((long)(buffer[7])<<24);
            p = Packet(val);
        } else if (packetType == MsgByte) {
            p = Packet(buffer[4]);
        } else if (packetType == MsgBoolean) {
            p = Packet(!(buffer[4] == 0));
        }

        if (p.isValid()) {
            network->sendMessageTo(buffer[1], buffer[2], p);
            const uint8_t cmd[] = { GraphCmdSendPacketDone, buffer[1], buffer[2], (uint8_t)packetType };
            transport->sendCommand(cmd, sizeof(cmd));
        } else {
            MICROFLO_DEBUG(this, DebugLevelError, DebugParserUnknownPacketType);
        }
    } else if (cmd == GraphCmdConfigureDebug) {
        network->setDebugLevel((DebugLevel)buffer[1]);
    } else if (cmd == GraphCmdSubscribeToPort) {
        network->subscribeToPort(buffer[1], buffer[2], (bool)buffer[3]);
    } else if (cmd == GraphCmdConnectSubgraphPort) {
#ifdef MICROFLO_ENABLE_SUBGRAPHS
        // FIXME: validate
        const bool isOutput = (unsigned int)buffer[1];
        const int subgraphNode = (unsigned int)buffer[2];
        const int subgraphPort = (unsigned int)buffer[3];
        const int childNode = (unsigned int)buffer[4];
        const int childPort = (unsigned int)buffer[5];
        network->connectSubgraph(isOutput, subgraphNode, subgraphPort, childNode, childPort);
#else
        MICROFLO_DEBUG(this, DebugLevelError, DebugNotSupported);
#endif
    } else if (cmd == GraphCmdPing) {
        const uint8_t cmd[] = { GraphCmdPong, cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7] };
        transport->sendCommand(cmd, sizeof(cmd));
    } else if (cmd == GraphCmdSetIoValue) {
        network->setIoValue(buffer, MICROFLO_CMD_SIZE);
    } else if (cmd >= GraphCmdInvalid) {
        MICROFLO_ASSERT(memcmp(buffer, MICROFLO_GRAPH_MAGIC, sizeof(MICROFLO_GRAPH_MAGIC)) == 0,
                        this, DebugLevelError, DebugParserInvalidCommand);
    } else {
        MICROFLO_DEBUG(this, DebugLevelError, DebugParserUnknownCommand);
    }
}

void Component::setComponentId(MicroFlo::ComponentId id) {
    componentId = id;
}

void Component::send(Packet out, MicroFlo::PortId port) {
    MICROFLO_RETURN_IF_FAIL(port < nPorts,
                            network->notificationHandler, DebugLevelError, DebugComponentSendInvalidPort);

    network->sendMessageFrom(this, port, out);
}

void Component::connect(MicroFlo::PortId outPort, Component *target, MicroFlo::PortId targetPort) {
    connections[outPort].target = target;
    connections[outPort].targetPort = targetPort;
}

void Component::disconnect(MicroFlo::PortId outPort, Component *target, MicroFlo::PortId targetPort) {
    connections[outPort].target = NULL;
    connections[outPort].targetPort = -2;
    connections[outPort].subscribed = false;
}

void Component::setNetwork(Network *net, int n, IO *i) {
    parentNodeId = 0; // no parent
    network = net;
    nodeId = n;
    io = i;
    for(int i=0; i<nPorts; i++) {
        connections[i].target = 0;
        connections[i].targetPort = -1;
        connections[i].subscribed = false;
    }
}

Network::Network(IO *io, MessageQueue *m)
    : lastAddedNodeIndex(Network::firstNodeId)
    , messageQueue(m)
    , notificationHandler(0)
    , io(io)
    , state(Reset)
{
    for (int i=0; i<MICROFLO_MAX_NODES; i++) {
        nodes[i] = 0;
    }
}

void Network::setNotificationHandler(NetworkNotificationHandler *handler) {
    notificationHandler = handler;
    io->debug = handler;
}

void Network::processMessages() {
    Message msg;
    messageQueue->newTick();

    while (messageQueue->pop(msg)) {
        Component *sender = 0;
        MicroFlo::PortId senderPort = resolveMessageTarget(msg, &sender);

        // send notification first, so we can listen also to ports for which there is no connections. For testing/MQTT etc
        const bool sendNotification = sender ? sender->connections[senderPort].subscribed : false;
        if (sendNotification && notificationHandler) {
            notificationHandler->packetSent(msg, sender, senderPort);
        }

        if (!msg.targetReferred) {
            continue; // could not resolve target, no-one connected on this port
        }
        Component *target = nodes[msg.node];
        if (!target) {
            continue; // FIXME: this should not happen
        }

        target->process(msg.pkg, msg.port);
    }
}

void Network::resolveMessageSubgraph(Message &msg, const Component *sender)
{
#ifdef MICROFLO_ENABLE_SUBGRAPHS
    // Note: Assumes msg is target referred
    Component *target = nodes[msg.node];
    MicroFlo::PortId targetPort = msg.port;
    const bool senderIsChild = sender && sender->parentNodeId >= Network::firstNodeId;
    if (senderIsChild) {
        SubGraph *parent = (SubGraph *)nodes[sender->parentNodeId];
        if (target == parent) {
            // Redirect output message from child outport, emit message on the parent outport
            // FIXME: should we change @sender / @senderPort, for debugging?
            msg.node = parent->outputConnections[targetPort].target->id();
            msg.port = parent->outputConnections[targetPort].targetPort;
        }
    }

    const bool targetIsSubGraph = target->componentId == MicroFlo::IdSubGraph;
    if (targetIsSubGraph) {
        SubGraph *targetSubGraph = (SubGraph *)target;
        // Redirect input message from, send to desired port on child
        // FIXME: should we change @sender / @senderPort, for debugging?
        msg.node = targetSubGraph->inputConnections[targetPort].target->id();
        msg.port = targetSubGraph->inputConnections[targetPort].targetPort;
    }
#endif
}

MicroFlo::PortId
Network::resolveMessageTarget(Message &msg, Component **out_sender)
{
    MicroFlo::PortId senderPort = -1;
    if (!msg.targetReferred) {
        *out_sender = nodes[msg.node];
        senderPort = msg.port;
        Connection &conn = (*out_sender)->connections[msg.port];
        if (conn.target) {
            msg.node = conn.target->id();
            msg.port = conn.targetPort;
            msg.targetReferred = true;
        }
    }
    if (msg.targetReferred) {
        resolveMessageSubgraph(msg, *out_sender);
    }
    return senderPort;
}

/* Note: must be interrupt-safe */
void Network::sendMessageFrom(Component *sender, MicroFlo::PortId senderPort, const Packet &pkg) {
    MICROFLO_RETURN_IF_FAIL(sender, notificationHandler, DebugLevelError, DebugSendMessageInvalidNode);

    Message msg;
    msg.pkg = pkg;
    msg.targetReferred = false;
    msg.node = sender->id();
    msg.port = senderPort;
    messageQueue->push(msg);
}

void Network::sendMessageTo(MicroFlo::NodeId targetId, MicroFlo::PortId targetPort, const Packet &pkg) {
    MICROFLO_RETURN_IF_FAIL(MICROFLO_VALID_NODEID(targetId),
                            notificationHandler, DebugLevelError, DebugSendMessageInvalidNode);

    Message msg;
    msg.pkg = pkg;
    msg.targetReferred = true;
    msg.node = targetId;
    msg.port = targetPort;
    messageQueue->push(msg);
}

void Network::distributePacket(const Packet &packet, MicroFlo::PortId port) {

    for (MicroFlo::NodeId i=0; i<MICROFLO_MAX_NODES; i++) {
        if (nodes[i]) {
            nodes[i]->process(packet, port);
        }
    }
}

void Network::runSetup() {
    if (state != Running) {
        return;
    }
    distributePacket(Packet(MsgSetup), -1);
}

void Network::runTick() {
    if (state != Running) {
        return;
    }

    // TODO: consider the balance between scheduling and messaging (bounded-buffer problem)

    // Deliver messages
    processMessages();

    // Schedule
    distributePacket(Packet(MsgTick), -1);
}

void Network::connect(MicroFlo::NodeId srcId, MicroFlo::PortId srcPort,
                      MicroFlo::NodeId targetId,MicroFlo::PortId targetPort) {
    MICROFLO_RETURN_IF_FAIL(MICROFLO_VALID_NODEID(srcId) && MICROFLO_VALID_NODEID(targetId),
                            notificationHandler, DebugLevelError, DebugNetworkConnectInvalidNodes);

    connect(nodes[srcId], srcPort, nodes[targetId], targetPort);
}

void Network::connect(Component *src, MicroFlo::PortId srcPort,
                      Component *target, MicroFlo::PortId targetPort) {
    src->connect(srcPort, target, targetPort);
    if (notificationHandler) {
        notificationHandler->nodesConnected(src, srcPort, target, targetPort);
    }
}

void Network::disconnect(MicroFlo::NodeId srcId, MicroFlo::PortId srcPort,
                      MicroFlo::NodeId targetId,MicroFlo::PortId targetPort) {
    MICROFLO_RETURN_IF_FAIL(MICROFLO_VALID_NODEID(srcId) && MICROFLO_VALID_NODEID(targetId),
                            notificationHandler, DebugLevelError, DebugNetworkConnectInvalidNodes);

    disconnect(nodes[srcId], srcPort, nodes[targetId], targetPort);
}

void Network::disconnect(Component *src, MicroFlo::PortId srcPort,
                      Component *target, MicroFlo::PortId targetPort) {
    src->disconnect(srcPort, target, targetPort);
    if (notificationHandler) {
        notificationHandler->nodesDisconnected(src, srcPort, target, targetPort);
    }
}

MicroFlo::NodeId Network::addNode(Component *node, MicroFlo::NodeId parentId) {
    MICROFLO_RETURN_VAL_IF_FAIL(node, 0,
                                notificationHandler, DebugLevelError, DebugAddNodeInvalidInstance);

    MICROFLO_RETURN_VAL_IF_FAIL(parentId <= lastAddedNodeIndex, 0,
                                notificationHandler, DebugLevelError, DebugAddNodeInvalidParent);

    const int nodeId = lastAddedNodeIndex;
    nodes[nodeId] = node;
    node->setNetwork(this, nodeId, this->io);
    if (parentId > 0) {
        node->setParent(parentId);
    }
    if (notificationHandler) {
        notificationHandler->nodeAdded(node, parentId);
    }
    lastAddedNodeIndex++;
    return nodeId;
}

MicroFlo::NodeId Network::removeNode(MicroFlo::NodeId nodeId) {
    MICROFLO_RETURN_VAL_IF_FAIL(nodeId <= lastAddedNodeIndex, 0,
                                notificationHandler, DebugLevelError, DebugRemoveNodeInvalidInstance);
    Component *node = nodes[nodeId];

    if (notificationHandler) {
        notificationHandler->nodeRemoved(node, node->parentNodeId);
    }
    delete node; // after notification, so it can refer it

    return nodeId;
}

void Network::reset() {
    state = Reset;
    for (int i=0; i<MICROFLO_MAX_NODES; i++) {
        if (nodes[i]) {
            delete nodes[i];
            nodes[i] = 0;
        }
    }
    lastAddedNodeIndex = Network::firstNodeId;
    messageQueue->clear();
    if (notificationHandler) {
        notificationHandler->networkStateChanged(state);
    }
}

void Network::start() {
    state = Running;
    if (notificationHandler) {
        notificationHandler->networkStateChanged(state);
    }

    runSetup();
}

void Network::stop() {
    state = Stopped;
    if (notificationHandler) {
        notificationHandler->networkStateChanged(state);
    }
}

void Network::setDebugLevel(DebugLevel level) {
    if (notificationHandler) {
        notificationHandler->debugChanged(level);
    }
}

void Network::setIoValue(const uint8_t *buf, uint8_t len) {
    io->setIoValue(buf, len);
}

void Network::subscribeToPort(MicroFlo::NodeId nodeId, MicroFlo::PortId portId, bool enable) {
    MICROFLO_RETURN_IF_FAIL(MICROFLO_VALID_NODEID(nodeId),
                            notificationHandler, DebugLevelError, DebugSubscribePortInvalidNode);
    Component *c = nodes[nodeId];
    MICROFLO_RETURN_IF_FAIL(portId >= 0 && portId < c->nPorts,
                            notificationHandler, DebugLevelError, DebugSubscribePortInvalidPort);

    c->connections[portId].subscribed = enable;
    if (notificationHandler) {
        notificationHandler->portSubscriptionChanged(nodeId, portId, enable);
    }
}

void Network::connectSubgraph(bool isOutput,
                              MicroFlo::NodeId subgraphNode, MicroFlo::PortId subgraphPort,
                              MicroFlo::NodeId childNode, MicroFlo::PortId childPort) {
#ifdef MICROFLO_ENABLE_SUBGRAPHS

    MICROFLO_RETURN_IF_FAIL(MICROFLO_VALID_NODEID(subgraphNode) && MICROFLO_VALID_NODEID(childNode),
                    notificationHandler, DebugLevelError, DebugSubGraphConnectInvalidNodes);

    Component *comp = nodes[subgraphNode];
    Component *child = nodes[childNode];
    MICROFLO_ASSERT(comp->component() == MicroFlo::IdSubGraph && child->parentNodeId >= Network::firstNodeId,
                    notificationHandler, DebugLevelError, DebugSubGraphConnectNotASubgraph);

    SubGraph *subgraph = (SubGraph *)comp;
    if (isOutput) {
        subgraph->connectOutport(subgraphPort, child, childPort);
    } else {
        subgraph->connectInport(subgraphPort, child, childPort);
    }
    if (notificationHandler) {
        notificationHandler->subgraphConnected(isOutput, subgraphNode, subgraphPort, childNode, childPort);
    }
#else
    MICROFLO_DEBUG(this, DebugLevelError, DebugNotSupported);
#endif
}

void HostCommunication::nodeAdded(Component *c, MicroFlo::NodeId parentId) {
    const uint8_t cmd[] = { GraphCmdNodeAdded, c->component(), c->id(), parentId };
    transport->sendCommand(cmd, sizeof(cmd));
}

void HostCommunication::nodeRemoved(Component *c, MicroFlo::NodeId parentId) {
    const uint8_t cmd[] = { GraphCmdNodeRemoved, c->id() };
    transport->sendCommand(cmd, sizeof(cmd));
}

void HostCommunication::nodesConnected(Component *src, MicroFlo::PortId srcPort,
                                       Component *target, MicroFlo::PortId targetPort) {

    const uint8_t cmd[] = { GraphCmdNodesConnected, src->id(), (uint8_t)srcPort,
                            target->id(), (uint8_t)targetPort };
    transport->sendCommand(cmd, sizeof(cmd));
}
void HostCommunication::nodesDisconnected(Component *src, MicroFlo::PortId srcPort,
                                       Component *target, MicroFlo::PortId targetPort) {

    const uint8_t cmd[] = { GraphCmdNodesDisconnected, src->id(), (uint8_t)srcPort,
                            target->id(), (uint8_t)targetPort };
    transport->sendCommand(cmd, sizeof(cmd));
}

void HostCommunication::networkStateChanged(Network::State s) {
    GraphCmd cmd = GraphCmdInvalid;
    if (s == Network::Running) {
        cmd = GraphCmdNetworkStarted;
    } else if (s == Network::Stopped) {
        cmd = GraphCmdNetworkStopped;
    } else if (s == Network::Reset) {
        cmd = GraphCmdNetworkReset;
    }
    transport->sendCommand((uint8_t *)&cmd, 1);
}

void HostCommunication::packetSent(const Message &m, const Component *src, MicroFlo::PortId srcPort) {
    if (!src) {
        return;
    }

    uint8_t cmd[MICROFLO_CMD_SIZE] = { GraphCmdPacketSent, src->id(), (uint8_t)srcPort,
                                       m.node, (uint8_t)m.port,
                                       (uint8_t)m.pkg.type(), 0, 0 };

    if (m.pkg.isData()) {
        if (m.pkg.isBool()) {
            cmd[6] = m.pkg.asBool();
        } else if (m.pkg.isNumber()){
            // FIXME: truncates
            // TODO: move into writeInt32 function, take endianness into account
            const int i = m.pkg.asInteger();
            cmd[6] = i>>0;
            cmd[7] = i>>8;
        } else if (m.pkg.isVoid()) {
            // Nothing needs doing
        } else {
            // FIXME: support all types
            MICROFLO_DEBUG(this, DebugLevelError, DebugNotImplemented);
        }
    }
    transport->sendCommand(cmd, sizeof(cmd));
}

void HostCommunication::emitDebug(DebugLevel level, DebugId id) {
#ifdef MICROFLO_ENABLE_DEBUG
    if (level <= debugLevel) {
        const uint8_t cmd[] = { GraphCmdDebugMessage, (uint8_t)level, (uint8_t)id };
        transport->sendCommand(cmd, sizeof(cmd));
    }
#endif
}

void HostCommunication::debugChanged(DebugLevel level) {
    const uint8_t cmd[] = { GraphCmdDebugChanged, (uint8_t)level};
    transport->sendCommand(cmd, sizeof(cmd));
}

void HostCommunication::portSubscriptionChanged(MicroFlo::NodeId nodeId, MicroFlo::PortId portId, bool enable) {
    const uint8_t cmd[] = { GraphCmdPortSubscriptionChanged, nodeId, (uint8_t)portId, enable};
    transport->sendCommand(cmd, sizeof(cmd));
}

void HostCommunication::subgraphConnected(bool isOutput,
                                      MicroFlo::NodeId subgraphNode, MicroFlo::PortId subgraphPort,
                                      MicroFlo::NodeId childNode, MicroFlo::PortId childPort) {
    const uint8_t cmd[] = { GraphCmdSubgraphPortConnected, isOutput,
                            subgraphNode, (uint8_t)subgraphPort, childNode, (uint8_t)childPort };
    transport->sendCommand(cmd, sizeof(cmd));
}

SerialHostTransport::SerialHostTransport(uint8_t port, int baudRate)
    : serialPort(port)
    , serialBaudrate(baudRate)
{

}

void SerialHostTransport::setup(IO *i, HostCommunication *c) {
    io = i;
    controller = c;

    io->SerialBegin(serialPort, serialBaudrate);
}


void SerialHostTransport::runTick() {
    if (io->SerialDataAvailable(serialPort) > 0) {
        unsigned char c = io->SerialRead(serialPort);
        controller->parseByte(c);
    }
}

void SerialHostTransport::sendCommand(const uint8_t *b, uint8_t len) {
    // Make sure to pad to the cmd size
    for (uint8_t i=0; i<MICROFLO_CMD_SIZE; i++) {
        io->SerialWrite(serialPort, (i < len) ?  b[i] : 0x00);
    }
}

#ifdef MICROFLO_ENABLE_SUBGRAPHS
SubGraph::SubGraph()
    : Component(outputConnections, MICROFLO_SUBGRAPH_MAXPORTS)
{
}

void SubGraph::connectInport(MicroFlo::PortId inPort, Component *child, MicroFlo::PortId childInPort) {
    if (inPort < 0 || inPort >= MICROFLO_SUBGRAPH_MAXPORTS) {
        return;
    }
    inputConnections[inPort].target = child;
    inputConnections[inPort].targetPort = childInPort;
}

void SubGraph::connectOutport(MicroFlo::PortId outPort, Component *child, MicroFlo::PortId childOutPort) {
    if (outPort < 0 || outPort >= MICROFLO_SUBGRAPH_MAXPORTS) {
        return;
    }
    outputConnections[outPort].target = child;
    outputConnections[outPort].targetPort = childOutPort;
}

void SubGraph::process(Packet in, MicroFlo::PortId port) {
    MICROFLO_ASSERT(port < 0,
                    network->notificationHandler,DebugLevelError, DebugSubGraphReceivedNormalMessage);
}
#endif

void FixedMessageQueue::newTick()
{
    // Messages may be emitted during delivery, so copy the range we intend to deliver
    previous = current;
}

void FixedMessageQueue::clear()
{
    previous = MessageRange();
    current = MessageRange();
}

bool FixedMessageQueue::push(const Message &msg)
{
    if (current.write >= maxMessages-1) {
        current.write = 0;
    }
    const MessageId msgIndex = current.write++;
    // FIXME: prevent overwriting
    messages[msgIndex] = msg;
    return true;
}

bool FixedMessageQueue::pop(Message &msg)
{
    if (previous.read == previous.write) {
        // no messages left
        current.read = previous.write;
        return false;
    }
    if (previous.read >= maxMessages-1) {
        previous.read = 0;
    }
    const MessageId msgIndex = previous.read++;
    msg = messages[msgIndex];
    return true;
}

// ComponentLibrary
MicroFlo::ComponentId
ComponentLibrary::add(CreateComponentFunction func, const char * const name) {
    // FIXME: ensure space is available
    const MicroFlo::ComponentId id = highest++;
    factories[id] = func;
    names[id] = name;
    return id;
}

Component *
ComponentLibrary::create(MicroFlo::ComponentId id) {
    // FIXME: input validation
    CreateComponentFunction func = factories[id];
    return func();
}



