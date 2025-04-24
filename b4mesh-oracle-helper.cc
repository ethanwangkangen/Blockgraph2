#include "b4mesh-oracle-helper.h"

B4MeshOracleHelper::B4MeshOracleHelper(){
  factory.SetTypeId("B4MeshOracle");
}

B4MeshOracleHelper::B4MeshOracleHelper(vector<B4MTraces>* t){
  factory.SetTypeId("B4MeshOracle");
  traces = t;
}


B4MeshOracleHelper::~B4MeshOracleHelper(){
}

ApplicationContainer B4MeshOracleHelper::Install(NodeContainer c){
  ApplicationContainer apps;

  // Get list of ip addresses
  vector<Ipv4Address> peers;
  for (uint32_t i=0; i<c.GetN(); ++i){
    Ptr<Ipv4> ipv4 = c.Get(i)->GetObject<Ipv4>();
    peers.push_back(ipv4->GetAddress(1, 0).GetLocal());
  }
  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i){
    cout << "Install B4MeshOracle on node : " << (*i)->GetId() << endl;
    Ptr<B4MeshOracle> app = factory.Create<B4MeshOracle>();
    app->traces = &(traces->at((*i)->GetId()));
    app->SetUp(*i, peers);
    (*i)->AddApplication(app);
    apps.Add(app);
  }
  return apps;
}
