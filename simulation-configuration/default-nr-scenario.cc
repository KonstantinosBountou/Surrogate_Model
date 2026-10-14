/* =============================================================================
 * default-nr-scenario.cc
 *
 * Single static UE -> gNB -> EPC -> cloud, UDP CBR or On/Off.
 * Configuration: scratch/default/dag.json, or --topologyFile.
 * Radio: scalar Friis plus reciprocal gNB cosine gain.
 * No shadowing/fading, array gain, beamforming or extra spatial streams.
 * NR panels remain 1x1 isotropic.
 *
 * Scheduler configuration:
 *   gNB[0].characteristics.mac.scheduler_type: TdmaRR, TdmaPF, TdmaMR, TdmaQos, TdmaRandom.
 *
 * RLC configuration:
 *   gNB[0].characteristics.rlc.mode: UM or AM.
 *   gNB[0].characteristics.rlc.buffer_size_bytes: positive uint32.
 *
 * HARQ configuration:
 *   gNB[0].characteristics.mac.harq_enabled
 *   gNB[0].characteristics.mac.harq_processes
 *
 * In the installed NR version:
 *   EnableHarqReTx=false -> 0 retransmissions.
 *   EnableHarqReTx=true  -> at most 3 retransmissions.
 * The initial transmission is not counted as a retransmission.
 *
 * Traffic configuration:
 *   traffic_model: CBR, OnOffConstant, OnOffExponential.
 *   dataRate: sending rate during ON (not the average including OFF).
 *   on_time_ms / off_time_ms: required positive durations for On/Off;
 *     constant durations or exponential means, depending on traffic_model.
 *   For On/Off, omit packet_interval_us and specify dataRate explicitly.
 *   connections[0].characteristics.application.packet_interval_us (optional).
 *   For CBR only, takes precedence over dataRate; absent -> dataRate input.
 *
 * Output: results.json, or --outputFile.
 * =============================================================================
 */

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;
using json = nlohmann::json;

static void
Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

// TRAFFIC: Το Tx trace ενεργοποιείται μετά από επιτυχή socket Send.
// Μετρά payload που παραδόθηκε στο UDP socket, όχι bytes που έλαβε ο cloud.
struct ApplicationTxCounters
{
    uint64_t bytes{0};
    uint64_t packets{0};
    void OnTx(Ptr<const Packet> packet)
    {
        bytes += packet->GetSize();
        ++packets;
    }
};

static double
Power(const json& value)
{
    return value.is_string()
        ? std::stod(value.get<std::string>())
        : value.get<double>();
}

// ANTENNA: Το NR panel δεν είναι AntennaModel.
// Το διάγραμμα του gNB εφαρμόζεται μία φορά στην scalar αλυσίδα Friis,
// αμοιβαία για UL και DL. Το UE παραμένει ισοτροπικό με κέρδος 0 dBi.
class ScenarioGnbAntennaGainModel : public PropagationLossModel
{
  public:
    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::ScenarioGnbAntennaGainModel")
                .SetParent<PropagationLossModel>()
                .SetGroupName("Propagation")
                .AddConstructor<ScenarioGnbAntennaGainModel>();

        return tid;
    }

    // ANTENNA: Συνδέει τις θέσεις των κόμβων με το διάγραμμα του gNB.
    void Configure(Ptr<MobilityModel> gnb,
                   Ptr<MobilityModel> ue,
                   Ptr<AntennaModel> pattern)
    {
        Require(gnb && ue && pattern,
                "Missing antenna gain model configuration.");

        m_gnb = gnb;
        m_ue = ue;
        m_pattern = pattern;
    }

    double GetGainTowardUeDb() const
    {
        Require(m_gnb && m_ue && m_pattern,
                "Antenna gain model is not configured.");

        // ANTENNA: Angles(στόχος, αρχή) δίνει την κατεύθυνση gNB -> UE.
        // Στο UL αυτή είναι η διεύθυνση από την οποία λαμβάνει ο gNB.
        // Το CosineAntennaModel αφαιρεί εσωτερικά το Orientation.
        const double gain = m_pattern->GetGainDb(
            Angles(m_ue->GetPosition(), m_gnb->GetPosition()));

        Require(std::isfinite(gain),
                "Non-finite antenna gain; avoid an exact pattern null.");

        return gain;
    }

  private:
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override
    {
        Require((a == m_gnb && b == m_ue) ||
                    (a == m_ue && b == m_gnb),
                "Antenna gain model only supports the configured UE-gNB pair.");

        // ANTENNA: Η είσοδος είναι η ισχύς που προκύπτει από το Friis.
        // Προσθέτουμε το κέρδος μία φορά: λήψης στο UL, εκπομπής στο DL.
        return txPowerDbm + GetGainTowardUeDb();
    }

    int64_t DoAssignStreams(int64_t) override
    {
        return 0; // Ντετερμινιστικό διάγραμμα.
    }

    Ptr<MobilityModel> m_gnb;
    Ptr<MobilityModel> m_ue;
    Ptr<AntennaModel> m_pattern;
};

// ---------------------------------------------------------------------------
// Node mobility and application services
// ---------------------------------------------------------------------------

static void
InstallGnbNodes(const NodeContainer& nodes,
                const std::vector<double>& position)
{
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(position[0], position[1], position[2]));
}

static void
InstallUeNodes(const NodeContainer& nodes, const Vector& position)
{
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(position);
}

static uint32_t
getServicePort(const json& cloud, const json& serviceName)
{
    uint32_t port = 0;

    for (const auto& service : cloud.at("services"))
    {
        if (service.at("id") == serviceName)
        {
            port = service.at("port");
        }
    }

    Require(port > 0 && port <= 65535,
            "Cloud service/UDP port not found or invalid.");

    return port;
}

// ---------------------------------------------------------------------------
// Results collection
// ---------------------------------------------------------------------------

static json
CollectFlowMonitorStats(FlowMonitorHelper& flowHelper,
                        Ptr<FlowMonitor> monitor,
                        Ipv4Address ueIp,
                        Ipv4Address cloudIp,
                        uint32_t port,
                        double start,
                        double stop)
{
    auto classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    json flows = json::array();

    for (const auto& [id, stats] : monitor->GetFlowStats())
    {
        const auto tuple = classifier->FindFlow(id);

        if (tuple.protocol != 17 ||
            tuple.sourceAddress != ueIp ||
            tuple.destinationAddress != cloudIp ||
            tuple.destinationPort != port)
        {
            continue;
        }

        json f = {
            {"tx_packets", stats.txPackets},
            {"rx_packets", stats.rxPackets},
            {"ip_rx_bytes", stats.rxBytes},
            {"ip_throughput_over_app_window_mbps",
             stats.rxBytes * 8.0 / (stop - start) / 1e6},
            {"unreceived_packets_after_drain",
             stats.txPackets - stats.rxPackets},
            {"mean_delay_ms", nullptr},
            {"mean_jitter_ms", nullptr},
            {"loss_fraction_after_drain", nullptr}
        };

        if (stats.rxPackets > 0)
        {
            f["mean_delay_ms"] =
                1000 * stats.delaySum.GetSeconds() / stats.rxPackets;
        }

        if (stats.rxPackets > 1)
        {
            f["mean_jitter_ms"] =
                1000 * stats.jitterSum.GetSeconds() /
                (stats.rxPackets - 1);
        }

        if (stats.txPackets > 0)
        {
            f["loss_fraction_after_drain"] =
                1.0 - double(stats.rxPackets) / stats.txPackets;
        }

        flows.push_back(f);
    }

    return flows;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int
main(int argc, char* argv[])
{
    try
    {
        // =====================================================================
        // Command-line arguments
        // =====================================================================

        // Το n8n αντικαθιστά την προεπιλογή με το config.json του πειράματος.
        std::string topologyFile = "scratch/default/dag.json";
        std::string outputFile = "results.json";

        CommandLine cmd(__FILE__);
        cmd.AddValue("topologyFile",
                     "Single-flow JSON configuration",
                     topologyFile);
        cmd.AddValue("outputFile",
                     "Results JSON filename",
                     outputFile);
        cmd.Parse(argc, argv);

        // =====================================================================
        // Topology loading and parameter validation
        // =====================================================================
        // ΑΝΑΓΝΩΣΗ CONFIG: Το --topologyFile δείχνει στο config.json του συγκεκριμένου experiment.
        // Το Create configuration του n8n δημιουργεί αυτό το αρχείο από το DAG και τις τιμές του experiment.

        std::ifstream input(topologyFile);
        Require(input.good(), "Cannot open topologyFile: " + topologyFile);

        const json topology = json::parse(input);

        Require(topology.at("gNB").size() == 1 &&
                    topology.at("ue").size() == 1 &&
                    topology.at("connections").size() == 1,
                "This scenario requires 1 gNB, 1 UE and 1 flow.");

        // JSON ALIASES: sim=simulation, ch=channel, core=core, gnb=gNB[0], ue=ue[0].
        // appCfg=connections[0].characteristics.application; gp/up=characteristics.phy του gNB/UE.
        // mac/rlc=gNB[0].characteristics.mac/rlc. Τα παρακάτω at()/value() διαβάζουν αυτά τα πεδία.
        const auto& sim = topology.at("simulation");
        const auto& ch = topology.at("channel");
        const auto& core = topology.at("core");
        const auto& gnb = topology.at("gNB").at(0);
        const auto& ue = topology.at("ue").at(0);
        const auto& cloud = topology.at("cloud");
        const auto& conn = topology.at("connections").at(0);
        const auto& appCfg =
            conn.at("characteristics").at("application");
        const auto& gp = gnb.at("characteristics").at("phy");
        const auto& up = ue.at("characteristics").at("phy");
        const auto& mac = gnb.at("characteristics").at("mac");
        const auto& rlc = gnb.at("characteristics").at("rlc");

        // =====================================================================
        // HARQ: Ανάγνωση και έλεγχος ρυθμίσεων
        // =====================================================================

        // HARQ: Υποχρεωτικό Boolean από:
        // gNB[0].characteristics.mac.harq_enabled
        //
        // true  -> EnableHarqReTx=true: έως 3 αναμεταδόσεις.
        // false -> EnableHarqReTx=false: 0 αναμεταδόσεις.
        //
        // Το false δεν καταργεί όλο το HARQ feedback ή buffering.
        Require(mac.at("harq_enabled").is_boolean(),
                "harq_enabled must be a JSON Boolean.");

        // ΑΝΑΓΝΩΣΗ HARQ ON/OFF: gNB[0].characteristics.mac.harq_enabled (Boolean).
        // Εφαρμογή: SetSchedulerAttribute("EnableHarqReTx"); έξοδος: parameters.harq_enabled.
        const bool harqEnabled =
            mac.at("harq_enabled").get<bool>();

        // HARQ: Παράγωγη μεταβλητή, όχι ανεξάρτητο ns-3 attribute.
        // Ο εγκατεστημένος scheduler χρησιμοποιεί το όριο:
        // m_enableHarqReTx ? 3 : 0
        //
        // Δεν διαβάζουμε harq_max_retransmissions από το DAG.
        // Οι τιμές 1 ή 2 απαιτούν αλλαγές μέσα στο NR module.
        // ΠΑΡΑΓΩΓΟ HARQ ΟΡΙΟ: Δεν διαβάζεται ανεξάρτητη τιμή από το JSON.
        // Η έξοδος parameters.harq_max_retransmissions περιγράφει το όριο 0/3, όχι πραγματικές αναμεταδόσεις.
        const uint32_t harqMaxRetransmissions =
            harqEnabled ? 3u : 0u;

        // HARQ: Συνολικές επιτρεπόμενες προσπάθειες ανά transport block:
        // μία αρχική μετάδοση + το όριο αναμεταδόσεων.
        // Αυτό είναι όριο, όχι αριθμός πραγματικών μεταδόσεων.
        const uint32_t harqMaxTransmissionAttempts =
            1u + harqMaxRetransmissions;

        // HARQ: Αριθμός processes από:
        // gNB[0].characteristics.mac.harq_processes
        //
        // Αν το πεδίο απουσιάζει από παλιότερο config, χρησιμοποιούμε 16.
        const json harqProcessesCfg =
        // ΑΝΑΓΝΩΣΗ HARQ PROCESSES: mac.harq_processes, προεπιλογή 16.
        // Εφαρμογή: NumHarqProcess στα δύο MAC; έξοδος: parameters.harq_processes.
            mac.value("harq_processes", json(16));

        Require(harqProcessesCfg.is_number_integer(),
                "harq_processes must be an integer.");

        // HARQ: Εύρος που επιτρέπουμε σε αυτό το scenario: 2..16.
        // Δεν είναι γενικός περιορισμός του προτύπου NR.
        // Στο execution 175, η τιμή 1 προκάλεσε fatal error:
        // "Received feedback for a process which is not active".
        // Την αποκλείουμε μέχρι να διερευνηθεί η αιτία.
        // Οι τιμές 2, 4, 8, 16 ολοκλήρωσαν τη μελέτη με TdmaRR.
        // Οι ενδιάμεσες τιμές δεν έχουν ακόμη δοκιμαστεί.
        Require(harqProcessesCfg >= json(2) &&
                    harqProcessesCfg <= json(16),
                "This scenario requires harq_processes in [2,16]; "
                "1 process is excluded pending investigation of a scheduler failure.");

        const uint32_t harqProcesses =
            harqProcessesCfg.get<uint32_t>();

        // HARQ: Μελέτη OFF/ON -> κρατάμε processes=16 στο config.
        // Μελέτη Processes -> κρατάμε harq_enabled=true στο config.
        //
        // Στην εγκατεστημένη έκδοση το NumHarqProcess χρησιμοποιείται
        // και στη διαχείριση χρονόμετρων HARQ. Η επίδραση της αλλαγής
        // του δεν περιορίζεται μόνο στον αριθμό εκκρεμών blocks.

        // =====================================================================
        // Antenna configuration
        // =====================================================================

        // ΑΝΑΓΝΩΣΗ ANTENNA: gNB[0].characteristics.antenna.
        // Η προεπιλογή ισχύει όταν λείπει όλο το αντικείμενο, όχι για μεμονωμένα πεδία του.
        // ANTENNA: Αν λείπει ολόκληρο το antenna από παλιό config,
        // χρησιμοποιούμε μηδενικό κέρδος προς όλες τις κατευθύνσεις.
        const json antennaCfg =
            gnb.at("characteristics").value(
                "antenna",
                json{
                    {"model", "Cosine"},
                    {"max_gain_dbi", 0.0},
                    {"orientation_deg", 0.0},
                    {"horizontal_beamwidth_deg", 360.0},
                    {"vertical_beamwidth_deg", 360.0}
                });

        // ΑΝΑΓΝΩΣΗ ANTENNA MODEL: antenna.model; επιτρέπεται μόνο Cosine.
        Require(antennaCfg.at("model") == "Cosine",
                "Only Cosine antenna pattern is supported.");

        // ANTENNA: Μέγιστο κέρδος στην κατεύθυνση του κύριου άξονα.
        // ΑΝΑΓΝΩΣΗ GAIN: antenna.max_gain_dbi σε dBi -> CosineAntennaModel.MaxGain.
        // Έξοδος: parameters.gnb_antenna_max_gain_dbi. Δεν μεταβάλλεται η TxPower.
        const double antennaMaxGain =
            antennaCfg.at("max_gain_dbi");

        // ANTENNA: Απόλυτη οριζόντια γωνία ως προς τον άξονα +x.
        // ΑΝΑΓΝΩΣΗ ORIENTATION: antenna.orientation_deg σε μοίρες -> Orientation.
        // Έξοδος: parameters.gnb_antenna_orientation_deg.
        const double antennaOrientation =
            antennaCfg.at("orientation_deg");

        // ANTENNA: Πλήρες οριζόντιο εύρος δέσμης -3 dB.
        // Οι 360° αντιστοιχούν σε σταθερό οριζόντιο κέρδος.
        // ΑΝΑΓΝΩΣΗ HORIZONTAL BEAMWIDTH: antenna.horizontal_beamwidth_deg -> HorizontalBeamwidth.
        // Έξοδος: parameters.gnb_antenna_horizontal_beamwidth_deg.
        const double antennaHorizontalBw =
            antennaCfg.at("horizontal_beamwidth_deg");

        // ΑΝΑΓΝΩΣΗ VERTICAL BEAMWIDTH: antenna.vertical_beamwidth_deg -> VerticalBeamwidth.
        // Στο παρόν scenario απαιτείται 360 μοίρες; έξοδος: parameters.gnb_antenna_vertical_beamwidth_deg.
        const double antennaVerticalBw =
            antennaCfg.at("vertical_beamwidth_deg");

        Require(std::isfinite(antennaMaxGain) &&
                    std::isfinite(antennaOrientation) &&
                    std::isfinite(antennaHorizontalBw) &&
                    std::isfinite(antennaVerticalBw),
                "Antenna parameters must be finite.");

        Require(antennaOrientation >= -360 &&
                    antennaOrientation <= 360,
                "Antenna orientation must be in [-360,360] degrees.");

        Require(antennaHorizontalBw > 0 &&
                    antennaHorizontalBw <= 360,
                "Horizontal beamwidth must be in (0,360] degrees.");

        // ANTENNA: Σταθερό κατακόρυφο διάγραμμα στις παρούσες μελέτες.
        Require(antennaVerticalBw == 360,
                "These horizontal antenna studies require vertical beamwidth = 360 degrees.");

        // =====================================================================
        // Remaining scenario validation
        // =====================================================================

        Require(conn.at("from") == ue.at("id") &&
                    conn.at("to") == cloud.at("id"),
                "The only connection must be UE -> cloud.");

        // ΑΝΑΓΝΩΣΗ TRAFFIC MODEL: connections[0].characteristics.application.traffic_model.
        // Επιλέγει σταθερή αποστολή CBR ή σταθερές/εκθετικές διάρκειες ON/OFF στο OnOffApplication.
        // Έξοδος: parameters.traffic_model και scenario.traffic_model. Το protocol ελέγχεται ως UDP.
        Require(appCfg.at("protocol") == "UDP", "Only UDP is implemented.");
        Require(appCfg.at("traffic_model").is_string(),
                "traffic_model must be a string.");
        const std::string trafficModel = appCfg.at("traffic_model").get<std::string>();
        Require(trafficModel == "CBR" || trafficModel == "OnOffConstant" ||
                    trafficModel == "OnOffExponential",
                "Supported traffic models: CBR, OnOffConstant, OnOffExponential.");
        const bool burstTraffic = trafficModel != "CBR";

        // ΑΝΑΓΝΩΣΗ ON/OFF TIMES: connections[0].characteristics.application.on_time_ms / off_time_ms.
        // Εφαρμόζονται στις OnTime/OffTime μετά από μετατροπή ms σε s. Για CBR δεν χρησιμοποιούνται.
        // Έξοδος: parameters.on_time_ms/off_time_ms (null για CBR), nominal_duty_cycle.
        // TRAFFIC: application.on_time_ms/off_time_ms, σε milliseconds.
        // Constant: ακριβείς διάρκειες. Exponential: μέσες διάρκειες,
        // με ανεξάρτητα RNG streams και χωρίς άνω αποκοπή (Bound=0).
        // Το CBR δεν χρησιμοποιεί αυτά τα πεδία.
        double onTimeMs = 0.0;
        double offTimeMs = 0.0;
        double dutyCycle = 1.0;
        if (burstTraffic)
        {
            Require(!appCfg.contains("packet_interval_us"),
                    "For On/Off remove packet_interval_us and specify the ON dataRate.");
            Require(appCfg.at("on_time_ms").is_number() &&
                        appCfg.at("off_time_ms").is_number(),
                    "On/Off durations must be JSON numbers.");
            onTimeMs = appCfg.at("on_time_ms").get<double>();
            offTimeMs = appCfg.at("off_time_ms").get<double>();
            Require(std::isfinite(onTimeMs) && std::isfinite(offTimeMs) &&
                        onTimeMs > 0 && offTimeMs > 0 &&
                        std::isfinite(onTimeMs + offTimeMs),
                    "On/Off durations must be finite and positive.");
            Require(onTimeMs / 1000 >= TimeStep(1).GetSeconds() &&
                        offTimeMs / 1000 >= TimeStep(1).GetSeconds(),
                    "On/Off durations must be at least one simulation time step.");
            dutyCycle = onTimeMs / (onTimeMs + offTimeMs);
        }

        Require(ch.at("pathloss_model") == "Friis" &&
                    !ch.at("shadowing_enabled").get<bool>() &&
                    !ch.at("fading_enabled").get<bool>(),
                "This baseline requires Friis without shadowing or fading.");
        // ΑΝΑΓΝΩΣΗ RLC MODE: gNB[0].characteristics.rlc.mode -> rlcMode.
        // Εφαρμογή: QosFlowToRlcMapping; έξοδος: parameters.rlc_mode και scenario.rlc_mapping/rlc_type_id.
        // RLC: Το gNB[0].characteristics.rlc.mode επιλέγει UM ή AM.
        // Η επιλογή εφαρμόζεται στο RRC mapping πριν δημιουργηθούν devices.
        Require(rlc.at("mode").is_string(),
                "RLC mode must be a JSON string.");
        const std::string rlcMode = rlc.at("mode").get<std::string>();
        Require(rlcMode == "UM" || rlcMode == "AM",
                "Supported RLC modes: UM, AM.");

        // RLC: Κοινή αριθμητική τιμή MaxTxBufferSize για UM και AM, σε bytes.
        // Απαιτούμε θετικό uint32 ώστε να μη συγκρίνουμε πεπερασμένο buffer
        // με την ειδική σημασία του μηδενός στο AM (unlimited).
        // ΑΝΑΓΝΩΣΗ RLC BUFFER: gNB[0].characteristics.rlc.buffer_size_bytes σε bytes.
        // Εφαρμογή: MaxTxBufferSize για NrRlcUm και NrRlcAm; έξοδος: parameters.rlc_buffer_size_bytes.
        const auto& rlcBufferCfg = rlc.at("buffer_size_bytes");
        Require(rlcBufferCfg.is_number_integer() &&
                    rlcBufferCfg > json(0) &&
                    rlcBufferCfg <= json(4294967295ULL),
                "RLC buffer_size_bytes must be an integer in [1,4294967295].");
        const uint32_t rlcBufferSize = rlcBufferCfg.get<uint32_t>();
        const std::string rlcMapping =
            rlcMode == "UM" ? "RlcUmAlways" : "RlcAmAlways";
        const std::string rlcTypeId =
            rlcMode == "UM" ? "ns3::NrRlcUm" : "ns3::NrRlcAm";

        // ΑΝΑΓΝΩΣΗ ΧΡΟΝΩΝ: simulation.app_start_s, app_stop_s, drain_time_s σε s.
        // Η πηγή λειτουργεί στο [start, stop). Η προσομοίωση/λήψη συνεχίζεται έως stop+drain.
        // Οι ρυθμοί διαιρούνται με stop-start, παρότι τα received counters περιλαμβάνουν το drain.
        const double start = sim.at("app_start_s");
        const double stop = sim.at("app_stop_s");
        const double drain = sim.at("drain_time_s");

        Require(start > 0 && stop > start && drain > 0,
                "Invalid application/drain times.");

        const double end = stop + drain;
        // ΑΝΑΓΝΩΣΗ CARRIER FREQUENCY: channel.carrier_frequency_hz σε Hz -> freq.
        // Εφαρμογή: Friis.Frequency και SimpleOperationBandConf; έξοδος: parameters.carrier_frequency_hz.
        const double freq = ch.at("carrier_frequency_hz");
        // ΑΝΑΓΝΩΣΗ BANDWIDTH: gNB[0].characteristics.phy.channel_bandwidth σε Hz -> bw.
        // Εφαρμογή: SimpleOperationBandConf; έξοδος: parameters.channel_bandwidth_hz.
        // Το n8n χρησιμοποιεί το όνομα bandwidth_hz, οπότε χρειάζεται αντιστοίχιση στην εξαγωγή CSV.
        const double bw = gp.at("channel_bandwidth");

        Require(freq > 0 && bw > 0 && freq > bw / 2,
                "Invalid frequency/bandwidth.");

        // ΑΝΑΓΝΩΣΗ NUMEROLOGY: gNB[0].characteristics.phy.numerology -> mu.
        // Εφαρμογή: gNB PHY Numerology; έξοδος: parameters.numerology.
        const uint32_t mu = gp.at("numerology");
        Require(mu <= 4,
                "Numerology must be in [0,4] for this baseline.");

        // ΑΝΑΓΝΩΣΗ TDD: gNB[0].characteristics.phy.pattern -> pattern.
        // Εφαρμογή: gNB PHY Pattern; έξοδος: parameters.tdd_pattern.
        const std::string pattern = gp.at("pattern");
        std::istringstream tokens(pattern);
        std::string token;
        bool hasDl = false;
        bool hasUl = false;

        while (std::getline(tokens, token, '|'))
        {
            Require(token == "DL" || token == "UL" ||
                        token == "F" || token == "S",
                    "Invalid TDD slot token.");

            hasDl |= token == "DL" || token == "F" || token == "S";
            hasUl |= token == "UL" || token == "F";
        }

        Require(hasDl && hasUl,
                "TDD requires DL control and UL data opportunities.");

        // PACKET INTERVAL: Το packetSize παραμένει payload σε bytes.
        // ΑΝΑΓΝΩΣΗ PACKET SIZE: connections[0].characteristics.application.packetSize σε payload bytes.
        // Εφαρμογή: source.SetConstantRate(offered, bytes); έξοδος: parameters.packet_size_bytes.
        // Με σταθερό interval, μεγαλύτερο payload αυξάνει και το προσφερόμενο φορτίο.
        const auto& packetSizeCfg = appCfg.at("packetSize");
        Require(packetSizeCfg.is_number_integer() &&
                    packetSizeCfg >= json(1) && packetSizeCfg <= json(1400),
                "packetSize must be an integer in [1,1400] bytes.");
        const uint32_t bytes = packetSizeCfg.get<uint32_t>();
        // ΑΝΑΓΝΩΣΗ MTU: core.mtu_bytes -> MTU των συνδέσεων S1-U και cloud.
        const uint32_t mtu = core.at("mtu_bytes");
        Require(mtu >= 1500, "Use core MTU >=1500 to avoid fragmentation.");

        // PACKET INTERVAL: Προαιρετικό πεδίο στο
        // connections[0].characteristics.application.packet_interval_us.
        // Μόνο για CBR: αν υπάρχει, έχει προτεραιότητα και το dataRate αγνοείται.
        // Για On/Off το πεδίο απαγορεύεται (ακόμη και null): χρησιμοποιείται dataRate.
        // ΑΝΑΓΝΩΣΗ PACKET INTERVAL: appCfg.packet_interval_us σε μs, μόνο για CBR.
        // Εφαρμογή μέσω υπολογισμού DataRate και SetConstantRate, όχι ανεξάρτητου timer attribute.
        // Έξοδος: parameters.packet_interval_requested_us, packet_interval_us, traffic_input_mode.
        const bool intervalConfigured = appCfg.contains("packet_interval_us");
        json requestedPacketIntervalUs = nullptr;
        DataRate offered;
        if (intervalConfigured)
        {
            Require(appCfg.at("packet_interval_us").is_number(),
                    "packet_interval_us must be a JSON number.");
            const double intervalUs = appCfg.at("packet_interval_us").get<double>();
            Require(std::isfinite(intervalUs) && intervalUs > 0,
                    "packet_interval_us must be finite and positive.");
            requestedPacketIntervalUs = intervalUs;

            // PACKET INTERVAL: R = payload_bytes * 8 * 1e6 / interval_us.
            // Το DataRate αποθηκεύει ακέραια bps: στρογγυλοποίηση στο πλησιέστερο.
            const double roundedBps = std::round(8.0 * bytes * 1e6 / intervalUs);
            Require(std::isfinite(roundedBps) && roundedBps >= 1.0 &&
                        roundedBps < std::ldexp(1.0, 64),
                    "packet_interval_us produces a rate outside uint64 bps.");
            offered = DataRate(static_cast<uint64_t>(roundedBps));
        }
        else
        {
        // ΑΝΑΓΝΩΣΗ DATA RATE: connections[0].characteristics.application.dataRate, π.χ. "200Mbps".
        // Αυτός είναι ο ρυθμός ON. Ο ονομαστικός μέσος ρυθμός είναι ON rate * dutyCycle.
        // Έξοδος: parameters.data_rate_bps/on_data_rate_bps/nominal_mean_offered_load_bps.
        // Το πραγματικό φορτίο μετριέται χωριστά από το Tx trace στο application.actual_offered_load_mbps.
            offered = DataRate(appCfg.at("dataRate").get<std::string>());
        }
        Require(offered.GetBitRate() > 0, "Application data rate must be positive.");

        // PACKET INTERVAL: Ονομαστικό interval από τον εφαρμοζόμενο ρυθμό.
        // Δεν είναι μέτρηση από Tx traces. Το ns-3 κβαντίζει τον χρόνο
        // σύμφωνα με το Time resolution κατά τον προγραμματισμό events.
        const double packetIntervalS = 8.0 * bytes / offered.GetBitRate();
        const double packetIntervalUs = packetIntervalS * 1e6;
        Require(std::isfinite(packetIntervalS) &&
                    packetIntervalS >= TimeStep(1).GetSeconds(),
                "Packet interval is below the simulation time resolution.");

        // ΑΝΑΓΝΩΣΗ SERVICE: application.point_to_service αντιστοιχίζεται σε cloud.services[].id/port.
        const uint32_t port =
            getServicePort(cloud, appCfg.at("point_to_service"));

        // ΑΝΑΓΝΩΣΗ ΘΕΣΗΣ gNB: gNB[0].position=[x,y,z] σε m -> ConstantPositionMobilityModel.
        const auto pos =
            gnb.at("position").get<std::vector<double>>();

        Require(pos.size() == 3,
                "gNB position requires [x,y,z] in metres.");

        // ΑΝΑΓΝΩΣΗ UE DISTANCE: ue[0].placement.distance_2d_m σε m.
        // height_m είναι απόλυτο ύψος z σε m, azimuth_deg οριζόντια γωνία θέσης ως προς +x.
        // Το ue[0].position δεν διαβάζεται: η θέση παράγεται παρακάτω από το placement και τη θέση gNB.
        // Έξοδος: parameters.ue_gnb_distance_2d_m/ue_gnb_distance_3d_m και derived.distance_3d_m.
        // Η γεωμετρία επηρεάζει Friis/κατεύθυνση κεραίας. Δεν έχει προστεθεί ξεχωριστό radio propagation delay model.
        const auto& placement = ue.at("placement");
        const double distance2d = placement.at("distance_2d_m");
        const double height = placement.at("height_m");
        const double az =
            placement.at("azimuth_deg").get<double>() *
            std::acos(-1.0) / 180;

        const double distance3d =
            std::hypot(distance2d, height - pos[2]);

        Require(distance2d > 0 &&
                    distance3d > 3 * 299792458.0 / freq,
                "Distance must be positive and in the Friis far-field region.");

        Require(core.at("s1u_delay_ms").get<double>() >= 0 &&
                    core.at("cloud_delay_ms").get<double>() >= 0,
                "Negative link delay.");

        // =====================================================================
        // Simulation randomness
        // =====================================================================

        // ΑΝΑΓΝΩΣΗ RNG: simulation.seed / simulation.run -> RngSeedManager πριν τη δημιουργία μοντέλων.
        // Τα AssignStreams παρακάτω ορίζουν ροές τυχαίων αριθμών για NR και για On/Off.
        // Το results.json δεν εξάγει seed/run: το n8n πρέπει να τα κρατά από το Create configuration.
        RngSeedManager::SetSeed(sim.at("seed").get<uint32_t>());
        RngSeedManager::SetRun(sim.at("run").get<uint64_t>());

        // =====================================================================
        // RLC configuration
        // =====================================================================

        // RLC: Το RRC επιλέγει NrRlcUm ή NrRlcAm για τα data bearers.
        // Η ρύθμιση προηγείται της δημιουργίας devices και του attachment.
        Config::SetDefault(
            "ns3::NrGnbRrc::QosFlowToRlcMapping",
            StringValue(rlcMapping));

        // RLC: Ρυθμίζουμε και τους δύο τύπους με την ίδια τιμή buffer.
        // Τα defaults εφαρμόζονται στις νέες entities και στα δύο άκρα.
        // Στο AM το όριο αφορά τον Txon buffer νέων δεδομένων,
        // όχι τη συνολική μνήμη μαζί με retransmission/receive buffers.
        Config::SetDefault(
            "ns3::NrRlcUm::MaxTxBufferSize",
            UintegerValue(rlcBufferSize));
        Config::SetDefault(
            "ns3::NrRlcAm::MaxTxBufferSize",
            UintegerValue(rlcBufferSize));

        // RLC: Δεν εκθέτουμε AM maxRetxThreshold ή SDU Discard Timer στη μελέτη.
        // Στην εγκατεστημένη υλοποίηση που ελέγχθηκε, το AM threshold είναι 5:
        // η υπέρβασή του προκαλεί log, όχι διακοπή αναμεταδόσεων ή απόρριψη.
        // Στο UM, ο κλάδος υπέρβασης HOL καλεί TxDrop αλλά κατόπιν εισάγει
        // το πακέτο στο buffer. Ο χωριστός κλάδος γεμάτου buffer απορρίπτει κανονικά.
        // Γι αυτό δεν έγιναν sweeps αυτών των δύο παραμέτρων. Απαιτείται πρώτα
        // επέκταση/διόρθωση και επαλήθευση του NR module για αυτές τις λειτουργίες.

        // =====================================================================
        // Create nodes and install mobility
        // =====================================================================

        NodeContainer gnbNodes;
        NodeContainer ueNodes;
        NodeContainer cloudNodes;

        gnbNodes.Create(1);
        ueNodes.Create(1);
        cloudNodes.Create(1);

        InstallGnbNodes(gnbNodes, pos);

        InstallUeNodes(
            ueNodes,
            Vector(pos[0] + distance2d * std::cos(az),
                   pos[1] + distance2d * std::sin(az),
                   height));

        // =====================================================================
        // EPC and NR module setup
        // =====================================================================

        auto nrEpcHelper = CreateObject<NrPointToPointEpcHelper>();

        // ΑΝΑΓΝΩΣΗ CORE S1-U: core.s1u_data_rate (DataRate string), s1u_delay_ms (ms -> s).
        // Εφαρμογή στα S1uLinkDataRate/S1uLinkDelay του EPC helper.
        nrEpcHelper->SetAttribute(
            "S1uLinkDataRate",
            DataRateValue(
                DataRate(core.at("s1u_data_rate").get<std::string>())));

        nrEpcHelper->SetAttribute(
            "S1uLinkDelay",
            TimeValue(
                Seconds(core.at("s1u_delay_ms").get<double>() / 1000)));

        nrEpcHelper->SetAttribute("S1uLinkMtu", UintegerValue(mtu));

        auto nrHelper = CreateObject<NrHelper>();
        nrHelper->SetEpcHelper(nrEpcHelper);

        // =====================================================================
        // Scalar Friis and reciprocal gNB antenna gain
        // =====================================================================

        auto channel = CreateObject<MultiModelSpectrumChannel>();
        auto loss = CreateObject<FriisPropagationLossModel>();
        loss->SetAttribute("Frequency", DoubleValue(freq));

        // ANTENNA: Αντιστοίχιση πεδίων JSON στα attributes του διαγράμματος.
        // MaxGain και beamwidth είναι ανεξάρτητες παράμετροι του μοντέλου.
        auto gnbPattern = CreateObject<CosineAntennaModel>();
        gnbPattern->SetAttribute(
            "MaxGain", DoubleValue(antennaMaxGain));
        gnbPattern->SetAttribute(
            "Orientation", DoubleValue(antennaOrientation));
        gnbPattern->SetAttribute(
            "HorizontalBeamwidth", DoubleValue(antennaHorizontalBw));
        gnbPattern->SetAttribute(
            "VerticalBeamwidth", DoubleValue(antennaVerticalBw));

        auto gnbMobility =
            gnbNodes.Get(0)->GetObject<MobilityModel>();
        auto ueMobility =
            ueNodes.Get(0)->GetObject<MobilityModel>();

        auto antennaGain = CreateObject<ScenarioGnbAntennaGainModel>();
        antennaGain->Configure(gnbMobility, ueMobility, gnbPattern);

        // ANTENNA: Στατική τοπολογία, σταθερό κέρδος σε όλο το run.
        const double effectiveGnbGain =
            antennaGain->GetGainTowardUeDb();

        const double friisReferenceRx =
            loss->CalcRxPower(0.0, ueMobility, gnbMobility);

        // ANTENNA: Friis -> κέρδος διαγράμματος, μία εφαρμογή ανά ζεύξη.
        loss->SetNext(antennaGain);

        // ANTENNA: Τα 0 dBm είναι μόνο τιμή αναφοράς για τον έλεγχο.
        // Δεν αλλάζουν την πραγματική ισχύ εκπομπής των PHY.
        const double ulReferenceRx =
            loss->CalcRxPower(0.0, ueMobility, gnbMobility);
        const double dlReferenceRx =
            loss->CalcRxPower(0.0, gnbMobility, ueMobility);

        Require(
            std::abs(ulReferenceRx -
                     (friisReferenceRx + effectiveGnbGain)) < 1e-9 &&
                std::abs(ulReferenceRx - dlReferenceRx) < 1e-9,
            "Antenna gain chain verification failed.");

        channel->AddPropagationLossModel(loss);

        // Match the slot-synchronous channel used by the baseline.
        // No separate radio propagation-delay model is installed.
        // Distance still controls Friis attenuation.

        // =====================================================================
        // Spectrum / BWP configuration (bandwidth)
        // =====================================================================

        CcBwpCreator ccBwpCreator;

        auto bandInfo =
            ccBwpCreator.CreateOperationBandContiguousCc(
                CcBwpCreator::SimpleOperationBandConf(freq, bw, 1));

        auto allBwps = CcBwpCreator::GetAllBwps({bandInfo});

        for (auto& bwp : allBwps)
        {
            bwp.get()->SetChannel(channel);
        }

        // =====================================================================
        // MAC, HARQ, PHY and antenna parameters
        // =====================================================================

        // ΑΝΑΓΝΩΣΗ SCHEDULER: gNB[0].characteristics.mac.scheduler_type.
        // Εφαρμογή: SetSchedulerTypeId; έξοδος: parameters.scheduler_type και scenario.scheduler_type_id.
        // SCHEDULER: Ανάγνωση από gNB[0].characteristics.mac.scheduler_type.
        // RR = Round Robin, PF = Proportional Fair, MR = Maximum Rate.
        // Κρατάμε την οικογένεια TDMA σταθερή στη συγκεκριμένη σύγκριση.
        Require(mac.at("scheduler_type").is_string(),
                "scheduler_type must be a JSON string.");
        const std::string schedulerType =
            mac.at("scheduler_type").get<std::string>();
        Require(schedulerType == "TdmaRR" ||
                    schedulerType == "TdmaPF" ||
                    schedulerType == "TdmaMR" ||
                    schedulerType == "TdmaQos" ||
                    schedulerType == "TdmaRandom",
                "Supported schedulers: TdmaRR, TdmaPF, TdmaMR, "
                "TdmaQos, TdmaRandom.");

        // SCHEDULER: Κατασκευή του πλήρους ns-3 TypeId και επιλογή του
        // πριν δημιουργηθεί το gNB device. Το ίδιο όνομα καταγράφεται παρακάτω.
        const std::string schedulerTypeId = "ns3::NrMacScheduler" + schedulerType;
        nrHelper->SetSchedulerTypeId(TypeId::LookupByName(schedulerTypeId));

        // SCHEDULER: TdmaQos και TdmaRandom χρησιμοποιούν τα defaults της έκδοσης.
        // Δεν αλλάζουμε bearer/QoS profile ή τον αλγόριθμο κατανομής μεταξύ LCs.
        // Το υπάρχον AssignStreams παρακάτω καλύπτει και τον Random scheduler.

        // HARQ: Στη μελέτη Scheduler type το config πρέπει να κρατά
        // harq_enabled=true και harq_processes=16 σε όλα τα πειράματα.
        // Δεν τα επιβάλλουμε καθολικά εδώ, ώστε να παραμένουν δυνατές
        // οι ξεχωριστές μελέτες HARQ μέσω των αντίστοιχων config.

        // HARQ: Το Boolean εφαρμόζεται στον scheduler μέσω του helper.
        // Πρέπει να προηγείται του InstallGnbDevice.
        nrHelper->SetSchedulerAttribute(
            "EnableHarqReTx",
            BooleanValue(harqEnabled));

        // HARQ: Εφαρμόζουμε τον ίδιο αριθμό processes στα MAC gNB και UE
        // πριν από InstallGnbDevice / InstallUeDevice.
        //
        // Ο scheduler παίρνει τον αριθμό από το gNB MAC και χρησιμοποιεί
        // αυτή την τιμή για τις δομές HARQ ανά UE, τόσο σε UL όσο και σε DL.
        // Το UE MAC χρησιμοποιεί την τιμή για τους UL HARQ buffers.
        nrHelper->SetGnbMacAttribute(
            "NumHarqProcess",
            UintegerValue(harqProcesses));

        nrHelper->SetUeMacAttribute(
            "NumHarqProcess",
            UintegerValue(harqProcesses));

        nrHelper->SetGnbPhyAttribute(
            "Numerology", UintegerValue(mu));
        nrHelper->SetGnbPhyAttribute(
            "Pattern", StringValue(pattern));
        // ΑΝΑΓΝΩΣΗ gNB TX POWER: gNB[0].characteristics.phy.tx_power σε dBm -> gNB PHY TxPower.
        nrHelper->SetGnbPhyAttribute(
            "TxPower", DoubleValue(Power(gp.at("tx_power"))));
        // ΑΝΑΓΝΩΣΗ UE TX POWER: ue[0].characteristics.phy.tx_power σε dBm -> UE PHY TxPower.
        // Η Power() δέχεται αριθμό ή αριθμητικό string. Έξοδος: parameters.ue_tx_power_dbm.
        // Στη μελέτη ισχύος κρατάμε uplink_power_control_enabled=false.
        nrHelper->SetUePhyAttribute(
            "TxPower", DoubleValue(Power(up.at("tx_power"))));
        // ΑΝΑΓΝΩΣΗ NOISE FIGURES: gNB/ue[0].characteristics.phy.noise_figure_db σε dB -> NoiseFigure.
        nrHelper->SetGnbPhyAttribute(
            "NoiseFigure", DoubleValue(gp.at("noise_figure_db")));
        nrHelper->SetUePhyAttribute(
            "NoiseFigure", DoubleValue(up.at("noise_figure_db")));
        // ΑΝΑΓΝΩΣΗ UL POWER CONTROL: ue[0].characteristics.phy.uplink_power_control_enabled.
        // Εφαρμογή: UE PHY EnableUplinkPowerControl (Boolean).
        nrHelper->SetUePhyAttribute(
            "EnableUplinkPowerControl",
            BooleanValue(
                up.at("uplink_power_control_enabled").get<bool>()));

        // ANTENNA: Πραγματικά NR panels 1x1 isotropic.
        // Το διάγραμμα gNB εφαρμόζεται μόνο στην propagation chain.
        nrHelper->SetGnbAntennaAttribute(
            "NumRows", UintegerValue(1));
        nrHelper->SetGnbAntennaAttribute(
            "NumColumns", UintegerValue(1));
        nrHelper->SetGnbAntennaAttribute(
            "IsDualPolarized", BooleanValue(false));
        nrHelper->SetGnbAntennaAttribute(
            "AntennaElement",
            PointerValue(CreateObject<IsotropicAntennaModel>()));

        nrHelper->SetUeAntennaAttribute(
            "NumRows", UintegerValue(1));
        nrHelper->SetUeAntennaAttribute(
            "NumColumns", UintegerValue(1));
        nrHelper->SetUeAntennaAttribute(
            "IsDualPolarized", BooleanValue(false));
        nrHelper->SetUeAntennaAttribute(
            "AntennaElement",
            PointerValue(CreateObject<IsotropicAntennaModel>()));

        // =====================================================================
        // Internet stack, NR devices and random streams
        // =====================================================================

        InternetStackHelper internet;
        internet.Install(cloudNodes);
        internet.Install(ueNodes);

        auto gnbNetDev =
            nrHelper->InstallGnbDevice(gnbNodes, allBwps);
        auto ueNetDev =
            nrHelper->InstallUeDevice(ueNodes, allBwps);

        int64_t stream = 1;
        stream += nrHelper->AssignStreams(gnbNetDev, stream);
        stream += nrHelper->AssignStreams(ueNetDev, stream);

        // =====================================================================
        // Cloud link, IP addressing and routing
        // =====================================================================

        PointToPointHelper p2p;

        // ΑΝΑΓΝΩΣΗ CORE CLOUD: core.cloud_data_rate και cloud_delay_ms -> σύνδεση προς cloud.
        // Η καθυστέρηση μετατρέπεται από ms σε s; το MTU έχει ήδη διαβαστεί από core.mtu_bytes.
        p2p.SetDeviceAttribute(
            "DataRate",
            DataRateValue(
                DataRate(core.at("cloud_data_rate").get<std::string>())));

        p2p.SetDeviceAttribute("Mtu", UintegerValue(mtu));

        p2p.SetChannelAttribute(
            "Delay",
            TimeValue(
                Seconds(core.at("cloud_delay_ms").get<double>() / 1000)));

        auto backhaul =
            p2p.Install(nrEpcHelper->GetPgwNode(), cloudNodes.Get(0));

        Ipv4AddressHelper addr;
        addr.SetBase("1.0.0.0", "255.0.0.0");

        auto cloudIps = addr.Assign(backhaul);
        auto ueIps = nrEpcHelper->AssignUeIpv4Address(ueNetDev);

        Ipv4StaticRoutingHelper routing;

        routing.GetStaticRouting(
            cloudNodes.Get(0)->GetObject<Ipv4>())
            ->AddNetworkRouteTo(
                Ipv4Address("7.0.0.0"),
                Ipv4Mask("255.0.0.0"),
                1);

        routing.GetStaticRouting(
            ueNodes.Get(0)->GetObject<Ipv4>())
            ->SetDefaultRoute(
                nrEpcHelper->GetUeDefaultGatewayAddress(),
                1);

        // =====================================================================
        // UE attachment
        // =====================================================================

        nrHelper->AttachToClosestGnb(ueNetDev, gnbNetDev);

        // =====================================================================
        // Application layer
        // =====================================================================

        PacketSinkHelper sinkHelper(
            "ns3::UdpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), port));

        auto sinks = sinkHelper.Install(cloudNodes.Get(0));
        sinks.Start(Seconds(0));
        sinks.Stop(Seconds(end));

        OnOffHelper source(
            "ns3::UdpSocketFactory",
            InetSocketAddress(cloudIps.GetAddress(1), port));

        // TRAFFIC: SetConstantRate κρατά την προηγούμενη συμπεριφορά CBR:
        // OnTime=1000 s, OffTime=0. Το παράθυρο της μελέτης είναι 10 s.
        // Για On/Off αντικαθιστούμε τις δύο μεταβλητές διάρκειας παρακάτω.
        // Το offered/data_rate_bps είναι ΠΑΝΤΑ ο ρυθμός κατά το ON.
        source.SetConstantRate(offered, bytes);
        source.SetAttribute("EnableSeqTsSizeHeader", BooleanValue(false));
        if (burstTraffic)
        {
            Ptr<RandomVariableStream> onRv;
            Ptr<RandomVariableStream> offRv;
            if (trafficModel == "OnOffConstant")
            {
                auto on = CreateObject<ConstantRandomVariable>();
                auto off = CreateObject<ConstantRandomVariable>();
                on->SetAttribute("Constant", DoubleValue(onTimeMs / 1000));
                off->SetAttribute("Constant", DoubleValue(offTimeMs / 1000));
                onRv = on;
                offRv = off;
            }
            else
            {
                auto on = CreateObject<ExponentialRandomVariable>();
                auto off = CreateObject<ExponentialRandomVariable>();
                on->SetAttribute("Mean", DoubleValue(onTimeMs / 1000));
                off->SetAttribute("Mean", DoubleValue(offTimeMs / 1000));
                on->SetAttribute("Bound", DoubleValue(0));
                off->SetAttribute("Bound", DoubleValue(0));
                onRv = on;
                offRv = off;
            }
            source.SetAttribute("OnTime", PointerValue(onRv));
            source.SetAttribute("OffTime", PointerValue(offRv));
        }

        auto sources = source.Install(ueNodes.Get(0));
        auto trafficApp = DynamicCast<OnOffApplication>(sources.Get(0));
        Require(trafficApp != nullptr, "Expected OnOffApplication.");
        // TRAFFIC: Streams μετά τα NR streams, ώστε να μην επικαλύπτονται.
        // seed/run επιλέγουν την αναπαραγώγιμη τυχαία ακολουθία.
        // Δεν αλλάζουμε την παλιά ανάθεση streams για CBR.
        const int64_t trafficStreamStart = burstTraffic ? stream : -1;
        if (burstTraffic)
        {
            stream += trafficApp->AssignStreams(stream);
        }
        ApplicationTxCounters applicationTx;
        Require(trafficApp->TraceConnectWithoutContext(
                    "Tx", MakeCallback(&ApplicationTxCounters::OnTx, &applicationTx)),
                "Could not connect application Tx trace.");
        // TRAFFIC: Η εφαρμογή ξεκινά με OFF. Διατηρεί residual bits
        // μεταξύ ON περιόδων. Δεν πρόκειται για Poisson packet arrivals.
        const double nominalMeanLoadBps = offered.GetBitRate() * dutyCycle;
        sources.Start(Seconds(start));
        sources.Stop(Seconds(stop));

        // =====================================================================
        // Flow monitor and simulation
        // =====================================================================

        FlowMonitorHelper flowHelper;
        NodeContainer endpoints;
        endpoints.Add(ueNodes);
        endpoints.Add(cloudNodes);

        auto monitor = flowHelper.Install(endpoints);
        Simulator::Stop(Seconds(end));

        std::cout
            << "[SETUP] 1 UE, 1 gNB, 1 UDP UL flow; Friis, no fading/shadowing\n"
            << "[SETUP] distance2d=" << distance2d
            << " m; distance3d=" << distance3d
            << " m; packet interval="
            << packetIntervalS
            << " s\n";

        std::cout
            << "[ANTENNA] model=Cosine; maxGain=" << antennaMaxGain
            << " dBi; orientation=" << antennaOrientation
            << " deg; horizontalBeamwidth=" << antennaHorizontalBw
            << " deg; effectiveGainTowardUe=" << effectiveGnbGain
            << " dB\n";

        // HARQ: Εμφάνιση των ρυθμίσεων που δόθηκαν στον helper.
        // Δεν είναι counters πραγματικών αναμεταδόσεων ή ενεργών processes.
        std::cout
            << "[HARQ] retransmissionsEnabled="
            << (harqEnabled ? "true" : "false")
            << "; maxRetransmissions=" << harqMaxRetransmissions
            << "; maxTransmissionAttempts=" << harqMaxTransmissionAttempts
            << "; processes=" << harqProcesses
            << "\n";

        // SCHEDULER: Εμφάνιση της επιλογής που δόθηκε στον NR helper.
        std::cout << "[SCHEDULER] type=" << schedulerType
                  << "; typeId=" << schedulerTypeId
                  << "\n";

        // RLC: Καταγραφή των επιλεγμένων ρυθμίσεων, όχι counters ή
        // runtime επιβεβαίωση του τύπου κάθε δημιουργημένης entity.
        std::cout << "[RLC] mode=" << rlcMode
                  << "; mapping=" << rlcMapping
                  << "; typeId=" << rlcTypeId
                  << "; maxTxBufferSizeBytes=" << rlcBufferSize
                  << "\n";

        std::cout << "[TRAFFIC] input="
                  << (intervalConfigured ? "packet_interval_us" : "dataRate")
                  << "; requestedIntervalUs=" << requestedPacketIntervalUs
                  << "; nominalIntervalUs=" << packetIntervalUs
                  << "; model=" << trafficModel
                  << "; onRateBps=" << offered.GetBitRate()
                  << "; nominalMeanLoadBps=" << nominalMeanLoadBps
                  << "; onTimeMs=" << onTimeMs
                  << "; offTimeMs=" << offTimeMs
                  << "\n";

        Simulator::Run();

        // =====================================================================
        // Results collection and JSON output
        // =====================================================================

        monitor->CheckForLostPackets();

        const json flows =
            CollectFlowMonitorStats(
                flowHelper,
                monitor,
                ueIps.GetAddress(0),
                cloudIps.GetAddress(1),
                port,
                start,
                stop);

        // ΜΕΤΡΗΣΗ GOODPUT: PacketSink payload bytes στον cloud, χωρίς IP/UDP headers.
        // Το FlowMonitor παραπάνω μετρά IP bytes: γι αυτό το IP throughput διαφέρει από το goodput.
        const uint64_t payloadRx =
            DynamicCast<PacketSink>(sinks.Get(0))->GetTotalRx();

        json result = {
            {"flow_count", flows.size()},
            {"flows", flows},
            {"derived", {
                {"distance_3d_m", distance3d},
                {"packet_interval_s", packetIntervalS},
                {"packet_interval_us", packetIntervalUs}
            }},
            {"application", {
                {"rx_payload_bytes", payloadRx},
                {"tx_payload_bytes", applicationTx.bytes},
                {"tx_packets", applicationTx.packets},
                // Πραγματικό μέσο φορτίο στο πλήρες application window,
                // με τις παύσεις και το αρχικό OFF μέσα στον παρονομαστή.
                {"actual_offered_load_mbps",
                 applicationTx.bytes * 8.0 / (stop - start) / 1e6},
                {"goodput_over_app_window_mbps",
                 payloadRx * 8.0 / (stop - start) / 1e6}
            }},
            {"measurement_note",
             "Received counters include drain time; throughput/goodput "
             "denominator is app_stop_s-app_start_s. Unreceived packets "
             "may still be queued if drain is insufficient. Delay/jitter "
             "are IP-level for received packets. No UL SINR is reported "
             "from DL traces."}
        };

        // ΕΞΟΔΟΣ ΠΑΡΑΜΕΤΡΩΝ: Οι τιμές αυτές περιγράφουν το configuration που εφαρμόστηκε.
        // Στο n8n: Read results -> Prepare dataset row αντιστοιχίζει parameters/application/mean σε στήλες CSV.
        // Τα paths και experiment_id/index προέρχονται από τα metadata του Create configuration.
        result["parameters"] = {
            // SCHEDULER: Σύντομο όνομα για αντιστοίχιση με n8n/config/CSV.
            {"scheduler_type", schedulerType},
            // HARQ: Αποθήκευση των ρυθμίσεων για ελέγχους n8n και CSV.
            // Το harq_max_retransmissions είναι το όριο 0/3 της έκδοσης,
            // όχι ο μετρημένος αριθμός αναμεταδόσεων.
            {"harq_enabled", harqEnabled},
            {"harq_max_retransmissions", harqMaxRetransmissions},
            {"harq_processes", harqProcesses},

            // ANTENNA: Ρυθμίσεις του scalar διαγράμματος.
            {"gnb_antenna_model", "Cosine"},
            {"gnb_antenna_max_gain_dbi", antennaMaxGain},
            {"gnb_antenna_orientation_deg", antennaOrientation},
            {"gnb_antenna_horizontal_beamwidth_deg", antennaHorizontalBw},
            {"gnb_antenna_vertical_beamwidth_deg", antennaVerticalBw},

            // ANTENNA: Υπολογισμένο κέρδος προς το συγκεκριμένο UE.
            {"gnb_antenna_effective_gain_toward_ue_db", effectiveGnbGain},

            {"carrier_frequency_hz", freq},
            {"channel_bandwidth_hz", bw},
            {"ue_gnb_distance_2d_m", distance2d},
            {"ue_gnb_distance_3d_m", distance3d},
            {"numerology", mu},
            {"tdd_pattern", pattern},
            // Το legacy data_rate_bps παραμένει ο ρυθμός ON.
            {"data_rate_bps", offered.GetBitRate()},
            {"traffic_model", trafficModel},
            {"on_data_rate_bps", offered.GetBitRate()},
            {"nominal_mean_offered_load_bps", nominalMeanLoadBps},
            {"nominal_duty_cycle", dutyCycle},
            {"on_time_ms", burstTraffic ? json(onTimeMs) : json(nullptr)},
            {"off_time_ms", burstTraffic ? json(offTimeMs) : json(nullptr)},
            {"traffic_rng_stream_start",
             burstTraffic ? json(trafficStreamStart) : json(nullptr)},
            // PACKET INTERVAL: null requested όταν χρησιμοποιείται το dataRate.
            {"traffic_input_mode", intervalConfigured ? "packet_interval_us" : "dataRate"},
            {"packet_interval_requested_us", requestedPacketIntervalUs},
            {"packet_interval_us", packetIntervalUs},
            {"packet_size_bytes", bytes},
            {"ue_tx_power_dbm", Power(up.at("tx_power"))},
            // RLC: Ίδια ονόματα για αντιστοίχιση config / n8n / CSV.
            {"rlc_mode", rlcMode},
            {"rlc_buffer_size_bytes", rlcBufferSize}
        };

        result["scenario"] = {
            {"traffic_model_note",
             "OnOff starts in OFF. Constant durations are fixed; exponential "
             "durations are independent, unbounded draws with configured means. "
             "DataRate and nominal packet interval describe the ON state. "
             "Nominal mean load = ON rate * mean ON/(mean ON + mean OFF); "
             "this is a long-run target, not the expected exact load of a finite run. "
             "Actual load is measured using successful application Tx events. "
             "Residual packet bits persist across ON periods. This is not a Poisson source."},
            {"packet_interval_note",
             "For CBR, packet_interval_us takes precedence over dataRate. "
             "On/Off requires dataRate without packet_interval_us. "
             "Rate is rounded to integer bps. Reported interval is nominal "
             "from payload size and configured rate, not measured Tx spacing; "
             "event scheduling is subject to simulator time resolution. "
             "Changing interval at fixed payload size also changes offered load."},
            {"rlc_mapping", rlcMapping},
            {"rlc_type_id", rlcTypeId},
            {"rlc_configuration_note",
             "Configured RRC data-bearer mapping and equal MaxTxBufferSize "
             "defaults for UM and AM. AM limit covers its new-data Txon "
             "buffer, not total RLC memory. Other RLC attributes retain "
             "their installed defaults."},
            // SCHEDULER: Πλήρες όνομα τύπου που επιλέχθηκε στον helper.
            {"scheduler_type_id", schedulerTypeId},
            {"scheduler_configuration_note",
             "Default scheduler attributes and existing bearer/LC configuration."},
            // HARQ: Τεκμηρίωση της σημασίας των ρυθμίσεων.
            {"harq_configuration_note",
             "EnableHarqReTx selects a retransmission limit of 0 or 3 "
             "in this NR version; initial transmission excluded. "
             "Disabling retransmissions does not remove all HARQ "
             "feedback or buffering."},

            {"harq_processes_note",
             "NumHarqProcess is configured equally at gNB and UE MAC. "
             "This NR version also uses it in HARQ timer handling. "
             "The scenario accepts 2..16; one process is excluded after "
             "an inactive-process feedback fatal error in execution 175. "
             "Values 2, 4, 8 and 16 completed the TdmaRR study."},

            // ANTENNA: Scalar gain, χωρίς beamforming ή MIMO array gain.
            {"antenna_gain_implementation",
             "Reciprocal scalar gNB cosine gain chained after Friis; "
             "UE gain 0 dBi."},
            {"antenna_gain_directions", "UL and DL"},
            {"nr_panel_configuration",
             "1x1 isotropic at UE and gNB; gNB pattern applied "
             "separately in propagation chain"},
            {"beamforming_enabled", false},

            {"environment", "open_space"},
            {"open_space", true},
            {"obstacles", false},
            {"buildings", false},
            {"mobility", false},
            {"constant_nodes", true},
            {"mobility_model", "ConstantPositionMobilityModel"},
            {"link_condition", "LOS"},
            {"link_condition_basis",
             "Assumed unobstructed free-space link; no LOS/NLOS "
             "condition model is installed."},
            {"pathloss_model", ch.at("pathloss_model")},
            {"shadowing_enabled", ch.at("shadowing_enabled")},
            {"fading_enabled", ch.at("fading_enabled")},
            {"traffic_direction", "UE -> gNB -> EPC -> cloud"},
            {"protocol", appCfg.at("protocol")},
            {"traffic_model", appCfg.at("traffic_model")},
            {"ue_count", ueNodes.GetN()},
            {"gnb_count", gnbNodes.GetN()}
        };

        // ΜΕΤΡΙΚΕΣ ΕΝΟΣ RUN: Το mean αφορά τη μοναδική ροή αυτής της εκτέλεσης.
        // Δεν είναι μέσος όρος των πέντε runs. Η σύνοψη μεταξύ runs γίνεται ξεχωριστά στο Excel.
        // Delay/jitter αφορούν μόνο τα παραληφθέντα πακέτα.
        // Loss after drain=(Tx-Rx)/Tx: δεν διακρίνει από μόνο του απόρριψη από πακέτα που παραμένουν σε ουρές.
        // Η αιτία απωλειών απαιτεί ξεχωριστά traces/logs για το συγκεκριμένο πείραμα.
        result["mean"] = {
            {"throughput_mbps", nullptr},
            {"end_to_end_delay_ms", nullptr},
            {"loss_fraction", nullptr},
            {"loss_percent", nullptr},
            {"jitter_ms", nullptr}
        };

        if (flows.size() == 1)
        {
            const auto& flow = flows.at(0);

            result["mean"]["throughput_mbps"] =
                flow.at("ip_throughput_over_app_window_mbps");

            result["mean"]["end_to_end_delay_ms"] =
                flow.at("mean_delay_ms");

            result["mean"]["loss_fraction"] =
                flow.at("loss_fraction_after_drain");

            if (!flow.at("loss_fraction_after_drain").is_null())
            {
                result["mean"]["loss_percent"] =
                    100.0 *
                    flow.at("loss_fraction_after_drain").get<double>();
            }

            result["mean"]["jitter_ms"] =
                flow.at("mean_jitter_ms");
        }

        result["metric_definitions"] = {
            {"actual_offered_load_mbps",
             "Payload bytes accepted by the UDP socket (application Tx trace) "
             "* 8 / full application window / 1e6, including OFF periods."},
            {"throughput_mbps",
             "Received IP bytes * 8 / application window / 1e6; "
             "includes IP/UDP headers and packets received during drain."},
            {"end_to_end_delay_ms",
             "Mean IP-level delay from UE transmission to cloud "
             "reception, for received packets."},
            {"loss_fraction",
             "(Transmitted packets - received packets) / transmitted "
             "packets after drain; may include packets still queued."},
            {"loss_percent",
             "100 * loss_fraction."},
            {"jitter_ms",
             "FlowMonitor sum of absolute consecutive packet delay "
             "differences / (received packets - 1), in ms."},
            {"undefined_metrics",
             "null when there are insufficient packets or no matching flow."}
        };

        // ΕΞΟΔΟΣ n8n: Γράφουμε το results.json στη διαδρομή --outputFile.
        // Το Read results διαβάζει αυτό το JSON. Prepare dataset row -> Convert to File -> Save CSV.
        // Το Merge experiment CSVs συγκεντρώνει τις γραμμές και τις αποτυχίες του batch.
        // Οι συγκεκριμένοι έλεγχοι και οι συνδέσεις των nodes ανήκουν στο workflow, όχι στον C++ κώδικα.
        std::ofstream output(outputFile);
        Require(output.good(), "Cannot open outputFile: " + outputFile);

        output << result.dump(2) << '\n';
        output.close();

        Require(!output.fail(), "Failed to write results.");

        std::cout
            << "[RESULT] flows=" << flows.size()
            << "; actual offered load="
            << result["application"]["actual_offered_load_mbps"]
            << " Mbps; application goodput="
            << result["application"]["goodput_over_app_window_mbps"]
            << " Mbps\n";

        Simulator::Destroy();
        return flows.size() == 1 ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Configuration/run error: "
            << error.what()
            << '\n';

        Simulator::Destroy();
        return 1;
    }
}
