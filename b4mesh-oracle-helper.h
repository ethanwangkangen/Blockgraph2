#ifndef B4MESH_Oracle_HELPER_H
#define B4MESH_Oracle_HELPER_H

#include "ns3/node.h"
#include "ns3/object-factory.h"
#include "ns3/node-container.h"
#include "ns3/application-container.h"

#include "b4m_traces.h"
#include "b4mesh-oracle.h"
#include <vector>

using namespace std;
using namespace ns3;

class B4MeshOracleHelper{
  public:
    B4MeshOracleHelper();
    B4MeshOracleHelper(vector<B4MTraces> *t);
    ~B4MeshOracleHelper();

    ApplicationContainer Install(NodeContainer c);

  private:
    ObjectFactory factory;
    vector<B4MTraces>* traces;
};

#endif
