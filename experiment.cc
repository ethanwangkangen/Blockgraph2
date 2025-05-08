#include "experiment.h"

Experiment::Experiment(int nNodes, int sTime, double timeBetweenTxn, 
                         int mMobility, int mLoss, int nScen, double speed) {

	trace_dir = ".";

  // Data link rate. Set for both non-unicast in Init(), and unicast in CreateWifi()
	phy_mode = "DsssRate11Mbps"; 

  this->nNodes = nNodes;
  this->sTime = sTime;
	this->timeBetweenTxn = timeBetweenTxn;
  this->mMobility = mMobility;
  this->mLoss = mLoss;
	this->nScen = nScen;
  this->speed = speed;

	cout << "RUN = "<< RngSeedManager::GetRun() << " --nNodes = " << nNodes;
	cout << " --sTime = " << sTime << " --timeBetweenTxn = " << timeBetweenTxn;
  cout << " --mMobility = " << mMobility << " --mLoss = " << mLoss;
  cout  <<  " --nScen = " << nScen << " --speed = " << speed << endl;

  // Rules of Simulation
	if (timeBetweenTxn <= 0) {
	  cout << " timeBetweenTxn must be a positive number " << endl;
    exit(1);
	}

  if (nNodes > 50) {
    cout << " nNodes must be at most 50 " << endl;
    exit(1);
  }

  if (sTime > 7200) {
    cout << " sTime must be at most 7200 seconds " << endl;
    exit(1);
  }

  if (mMobility <1 || mMobility > 4) {
    cout << " mMobility must be (1, 2, or 3) " << endl;
    exit(1);
  }

  if (mLoss < 1 || mLoss > 4) {
    cout << " mLoss must be (1, 2, 3 or 4) " << endl;
    exit(1);
  }

  if (nScen < 1 || nScen > 4) {
    cout << " nScen can only be (1, 2, 3 or 4) " << nScen << endl;
    exit(1);
  }

  if (nScen == 1 && nNodes < 3) {
    cout << " nNodes must be at least 3 for this scenario " << endl;
    exit(1);
  }

  if (nScen == 2 && nNodes < 4) {
    cout << " nNodes must be at least 4 for this scenario " << endl;
    exit(1);
  }

  if ( (nScen == 3 || nScen == 4) && nNodes < 9) {
    cout << " nNodes must be at least 9 for this scenario " << endl;
    exit(1);
  }

	Init();
	nodes.Create(nNodes); // Create nodes in the NodeContainer

  CreateWifi(mLoss);
  CreateMobility(mMobility);
	CreateAddresses();
	CreateOracleConsensus();
  CreateApplications();
  CreateMobilityApplication();

}

Experiment::~Experiment(){
}

void Experiment::Init(){

  // Sets the physical layer data rate for non-unicast transmissions like broadcast, multicast
	Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
		StringValue(phy_mode));
}

void Experiment::CreateWifi(int lossModel){
  // Initialise YansWifiPhyHelper
  YansWifiPhyHelper wifiPhy;
  wifiPhy.Set("RxGain", DoubleValue(-10));
  wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

  // Create and configure the channel
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");

  // Configure propagation loss model
  switch (lossModel)
  {
    case Friis:
      wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
      break;

    case Range:
      wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel",
                                     "MaxRange", DoubleValue(100)); // Adjustable range
      break;

    case LogDist:
      wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                     "Exponent", DoubleValue(2.5));
      break;

    case Fixed:
      wifiChannel.AddPropagationLoss("ns3::FixedRssLossModel",
                                     "Rss", DoubleValue(-80));
      break;
  }

  // Attach channel to PHY
  wifiPhy.SetChannel(wifiChannel.Create());

  // Configure Wifi standard and rate
  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211g);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(phy_mode),
                               "ControlMode", StringValue(phy_mode));

  // Set MAC to adhoc mode
  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");

  // Install on nodes
  devices = wifi.Install(wifiPhy, wifiMac, nodes);
}

void Experiment::CreateMobility(int mobilityModel){
  
  MobilityHelper mobility;

  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
    "MinX", DoubleValue(50.0), //was 0.0
    "MinY", DoubleValue(50.0), //was 0.0
    "DeltaX", DoubleValue(5),
    "DeltaY", DoubleValue(5),
    "GridWidth", UintegerValue(5),
    "LayoutType", StringValue("RowFirst"));

  cout << " mobility model is: " << mobilityModel << endl;

  // Mobility Model
  if (mobilityModel == cPosition){
    // Constant Position
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

  } else if (mobilityModel == rWalk2){
    // Random Walk 2D
    string s_speed = to_string(speed);

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
        "Mode", StringValue ("Time"),
        "Time", StringValue("20s"),   // change course every 20 seconds 
        "Direction", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=5.0]"), // Radians
        "Speed", StringValue("ns3::ConstantRandomVariable[Constant="+s_speed+"]"),    // m/s
        "Bounds", RectangleValue (Rectangle(0.0, 500, 0.0, 500)));

    mobility.Install(nodes);

  } else if (mobilityModel == cVelosity){
    // Constant Velocity
    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(nodes);

  } else if (mobilityModel == grouprWalk){

    MobilityHelper mobility2;

    mobility2.SetPositionAllocator("ns3::GridPositionAllocator",
    "MinX", DoubleValue(0.0),
    "MinY", DoubleValue(0.0),
    "DeltaX", DoubleValue(5),
    "DeltaY", DoubleValue(5),
    "GridWidth", UintegerValue(5),
    "LayoutType", StringValue("RowFirst"));

    if (nNodes == 10){ 
      nGroup = 2; 
    } else if (nNodes == 15 ){    
      nGroup = 3;
    } else if (nNodes == 30){
      nGroup = 5;
    } else if (nNodes == 50){
      nGroup = 5;
    }

    cout << "nGroup = " << nGroup << endl;

    for(int i = 0; i < nNodes; i++){
      if (i % (nNodes/nGroup) == 0){
        leaders.Add(nodes.Get(i));  
        cout << "Installing leaders in container : "<< i << endl;
      } else {
        followers.Add(nodes.Get(i));
        cout << " Installing followers in container : " << i << endl;
      }
    }

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    string s_speed = to_string(speed);

    mobility2.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
        "Mode", StringValue ("Time"),
        "Time", StringValue("20s"),   // change course every 20 seconds 
        "Direction", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=5.0]"), // Radians
        "Speed", StringValue("ns3::ConstantRandomVariable[Constant="+s_speed+"]"),    // m/s
        "Bounds", RectangleValue (Rectangle(0.0, 500, 0.0, 500)));

    mobility.Install(followers);
    mobility2.Install(leaders);

  }
}

void Experiment::CreateAddresses(){

  Ipv4ListRoutingHelper list;
  list.Add(olsr, 10);

  InternetStackHelper internet;
  internet.SetRoutingHelper(list);
  internet.Install(nodes);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.0.0", "255.255.0.0");
  interfaces = ipv4.Assign(devices);

}

void Experiment::CreateOracleConsensus(){

	consensus_oracle_traces = vector<B4MTraces>(nodes.GetN());
	B4MeshOracleHelper consensus_oracle_Helper(&consensus_oracle_traces);

	consensus_oracle_apps = consensus_oracle_Helper.Install(nodes);
	consensus_oracle_apps.Start(Seconds(3));
	consensus_oracle_apps.Stop(Seconds(sTime));

}

void Experiment::CreateApplications(){

  B4MeshHelper b4meshHelper(&b4mesh_traces);
  b4mesh_apps = b4meshHelper.Install(nodes, timeBetweenTxn);
  b4mesh_apps.Start(Seconds(6));
  b4mesh_apps.Stop(Seconds(sTime));

}

void Experiment::CreateMobilityApplication(){

    B4MeshMobilityHelper b4meshMobility;
    mobility_apps = b4meshMobility.Install(nodes, sTime, nScen, mMobility, speed);
    mobility_apps.Start(Seconds(5));
    mobility_apps.Stop(Seconds(sTime));
    
}

void Experiment::Run(){

  Simulator::Stop(Seconds(sTime + 30));
  Simulator::Run();
  Simulator::Destroy();
	b4mesh_traces.ExportResults();
}
