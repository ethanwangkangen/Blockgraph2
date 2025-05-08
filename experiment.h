#ifndef EXPERIMENT_H
#define EXPERIMENT_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/olsr-helper.h"
#include "ns3/aodv-helper.h"
#include "ns3/simulator.h"

#include "b4mesh-helper.h"
#include "b4mesh-mobility-helper.h"
#include "b4m_traces.h"
#include "b4mesh-oracle-helper.h"

using namespace ns3;
using namespace std;

class Experiment{
  public:
    // Enum for ns3 Signal Propagation Model
    enum{Friis=1, Range, LogDist, Fixed};
    // Enum for ns3 Mobility Model
    enum{cPosition=1, rWalk2, cVelosity, grouprWalk};

    // Constructors and destructor
    Experiment(int nNodes, int sTime, double timeBetweenTxn, 
               int mMobility, int mLoss, int nScen, double speed);
    ~Experiment();

  public:
    /**
     * Initialize the environment of the simulator.
     */
    void Init();

    /**
     * Start the simulation.
     */
    void Run();

    /**
     * Setup the wifi interface of nodes (layer 2).
     */
    void CreateWifi(int lossModel);

    /**
     * Chooses a mobility model and deploys the nodes 
     * over an area in a grid topology.
     */
    void CreateMobility(int mobilityModel);

    /**
     * Setup the Ipv4 address of nodes (layer 3) and routing protocol.
     */
    void CreateAddresses();

    /**
     * Setup applications into the nodes.
     */
    void CreateApplications();

    /**
     * Setup the consensus application into the nodes.
     */
    void CreateConsensus();

    /**
     * Create the consensus application base in the oracle into the nodes.
     */
    void CreateOracleConsensus();

    /**
     * Setup the mobility model for b4mesh as an application that changes nodes
     * cartesian position.
     */
    void CreateMobilityApplication();

  private:
    // Attributes
    NodeContainer nodes;
    NodeContainer leaders;
    NodeContainer followers;

    WifiHelper wifi;
    YansWifiPhyHelper wifiPhy;
    NetDeviceContainer devices;
    Ipv4InterfaceContainer interfaces;
    OlsrHelper olsr;
    AodvHelper aodv;

    ApplicationContainer b4mesh_apps;
    ApplicationContainer mobility_apps;
    ApplicationContainer consensus_oracle_apps;
    
    vector<B4MTraces> consensus_traces;
    vector<B4MTraces> consensus_oracle_traces;

    B4MTraces b4mesh_traces;

    string trace_dir;
    string phy_mode;

    int nNodes;
    int nGroup;
    int sTime;
    float timeBetweenTxn;
    int mMobility;
    int mLoss;
    int nScen;
    double speed;

  public:
    /*
     * Testing and try
    //  */
    // static void CourseChange (std::string foo, Ptr<const MobilityModel> mobility)
    // {
    //   Vector pos = mobility->GetPosition ();
    //   Vector vel = mobility->GetVelocity ();
    //   std::cout << Simulator::Now () << ", model=" << mobility << ", POS: x=" << pos.x << ", y=" << pos.y <<
    //     ", z=" << pos.z << "; VEL:" << vel.x << ", y=" << vel.y << ", z=" << vel.z << std::endl;
    // }
};
#endif
