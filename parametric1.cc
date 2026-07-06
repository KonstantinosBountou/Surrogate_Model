#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/nr-module.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Parametric5G");

static double g_sinrSum = 0.0;
static int g_sinrCount = 0;

void
DlDataSinrCallback(uint16_t cellId, uint16_t rnti, double sinr, uint16_t bwpId)
{
  if (sinr > 0.0)
    {
      g_sinrSum += 10.0 * std::log10(sinr);
      g_sinrCount++;
    }
}

int
main(int argc, char* argv[])
{
  double distance = 100.0;
  double txPowerGnb = 40.0;
  double txPowerUe = 23.0;
  double bwMhz = 20.0;
  double bwHz = 0.0;
  uint16_t numUes = 4;
  uint16_t numerology = 1;
  double simTime = 10.0;
  uint32_t packetSize = 1200;
  std::string appDataRate = "1500Kbps";
  std::string scenario = "UMa";
  std::string channelCondition = "LOS";
  std::string propagation = "ThreeGpp";
  bool shadowingEnabled = false;
  std::string scheduler = "TdmaPF";
  std::string outFile = "results.csv";

  std::string rlcMode = "AM";
  bool harqEnabled = true;
  std::string trafficType = "OnOff";

  CommandLine cmd(__FILE__);
  cmd.AddValue("distance", "UE-gNB distance in meters", distance);
  cmd.AddValue("txPowerGnb", "gNB TX power in dBm", txPowerGnb);
  cmd.AddValue("txPowerUe", "UE TX power in dBm", txPowerUe);
  cmd.AddValue("bwMhz", "Bandwidth in MHz", bwMhz);
  cmd.AddValue("bwHz", "Bandwidth in Hz. Overrides bwMhz when > 0.", bwHz);
  cmd.AddValue("numUes", "Number of UEs", numUes);
  cmd.AddValue("numerology", "NR numerology", numerology);
  cmd.AddValue("simTime", "Simulation time in seconds", simTime);
  cmd.AddValue("packetSize", "UDP packet size in bytes", packetSize);
  cmd.AddValue("appDataRate", "OnOff application data rate", appDataRate);
  cmd.AddValue("scenario", "3GPP scenario, e.g. UMa, UMi, RMa", scenario);
  cmd.AddValue("channelCondition", "Channel condition, e.g. LOS or NLOS", channelCondition);
  cmd.AddValue("propagation", "Propagation model family, usually ThreeGpp", propagation);
  cmd.AddValue("shadowingEnabled", "Enable 3GPP shadowing", shadowingEnabled);
  cmd.AddValue("scheduler", "Scheduler shortcut: TdmaPF, TdmaRR, OfdmaPF, OfdmaRR", scheduler);
  cmd.AddValue("outFile", "Output CSV file", outFile);

  cmd.AddValue("rlcMode", "RLC mode: AM or UM (wired via LteEnbRrc::EpsBearerToRlcMapping)", rlcMode);
  cmd.AddValue("harqEnabled", "Enable/disable HARQ retransmissions (wired via scheduler EnableHarqReTx)", harqEnabled);
  cmd.AddValue("trafficType",
               "Traffic model: OnOff (default, bursty) or NgmnVideo (constant, "
               "confirmed via contrib/nr/examples/cttc-nr-traffic-ngmn-mixed.cc)",
               trafficType);

  cmd.Parse(argc, argv);

  if (bwHz > 0.0)
    {
      bwMhz = bwHz / 1e6;
    }
  else
    {
      bwHz = bwMhz * 1e6;
    }

  // RLC mode wiring (confirmed via LteEnbRrc::EpsBearerToRlcMapping attribute,
  // src/lte/model/lte-enb-rrc.cc:1894). Must be set before the eNB/gNB RRC
  // object is created, i.e. before NrHelper::InstallGnbDevice() runs below.
  if (rlcMode == "UM")
    {
      Config::SetDefault("ns3::LteEnbRrc::EpsBearerToRlcMapping", StringValue("RlcUmAlways"));
    }
  else
    {
      Config::SetDefault("ns3::LteEnbRrc::EpsBearerToRlcMapping", StringValue("RlcAmAlways"));
      rlcMode = "AM";
    }

  g_sinrSum = 0.0;
  g_sinrCount = 0;

  NodeContainer gnbNodes;
  NodeContainer ueNodes;
  gnbNodes.Create(1);
  ueNodes.Create(numUes);

  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(gnbNodes);
  mobility.Install(ueNodes);

  gnbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 25.0));

  for (uint32_t i = 0; i < ueNodes.GetN(); i++)
    {
      const double angle = i * 2.0 * M_PI / ueNodes.GetN();

      ueNodes.Get(i)->GetObject<MobilityModel>()->SetPosition(
          Vector(distance * std::cos(angle), distance * std::sin(angle), 1.5));
    }

  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
  Ptr<IdealBeamformingHelper> beamformingHelper = CreateObject<IdealBeamformingHelper>();
  Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();

  nrHelper->SetBeamformingHelper(beamformingHelper);
  nrHelper->SetEpcHelper(epcHelper);

  Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
  channelHelper->ConfigureFactories(scenario, channelCondition, propagation);
  channelHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(shadowingEnabled));

  BandwidthPartInfoPtrVector allBwps;
  CcBwpCreator ccBwpCreator;
  CcBwpCreator::SimpleOperationBandConf bandConf(3.5e9, bwHz, 1);

  OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
  channelHelper->AssignChannelsToBands({band});
  allBwps = CcBwpCreator::GetAllBwps({band});

  if (scheduler == "TdmaRR")
    {
      nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerTdmaRR"));
    }
  else if (scheduler == "OfdmaPF")
    {
      nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerOfdmaPF"));
    }
  else if (scheduler == "OfdmaRR")
    {
      nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerOfdmaRR"));
    }
  else
    {
      nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerTdmaPF"));
      scheduler = "TdmaPF";
    }

  // HARQ wiring (confirmed via NrMacSchedulerNs3::EnableHarqReTx attribute,
  // contrib/nr/model/nr-mac-scheduler-ns3.cc:197).
  nrHelper->SetSchedulerAttribute("EnableHarqReTx", BooleanValue(harqEnabled));

  nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(txPowerGnb));
  nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(numerology));
  nrHelper->SetUePhyAttribute("TxPower", DoubleValue(txPowerUe));

  nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(4));
  nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
  nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
  nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(4));

  NetDeviceContainer gnbDevices = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
  NetDeviceContainer ueDevices = nrHelper->InstallUeDevice(ueNodes, allBwps);

  for (uint32_t i = 0; i < ueDevices.GetN(); i++)
    {
      Ptr<NrUeNetDevice> ueDevice = DynamicCast<NrUeNetDevice>(ueDevices.Get(i));

      if (ueDevice)
        {
          ueDevice->GetPhy(0)->TraceConnectWithoutContext(
              "DlDataSinr",
              MakeCallback(&DlDataSinrCallback));
        }
    }

  InternetStackHelper internet;
  internet.Install(ueNodes);

  Ipv4InterfaceContainer ueIps = epcHelper->AssignUeIpv4Address(ueDevices);
  nrHelper->AttachToClosestGnb(ueDevices, gnbDevices);

  Ipv4StaticRoutingHelper routingHelper;

  for (uint32_t i = 0; i < ueNodes.GetN(); i++)
    {
      routingHelper.GetStaticRouting(ueNodes.Get(i)->GetObject<Ipv4>())
          ->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

  Ptr<Node> pgw = epcHelper->GetPgwNode();

  ApplicationContainer servers;
  ApplicationContainer clients;

  for (uint32_t i = 0; i < ueNodes.GetN(); i++)
    {
      const uint16_t port = 2000 + i;

      if (trafficType == "NgmnVideo")
        {
          // NGMN Video traffic generator (confirmed API from
          // contrib/nr/examples/cttc-nr-traffic-ngmn-mixed.cc:1306-1333 and
          // contrib/nr/utils/traffic-generators/helper/traffic-generator-helper.h:42).
          // Sink runs on the UE, generator runs on the pgw side (mirrors the
          // remoteHost -> UE direction used in the confirmed example).
          PacketSinkHelper packetSinkHelper(
              "ns3::UdpSocketFactory",
              InetSocketAddress(Ipv4Address::GetAny(), port));
          servers.Add(packetSinkHelper.Install(ueNodes.Get(i)));

          TrafficGeneratorHelper trafficGeneratorHelper("ns3::UdpSocketFactory",
                                                         Address(),
                                                         TrafficGeneratorNgmnVideo::GetTypeId());
          trafficGeneratorHelper.SetAttribute("NumberOfPacketsInFrame", UintegerValue(8));
          trafficGeneratorHelper.SetAttribute("InterframeIntervalTime",
                                               TimeValue(Seconds(0.100)));
          trafficGeneratorHelper.SetAttribute(
              "Remote",
              AddressValue(InetSocketAddress(ueIps.GetAddress(i), port)));

          clients.Add(trafficGeneratorHelper.Install(pgw));
        }
      else
        {
          trafficType = "OnOff";

          UdpServerHelper server(port);
          servers.Add(server.Install(ueNodes.Get(i)));

          OnOffHelper onoff(
              "ns3::UdpSocketFactory",
              InetSocketAddress(ueIps.GetAddress(i), port));

          onoff.SetAttribute("DataRate", DataRateValue(DataRate(appDataRate)));
          onoff.SetAttribute("PacketSize", UintegerValue(packetSize));
          onoff.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
          onoff.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.5]"));

          clients.Add(onoff.Install(pgw));
        }
    }

  servers.Start(Seconds(0.5));
  servers.Stop(Seconds(simTime));

  clients.Start(Seconds(0.5));
  clients.Stop(Seconds(std::max(0.5, simTime - 0.5)));

  FlowMonitorHelper flowMonitorHelper;
  Ptr<FlowMonitor> monitor = flowMonitorHelper.InstallAll();

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  monitor->CheckForLostPackets();

  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowMonitorHelper.GetClassifier());

  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  double sumTput = 0.0;
  double sumDelay = 0.0;
  double sumPlr = 0.0;
  double sumJitter = 0.0;
  double sumRi = 0.0;
  double sumRi2 = 0.0;
  int validFlows = 0;

  for (const auto& flow : stats)
    {
      if (flow.second.rxPackets == 0 || flow.second.txPackets == 0)
        {
          continue;
        }

      Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);

      if (tuple.sourceAddress != Ipv4Address("7.0.0.1"))
        {
          continue;
        }

      const double duration =
          flow.second.timeLastRxPacket.GetSeconds() -
          flow.second.timeFirstRxPacket.GetSeconds();

      if (duration <= 0.0)
        {
          continue;
        }

      const double throughput = flow.second.rxBytes * 8.0 / duration / 1e6;
      const double delay = flow.second.delaySum.GetSeconds() / flow.second.rxPackets * 1000.0;
      const double plr =
          (flow.second.txPackets - flow.second.rxPackets) * 100.0 / flow.second.txPackets;

      const double jitter =
          (flow.second.rxPackets > 1)
              ? flow.second.jitterSum.GetSeconds() / (flow.second.rxPackets - 1) * 1000.0
              : 0.0;

      sumTput += throughput;
      sumDelay += delay;
      sumPlr += plr;
      sumJitter += jitter;
      sumRi += throughput;
      sumRi2 += throughput * throughput;
      validFlows++;
    }

  const double avgTput = validFlows > 0 ? sumTput / validFlows : 0.0;
  const double avgDelay = validFlows > 0 ? sumDelay / validFlows : 0.0;
  const double avgPlr = validFlows > 0 ? sumPlr / validFlows : 0.0;
  const double avgJitter = validFlows > 0 ? sumJitter / validFlows : 0.0;

  const double spectralEff =
      (validFlows > 0 && bwHz > 0.0)
          ? (sumTput * 1e6) / (bwHz * validFlows)
          : 0.0;

  const double jainFairness =
      (validFlows > 0 && sumRi2 > 0.0)
          ? (sumRi * sumRi) / (validFlows * sumRi2)
          : 0.0;

  const double avgSinr =
      g_sinrCount > 0
          ? g_sinrSum / g_sinrCount
          : -999.0;

  const bool fileExists = std::ifstream(outFile).good();
  std::ofstream csv(outFile, std::ios::app);

  if (!csv.is_open())
    {
      std::cerr << "Could not open output CSV: " << outFile << std::endl;
      Simulator::Destroy();
      return 1;
    }

  if (!fileExists)
    {
      csv << "distance_m,"
          << "txpower_gnb_dbm,"
          << "txpower_ue_dbm,"
          << "bandwidth_mhz,"
          << "num_ues,"
          << "numerology,"
          << "sim_time_s,"
          << "scenario,"
          << "channel_condition,"
          << "shadowing_enabled,"
          << "scheduler,"
          << "rlc_mode,"
          << "harq_enabled,"
          << "traffic_type,"
          << "packet_size,"
          << "app_data_rate,"
          << "throughput_mbps,"
          << "delay_ms,"
          << "plr_pct,"
          << "jitter_ms,"
          << "sinr_db,"
          << "spectral_eff_bpshz,"
          << "jain_fairness\n";
    }

  csv << distance << ","
      << txPowerGnb << ","
      << txPowerUe << ","
      << bwMhz << ","
      << numUes << ","
      << numerology << ","
      << simTime << ","
      << scenario << ","
      << channelCondition << ","
      << (shadowingEnabled ? 1 : 0) << ","
      << scheduler << ","
      << rlcMode << ","
      << (harqEnabled ? 1 : 0) << ","
      << trafficType << ","
      << packetSize << ","
      << appDataRate << ","
      << avgTput << ","
      << avgDelay << ","
      << avgPlr << ","
      << avgJitter << ","
      << avgSinr << ","
      << spectralEff << ","
      << jainFairness << "\n";

  csv.close();

  std::cout << "\n===== 5G-LENA RESULTS =====\n";
  std::cout << "Distance      : " << distance << " m\n";
  std::cout << "TxPower gNB   : " << txPowerGnb << " dBm\n";
  std::cout << "TxPower UE    : " << txPowerUe << " dBm\n";
  std::cout << "Bandwidth     : " << bwMhz << " MHz\n";
  std::cout << "numUEs        : " << numUes << "\n";
  std::cout << "Numerology    : " << numerology << "\n";
  std::cout << "Scheduler     : " << scheduler << "\n";
  std::cout << "RLC Mode      : " << rlcMode << "\n";
  std::cout << "HARQ Enabled  : " << (harqEnabled ? "true" : "false") << "\n";
  std::cout << "Traffic Type  : " << trafficType << "\n";
  std::cout << "Throughput    : " << avgTput << " Mbps\n";
  std::cout << "E2E Delay     : " << avgDelay << " ms\n";
  std::cout << "PLR           : " << avgPlr << " %\n";
  std::cout << "Jitter        : " << avgJitter << " ms\n";
  std::cout << "SINR          : " << avgSinr << " dB\n";
  std::cout << "Spectral Eff. : " << spectralEff << " bps/Hz\n";
  std::cout << "Jain Fairness : " << jainFairness << "\n";
  std::cout << "CSV           : " << outFile << "\n";
  std::cout << "===========================\n";

  Simulator::Destroy();
  return 0;
}
