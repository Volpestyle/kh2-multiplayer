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
#if defined(KH2COOP_CLIP_V2)
    if (name == "busy_lease" || name == "postrelease_cancel") {
        Handle ready(CreateEventW(nullptr,TRUE,FALSE,nullptr));
        Handle release(CreateEventW(nullptr,TRUE,FALSE,nullptr));
        if (!ready || !release) throw std::runtime_error("Lease test events unavailable");
        std::thread owner([&]() {
            kh2coop::CaptureLease lease;
            if (lease.Acquire(GetCurrentProcessId())!=kh2coop::CaptureLeaseStatus::Acquired) return;
            SetEvent(ready.value);WaitForSingleObject(release.value,2000);
        });
        const bool readyOk=WaitForSingleObject(ready.value,1000)==WAIT_OBJECT_0;
        kh2coop::CaptureLease contender;
        const auto busy=contender.Acquire(GetCurrentProcessId());
        SetEvent(release.value);owner.join(); // destructor ran on owning thread
        if (!readyOk || busy!=kh2coop::CaptureLeaseStatus::Busy) throw std::runtime_error("Busy lease admitted");
        if (contender.Acquire(GetCurrentProcessId())!=kh2coop::CaptureLeaseStatus::Acquired)
            throw std::runtime_error("Actual destructor did not release lease");
        contender.Release();
        if (name=="postrelease_cancel") {
            SetEvent(event.value);bool refused=false;
            try { bound.Check(); } catch (const std::exception&) { refused=true; }
            if (!refused) throw std::runtime_error("Post-release cancellation admitted encode");
        }
    } else if (name == "raw_inventory") {
        const auto raw=root/"raw";std::filesystem::create_directory(raw);
        std::ostringstream manifest;
        for (unsigned index=0;index<480;++index) {
            std::ostringstream leaf;leaf<<"f_"<<std::setw(5)<<std::setfill('0')<<index<<".bmp";
            const std::string bytes(55,'x');Write(raw/leaf.str(),bytes);
            manifest<<leaf.str()<<" 55 "<<HashBytes(bytes)<<"\n";
        }
        const auto inventory=root/"raw.tsv";Write(inventory,manifest.str());
        ValidateRawInventory(raw,inventory,HashBytes(manifest.str()),bound);
        Write(raw/"f_00001.bmp",std::string(55,'y'));bool refused=false;
        try { ValidateRawInventory(raw,inventory,HashBytes(manifest.str()),bound); }
        catch (const std::exception&) { refused=true; }
        if (!refused) throw std::runtime_error("Changed original raw frame admitted");
    } else
#endif
    if (name == "modes") {
#if defined(KH2COOP_CLIP_V2)
        if (Mode("3","30").frames != 90 || Mode("16","30").frames != 480) throw std::runtime_error("Mode/count mismatch");
        for (const auto& value : {"0","1","12","17","3.0","016"}) {
            bool refused=false; try { Mode(value,"30"); } catch (const std::exception&) { refused=true; }
            if (!refused) throw std::runtime_error("Unqualified duration admitted");
        }
        bool refused=false; try { Mode("16","60"); } catch (const std::exception&) { refused=true; }
        if (!refused) throw std::runtime_error("Wrong fps admitted");
#else
        throw std::runtime_error("Extended controls unavailable");
#endif
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
            const int frames = name == "encode_decode" ? 90 : 480;
            if (argc != 6) throw std::runtime_error("Encode/decode test requires pinned ffmpeg path and hash");
            const auto tool = std::filesystem::canonical(argv[4]); const std::string digest = argv[5];
            if (HashFile(tool, bound) != digest) throw std::runtime_error("Offline encoder pin mismatch");
            const auto raw = root / (frames == 480 ? "hb_arrival_peer0_raw" : "raw"); const auto decoded = root / "hb_arrival_peer0_frames";
            std::filesystem::create_directory(raw); std::filesystem::create_directory(decoded);
            for (int frame = 0; frame < frames; ++frame) {
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
#if defined(KH2COOP_CLIP_V2)
            if (frames == 480) {
                // Exercise the actual post-run command, not a stand-in encoder leaf.
                std::filesystem::create_directory(root / "bin");
                std::filesystem::copy_file(tool, root / "bin/ffmpeg.exe");
                const auto inventory = root / "hb_arrival_peer0_raw.tsv";
                std::ostringstream rows;
                for (unsigned index = 0; index < 480; ++index) {
                    std::ostringstream leaf; leaf << "f_" << std::setw(5) << std::setfill('0') << index << ".bmp";
                    const auto framePath = raw / leaf.str();
                    rows << leaf.str() << ' ' << std::filesystem::file_size(framePath) << ' ' << HashFile(framePath,bound) << '\n';
                }
                Write(inventory,rows.str());
                const auto closure = root / "postrun-closure.json";
                Write(closure,"{\"offlineGeneratedControl\":true}");
                const auto cancelName = L"Local\\kh2coop_clip_cancel_test_" + std::to_wstring(NowNs());
                Handle postCancel(CreateEventW(nullptr,TRUE,FALSE,cancelName.c_str()));
                if (!postCancel) throw std::runtime_error("Post-run test cancellation event failed");
                const auto previousCwd = std::filesystem::current_path();
                try {
                    std::filesystem::current_path(root);
                    const auto result = CmdOwnedClipPostprocess({"--out",video.string(),"--raw-dir",raw.string(),
                        "--inventory",inventory.string(),"--inventory-sha256",HashBytes(rows.str()),
                        "--closure",closure.string(),"--closure-sha256",HashFile(closure,bound),
                        "--deadline-ns",std::to_string(bound.end),"--cancel-event",std::filesystem::path(cancelName).string(),
                        "--tool-sha256",digest,"--encoded-fps","30"});
                    if (result.exitCode != 0) throw std::runtime_error("Actual post-run command failed");
                    Write(root/"native-control.json",result.json);
                    std::filesystem::current_path(previousCwd);
                } catch (...) { std::filesystem::current_path(previousCwd); throw; }
            } else {
#endif
            const std::vector<std::string> encode{tool.string(),"-nostdin","-n","-loglevel","error","-framerate","30","-start_number","0","-i",(raw/"f_%05d.bmp").string(),"-frames:v",std::to_string(frames),"-c:v","libx264","-threads","1","-pix_fmt","yuv420p","-crf","20",video.string()};
            const auto encoded = Child(encode,digest,root,"hb_arrival_peer0","encoder",job.value,bound);
            const std::vector<std::string> decode{tool.string(),"-nostdin","-n","-xerror","-loglevel","info","-threads","1","-err_detect","explode","-i",video.string(),"-vf","format=rgb24,showinfo","-fps_mode","passthrough","-c:v","png","-threads","1","-compression_level","1","-start_number","0",(decoded/"decoded_%03d.png").string()};
            const auto verified = Child(decode,digest,root,"hb_arrival_peer0","verifier",job.value,bound);
#if defined(KH2COOP_CLIP_V2)
            Write(root/"native-control.json","{\"ok\":true,\"offlineGeneratedPixels\":true,\"nativeGameExecuted\":false,\"processes\":{\"encoder\":"+encoded.json+",\"verifier\":"+verified.json+"}}");
            }
#endif
            int decodedCount = 0;
            for (const auto& entry : std::filesystem::directory_iterator(decoded)) {
                if (entry.is_regular_file() && entry.path().extension() == ".png") ++decodedCount;
            }
            if (decodedCount != frames) throw std::runtime_error("Full generated decode count differs from requested mode");
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
