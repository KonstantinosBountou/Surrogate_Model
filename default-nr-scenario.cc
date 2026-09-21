/* =============================================================================
 * cvae-nr-simulation.cc
 *
 * NS3 version    : 3.46.1
 * 5G-LENA module : v4.1.1
 *
 * Implements an OnOffApplication (UDP, downlink) over a 5G NR air interface.
 * Every parameter defined in Table 1 of machine-learning/cvae/instructions.md
 * is exposed as a command-line argument so that batch runners can sweep the
 * full seed / validation grids encoded in seed_simulations.json and
 * validate_simulations.json.
 *
 * Build:
 *   Copy this file to <ns3-root>/scratch/cvae-nr-simulation/
 *   ./ns3 configure --enable-examples --enable-tests
 *   ./ns3 build
 *
 * Example run:
 *   ./ns3 run "cvae-nr-simulation \
 *       --netUeNodes=10 --netGnbNodes=1 \
 *       --phyCarrierFrequency=3.5e9 --phyBandwidth=20e6 \
 *       --phySubcarrierSpacing=60 \
 *       --phyAntennaRowsGnb=1 --phyAntennaColumnsGnb=1 \
 *       --phyAntennaRowsUe=1  --phyAntennaColumnsUe=1  \
 *       --phyMimoLayers=1 --phyBandwidthParts=1 \
 *       --macHarqMaxRetx=4 --macBwpSwitchingDelay=0.1 \
 *       --outputTag=run_001"
 *
 * Output:
 *   cvae-nr-result[-<outputTag>].json  — input parameters + KPI results
 * =============================================================================
 */

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/names.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/config-store-module.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using namespace ns3;
using json = nlohmann::json;

FlowMonitorHelper flowMonHelper;
Ptr<FlowMonitor>  flowMonitor;
std::vector<Ipv4Address> ipAddresses;
double   g_sinrLinearSum   = 0.0;   // SINR Visualization
uint64_t g_sinrSampleCount = 0;     // SINR Visualization

NS_LOG_COMPONENT_DEFINE("CvaeNrSimulation");

template <typename T>
void AddUnique(std::vector<T>& v, const T& value){
    if (std::find(v.begin(), v.end(), value) == v.end()){
        v.push_back(value);
    }
}

struct UeService {
    std::string id;
    uint16_t port;
};

void from_json(const nlohmann::json& j, UeService& s){
    j.at("id").get_to(s.id);
    j.at("port").get_to(s.port);
}

// ---------------------------------------------------------------------------
// Progress ticker — prints a one-liner every `interval` simulated seconds
// ---------------------------------------------------------------------------
// static void PrintProgress(double interval, double totalTime){
//      double now = Simulator::Now().GetSeconds();
//      std::cout << "[SIM] " << std::fixed << std::setprecision(1)
//                << now << " / " << totalTime << " s  ("
//                << std::setprecision(0) << (now / totalTime * 100.0) << " %)\n"
//                << std::flush;
//      if (now + interval < totalTime)
//      {
//          Simulator::Schedule(Seconds(interval), &PrintProgress, interval, totalTime);
//      }
// }

std::string IpToString(Ipv4Address addr){
    std::ostringstream oss;
    oss << addr;
    return oss.str();
}

void DlDataSinrCallback(std::string context,
                   uint16_t cellId,
                   uint16_t rnti,
                   double sinr,
                   uint16_t bwpId)
{
    double sinrDb = 10 * std::log10(sinr); // Πετάει error καθώς δεν χρησιμοποιείται πουθένα.
    g_sinrLinearSum += sinr;        // SINR Visualization — άθροισμα σε γραμμική κλίμακα
    ++g_sinrSampleCount;            //  SINR Visualization
    //std::cout << "Context=" << context
    //          << " CellId=" << cellId
    //          << " RNTI=" << rnti
    //          << " SINR(dB)=" << sinrDb
    //          << " BWP=" << bwpId
    //          << std::endl;
}

void PrintFlowMonitorStats (){

    // flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());

    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();
    double totalThroughput = 0.0;
    double totalDelay      = 0.0;
    double totalLossRate   = 0.0;
    double totalJitter     = 0.0;
    uint32_t flowCount     = 0;

    for (auto& kv : stats){
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        if (std::find(ipAddresses.begin(), ipAddresses.end(), t.sourceAddress) != ipAddresses.end() &&
                std::find(ipAddresses.begin(), ipAddresses.end(), t.destinationAddress) != ipAddresses.end()
            ){
            std::cout << "  Src Addr: " << t.sourceAddress << " -->  Dst Addr: " << t.destinationAddress << "  Protocol: " << (uint16_t)t.protocol << " ------- ";
            double duration = (kv.second.timeLastRxPacket- kv.second.timeFirstTxPacket).GetSeconds();
            double throughput =  (duration > 0.0 && kv.second.rxBytes > 0) ? (kv.second.rxBytes * 8.0 / duration / 1.0e6) : 0.0; // in Mbps
            double delay =  (kv.second.rxPackets > 0) ? (kv.second.delaySum.GetSeconds() / kv.second.rxPackets * 1.0e3) : 0.0;
            double jitter = (kv.second.rxPackets > 1) ? (kv.second.jitterSum.GetSeconds() / (kv.second.rxPackets - 1) * 1.0e3) : 0.0; // in ms
            double lossRate = (kv.second.txPackets > 0) ? (1.0 - (static_cast<double>(kv.second.rxPackets) / static_cast<double>(kv.second.txPackets))) : 0.0;
            std::cout << flowCount << "::  Throughput: " << throughput << "Mbps -->  Delay: " << delay << "ms  Lost(%): " << (lossRate * 100) <<  "% (Tx: " << kv.second.txPackets << " / Rx: " << kv.second.rxPackets << ")" << std::endl;

            totalThroughput += throughput;
            totalDelay      += delay;
            totalJitter     += jitter;
            totalLossRate   += lossRate;
            ++flowCount;
        }
    }

    double avgThroughput = (flowCount > 0) ? totalThroughput / flowCount : 0.0;
    double avgDelay      = (flowCount > 0) ? totalDelay      / flowCount : 0.0;
    double avgJitter     = (flowCount > 0) ? totalJitter     / flowCount : 0.0;
    double avgLossRate   = (flowCount > 0) ? totalLossRate   / flowCount : 0.0;

    std::cout << "================== [RESULTS] ============================\n"
    << "  throughput=" << avgThroughput  << "Mbps \n"
    << "  delay="      << avgDelay       << "ms \n"
    << "  jitter="     << avgJitter      << "ms \n"
    << "  loss(%)="    << (avgLossRate * 100 ) << "% \n"
    << "  flows="      << flowCount << "\n"
    << "==========================================================\n";

    Simulator::Schedule (Seconds (1.0), &PrintFlowMonitorStats);

}

/**
 * Install gNB Nodes
 * @param gnbNodesContainer - The container to install
 * @param gNBs - The definition of gNB
 */
static void InstallGnbNodes(const NodeContainer& gnbNodesContainer, const json& gNBs){
    MobilityHelper mob;
    mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mob.Install(gnbNodesContainer);
    //TODO (AssignStreams) - for Randomness
    int i = 0;
    for (const auto& gnbDef : gNBs){
        std::vector<double> position = gnbDef["position"].get<std::vector<double>>();
        gnbNodesContainer.Get(i)->GetObject<ConstantPositionMobilityModel>() -> SetPosition(Vector(position[0], position[1], position[2]));
        i++;
    }
}

/**
 * Install UE Nodes
 * @param ueNodesContainer - The container to install
 * @oaram ues - The definition of UEs
 */
static void InstallUeNodes(const NodeContainer& ueNodesContainer, const json& ues){
    MobilityHelper mob;
    mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mob.Install(ueNodesContainer);
    //TODO (AssignStreams) - for Randomness
    int i = 0;
    for (const auto& ueDef : ues){
        std::vector<double> position = ueDef["position"].get<std::vector<double>>();
        ueNodesContainer.Get(i)->GetObject<ConstantPositionMobilityModel>() -> SetPosition(Vector(position[0], position[1], position[2]));
        i++;
    }
}

static uint16_t getServicePort(const json& Ues, const json& cloud, const std::string ueId, const std::string serviceName){
    std::string cloudId = cloud["id"].get<std::string>();
    if ( ueId == cloudId ){
        std::vector<UeService> services = cloud["services"].get<std::vector<UeService>>();
        for (const auto& serv : services){
            if ( serv.id == serviceName ){
                return serv.port;
            }
        }
    }

    for (const auto& ueDef : Ues){
        if ( ueDef["id"] == ueId ){
            std::vector<UeService> services = ueDef["services"].get<std::vector<UeService>>();
            for (const auto& serv : services){
                if ( serv.id == serviceName ){
                    return serv.port;
                }
            }
        }
    }
    NS_FATAL_ERROR("No Service found for: " + ueId + " - " + serviceName);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int
main(int argc, char* argv[])
{

    /**
     * Constants
    **/
    const uint32_t SIMULATIONSEED = 42;

    /**
     * Variables
    **/
    double simTime = 10.0; // In seconds
    std::string topologyFile = "scratch/default/dag.json";
    uint32_t simTrial = 1; //Randomness in the simulation, Changing this produce statistical variation.

    // --- PHY layer) ----------------------------------------------------
    // phyCarrierCentralFrequency --> phyBandwidth --> ComponentCarriers (phyComponentCarrierPerBand) --> Bandwidth Parts (phyBandwidthParts) --> phyNumerology --> UE uses ONE BWP per CC
    double phyCarrierCentralFrequency = 3.5e9; //in Hz phy_carrier_central_frequency
    uint32_t phyComponentCarrierPerBand = 1; //Component Carrier per Band
    uint32_t phyBandwidthParts = 1; // parts (BWPs) - phy_bandwidth_parts per component carrier.
    bool phyShadowingEnabled = false;   // Νέα μεταβλητή για shadowing (true/false) 
    // double phyTxPowerGnb = 4.0; // Default gNB Tx Power   //Έρχονται μόνο από CLI flags (--phyTxPowerGnb=..., --phyTxPowerUe=...) και μέτα εμφανίζονται global σε όλες τις συσκευές - hardcoded defaults
    // double phyTxPowerUe = 2.0; //Default UE Tx Power  //Έρχονται μόνο από CLI flags (--phyTxPowerGnb=..., --phyTxPowerUe=...) και μέτα εμφανίζονται global σε όλες τις συσκευές - hardcoded defaults

    // --- MAC layer) ----------------------------------------------------
    bool macEnableHarqRetx = true;

    // --- Application layer) ----------------------------------------------------
    uint32_t appPacketSize = 1400; // in bytes
    std::string appDataRate = "25Mbps"; //

    // // =========================================================================
    // // Table 1 parameters — defaults equal the first seed value in each row
    // // =========================================================================
    //
    // // --- app_ (OnOffApplication) ---------------------------------------------
    // // app_protocol is always UDP; no command-line switch needed.
    //
    //

    //
    //           phy_carrier_frequency
    //
    // uint32_t    phySubcarrierSpacing = 60;          // kHz         phy_subcarrier_spacing (informational; derived from numerology)
    // double      phyTxPowerGnb        = 41.0;        // dBm         phy_tx_power_gnb
    // double      phyTxPowerUe         = 23.0;        // dBm         phy_tx_power_ue
    // std::string phyMcsTable          = "NrEesmErrorModel"; //      phy_mcs_table
    // int32_t     phyMcsIndex          = 22;          // 0-28        phy_mcs_index
    // uint32_t    phyAntennaRowsGnb    = 1;           // elements    phy_antenna_rows_gnb
    // uint32_t    phyAntennaColumnsGnb = 1;           // elements    phy_antenna_columns_gnb
    // uint32_t    phyAntennaRowsUe     = 1;           // elements    phy_antenna_rows_ue
    // uint32_t    phyAntennaColumnsUe  = 1;           // elements    phy_antenna_columns_ue
    // uint32_t    phyMimoLayers        = 1;           // layers      phy_mimo_layers
    // // phy_duplex_mode is always TDD for this study.
    // std::string phyTddPattern        = "DL|S|UL|UL|UL|DL|S|UL|UL|UL|"; // phy_tdd_pattern
    //
    //
    // // --- mac_ (MAC layer) ----------------------------------------------------
    // uint32_t    macHarqMaxRetx       = 4;           // 1-4         mac_harq_max_retx
    // // mac_random_access_type is always CBRA; no command-line switch needed.
    // double      macBwpSwitchingDelay = 0.1;         // ms          mac_bwp_switching_delay
    //
    // // --- simulation control --------------------------------------------------

    // std::string outputTag            = "";          // appended to output file name
    //
    // // =========================================================================
    // // Command-line parsing
    // // =========================================================================
    //

    CommandLine cmd(__FILE__);
    // === Simulation
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("simTrial", "Simulation trial (For multiple simulations (statistical simulations), change it.", simTrial);
    cmd.AddValue("topologyFile", "Simulation topology", topologyFile);

    // === PHY
    /**
     *  Typical Slice Configuration
     *  Band (n78) - Define phyCarrierCentralFrequency
     *  Bandwidth - Define the channel bandwidth - phyBandwidth
     *  Define Bandwidth Parts per component carrier - phyBandwidthParts
     *  //TODO (This can go to the dag.json) - This must be combined configuration.
     **/
    cmd.AddValue("phyCarrierCentralFrequency", "Carrier center frequency in Hz (phy_carrier_frequency)", phyCarrierCentralFrequency);
    cmd.AddValue("phyComponentCarrierPerBand",  "Channel bandwidth in Hz (phy_component_carrier_per_band)", phyComponentCarrierPerBand);
    cmd.AddValue("phyBandwidthParts","Number of bandwidth parts per component carrier 1-4 (phy_bandwidth_parts)", phyBandwidthParts); // Component Carries split (up to 4 for each DL/UL)
    cmd.AddValue("phyShadowingEnabled", "Enable log-normal shadow fading (phy_shadowing_enabled)", phyShadowingEnabled); //Δημιουργία CLI Argument
    // cmd.AddValue("phyTxPowerGnb", "Default gNB transmit power in dBm (phy_tx_power_gnb) for all gNBs  - Default(5G-Lena): 4dBm", phyTxPowerGnb);   //Δεν έχω json::parse άρα πρέπει να φύγει γιατί αλλίως θα λείπει η μεταβλητή phyTxPowerGnb, αφού το topology φορτωνεται αργότερα.
    // cmd.AddValue("phyTxPowerUe", "Default UE transmit power in dBm for all UEs (phy_tx_power_ue) - Default(5G-Lena): 2dBm", phyTxPowerUe);

    // === MAC
    cmd.AddValue("macEnableHarqRetx", "Enable HARQ re-tranmissions", macEnableHarqRetx); //TODO - Default Re-transmissions is 3 - Cannot be modified.

    // === Application Layer
    cmd.AddValue("appDataRate", "OnOff constant data rate, e.g. 250Mbps (app_data_rate)", appDataRate);

    //
    // // app_
    // cmd.AddValue("appPacketSize",
    //              "Packet payload size in bytes (app_packet_size)",
    //              appPacketSize);
    //
    //
    // // phy_

    //
    // cmd.AddValue("phyMcsTable",
    //              "Error model type: NrEesmErrorModel | NrLteMiErrorModel (phy_mcs_table)",
    //              phyMcsTable);
    // cmd.AddValue("phyMcsIndex",
    //              "Fixed MCS index 0-28; set -1 for adaptive AMC (phy_mcs_index)",
    //              phyMcsIndex);
    // cmd.AddValue("phyAntennaRowsGnb",
    //              "gNB antenna panel rows (phy_antenna_rows_gnb)",
    //              phyAntennaRowsGnb);
    // cmd.AddValue("phyAntennaColumnsGnb",
    //              "gNB antenna panel columns (phy_antenna_columns_gnb)",
    //              phyAntennaColumnsGnb);
    // cmd.AddValue("phyAntennaRowsUe",
    //              "UE antenna panel rows (phy_antenna_rows_ue)",
    //              phyAntennaRowsUe);
    // cmd.AddValue("phyAntennaColumnsUe",
    //              "UE antenna panel columns (phy_antenna_columns_ue)",
    //              phyAntennaColumnsUe);
    // cmd.AddValue("phyMimoLayers",
    //              "Number of MIMO spatial layers / rank indicator (phy_mimo_layers)",
    //              phyMimoLayers);
    // cmd.AddValue("phyTddPattern",
    //              "TDD slot pattern string, e.g. DL|S|UL|UL|UL|DL|S|UL|UL|UL| (phy_tdd_pattern)",
    //              phyTddPattern);
    //
    //
    // // mac_
    // cmd.AddValue("macBwpSwitchingDelay",
    //              "BWP switching delay in ms 0.1-1.0 (mac_bwp_switching_delay)",
    //              macBwpSwitchingDelay);
    //
    // // misc

    // cmd.AddValue("cellRadius",  "Cell radius in metres for UE placement",   cellRadius);
    // cmd.AddValue("outputTag",   "Tag appended to the JSON output filename",  outputTag);
    //
    cmd.Parse(argc, argv);

    // LogLevel logLevel1 = (LogLevel)(LOG_PREFIX_FUNC | LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_INFO);
    // LogComponentEnable("NrMacSchedulerNs3", logLevel1);
    // LogComponentEnable("NrMacSchedulerTdma", logLevel1);

    std::ifstream f(topologyFile);
    json topology = json::parse(f);

    // Διαβάζει τιμές από topology

    double phyTxPowerGnb = std::stod(topology["gNB"][0]["characteristics"]["phy"]["tx_power"].get<std::string>()); //strings άρα std::stod. Άν γίνουν numeric .get<double> χωρις stod.
    double phyTxPowerUe  = std::stod(topology["ue"][0]["characteristics"]["phy"]["tx_power"].get<std::string>());
    std::string schedulerType = topology["gNB"][0]["characteristics"]["mac"]["scheduler_type"].get<std::string>(); // Ανάγνωση scheduler_type από topology.
    
    // =========================================================================
    // Simulation Randomness
    // =========================================================================
    RngSeedManager::SetSeed(SIMULATIONSEED);
    RngSeedManager::SetRun(simTrial);

    // Remote host simulates the application server (core network side)
    // =========================================================================
    // Install Internet Servers (Remote Hosts)
    // =========================================================================
    std::cout << "[SETUP] Installing Application Servers (Cloud)...\n" << std::flush;
    NodeContainer cloudServers;
    cloudServers.Create(1); //TODO
    Names::Add(topology["cloud"]["id"], cloudServers.Get(0));
    std::cout << "[SETUP] Application Servers installed\n" << std::flush;

    // =========================================================================
    // Internet stack (remote host only; UEs get it after EPC install)
    // =========================================================================
    InternetStackHelper internet;
    internet.Install(cloudServers);

    // =========================================================================
    // EPC / NR helpers
    // =========================================================================
    Ptr<NrPointToPointEpcHelper> nrEpcHelper = CreateObject<NrPointToPointEpcHelper>();
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>(); // NrHelper sits at the center controlling --> UEs -> gNBs -> EPC (NR Stack, PHY,MAC,RLC,PDCP)
    nrHelper->SetEpcHelper(nrEpcHelper);

    //TODO (Configure RLC attributes) - Need Furter investigation.
    // NrEpsBearer
    // nrHelper -> ActivateDedicatedEpsBearer();
    // Config::SetDefault("ns3::NrRlcUm::MaxTxBufferSize", UintegerValue(10*1024));
    // nrHelper->ActivateDataRadioBearer (ueNetDev.Get (0), flow); --> This is for UEs or gNBs?

    Ptr<Node> pgw = nrEpcHelper->GetPgwNode(); //TODO (What is this?)

    // Backhaul: PGW ↔ remote host (ideal 10 Gbps, 1 ms) Setup
    std::cout << "[SETUP] Backhaul Network \n" << std::flush;
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue("100Gbps")); //TODO
    p2p.SetChannelAttribute("Delay",    StringValue("1ms")); //TODO
    NetDeviceContainer backhaul = p2p.Install(pgw, cloudServers.Get(0));//Names::Find<Node>(topology["cloud"]["id"]));

    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(backhaul);

    // Default route on remote host → PGW
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    // Ptr<Ipv4StaticRouting> remoteStaticRoute = ipv4RoutingHelper.GetStaticRouting(Names::Find<Node>(topology["cloud"]["id"])->GetObject<Ipv4>());
    Ptr<Ipv4StaticRouting> remoteStaticRoute = ipv4RoutingHelper.GetStaticRouting(cloudServers.Get(0)->GetObject<Ipv4>());
    remoteStaticRoute->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    // ====================
    // Creating Nodes
    // ====================
    std::cout << "[SETUP] Number of gNBs: " << topology["gNB"].size() << std::endl;
    NodeContainer gnbNodes;
    gnbNodes.Create(topology["gNB"].size());

    std::cout << "[SETUP] Number of UEs: " << topology["ue"].size() << std::endl;
    NodeContainer ueNodes;
    ueNodes.Create(topology["ue"].size());

    // =========================================================================
    // Mobility
    // =========================================================================
    std::cout << "[SETUP] Installing mobility (Use topology: " << topologyFile << ")...\n" << std::flush;
    InstallGnbNodes(gnbNodes, topology["gNB"]);
    InstallUeNodes(ueNodes, topology["ue"]);

    // =========================================================================
    // NR Module Setup
    // =========================================================================
    std::cout << "[SETUP] Initializing NR-Module \n" << std::flush;
    /**
     *  EPC is the core network, in 5G this is 5GC.
     **/
    nrHelper->SetEpcHelper(nrEpcHelper);

    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>(); //TODO Check all NRChannelHelper and apply
    channelHelper->ConfigureFactories("UMi", "Default", "ThreeGpp"); //TODO Check and update. E.g. UMa

    /**
     * Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(100))); --> what is this? is same with below?
     *
    **/
    channelHelper->SetChannelConditionModelAttribute("UpdatePeriod", TimeValue(MilliSeconds(0))); //TODO Check and update. understand
    channelHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(phyShadowingEnabled)); // Χρήση shadowing μέσω flag.

    //TODO  -
    // nrHelper->SetSchedulerTypeId (TypeId::LookupByName ("ns3::NrMacSchedulerTdmaRR"));
    // Config::SetDefault ("ns3::NrAmc::NumRefScPerRb", UintegerValue (1));
    // Config::SetDefault ("ns3::NrUePhy::MaxRank", UintegerValue (2));

    // =========================================================================
    // Spectrum / BWP configuration
    // =========================================================================
    //TODO According to 3GG - Each gNB can have different operation band config.
    //TODO To have multiple bands --> Define a handler/scheduler.
    //TODO Here we assume only one gNB and getting this value --> this channel_bandwidth must be part of the slice configuration.
    double phyBandwidth = topology["gNB"][0]["characteristics"]["phy"]["channel_bandwidth"];
    std::cout << "[SETUP] Initializing Bandwidth(PHY) " << phyBandwidth << " (Hz)...\n" << std::flush;

    CcBwpCreator::SimpleOperationBandConf bandConf( phyCarrierCentralFrequency,phyBandwidth, phyComponentCarrierPerBand);
    CcBwpCreator ccBwpCreator;
    OperationBandInfo bandInfo = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({bandInfo}); // !Important - This must be called before InstallGnbDevice / InstallUeDevice
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({bandInfo});

    // =========================================================================
    // Setting up PHY parameters.
    // =========================================================================

    /**
     * Numerology is set before device installation via SetGnbPhyAttribute;
     * //TODO - In 5G NR, numerology is typically tied to Bandwidth Parts (BWPs). This is the more realistic approach.
     **/

    //TODO Check if carrier spacing can be defined or is standard. - Traffic controller. Numerology differentiates: Throughput vs latency, Check how to configure different numerology for different parts.
    uint32_t phyNumerology  = topology["gNB"][0]["characteristics"]["phy"]["numerology"];
    nrHelper->SetGnbPhyAttribute("Numerology",   UintegerValue(phyNumerology));

    //nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerTdmaMR")); //TODO Evaluate different scheduler, - Check nr-mac-scheduler-* classes.
    
    std::string schedulerClass = "ns3::NrMacScheduler" + schedulerType;
    nrHelper->SetSchedulerTypeId(TypeId::LookupByName(schedulerClass)); // Επιτρέπει switch ανάμεσα σε TdmaMR/TdmaRR/TdmaPF/TdmaQos χωρίς recompile
    
    // Setup the OpenGym interface
    // Ptr<OpenGymInterface> openGymInterface = CreateObject<OpenGymInterface>(5555);
    // Ptr<NrMacSchedulerAiNs3GymEnv> myGymEnv = CreateObject<NrMacSchedulerAiNs3GymEnv>(2);
    // myGymEnv -> SetOpenGymInterface(openGymInterface);
    // nrHelper->SetSchedulerAttribute(
    //         "NotifyCbDl",
    //         CallbackValue(
    //             MakeCallback(&NrMacSchedulerAiNs3GymEnv::NotifyCurrentIteration, myGymEnv)));
    // nrHelper->SetSchedulerAttribute(
    //     "ActiveDlAi",
    //     BooleanValue(true)); // Activate the AI model for the downlink
    // std::cout << "AI scheduler is enabled" << std::endl;


    nrHelper->SetSchedulerAttribute("EnableHarqReTx", BooleanValue(macEnableHarqRetx));
    nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(phyTxPowerGnb));
    nrHelper->SetUePhyAttribute("TxPower", DoubleValue(phyTxPowerUe));
    // nrHelper->SetGnbPhyAttribute("Pattern",StringValue("DL|DL|F|UL|UL|")); // TODO - Dynamic TDD (SFI) - SFI Controller --> how? The network can change slot formats on the fly, Controlled via Slot Format Indicator (SFI)

    std::string phyDlUlPattern = topology["gNB"][0]["characteristics"]["phy"]["pattern"]; //TODO we take 0. - Must be part of the slice.
    std::cout << "[SETUP] Initializing TDD Pattern to " << phyDlUlPattern << " ...\n" << std::flush;
    nrHelper->SetGnbPhyAttribute("Pattern",StringValue(phyDlUlPattern));

    std::cout << "[SETUP] Configuring Default MIMO/Beamforming...\n" << std::flush;
    nrHelper->SetGnbAntennaAttribute ("NumRows", UintegerValue (4)); // TODO (Variable)
    nrHelper->SetGnbAntennaAttribute ("NumColumns", UintegerValue (4)); // TODO (Variable)
    nrHelper->SetGnbAntennaAttribute("AntennaElement", PointerValue(CreateObject<IsotropicAntennaModel>())); //TODO - Other Antenna Model?
    nrHelper->SetGnbAntennaAttribute ("IsDualPolarized", BooleanValue (true)); // TODO (Variable)

    nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (2)); // TODO (Variable)
    nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (2)); // TODO (Variable)

    // ===============
    // BeamformingHelper
    // ===============
    Ptr<IdealBeamformingHelper> idealBeamformingHelper = CreateObject<IdealBeamformingHelper>(); // TODO (Other?) --> DirectPathBeamforming, QuasiOmniBeamforming
    nrHelper->SetBeamformingHelper(idealBeamformingHelper);

    std::cout << "[SETUP] Configuring Default MIMO/Beamforming... [Done!]\n" << std::flush;

    //
    // // Error model / MCS table
    // // In 5G-LENA v4 the error model is configured via SetDlErrorModel /
    // // SetUlErrorModel on NrHelper, not as a PHY attribute.
    // // "NrEesmErrorModel" maps to the concrete NrEesmCcT2 class (EESM, Chase
    // // Combining, Table 2 — standard choice for NR link adaptation).
    // std::string errorModelClass;
    // if (phyMcsTable == "NrEesmErrorModel")
    // {
    //     errorModelClass = "ns3::NrEesmCcT2";
    // }
    // else if (phyMcsTable == "NrLteMiErrorModel")
    // {
    //     errorModelClass = "ns3::NrLteMiErrorModel";
    // }
    // else
    // {
    //     errorModelClass = "ns3::" + phyMcsTable;
    // }
    // nrHelper->SetDlErrorModel(errorModelClass);
    // nrHelper->SetUlErrorModel(errorModelClass);
    //
    //
    // // =========================================================================
    // // NrHelper MAC attributes  (mac_ parameters)
    // // =========================================================================
    //
    // // HARQ — not a NrGnbPhy attribute in 5G-LENA v4; controlled at MAC level.
    // // HarqEnabled:  set globally via NrHelper before device installation.
    // // MaxHarqRound: maps to NumHarqProcess on NrGnbMac (processes, not retx
    // //               count, but drives the HARQ pipeline depth).
    // // mac_harq_max_retx / mac_harq_enabled — recorded in the output JSON but
    // // not applied as NS3 attributes: NrGnbMac::NumHarqProcess does not exist
    // // in this build and setting it causes a runtime crash in StartSlot.
    // // HARQ is always active in 5G-LENA v4; the number of processes is fixed
    // // at the compiled default (typically 8).
    //

    // =========================================================================
    // Install NR devices
    // =========================================================================
    std::cout << "[SETUP] Installing NR devices...\n" << std::flush;
    NetDeviceContainer gnbNetDev = nrHelper->InstallGnbDevice(gnbNodes, allBwps); //TODO - allBwps must be optimized.
    internet.Install(ueNodes); // Installs IP Layer
    NetDeviceContainer ueNetDev  = nrHelper->InstallUeDevice (ueNodes,  allBwps); //Install NR-Radio Stack
    std::cout << "[SETUP] NR devices installed\n" << std::flush;


    // =========================================================================
    // Internet stack on UEs + IP addressing
    // =========================================================================
    Ipv4InterfaceContainer ueIpIfaces = nrEpcHelper -> AssignUeIpv4Address(ueNetDev);
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u){ // Set Default for each UE --> EPC PDN GW (TODO - What is EPC PDN GW)
        Ptr<Node> node = ueNodes.Get(u);
        Ptr<Ipv4StaticRouting> ueStaticRoute = ipv4RoutingHelper.GetStaticRouting(node->GetObject<Ipv4>());
        ueStaticRoute -> SetDefaultRoute(nrEpcHelper -> GetUeDefaultGatewayAddress(), 1);
        Names::Add(topology["ue"][u]["id"], node);
    }


    // =========================================================================
    // Attach UEs to nearest gNB
    // =========================================================================

    std::cout << "[SETUP] Attaching UEs to closest gNB...\n" << std::flush;
    nrHelper->AttachToClosestGnb(ueNetDev, gnbNetDev); //TODO
    std::cout << "[SETUP] All UEs attached\n" << std::flush;

    double appStart = 0.5;   // seconds — allow RRC / RACH to complete // TODO
    for (const auto& connection : topology["connections"]){
        std::cout << "[SETUP] DAG Connection From: " << connection["from"] <<  " --> To: " << connection["to"] << " ====> " ;
        Ptr<Node> from = Names::Find<Node>(connection["from"]);
        Ptr<Node> to = Names::Find<Node>(connection["to"]);
        Ipv4Address fromIp = from -> GetObject<Ipv4>() -> GetAddress(1, 0).GetLocal();
        AddUnique(ipAddresses, fromIp);
        Ipv4Address toIp = to -> GetObject<Ipv4>() -> GetAddress(1, 0).GetLocal();
        AddUnique(ipAddresses, toIp);
        std::cout << " DAG Connection From(IP): " << fromIp <<  " --> To(IP): " << toIp << "" << std::endl;
    }

    std::cout << "[SETUP] Initialize Sink Application to UE/Cloud/\n" << std::flush;

    for (const auto& ueInfo : topology["ue"]){
        // Server: PacketSink on each UE (receives downlink traffic) //TODO Need to change this.
        std::vector<UeService> services = ueInfo["services"].get<std::vector<UeService>>();
        for (const auto& serv : services){
            appStart = appStart + 0.01;
            PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), serv.port));
            Ptr<Node> sinkerNode = Names::Find<Node>(ueInfo["id"]);
            ApplicationContainer serverApps = sinkHelper.Install(sinkerNode); //ueNodes.Get(0)
            serverApps.Start(Seconds(appStart));
            serverApps.Stop (Seconds(simTime));
        }

    }


    for (const auto& connection : topology["connections"]){
        appStart = appStart + 0.01;
        std::string ueId = connection["to"];
        Ptr<Node> rxNode = Names::Find<Node>(ueId);
        Ipv4Address sinkerNodeIp = rxNode -> GetObject<Ipv4>() -> GetAddress(1, 0).GetLocal();
        std::string serviceName = connection["characteristics"]["application"]["point_to_service"].get<std::string>();
        OnOffHelper onoff( "ns3::UdpSocketFactory", InetSocketAddress(sinkerNodeIp, getServicePort(topology["ue"], topology["cloud"], ueId, serviceName)));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        onoff.SetConstantRate(connection["characteristics"]["application"]["dataRate"].get<std::string>(), appPacketSize);
        Ptr<Node> txNode = Names::Find<Node>(connection["from"]);
        ApplicationContainer app = onoff.Install(txNode);
        app.Start(Seconds(appStart));
        app.Stop (Seconds(simTime));
    }


    // =========================================================================
    // Flow monitor - Traces Monitoring
    // =========================================================================
    nrHelper->EnableDlDataPhyTraces(); //TODO Check further the implementation.
    nrHelper->EnableDlCtrlPhyTraces(); //TODO Check further the implementation.
    nrHelper->EnableGnbPhyCtrlMsgsTraces();
    nrHelper->EnableGnbMacCtrlMsgsTraces();
    nrHelper->EnableUePhyCtrlMsgsTraces();
    nrHelper->EnableUeMacCtrlMsgsTraces();
    nrHelper->EnableDlMacSchedTraces();
    nrHelper->EnableUlMacSchedTraces();

    flowMonitor = flowMonHelper.InstallAll();

    // =========================================================================
    // Run simulation
    // =========================================================================

    std::cout << "[SIM]   Starting — progress every 1 s\n" << std::flush;
    // Simulator::Schedule(Seconds(1.0), &PrintProgress, 1.0, simTime);
    Simulator::Schedule (Seconds (1.0), &PrintFlowMonitorStats);

    Config::Connect(
    "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/DlDataSinr",
    MakeCallback(&DlDataSinrCallback)
    );

    // Config::SetDefault("ns3::ConfigStore::Filename", StringValue("output-attributes.txt"));
    // Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("RawText"));
    // Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));

    // ConfigStore outputConfig;
    // outputConfig.ConfigureAttributes();

    Simulator::Stop(Seconds(simTime + 0.5));
    Simulator::Run();
    std::cout << "[SIM]   Finished\n" << std::flush;

    // =========================================================================
    // Results collection
    // =========================================================================

    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());

    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();
    double totalThroughput = 0.0;
    double totalDelay      = 0.0;
    double totalLossRate   = 0.0;
    double totalJitter     = 0.0;
    uint32_t flowCount     = 0;
    std::vector<double> flowThroughputs; // Λίστα που κάνει save το throughput κάθε ξεχωριστού flow.

    for (auto& kv : stats){
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        if (std::find(ipAddresses.begin(), ipAddresses.end(), t.sourceAddress) != ipAddresses.end() &&
                std::find(ipAddresses.begin(), ipAddresses.end(), t.destinationAddress) != ipAddresses.end()
            ){
            std::cout << "  Src Addr: " << t.sourceAddress << " -->  Dst Addr: " << t.destinationAddress << "  Protocol: " << (uint16_t)t.protocol << " ------- ";
            double duration = (kv.second.timeLastRxPacket- kv.second.timeFirstTxPacket).GetSeconds();
            double throughput =  (duration > 0.0 && kv.second.rxBytes > 0) ? (kv.second.rxBytes * 8.0 / duration / 1.0e6) : 0.0; // in Mbps
            double delay =  (kv.second.rxPackets > 0) ? (kv.second.delaySum.GetSeconds() / kv.second.rxPackets * 1.0e3) : 0.0;
            double jitter = (kv.second.rxPackets > 1) ? (kv.second.jitterSum.GetSeconds() / (kv.second.rxPackets - 1) * 1.0e3) : 0.0; // in ms
            double lossRate = (kv.second.txPackets > 0) ? (1.0 - (static_cast<double>(kv.second.rxPackets) / static_cast<double>(kv.second.txPackets))) : 0.0;
            std::cout << flowCount << "::  Throughput: " << throughput << "Mbps -->  Delay: " << delay << "ms  Lost(%): " << (lossRate * 100) <<  "% (Tx: " << kv.second.txPackets << " / Rx: " << kv.second.rxPackets << ")" << std::endl;

            totalThroughput += throughput;
            flowThroughputs.push_back(throughput); // Σε κάθε flow που περνάει από το loop, προσθέτει το throughtput στη λίστα.
            totalDelay      += delay;
            totalJitter     += jitter;
            totalLossRate   += lossRate;
            ++flowCount;
        }
    }

    double avgThroughput = (flowCount > 0) ? totalThroughput / flowCount : 0.0;
    double avgDelay      = (flowCount > 0) ? totalDelay      / flowCount : 0.0;
    double avgJitter     = (flowCount > 0) ? totalJitter     / flowCount : 0.0;
    double avgLossRate   = (flowCount > 0) ? totalLossRate   / flowCount : 0.0;
    double avgSinrDb = (g_sinrSampleCount > 0)
    ? 10 * std::log10(g_sinrLinearSum / g_sinrSampleCount)
    : 0.0;
    // Υπολογισμός Jain's Index.
        double sumX = 0.0, sumX2 = 0.0;
    for (double x : flowThroughputs) {
        sumX  += x;
        sumX2 += x * x;
    }
    double jainIndex = (flowCount > 0 && sumX2 > 0)
        ? (sumX * sumX) / (flowCount * sumX2)
        : 0.0;

    json j;
    j["flows"] = "";
    j["mean"] = {avgThroughput, avgDelay, avgJitter, avgLossRate, avgSinrDb, jainIndex};
    j["parameters"] = {
      {"channel_bandwidth", phyBandwidth},   // σε Hz, όπως στο dag.json
      {"numerology", phyNumerology},
      {"tx_power_gnb", phyTxPowerGnb},
      {"tx_power_ue", phyTxPowerUe},
      {"scheduler_type", schedulerType},
      {"shadowing_enabled", phyShadowingEnabled}
    };
    std::ofstream file("results.json");
    file << j.dump(4);
    std::cout << "================== [RESULTS] ============================\n"
    << "  throughput=" << avgThroughput  << "Mbps \n"
    << "  delay="      << avgDelay       << "ms \n"
    << "  jitter="     << avgJitter      << "ms \n"
    << "  loss(%)="       << (avgLossRate * 100 ) << "% \n"
    << "  sinr="       << avgSinrDb      << "dB \n"
    << "  jain_fairness=" << jainIndex   << " \n"
    << "  flows="      << flowCount << "\n"
    << "==========================================================\n";

    // myGymEnv->NotifySimulationEnd();

    Simulator::Destroy();
    return 0;
}