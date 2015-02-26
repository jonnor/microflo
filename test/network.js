/* MicroFlo - Flow-Based Programming for microcontrollers
 * Copyright (c) 2013 Jon Nordby <jononor@gmail.com>
 * MicroFlo may be freely distributed under the MIT license
 */

var chai = require("chai")
var microflo = require("../lib/microflo");

if (microflo.simulator.RuntimeSimulator) {
    describeIfHasSimulator = describe
} else {
    describeIfHasSimulator = describe.skip
}

var fbp = require("fbp");

describeIfHasSimulator('Network', function(){
  describe.skip('sending packets into graph of Forward components', function(){
    it('should give the same packets out on other side', function(){

        var componentLib = new microflo.componentlib.ComponentLibrary();
        var compare = microflo.simulator.createCompare()

        // Host runtime impl.
        var s = new microflo.simulator.RuntimeSimulator();
        var net = s.network
        var nodes = 7;
        var messages = [];
        for (var i=0; i<10; i++) {
            messages[i] = i;
        }

        var firstNode = -1;
        for (i=0; i<nodes; i++) {
            var node = net.addNode(componentLib.getComponent("Forward").id);
            if (firstNode < 0) {
                firstNode = node;
            }
        }

        for (i=firstNode; i<nodes; i++) {
            net.connect(i, 0, i+1, 0);
        }

        compare.expected = messages;
        net.connect(nodes-1, 0, net.addNode(compare), 0);
        for (i=0; i<messages.length; i++) {
            net.sendMessage(1, 0, messages[i]);
        }

        var deadline = new Date().getTime() + 1*1000; // ms
        net.start();
        while (compare.expectingMore()) {
            net.runTick();
            if (new Date().getTime() > deadline) {
                chai.expect(compare.actual.length).to.equal(compare.expected.length,
                            "Did not get expected packages within deadline");
                break;
            }
        }
        chai.expect(compare.actual.length, 10);
        chai.expect(compare.actual).to.deep.equal(compare.expected);
    })
  })

  describe.skip('sending packets through subgraph', function(){
    it('should give the same packets out on other side', function(){

        var messages = [0,1,2,3];
        var compare = microflo.simulator.createCompare(messages);

        var s = new microflo.simulator.RuntimeSimulator();
        var net = s.network
        var inputNode = net.addNode(s.library.getComponent("Forward").id);
        var subgraphNode = net.addNode(s.library.getComponent("SubGraph").id);
        var innerNode = net.addNode(s.library.getComponent("Forward").id, subgraphNode);
        var outputNode = net.addNode(s.library.getComponent("Forward").id);
        var compareNode = net.addNode(compare);

        net.connect(inputNode, 0, subgraphNode, 0);
        net.connectSubgraph(false, subgraphNode, 0, innerNode, 0); //in
        net.connectSubgraph(true, subgraphNode, 0, innerNode, 0);  //ou
        net.connect(innerNode, 0, outputNode, 0);
        net.connect(outputNode, 0, compareNode, 0);

        for (i=0; i<messages.length; i++) {
            net.sendMessage(inputNode, 0, messages[i]);
        }

        var deadline = new Date().getTime() + 1*1000; // ms
        net.start();
        while (compare.expectingMore()) {
            net.runTick();
            if (new Date().getTime() > deadline) {
                chai.expect(compare.actual.length).to.equal(compare.expected.length,
                            "Did not get expected packages within deadline");
                break;
            }
        }
        chai.expect(compare.actual.length).to.equal(messages.length);
        chai.expect(compare.actual).to.equal(compare.expected);
    })
  })
  describe('Uploading a graph via commandstream', function(){
    it('gives one response per command', function(finish){

        var s = new microflo.simulator.RuntimeSimulator();
        s.library.addComponent("Forward", {}, "Forward.hpp");

        var graph = fbp.parse("a(Forward) OUT -> IN b(Forward) OUT -> IN c(Forward)");
        var cmdstream = microflo.commandstream.cmdStreamFromGraph(s.library, graph);

        var expectedResponses = 9;
        var actualResponses = 0;
        // TODO: API should allow to get callback when everything is completed
        var handleFunc = function() {
            if (arguments[0] != 'IOCHANGE') {
                actualResponses++;
            }
            if (arguments[0] === "NETSTART") {
                chai.expect(actualResponses).to.equal(expectedResponses);
                finish();
            }
        }

        s.start();
        s.device.on('response', handleFunc);
        s.device.open(function() {
            s.uploadGraph(graph, function() {
                s.device.close(function() {
                    ;
                })
            });
        });


    })
  })
})
