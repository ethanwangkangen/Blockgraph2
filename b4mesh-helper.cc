#include "b4mesh-helper.h"

B4MeshHelper::B4MeshHelper(){
  factory.SetTypeId("B4Mesh");
}

B4MeshHelper::B4MeshHelper(B4MTraces* t){
  factory.SetTypeId("B4Mesh");
  traces = t;
}

B4MeshHelper::~B4MeshHelper(){
}

ApplicationContainer B4MeshHelper::Install(NodeContainer c, float timeBetweenTxn){
  ApplicationContainer apps;

  // Get list of ip addresses
  vector<Ipv4Address> peers;
  for (uint32_t i=0; i<c.GetN(); ++i){
    Ptr<Ipv4> ipv4 = c.Get(i)->GetObject<Ipv4>();
    peers.push_back(ipv4->GetAddress(1, 0).GetLocal());
  }

  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i){
    cout << "Install b4mesh on node : " << (*i)->GetId() << endl;
    Ptr<B4Mesh> b4meshApp = factory.Create<B4Mesh>();
    b4meshApp->SetUp(*i, peers, timeBetweenTxn); // Pass in current node, list of all peer IP, and timeBetweenTxn value
    b4meshApp->traces = traces;
    (*i)->AddApplication(b4meshApp);
    apps.Add(b4meshApp);
  }
  return apps;
}
