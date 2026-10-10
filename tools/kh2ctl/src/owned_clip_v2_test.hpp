// Offline-only executable: generated pixels and owned self-test child processes.
int OwnedClipTestChild(const std::string& mode) {
    if (mode == "--test-child-sleep") { Sleep(30000); return 0; }
    if (mode == "--test-child-descendant") {
        std::array<wchar_t, 32768> name{}; GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
        std::wstring command = owned_clip::Quote(name.data()) + L" --test-child-sleep";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
        if (!CreateProcessW(name.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return 2;
        CloseHandle(child.hThread); CloseHandle(child.hProcess); return 0;
    }
    return mode == "--test-child-exit" ? 0 : 2;
}

int OwnedClipOfflineTest(int argc, char** argv) {
    using namespace owned_clip;
    if (argc < 4) throw std::runtime_error("Offline test needs case and evidence parent");
    const std::string name = argv[2];
    const auto root = std::filesystem::absolute(argv[3]) / (name + "_" + std::to_string(NowNs()));
    std::filesystem::create_directories(root);
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) throw std::runtime_error("Offline cancellation event failed");
    Bound bound{NowNs() + 45000000000ULL, event.value};
    if (name == "modes") {
        const auto shortMode = SelectMode(); const auto longMode = SelectMode("16");
        if (shortMode.seconds != 3 || shortMode.frames != 90 || longMode.frames != 480) throw std::runtime_error("Mode count mismatch");
        for (const auto* bad : {"12", "03", "3.0", "0", "17"}) {
            bool refused = false; try { SelectMode(bad); } catch (const std::exception&) { refused = true; }
            if (!refused) throw std::runtime_error("Unsupported mode admitted");
        }
        if (AcquisitionEnd(100000000000ULL,1,shortMode)!=10000000001ULL || AcquisitionEnd(100000000000ULL,1,longMode)!=23000000001ULL || AcquisitionEnd(2,1,longMode)!=2) throw std::runtime_error("Acquisition hard cap changed");
        if (PhaseEnd(100000000000ULL,1,longMode)!=90000000001ULL || PhaseEnd(2,1,longMode)!=2 || PhaseEnd(100000000000ULL,1,shortMode)!=100000000000ULL) throw std::runtime_error("Phase deadline changed");
    } else if (name == "capture_release") {
        const auto path = root / "capture_released.json";
        PublishRelease(path,"{\"test\":true}");
        if (!std::filesystem::exists(path) || std::filesystem::exists(std::filesystem::path(path.wstring()+L".pending"))) throw std::runtime_error("Atomic release publication incomplete");
        bool refused = false;
        try { PublishRelease(path,"{\"test\":false}"); } catch (const std::exception&) { refused = true; }
        if (!refused) throw std::runtime_error("Release overwrite admitted");
    } else if (name == "lease_busy") {
        const auto pid = GetCurrentProcessId();
        kh2coop::CaptureLeaseStatus status = kh2coop::CaptureLeaseStatus::Error;
        {
            kh2coop::CaptureLease original;
            if (original.Acquire(pid)!=kh2coop::CaptureLeaseStatus::Acquired) throw std::runtime_error("Test lease unavailable");
            std::thread competing([&]() { kh2coop::CaptureLease other; status=other.Acquire(pid); }); competing.join();
            if (status!=kh2coop::CaptureLeaseStatus::Busy) throw std::runtime_error("Busy capture admitted");
        }
        std::thread after([&]() { kh2coop::CaptureLease other; status=other.Acquire(pid); }); after.join();
        if (status!=kh2coop::CaptureLeaseStatus::Acquired) throw std::runtime_error("Actual destructor did not release");
    } else if (name == "clock") {
        const auto first = NowNs(); Sleep(1); if (NowNs() <= first) throw std::runtime_error("Clock did not advance");
    } else if (name == "hash") {
        if (HashBytes("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") throw std::runtime_error("SHA256 mismatch");
        Write(root / "abc.txt", "abc"); if (HashFile(root / "abc.txt", bound) != HashBytes("abc")) throw std::runtime_error("File SHA mismatch");
    } else if (name == "quote") {
        if (Quote(L"abc") != L"\"abc\"" || Quote(L"abc\\") != L"\"abc\\\\\"" || Quote(L"a\"b") != L"\"a\\\"b\"") throw std::runtime_error("Windows argv quoting mismatch");
    } else {
        Handle job(CreateJobObjectW(nullptr, nullptr)); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("Offline job failed");
        std::array<wchar_t, 32768> executable{};
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        const auto self = std::filesystem::canonical(executable.data());
        if (name == "encode_decode" || name == "encode_decode_480") {
            const int frameCount = name == "encode_decode_480" ? 480 : 90;
            if (argc != 6) throw std::runtime_error("Encode/decode test requires pinned ffmpeg path and hash");
            const auto tool = std::filesystem::canonical(argv[4]); const std::string digest = argv[5];
            if (HashFile(tool, bound) != digest) throw std::runtime_error("Offline encoder pin mismatch");
            const auto raw = root / "raw"; const auto decoded = root / "hb_arrival_peer0_frames";
            std::filesystem::create_directory(raw); std::filesystem::create_directory(decoded);
            for (int frame = 0; frame < frameCount; ++frame) {
                bound.Check(); constexpr int width = 320, height = 180;
                BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
                file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info);
                file.bfSize = file.bfOffBits + width * height * 3;
                info.biSize = sizeof(info); info.biWidth = width; info.biHeight = height;
                info.biPlanes = 1; info.biBitCount = 24; info.biCompression = BI_RGB;
                std::ostringstream leaf; leaf << "f_" << std::setw(5) << std::setfill('0') << frame << ".bmp";
                std::ofstream out(raw / leaf.str(), std::ios::binary);
                out.write(reinterpret_cast<const char*>(&file), sizeof(file)); out.write(reinterpret_cast<const char*>(&info), sizeof(info));
                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                    const bool square = y >= 60 && y < 100 && x >= (frame * 3) % 280 && x < (frame * 3) % 280 + 40;
                    const std::array<unsigned char, 3> pixel = square ? std::array<unsigned char,3>{220,230,240} : std::array<unsigned char,3>{60,50,40};
                    out.write(reinterpret_cast<const char*>(pixel.data()), static_cast<std::streamsize>(pixel.size()));
                }
                if (!out) throw std::runtime_error("Generated control BMP failed");
            }
            const auto video = root / "hb_arrival_peer0.mp4";
            const std::vector<std::string> encode{tool.string(),"-nostdin","-n","-loglevel","error","-framerate","30","-start_number","0","-i",(raw/"f_%05d.bmp").string(),"-frames:v",std::to_string(frameCount),"-c:v","libx264","-threads","1","-pix_fmt","yuv420p","-crf","20",video.string()};
            const auto encoded = Child(encode,digest,root,"hb_arrival_peer0","encoder",job.value,bound);
            const std::vector<std::string> decode{tool.string(),"-nostdin","-n","-xerror","-loglevel","info","-threads","1","-err_detect","explode","-i",video.string(),"-vf","format=rgb24,showinfo","-fps_mode","passthrough","-c:v","png","-threads","1","-compression_level","1","-start_number","0",(decoded/"decoded_%03d.png").string()};
            const auto verified = Child(decode,digest,root,"hb_arrival_peer0","verifier",job.value,bound);
            std::size_t decodedCount = 0;
            for (const auto& entry : std::filesystem::directory_iterator(decoded)) if (entry.path().extension()==L".png") ++decodedCount;
            if (decodedCount != static_cast<std::size_t>(frameCount)) throw std::runtime_error("Full decoded frame count mismatch");
            Write(root/"native-control.json","{\"ok\":true,\"offlineGeneratedPixels\":true,\"nativeGameExecuted\":false,\"processes\":{\"encoder\":"+encoded.json+",\"verifier\":"+verified.json+"}}");
        } else {
            const std::string mode = name == "exited" ? "--test-child-exit" : name == "descendant" ? "--test-child-descendant" : "--test-child-sleep";
            const auto digest = HashFile(self,bound); std::thread signal;
            if (name == "cancel") signal = std::thread([&]() { Sleep(100); SetEvent(event.value); });
            if (name == "deadline") bound.end = NowNs() + 2200000000ULL;
            bool refused = false; std::string refusal;
            try { Child({self.string(),mode},digest,root,name,"child",job.value,bound); }
            catch (const std::exception& error) { refused = true; refusal = error.what(); Write(root/"refusal.txt",refusal); }
            if (signal.joinable()) signal.join();
            if ((name == "exited") == refused) throw std::runtime_error("Owned child success/refusal mismatch: " + refusal);
            if (name != "exited" && name != "cancel" && name != "deadline" && name != "descendant") throw std::runtime_error("Unknown offline case");
            const std::string expected = name == "cancel" ? "cancellation" : name == "deadline" ? "deadline" : "descendant remains";
            if (refused && refusal.find(expected) == std::string::npos) throw std::runtime_error("Wrong refusal cause: " + refusal);
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            for (int attempt = 0; attempt < 100; ++attempt) {
                if (!QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)) throw std::runtime_error("Offline job census failed");
                if (accounting.ActiveProcesses == 0) break; Sleep(10);
            }
            if (accounting.ActiveProcesses) throw std::runtime_error("Owned offline descendant remains");
        }
    }
    Write(root/"result.json","{\"ok\":true,\"nativeGameExecuted\":false,\"case\":"+JsonString(name)+"}");
    std::cout << "{\"ok\":true,\"case\":" << JsonString(name) << ",\"evidence\":" << JsonString(root.string()) << "}" << std::endl;
    return 0;
}
