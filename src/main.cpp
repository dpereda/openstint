#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <iomanip>
#include <cmath>
#include <unistd.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>
#include <complex>
#include <algorithm>
#include <numeric>
#include <functional>
#include <algorithm>
#include <vector>

#include "complex_cast.hpp"

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include <liquid/liquid.h>

#include "sdr_device.hpp"
#include "preamble.hpp"
#include "transponder.hpp"
#include "frame.hpp"
#include "passing.hpp"
#include "counters.hpp"


static std::unique_ptr<SdrDevice> sdr_device = nullptr;
static std::atomic<bool> do_exit(false);

static const uint64_t CENTER_FREQ_HZ       = 5000000ULL;
static const uint32_t SAMPLE_RATE          = 5000000;
static const uint32_t SYMBOL_RATE          = 1250000;
static const uint32_t BB_FILTER_BW         = 1750000;
static const uint32_t SAMPLES_PER_SYMBOL   = SAMPLE_RATE / SYMBOL_RATE;
static const uint8_t DEFAULT_LNA_GAIN      = 24;           // 0-40 in steps of 8 or so; experiment
static const uint8_t DEFAULT_VGA_GAIN      = 24;           // 0-62
static const int DEFAULT_ZEROMQ_PORT       = 5556;

static enum FrameParseMode { FRAME_SEEK, FRAME_FOUND } frame_parse_mode = FRAME_SEEK;
static FrameDetector frame_detector(0.9f);
static SymbolReader symbol_reader;
static Frame frame;

static PassingDetector passing_detector;
static RxStatistics rx_stats;
static bool monitor_mode = false;

static const uint64_t startup_ts = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()
).count();

// signal handler to break the capture loop
void signal_handler(int signum) {
    std::cerr << "\nCaught signal " << signum << " — stopping...\n";
    do_exit = true;
}

bool process_frame(Frame* frame) {
    const uint8_t *softbits = frame->bits();
    if (!softbits) {
        // preamble not found
        return false;
    }

    if (monitor_mode) {
        std::cout << "F " << *frame << std::endl;
    }

    uint32_t transponder_id;
    switch (frame->transponder_type) {
        case TransponderType::OpenStint:
        if (decode_openstint(softbits, &transponder_id)) {
            if (transponder_id < 10000000u) {
                passing_detector.append(frame, transponder_id);
            } else if ((transponder_id & 0x00A00000) == 0x00A00000) {
                uint32_t transponder_timestamp = (transponder_id & 0x000FFFFF);
                passing_detector.timesync(frame, transponder_timestamp);
            }
            return true;
        }
        break;
        case TransponderType::Legacy:
            if (decode_legacy(softbits, &transponder_id)) {
                if (transponder_id < 10000000) { // extra check (7-digit max)
                    passing_detector.append(frame, transponder_id);
                }
                return true;
            }
        break;
    }
    return false;
}


int main(int argc, char** argv) {
    SdrBackend backend = SdrBackend::HackRF;  // Default to HackRF for backward compatibility

    const uint64_t freq_hz = CENTER_FREQ_HZ;
    const uint32_t sample_rate = SAMPLE_RATE;
    const uint32_t filter_bw = BB_FILTER_BW;
    uint8_t lna_gain = DEFAULT_LNA_GAIN;
    uint8_t vga_gain = DEFAULT_VGA_GAIN;
    uint8_t unified_gain = 50;  // 0-100 unified gain for RTL-SDR
    bool bias_tee = false;
    bool amp_enable = false; // hackrf has a custom, +13 dB preamp
    int zmq_port = DEFAULT_ZEROMQ_PORT;
    const char* device_serial = nullptr;

    // process command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-r") {
            backend = SdrBackend::RTL_SDR;
        } else if (arg == "-d" && i + 1 < argc) {
            device_serial = argv[++i];
        } else if (arg == "-g" && i + 1 < argc) {
            unified_gain = std::atoi(argv[++i]);
            if (unified_gain > 100) {
                std::cerr << "Error: Unified gain must be between 0 and 100.\n";
                return 1;
            }
        } else if (arg == "-l" && i + 1 < argc) {
            lna_gain = std::atoi(argv[++i]);
            lna_gain = (lna_gain / 8) * 8; // steps of 8
            if (lna_gain > 40) {
                std::cerr << "Error: LNA gain must be between 0 and 40.\n";
                return 1;
            }
        } else if (arg == "-v" && i + 1 < argc) {
            vga_gain = std::atoi(argv[++i]);
            vga_gain = (vga_gain / 2) * 2; // steps of 2
            if (vga_gain > 62) {
                std::cerr << "Error: VGA gain must be between 0 and 62.\n";
                return 1;
            }
        } else if (arg == "-b") {
            bias_tee = true;
        } else if (arg == "-a") {
            amp_enable = true;
        } else if (arg == "-p" && i + 1 < argc) {
            zmq_port = std::atoi(argv[++i]);
        } else if (arg == "-m") {
            monitor_mode = true;
        } else {
            if (arg != "-h") {
                std::cerr << "Unknown argument: " << arg << "\n";
            }
            std::cerr << "Usage: " << argv[0] << " [-r] [-d ser_nr] [-p tcp_port] [-g <0..100>] [-l <0..40>] [-v <0..62>] [-a] [-b] [-m]\n";
            std::cerr << "\t-r          default:off \tUse RTL-SDR instead of HackRF\n";
            std::cerr << "\t-d ser_nr   default:first\tserial number of the desired device\n";
            std::cerr << "\t-p port     default:" << DEFAULT_ZEROMQ_PORT << "\tZeroMQ publisher port\n";
            std::cerr << "\t-g <0..100> default:50  \tUnified gain (works with both HackRF and RTL-SDR)\n";
            std::cerr << "\t-l <0..40>  default:" << static_cast<int>(DEFAULT_LNA_GAIN) << "  \tLNA gain [HackRF only] (rf signal amplifier; valid values: 0/8/16/24/32/40)\n";
            std::cerr << "\t-v <0..62>  default:" << static_cast<int>(DEFAULT_VGA_GAIN) << "  \tVGA gain [HackRF only] (baseband signal amplifier, steps of 2)\n";
            std::cerr << "\t-a          default:off \tEnable preamp/LNA boost\n";
            std::cerr << "\t-b          default:off \tEnable bias-tee (+3.3 V, 50 mA max)\n";
            std::cerr << "\t-m          default:off \tEnable monitor mode (print received frames to stdout)\n";

            return 1;
        }
    }

    // transponder processing (allocate viterbi trellis); TODO RAII
    init_transponders();

    //  Prepare our context and publisher
    std::string zmq_address;
    std::format_to(std::back_inserter(zmq_address), "tcp://*:{}", zmq_port);
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind(zmq_address);
    std::cout << "Listening on " << zmq_address << std::endl;

    // install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create SDR device
    sdr_device = create_sdr_device(backend);
    if (!sdr_device) {
        std::cerr << "Failed to create SDR device\n";
        return EXIT_FAILURE;
    }

    // Initialize SDR device
    if (!sdr_device->initialize()) {
        std::cerr << "Failed to initialize " << sdr_device->get_backend_name()
                  << ": " << sdr_device->get_last_error() << "\n";
        return EXIT_FAILURE;
    }

    // Open device
    if (!sdr_device->open(device_serial)) {
        std::cerr << "Failed to open device: " << sdr_device->get_last_error() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Device: " << sdr_device->get_device_info() << "\n";

    // Define callback lambda
    auto rx_callback = [&](const std::complex<int8_t>* samples, uint32_t sample_count) {
        if (do_exit) {
            return;
        }

        uint64_t buffer_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count() - startup_ts;

        bool frame_detected = false;
        for (uint32_t idx=0; (idx+SAMPLES_PER_SYMBOL)<=sample_count; idx+=SAMPLES_PER_SYMBOL) {
            if (frame_parse_mode == FRAME_SEEK) {
                const std::optional<TransponderType> detected = frame_detector.process_baseband(samples+idx);
                if (detected) {
                    frame_parse_mode = FRAME_FOUND;
                    frame_detected = true;
                    uint64_t timestamp = buffer_timestamp + (1000 * idx) / SAMPLE_RATE;
                    frame = Frame(detected.value(), timestamp, frame_detector.symbol_energy());
                    symbol_reader.read_preamble(&frame, frame_detector.dc_offset(), samples, idx+4);
                }
            } else if (frame_parse_mode == FRAME_FOUND) {
                symbol_reader.read_symbol(&frame, frame_detector.dc_offset(), samples+idx);
                if (symbol_reader.is_frame_complete(&frame)) {
                    frame_parse_mode = FRAME_SEEK;
                    bool frame_processed = process_frame(&frame);
                    rx_stats.register_frame(frame_processed);
                }
            }
        }

        symbol_reader.update_reserve_buffer(samples, sample_count);

        if (frame_detected) {
            frame_detector.reset_statistics_counters();
        } else {
            frame_detector.update_statistics();
            rx_stats.save_channel_characteristics(frame_detector.dc_offset(), frame_detector.noise_energy());
        }
    };

    // Configure SDR device
    SdrConfig config;
    config.center_freq_hz = freq_hz;
    config.sample_rate = sample_rate;
    config.baseband_filter_bw = filter_bw;
    config.lna_gain = lna_gain;
    config.vga_gain = vga_gain;
    config.unified_gain = unified_gain;
    config.amp_enable = amp_enable;
    config.bias_tee = bias_tee;
    config.device_serial = device_serial;

    if (!sdr_device->configure(config)) {
        std::cerr << "Failed to configure device: " << sdr_device->get_last_error() << "\n";
        goto cleanup;
    }

    std::cout << sdr_device->get_backend_name() << " RX: freq=" << freq_hz << " Hz, sample_rate=" << sample_rate << " Hz\n";


    // Start receiving
    if (!sdr_device->start_rx(rx_callback)) {
        std::cerr << "Failed to start RX: " << sdr_device->get_last_error() << "\n";
        goto cleanup;
    }

    std::cerr << "Streaming... stop with Ctrl-C\n";

    // main loop — exit when handler sets do_exit (Ctrl-C) or device stops
    while (!do_exit && sdr_device->is_streaming()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count() - startup_ts;

        // report status once a second
        if (rx_stats.reporting_due(now)) {
            const std::string report = std::format("S {} {}", now, rx_stats.to_string());
            rx_stats.reset(now);

            std::cout << report << std::endl;
            publisher.send(zmq::buffer(report), zmq::send_flags::none);
        }
        
        std::vector<TimeSync> timesyncs = passing_detector.identify_timesyncs(500ul);
        for (const auto& time_sync : timesyncs) {
            const std::string report = std::format("T {} {} {} {}",
                time_sync.timestamp,
                transponder_props(time_sync.transponder_type).prefix, // always openstint
                time_sync.transponder_id,
                time_sync.transponder_timestamp
            );

            std::cout << report << std::endl;
            publisher.send(zmq::buffer(report), zmq::send_flags::none);
        }

        std::vector<Passing> passings = passing_detector.identify_passings(now - 250ul);
        for (const auto& passing : passings) {
            const std::string report = std::format("P {} {} {} {:.2f} {} {:.2f}",
                passing.timestamp,
                transponder_props(passing.transponder_type).prefix,
                passing.transponder_id,
                passing.rssi,
                passing.hits,
                passing.evm
            );

            std::cout << report << std::endl;
            publisher.send(zmq::buffer(report), zmq::send_flags::none);
        }
    }

    // Stop RX
    sdr_device->stop_rx();

cleanup:
    std::cout << "cleanup\n";
    if (sdr_device) {
        sdr_device->close();
    }

    std::cerr << "Done.\n";
    return 0;
}