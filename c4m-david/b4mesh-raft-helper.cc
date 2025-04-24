#include "b4mesh-raft-helper.h"

B4MeshRaftHelper::B4MeshRaftHelper(){
  factory.SetTypeId("B4MeshRaft");
}

B4MeshRaftHelper::B4MeshRaftHelper(vector<B4MTraces>* t){
  factory.SetTypeId("B4MeshRaft");
  traces = t;
}


B4MeshRaftHelper::~B4MeshRaftHelper(){
}

ApplicationContainer B4MeshRaftHelper::Install(NodeContainer c, int hbeat, int eTimeout){
  ApplicationContainer apps;

  // Get list of ip addresses
  vector<Ipv4Address> peers;
  for (uint32_t i=0; i<c.GetN(); ++i){
    Ptr<Ipv4> ipv4 = c.Get(i)->GetObject<Ipv4>();
    peers.push_back(ipv4->GetAddress(1, 0).GetLocal());
  }
  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i){
    cout << "Install B4MeshRaft on node : " << (*i)->GetId() << endl;
    Ptr<B4MeshRaft> app = factory.Create<B4MeshRaft>();
    app->traces = &(traces->at((*i)->GetId()));
    app->SetUp(*i, peers, hbeat, eTimeout);
    (*i)->AddApplication(app);
    apps.Add(app);
  }
  return apps;
}
