/* MicroFlo - Flow-Based Programming for microcontrollers
 * Copyright (c) 2013 Jon Nordby <jononor@gmail.com>
 * MicroFlo may be freely distributed under the MIT license
 */

#include <node.h>
#include <v8.h>

#define HOST_BUILD
#define MICROFLO_NO_MAIN
#include "microflo/microflo.hpp"
#include "microflo/host.hpp"

// Packet
v8::Handle<v8::Value> PacketToJsObject(const Packet &p) {
    v8::HandleScope scope;
    v8::Persistent<v8::Object> obj = v8::Persistent<v8::Object>::New(v8::Object::New());
    obj->Set(v8::String::NewSymbol("type"), v8::Number::New(p.type()));
    v8::Handle<v8::Value> val = v8::Undefined();

    // TODO: extend to cover all types
    if (p.isInteger()) {
        val = v8::Number::New(p.asInteger());
    }
    obj->Set(v8::String::NewSymbol("value"), val);
    return scope.Close(obj);
}

Packet JsValueToPacket(v8::Handle<v8::Value> val) {
    if (val->IsNumber()) {
        return Packet((long)val->Int32Value());
    }
    return Packet();
}

// Component
class JavaScriptComponent : public node::ObjectWrap, public Component  {
public:
    JavaScriptComponent();

    static void Init(v8::Handle<v8::Object> exports);

    // Implements Component
    virtual void process(Packet in, int port);
private:
    ~JavaScriptComponent();

    static v8::Handle<v8::Value> New(const v8::Arguments& args);
    static v8::Handle<v8::Value> On(const v8::Arguments& args);
    static v8::Handle<v8::Value> Send(const v8::Arguments& args);
private:
    v8::Persistent<v8::Function> onProcess;
    Connection outPorts[MICROFLO_MAX_PORTS];
};

JavaScriptComponent::JavaScriptComponent()
    : Component(outPorts, MICROFLO_MAX_PORTS)
{
}

JavaScriptComponent::~JavaScriptComponent()
{}

void JavaScriptComponent::Init(v8::Handle<v8::Object> exports) {
  // Prepare constructor template
  v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(New);
  tpl->SetClassName(v8::String::NewSymbol("Component"));
  tpl->InstanceTemplate()->SetInternalFieldCount(2);
  // Prototype
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("on"),
                                v8::FunctionTemplate::New(On)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("send"),
                                v8::FunctionTemplate::New(Send)->GetFunction());

  v8::Persistent<v8::Function> constructor = v8::Persistent<v8::Function>::New(tpl->GetFunction());
  exports->Set(v8::String::NewSymbol("Component"), constructor);
}

v8::Handle<v8::Value> JavaScriptComponent::New(const v8::Arguments& args) {
  v8::HandleScope scope;
  JavaScriptComponent* obj = new JavaScriptComponent();
  obj->Wrap(args.This());
  return args.This();
}

void JavaScriptComponent::process(Packet in, int port) {
    // call the JavaScript callback
    const int argc = 2;
    v8::Local<v8::Value> argv[argc] = {
        v8::Local<v8::Value>::New(PacketToJsObject(in)),
        v8::Local<v8::Value>::New(v8::Number::New(port)),
    };
    onProcess->Call(v8::Context::GetCurrent()->Global(), argc, argv);
}

v8::Handle<v8::Value> JavaScriptComponent::On(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptComponent* obj = node::ObjectWrap::Unwrap<JavaScriptComponent>(args.This());
  v8::String::Utf8Value event(args[0]);
  if (*event == std::string("process")) {
      v8::Persistent<v8::Function> cb = v8::Persistent<v8::Function>::New(v8::Local<v8::Function>::Cast(args[1]));
      obj->onProcess = cb;
  }
  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> JavaScriptComponent::Send(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptComponent* obj = node::ObjectWrap::Unwrap<JavaScriptComponent>(args.This());
  Packet p = JsValueToPacket(args[1]);
  const int portId = args[1]->Int32Value();
  obj->send(p, portId);

  return scope.Close(v8::Undefined());
}


// HostTransport
class JavaScriptHostTransport : public node::ObjectWrap, public HostTransport  {
public:
    // implements HostTransport
    virtual void setup(IO *i, HostCommunication *c);
    virtual void runTick();
    virtual void sendCommandByte(uint8_t b);
public:
    static void Init(v8::Handle<v8::Object> exports);

private:
    static v8::Handle<v8::Value> New(const v8::Arguments& args);
    static v8::Handle<v8::Value> On(const v8::Arguments& args);
    static v8::Handle<v8::Value> Send(const v8::Arguments& args);
    static v8::Handle<v8::Value> RunTick(const v8::Arguments& args);

private:
    HostCommunication *controller;
    v8::Persistent<v8::Function> receiveFunc;
    v8::Persistent<v8::Function> pullFunc;
};

void JavaScriptHostTransport::setup(IO *i, HostCommunication *c) {
    controller = c;
}

void JavaScriptHostTransport::runTick() {
    const int argc = 0;
    v8::Local<v8::Value> argv[argc] = {

    };
    pullFunc->Call(v8::Context::GetCurrent()->Global(), argc, argv);
}

void JavaScriptHostTransport::sendCommandByte(uint8_t b) {

    const int argc = 1;
    v8::Local<v8::Value> argv[argc] = {
        v8::Local<v8::Value>::New(v8::Number::New(b)),
    };
    receiveFunc->Call(v8::Context::GetCurrent()->Global(), argc, argv);
}

void JavaScriptHostTransport::Init(v8::Handle<v8::Object> exports) {
  // Prepare constructor template
  v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(New);
  tpl->SetClassName(v8::String::NewSymbol("HostTransport"));
  tpl->InstanceTemplate()->SetInternalFieldCount(2);
  // Prototype
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("on"),
                                v8::FunctionTemplate::New(On)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("send"),
                                v8::FunctionTemplate::New(Send)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("runTick"),
                                v8::FunctionTemplate::New(RunTick)->GetFunction());

  v8::Persistent<v8::Function> constructor = v8::Persistent<v8::Function>::New(tpl->GetFunction());
  exports->Set(v8::String::NewSymbol("HostTransport"), constructor);
}

v8::Handle<v8::Value> JavaScriptHostTransport::New(const v8::Arguments& args) {
  v8::HandleScope scope;
  JavaScriptHostTransport* obj = new JavaScriptHostTransport();
  obj->Wrap(args.This());
  return args.This();
}

v8::Handle<v8::Value> JavaScriptHostTransport::On(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptHostTransport* obj = node::ObjectWrap::Unwrap<JavaScriptHostTransport>(args.This());
  v8::String::Utf8Value event(args[0]);
  if (*event == std::string("_receive")) {
      v8::Persistent<v8::Function> cb = v8::Persistent<v8::Function>::New(v8::Local<v8::Function>::Cast(args[1]));
      obj->receiveFunc = cb;
  } else if (*event == std::string("_pull")) {
      v8::Persistent<v8::Function> cb = v8::Persistent<v8::Function>::New(v8::Local<v8::Function>::Cast(args[1]));
      obj->pullFunc = cb;
  }
  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> JavaScriptHostTransport::Send(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptHostTransport* obj = node::ObjectWrap::Unwrap<JavaScriptHostTransport>(args.This());
  const uint8_t b = args[0]->Int32Value();
  if (obj->controller) {
      obj->controller->parseByte(b);
  }

  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> JavaScriptHostTransport::RunTick(const v8::Arguments& args) {
  v8::HandleScope scope;
  JavaScriptHostTransport* obj = node::ObjectWrap::Unwrap<JavaScriptHostTransport>(args.This());
  obj->runTick();
  return scope.Close(v8::Undefined());
}

// Network
class JavaScriptNetwork : public Network, public node::ObjectWrap {
public:
    static void Init(v8::Handle<v8::Object> exports);

private:
    JavaScriptNetwork();
    ~JavaScriptNetwork();

    static v8::Handle<v8::Value> SetTransport(const v8::Arguments& args);

    static v8::Handle<v8::Value> New(const v8::Arguments& args);
    static v8::Handle<v8::Value> AddNode(const v8::Arguments& args);
    static v8::Handle<v8::Value> Connect(const v8::Arguments& args);
    static v8::Handle<v8::Value> ConnectSubgraph(const v8::Arguments& args);
    static v8::Handle<v8::Value> SendMessage(const v8::Arguments& args);
    static v8::Handle<v8::Value> Start(const v8::Arguments& args);
    static v8::Handle<v8::Value> RunTick(const v8::Arguments& args);
private:
    HostCommunication controller;
    JavaScriptHostTransport *transport;
};

JavaScriptNetwork::JavaScriptNetwork()
    : Network(new HostIO)
    , transport(0)
{
}

JavaScriptNetwork::~JavaScriptNetwork()
{}

void JavaScriptNetwork::Init(v8::Handle<v8::Object> exports) {
  // Prepare constructor template
  v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(New);
  tpl->SetClassName(v8::String::NewSymbol("Network"));
  tpl->InstanceTemplate()->SetInternalFieldCount(3);
  // Prototype
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("addNode"),
                                v8::FunctionTemplate::New(AddNode)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("connect"),
                                v8::FunctionTemplate::New(Connect)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("connectSubgraph"),
                                v8::FunctionTemplate::New(ConnectSubgraph)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("sendMessage"),
                                v8::FunctionTemplate::New(SendMessage)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("start"),
                                v8::FunctionTemplate::New(Start)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("runTick"),
                                v8::FunctionTemplate::New(RunTick)->GetFunction());
  tpl->PrototypeTemplate()->Set(v8::String::NewSymbol("setTransport"),
                                v8::FunctionTemplate::New(SetTransport)->GetFunction());

  v8::Persistent<v8::Function> constructor = v8::Persistent<v8::Function>::New(tpl->GetFunction());
  exports->Set(v8::String::NewSymbol("Network"), constructor);
}

v8::Handle<v8::Value> JavaScriptNetwork::New(const v8::Arguments& args) {
  v8::HandleScope scope;
  JavaScriptNetwork* obj = new JavaScriptNetwork();
  obj->Wrap(args.This());
  return args.This();
}

v8::Handle<v8::Value> JavaScriptNetwork::SetTransport(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptNetwork* network = node::ObjectWrap::Unwrap<JavaScriptNetwork>(args.This());
  JavaScriptHostTransport* transport = node::ObjectWrap::Unwrap<JavaScriptHostTransport>(args[0]->ToObject());

  network->transport = transport;
  network->transport->setup(0, &(network->controller));
  network->controller.setup(network, transport);

  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> JavaScriptNetwork::RunTick(const v8::Arguments& args) {
  v8::HandleScope scope;
  JavaScriptNetwork* obj = node::ObjectWrap::Unwrap<JavaScriptNetwork>(args.This());
  obj->runTick();
  return scope.Close(v8::Undefined());
}
v8::Handle<v8::Value> JavaScriptNetwork::Start(const v8::Arguments& args) {
  v8::HandleScope scope;
  JavaScriptNetwork* obj = node::ObjectWrap::Unwrap<JavaScriptNetwork>(args.This());
  obj->start();
  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> JavaScriptNetwork::AddNode(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptNetwork* network = node::ObjectWrap::Unwrap<JavaScriptNetwork>(args.This());
  Component *component = 0;
  if (args[0]->IsObject()) {
      component = node::ObjectWrap::Unwrap<JavaScriptComponent>(args[0]->ToObject());
  } else {
      component = Component::create((ComponentId)args[0]->Int32Value());
  }
  const int parentId = args[1]->Int32Value();
  const int nodeId = network->addNode(component, parentId);

  return scope.Close(v8::Number::New(nodeId));
}

v8::Handle<v8::Value> JavaScriptNetwork::Connect(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptNetwork* obj = node::ObjectWrap::Unwrap<JavaScriptNetwork>(args.This());
  const int srcNode = args[0]->Int32Value();
  const int srcPort = args[1]->Int32Value();
  const int targetNode = args[2]->Int32Value();
  const int targetPort = args[3]->Int32Value();
  obj->connect(srcNode, srcPort, targetNode, targetPort);

  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> JavaScriptNetwork::ConnectSubgraph(const v8::Arguments& args) {
    v8::HandleScope scope;

    JavaScriptNetwork* obj = node::ObjectWrap::Unwrap<JavaScriptNetwork>(args.This());
    const bool isOutput = args[0]->BooleanValue();
    const int subGraphNode = args[1]->Int32Value();
    const int subGraphPort = args[2]->Int32Value();
    const int childNode = args[3]->Int32Value();
    const int childPort = args[4]->Int32Value();
    obj->connectSubgraph(isOutput, subGraphNode, subGraphPort, childNode, childPort);

    return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> JavaScriptNetwork::SendMessage(const v8::Arguments& args) {
  v8::HandleScope scope;

  JavaScriptNetwork* obj = node::ObjectWrap::Unwrap<JavaScriptNetwork>(args.This());
  obj->sendMessage(args[0]->Int32Value(), args[1]->Int32Value(), JsValueToPacket(args[2]));

  return scope.Close(v8::Undefined());
}

void init(v8::Handle<v8::Object> exports) {
  JavaScriptComponent::Init(exports);
  JavaScriptHostTransport::Init(exports);
  JavaScriptNetwork::Init(exports);
}

NODE_MODULE(MicroFloCc, init)
